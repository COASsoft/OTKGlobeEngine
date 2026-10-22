#include "raster/tile_cache.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace otk {

namespace {

constexpr int kLimitCheckInterval = 256;
constexpr int kEvictBatch = 512;

}

std::int64_t TileCache::now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool TileCache::openAt(const std::string& path) {
    if (!db_.open(path, false)) return false;
#if defined(__EMSCRIPTEN__)
    db_.exec("PRAGMA locking_mode=EXCLUSIVE");
#endif
    if (!db_.exec("PRAGMA journal_mode=WAL")) return false;
    db_.exec("PRAGMA synchronous=NORMAL");
    if (!db_.exec("CREATE TABLE IF NOT EXISTS tiles (source TEXT NOT NULL, z INTEGER NOT NULL, x INTEGER NOT NULL, "
                  "y INTEGER NOT NULL, data BLOB NOT NULL, fetched INTEGER NOT NULL, expires INTEGER NOT NULL, "
                  "pinned INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (source, z, x, y)) WITHOUT ROWID"))
        return false;
    bool hasPinned = false;
    {
        auto info = db_.prepare("PRAGMA table_info(tiles)");
        while (info.valid() && info.step())
            if (info.text(1) == "pinned") hasPinned = true;
    }
    if (!hasPinned && !db_.exec("ALTER TABLE tiles ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0")) return false;
    get_ = db_.prepare("SELECT data, expires FROM tiles WHERE source=? AND z=? AND x=? AND y=?");
    has_ = db_.prepare("SELECT 1 FROM tiles WHERE source=? AND z=? AND x=? AND y=?");
    put_ = db_.prepare("INSERT INTO tiles (source, z, x, y, data, fetched, expires, pinned) VALUES (?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(source, z, x, y) DO UPDATE SET data=excluded.data, fetched=excluded.fetched, "
                       "expires=excluded.expires, pinned=MAX(pinned, excluded.pinned)");
    pin_ = db_.prepare("UPDATE tiles SET pinned=1 WHERE source=? AND z=? AND x=? AND y=?");
    del_ = db_.prepare("DELETE FROM tiles WHERE source=? AND z=? AND x=? AND y=?");
    range_ = db_.prepare("SELECT x, y, data FROM tiles WHERE source=? AND z=? AND x BETWEEN ? AND ? AND y BETWEEN ? AND ?");
    return get_.valid() && has_.valid() && put_.valid() && pin_.valid() && del_.valid() && range_.valid();
}

bool TileCache::open(const std::string& path) {
    std::lock_guard lock(mutex_);
    get_ = {};
    has_ = {};
    put_ = {};
    pin_ = {};
    del_ = {};
    range_ = {};
    path_ = path;
    if (openAt(path)) return true;
    get_ = {};
    has_ = {};
    put_ = {};
    pin_ = {};
    del_ = {};
    range_ = {};
    db_.close();
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(path), ec)) return false;
    std::string moved = path + ".corrupt-" + std::to_string(now());
    std::filesystem::rename(std::filesystem::u8path(path), std::filesystem::u8path(moved), ec);
    if (ec) return false;
    for (const char* s : {"-wal", "-shm"}) std::filesystem::remove(std::filesystem::u8path(path + s), ec);
    if (openAt(path)) return true;
    db_.close();
    return false;
}

bool TileCache::get(const std::string& source, int z, int x, int y, std::vector<std::uint8_t>& data,
                    std::int64_t& expires) {
    std::lock_guard lock(mutex_);
    if (!get_.valid()) return false;
    get_.reset();
    get_.bind(1, source);
    get_.bind(2, z);
    get_.bind(3, x);
    get_.bind(4, y);
    bool found = get_.step();
    if (found) {
        data = get_.blob(0);
        expires = get_.int64(1);
    }
    get_.reset();
    return found && !data.empty();
}

bool TileCache::has(const std::string& source, int z, int x, int y) {
    std::lock_guard lock(mutex_);
    if (!has_.valid()) return false;
    has_.reset();
    has_.bind(1, source);
    has_.bind(2, z);
    has_.bind(3, x);
    has_.bind(4, y);
    bool found = has_.step();
    has_.reset();
    return found;
}

