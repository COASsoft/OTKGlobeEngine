#include "raster/pmtiles.hpp"

#include "ote/image.hpp"
#include "raster/vector_tile.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace ote {

namespace {

constexpr std::size_t kHeaderBytes = 127;
constexpr std::size_t kDirectoryCacheEntries = 64;
constexpr int kMaxDepth = 4;

std::uint64_t u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

std::int32_t i32(const std::uint8_t* p) {
    std::uint32_t v = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                      (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    std::int32_t s;
    std::memcpy(&s, &v, 4);
    return s;
}

struct Varints {
    const std::uint8_t* p;
    const std::uint8_t* end;
    bool failed = false;
    std::uint64_t next() {
        std::uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p >= end) {
                failed = true;
                return 0;
            }
            std::uint8_t b = *p++;
            v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        failed = true;
        return 0;
    }
};

std::FILE* openUtf8(const std::string& path) {
#if defined(_WIN32)
    return _wfopen(std::filesystem::u8path(path).wstring().c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool seek(std::FILE* f, std::uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

}

std::uint64_t PmTilesArchive::tileId(int z, std::uint32_t x, std::uint32_t y) {
    if (z <= 0) return 0;
    std::uint64_t acc = ((std::uint64_t(1) << (2 * z)) - 1) / 3;
    std::uint64_t n = std::uint64_t(1) << z;
    std::uint64_t xx = x, yy = y, d = 0;
    for (std::uint64_t s = n / 2; s > 0; s /= 2) {
        std::uint64_t rx = (xx & s) ? 1 : 0;
        std::uint64_t ry = (yy & s) ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) {
                xx = n - 1 - xx;
                yy = n - 1 - yy;
            }
            std::swap(xx, yy);
        }
    }
    return acc + d;
}

std::shared_ptr<PmTilesArchive> PmTilesArchive::open(const std::string& path, std::string* error) {
    auto fail = [error](const std::string& why) -> std::shared_ptr<PmTilesArchive> {
        if (error) *error = why;
        return nullptr;
    };
    std::shared_ptr<PmTilesArchive> a(new PmTilesArchive());
    a->file_ = openUtf8(path);
    if (!a->file_) return fail("cannot open " + path);
    std::error_code ec;
    a->fileSize_ = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    std::vector<std::uint8_t> h;
    if (!a->read(0, kHeaderBytes, h) || std::memcmp(h.data(), "PMTiles", 7) != 0) return fail(path + " is not a PMTiles archive");
    if (h[7] != 3) return fail(path + " is PMTiles version " + std::to_string(h[7]) + "; only version 3 is read");
    Header& hd = a->header_;
    hd.rootOffset = u64(&h[8]);
    hd.rootLength = u64(&h[16]);
    hd.metadataOffset = u64(&h[24]);
    hd.metadataLength = u64(&h[32]);
    hd.leafOffset = u64(&h[40]);
    hd.leafLength = u64(&h[48]);
    hd.dataOffset = u64(&h[56]);
    hd.dataLength = u64(&h[64]);
    hd.addressedTiles = u64(&h[72]);
    hd.internalCompression = h[97];
    hd.tileCompression = h[98];
    hd.tileType = static_cast<TileType>(h[99] <= 5 ? h[99] : 0);
    hd.minZoom = h[100];
    hd.maxZoom = h[101];
    hd.west = i32(&h[102]) / 1e7;
    hd.south = i32(&h[106]) / 1e7;
    hd.east = i32(&h[110]) / 1e7;
    hd.north = i32(&h[114]) / 1e7;
    hd.centerZoom = h[118];
    hd.centerLon = i32(&h[119]) / 1e7;
    hd.centerLat = i32(&h[123]) / 1e7;
    if (hd.internalCompression > 2) return fail(path + " compresses its directories with brotli or zstd, which is not supported");
    if (hd.maxZoom < hd.minZoom || hd.maxZoom > 30) return fail(path + " declares zoom levels " + std::to_string(hd.minZoom) + "-" + std::to_string(hd.maxZoom));
    if (!a->directory(hd.rootOffset, hd.rootLength)) return fail(path + " has an unreadable root directory");
    if (hd.tileType == TileType::Png || hd.tileType == TileType::Jpeg || hd.tileType == TileType::Webp) {
        std::uint32_t n = 1u << hd.minZoom;
        std::uint32_t cx = static_cast<std::uint32_t>(std::clamp((hd.centerLon + 180.0) / 360.0, 0.0, 0.999999) * n);
        std::vector<std::uint8_t> t;
        int w = 0, ht = 0;
        bool sized = false;
        for (std::uint32_t y = 0; y < n && !sized && y < 64; ++y)
            if (a->status(hd.minZoom, cx, y, t) == 200 && imageSize(t.data(), t.size(), w, ht) && w > 0) sized = true;
        if (sized) a->tileSize_ = w;
    }
    return a;
}

PmTilesArchive::~PmTilesArchive() {
    if (file_) std::fclose(file_);
}

bool PmTilesArchive::read(std::uint64_t offset, std::uint64_t length, std::vector<std::uint8_t>& out) const {
    if (fileSize_ && (offset > fileSize_ || length > fileSize_ - offset)) return false;
    if (length > (std::uint64_t(1) << 31)) return false;
    out.resize(static_cast<std::size_t>(length));
    std::lock_guard lock(mutex_);
    if (!seek(file_, offset)) return false;
    return length == 0 || std::fread(out.data(), 1, out.size(), file_) == out.size();
}

bool PmTilesArchive::decompress(int compression, std::vector<std::uint8_t>& data) const {
    if (compression == 0 || compression == 1) return true;
    if (compression == 2) {
        if (data.size() < 2 || data[0] != 0x1F || data[1] != 0x8B) return true;
        std::vector<std::uint8_t> out = gunzip(data);
        if (out.empty()) return false;
        data = std::move(out);
        return true;
    }
    return false;
}

PmTilesArchive::Directory PmTilesArchive::directory(std::uint64_t offset, std::uint64_t length) const {
    {
        std::lock_guard lock(mutex_);
        auto it = dirs_.find(offset);
        if (it != dirs_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.second);
            return it->second.first;
        }
    }
    std::vector<std::uint8_t> raw;
    if (!read(offset, length, raw) || !decompress(header_.internalCompression, raw)) return nullptr;
    Varints v{raw.data(), raw.data() + raw.size()};
    std::uint64_t count = v.next();
    if (v.failed || count > raw.size()) return nullptr;
    auto entries = std::make_shared<std::vector<Entry>>(static_cast<std::size_t>(count));
    std::uint64_t id = 0;
    for (auto& e : *entries) {
        id += v.next();
        e.tileId = id;
    }
    for (auto& e : *entries) e.runLength = static_cast<std::uint32_t>(v.next());
    for (auto& e : *entries) e.length = static_cast<std::uint32_t>(v.next());
    for (std::size_t i = 0; i < entries->size(); ++i) {
        std::uint64_t o = v.next();
        Entry& e = (*entries)[i];
        e.offset = (o == 0 && i > 0) ? (*entries)[i - 1].offset + (*entries)[i - 1].length : o - 1;
    }
    if (v.failed) return nullptr;
    std::lock_guard lock(mutex_);
    auto have = dirs_.find(offset);
    if (have != dirs_.end()) return have->second.first;
    lru_.push_front(offset);
    dirs_[offset] = {entries, lru_.begin()};
    while (dirs_.size() > kDirectoryCacheEntries) {
        if (lru_.back() == header_.rootOffset) {
            lru_.splice(lru_.begin(), lru_, std::prev(lru_.end()));
            continue;
        }
        dirs_.erase(lru_.back());
        lru_.pop_back();
    }
    return entries;
}

