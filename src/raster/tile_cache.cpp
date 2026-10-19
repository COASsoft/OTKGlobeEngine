#include "raster/tile_cache.hpp"

#include <chrono>

namespace ote {

std::int64_t TileCache::now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool TileCache::open(const std::string& path) {
    std::lock_guard lock(mutex_);
    if (!db_.open(path, false)) return false;
    db_.exec("PRAGMA journal_mode=WAL");
    db_.exec("PRAGMA synchronous=NORMAL");
    db_.exec("CREATE TABLE IF NOT EXISTS tiles (source TEXT NOT NULL, z INTEGER NOT NULL, x INTEGER NOT NULL, "
             "y INTEGER NOT NULL, data BLOB NOT NULL, fetched INTEGER NOT NULL, expires INTEGER NOT NULL, "
             "PRIMARY KEY (source, z, x, y)) WITHOUT ROWID");
    get_ = db_.prepare("SELECT data, expires FROM tiles WHERE source=? AND z=? AND x=? AND y=?");
    has_ = db_.prepare("SELECT 1 FROM tiles WHERE source=? AND z=? AND x=? AND y=?");
    put_ = db_.prepare("INSERT OR REPLACE INTO tiles (source, z, x, y, data, fetched, expires) VALUES (?,?,?,?,?,?,?)");
    return get_.valid() && has_.valid() && put_.valid();
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
    return found;
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
                    std::int64_t expires) {
    std::lock_guard lock(mutex_);
    if (!put_.valid()) return;
    put_.reset();
    put_.bind(1, source);
    put_.bind(2, z);
    put_.bind(3, x);
    put_.bind(4, y);
    put_.bind(5, data);
    put_.bind(6, now());
    put_.bind(7, expires);
    put_.exec();
    put_.reset();
}

}