void TileCache::put(const std::string& source, int z, int x, int y, const std::vector<std::uint8_t>& data,
                    std::int64_t expires, bool pinned) {
    std::lock_guard lock(mutex_);
    if (!put_.valid() || data.empty()) return;
    put_.reset();
    put_.bind(1, source);
    put_.bind(2, z);
    put_.bind(3, x);
    put_.bind(4, y);
    put_.bind(5, data);
    put_.bind(6, now());
    put_.bind(7, expires);
    put_.bind(8, pinned ? 1 : 0);
    put_.exec();
    put_.reset();
    if (limit_ > 0 && batch_ == 0 && ++putsSinceCheck_ >= kLimitCheckInterval) {
        putsSinceCheck_ = 0;
        evictLocked();
    }
}

void TileCache::pin(const std::string& source, int z, int x, int y) {
    std::lock_guard lock(mutex_);
    if (!pin_.valid()) return;
    pin_.reset();
    pin_.bind(1, source);
    pin_.bind(2, z);
    pin_.bind(3, x);
    pin_.bind(4, y);
    pin_.exec();
    pin_.reset();
}

void TileCache::remove(const std::string& source, int z, int x, int y) {
    std::lock_guard lock(mutex_);
    if (!del_.valid()) return;
    del_.reset();
    del_.bind(1, source);
    del_.bind(2, z);
    del_.bind(3, x);
    del_.bind(4, y);
    del_.exec();
    del_.reset();
}

std::int64_t TileCache::bytesLocked() {
    if (!db_.isOpen()) return 0;
    auto pages = db_.prepare("PRAGMA page_count");
    auto freePages = db_.prepare("PRAGMA freelist_count");
    auto size = db_.prepare("PRAGMA page_size");
    std::int64_t p = pages.valid() && pages.step() ? pages.int64(0) : 0;
    std::int64_t f = freePages.valid() && freePages.step() ? freePages.int64(0) : 0;
    std::int64_t s = size.valid() && size.step() ? size.int64(0) : 0;
    return (p - f) * s;
}

std::int64_t TileCache::bytes() {
    std::lock_guard lock(mutex_);
    return bytesLocked();
}

std::int64_t TileCache::evictLocked() {
    if (limit_ <= 0 || !db_.isOpen()) return 0;
    std::int64_t removed = 0;
    auto evict = db_.prepare("DELETE FROM tiles WHERE (source, z, x, y) IN (SELECT source, z, x, y FROM tiles "
                             "WHERE pinned=0 ORDER BY fetched LIMIT ?)");
    if (!evict.valid()) return 0;
    while (bytesLocked() > limit_) {
        evict.reset();
        evict.bind(1, kEvictBatch);
        evict.exec();
        int n = sqlite3_changes(db_.handle());
        evict.reset();
        if (n <= 0) break;
        removed += n;
    }
    return removed;
}

void TileCache::setLimit(std::int64_t bytes) {
    std::lock_guard lock(mutex_);
    limit_ = bytes;
    putsSinceCheck_ = 0;
    if (limit_ > 0 && db_.isOpen()) {
        db_.exec("CREATE INDEX IF NOT EXISTS tiles_age ON tiles (pinned, fetched)");
        evictLocked();
    }
}

std::int64_t TileCache::enforceLimit() {
    std::lock_guard lock(mutex_);
    return evictLocked();
}

void TileCache::forEach(const std::string& source, int z, int x0, int x1, int y0, int y1,
                        const std::function<void(int x, int y, const std::vector<std::uint8_t>&)>& fn) {
    std::vector<std::pair<std::pair<int, int>, std::vector<std::uint8_t>>> rows;
    {
        std::lock_guard lock(mutex_);
        if (!range_.valid()) return;
        range_.reset();
        range_.bind(1, source);
        range_.bind(2, z);
        range_.bind(3, x0);
        range_.bind(4, x1);
        range_.bind(5, y0);
        range_.bind(6, y1);
        while (range_.step()) rows.push_back({{range_.integer(0), range_.integer(1)}, range_.blob(2)});
        range_.reset();
    }
    for (const auto& r : rows) fn(r.first.first, r.first.second, r.second);
}

void TileCache::begin() {
    std::lock_guard lock(mutex_);
    if (db_.isOpen() && batch_++ == 0) db_.exec("BEGIN");
}

void TileCache::commit() {
    std::lock_guard lock(mutex_);
    if (db_.isOpen() && batch_ > 0 && --batch_ == 0) {
        db_.exec("COMMIT");
        evictLocked();
    }
}

}