int PmTilesArchive::status(int z, int x, int y, std::vector<std::uint8_t>& out) const {
    out.clear();
    if (z < header_.minZoom || z > header_.maxZoom || x < 0 || y < 0 || x >= (1 << z) || y >= (1 << z)) return 404;
    std::uint64_t id = tileId(z, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    std::uint64_t offset = header_.rootOffset, length = header_.rootLength;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        Directory dir = directory(offset, length);
        if (!dir) return -1;
        auto it = std::upper_bound(dir->begin(), dir->end(), id, [](std::uint64_t v, const Entry& e) { return v < e.tileId; });
        if (it == dir->begin()) return 404;
        const Entry& e = *std::prev(it);
        if (e.runLength == 0) {
            offset = header_.leafOffset + e.offset;
            length = e.length;
            continue;
        }
        if (id >= e.tileId + e.runLength) return 404;
        if (!read(header_.dataOffset + e.offset, e.length, out) || !decompress(header_.tileCompression, out)) {
            out.clear();
            return -1;
        }
        return out.empty() ? 404 : 200;
    }
    return 404;
}

std::string PmTilesArchive::metadata() const {
    std::vector<std::uint8_t> raw;
    if (!read(header_.metadataOffset, header_.metadataLength, raw) || !decompress(header_.internalCompression, raw)) return {};
    return std::string(raw.begin(), raw.end());
}

}
