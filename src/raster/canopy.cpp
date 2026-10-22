#include "raster/canopy.hpp"

#include "net/http.hpp"
#include "raster/tile_cache.hpp"

#include <miniz.h>

#include <algorithm>
#include <cstring>

namespace otk {

namespace {

constexpr int kSourceZoom = 10;
constexpr const char* kBaseUrl = "https://data.source.coop/tge-labs/meta-chm-v2/chm/";
constexpr const char* kTileKey = "canopy|meta-chm-v2";
constexpr const char* kHeaderKey = "canopy-header|meta-chm-v2";
constexpr const char* kUserAgent = "OTKGlobeEngine/0.1";
constexpr std::int64_t kNever = std::int64_t(1) << 50;
constexpr std::uint64_t kHeaderBytes = 65536;

std::string quadkey(int x, int y, int z) {
    std::string q;
    for (int i = z - 1; i >= 0; --i) q.push_back(static_cast<char>('0' + (((x >> i) & 1) | (((y >> i) & 1) << 1))));
    return q;
}

std::uint64_t read(const std::vector<std::uint8_t>& b, std::size_t at, int bytes) {
    std::uint64_t v = 0;
    if (at + static_cast<std::size_t>(bytes) > b.size()) return 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | b[at + static_cast<std::size_t>(i)];
    return v;
}

void put64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::vector<std::uint8_t> pack(const std::vector<std::uint8_t>& raw) {
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::uint8_t> out(size);
    if (mz_compress(out.data(), &size, raw.data(), static_cast<mz_ulong>(raw.size())) != MZ_OK) return {};
    out.resize(size);
    return out;
}

bool unpack(const std::vector<std::uint8_t>& in, std::size_t expected, std::vector<std::uint8_t>& out) {
    out.assign(expected, 0);
    mz_ulong size = static_cast<mz_ulong>(expected);
    return mz_uncompress(out.data(), &size, in.data(), static_cast<mz_ulong>(in.size())) == MZ_OK && size == expected;
}

}

CanopySource::CanopySource(TileCache* cache) : cache_(cache) {}

bool CanopySource::range(const std::string& url, std::uint64_t first, std::uint64_t count, std::vector<std::uint8_t>& out, int& status) {
    net::Headers headers{{"Range", "bytes=" + std::to_string(first) + "-" + std::to_string(first + count - 1)}};
    net::Response r = net::get(url, kUserAgent, headers);
    status = r.status;
    if (r.status == 206) {
        out = std::move(r.body);
        return true;
    }
    if (r.status == 200 && r.body.size() >= first + count) {
        out.assign(r.body.begin() + static_cast<std::ptrdiff_t>(first), r.body.begin() + static_cast<std::ptrdiff_t>(first + count));
        return true;
    }
    return false;
}

bool CanopySource::header(int qx, int qy, Header& out) {
    const std::uint64_t key = (static_cast<std::uint64_t>(qx) << 32) | static_cast<std::uint32_t>(qy);
    {
        std::lock_guard lock(mutex_);
        auto it = headers_.find(key);
        if (it != headers_.end()) {
            out = it->second;
            return true;
        }
    }
    Header h;
    std::vector<std::uint8_t> stored;
    std::int64_t expires = 0;
    if (cache_ && cache_->get(kHeaderKey, kSourceZoom, qx, qy, stored, expires) && stored.size() >= 9) {
        h.missing = stored[0] != 0;
        h.across = static_cast<int>(read(stored, 1, 4));
        h.block = static_cast<int>(read(stored, 5, 4));
        const std::size_t n = (stored.size() - 9) / 16;
        for (std::size_t i = 0; i < n; ++i) {
            h.offsets.push_back(read(stored, 9 + i * 16, 8));
            h.counts.push_back(read(stored, 9 + i * 16 + 8, 8));
        }
    } else {
        if (offline_) return false;
        const std::string url = kBaseUrl + quadkey(qx, qy, kSourceZoom) + ".tif";
        std::vector<std::uint8_t> b;
        int status = 0;
        if (!range(url, 0, kHeaderBytes, b, status)) {
            if (status != 404 && status != 403) return false;
            h.missing = true;
        } else {
            if (b.size() < 16 || b[0] != 'I' || b[1] != 'I') return false;
            const bool big = read(b, 2, 2) == 43;
            std::uint64_t ifd = big ? read(b, 8, 8) : read(b, 4, 4);
            const std::uint64_t entries = big ? read(b, static_cast<std::size_t>(ifd), 8) : read(b, static_cast<std::size_t>(ifd), 2);
            const std::size_t first = static_cast<std::size_t>(ifd) + (big ? 8 : 2);
            const std::size_t size = big ? 20 : 12;
            std::uint64_t width = 0, tileWidth = 0, offAt = 0, cntAt = 0, n = 0;
            int offBytes = 4, cntBytes = 4;
            for (std::uint64_t i = 0; i < entries; ++i) {
                const std::size_t e = first + static_cast<std::size_t>(i) * size;
                const auto tag = read(b, e, 2), type = read(b, e + 2, 2);
                const auto count = big ? read(b, e + 4, 8) : read(b, e + 4, 4);
                const int typeBytes = type == 3 ? 2 : (type == 16 ? 8 : 4);
                const std::uint64_t value = read(b, e + (big ? 12 : 8), count == 1 ? typeBytes : (big ? 8 : 4));
                if (tag == 256) width = value;
                if (tag == 322) tileWidth = value;
                if (tag == 324) {
                    offAt = value;
                    n = count;
                    offBytes = typeBytes;
                }
                if (tag == 325) {
                    cntAt = value;
                    cntBytes = typeBytes;
                }
            }
            if (!width || !tileWidth || !n || !offAt || !cntAt) return false;
            const std::uint64_t end = std::max(offAt + n * offBytes, cntAt + n * cntBytes);
            if (end > b.size()) {
                std::vector<std::uint8_t> more;
                if (!range(url, b.size(), end - b.size(), more, status)) return false;
                b.insert(b.end(), more.begin(), more.end());
            }
            h.across = static_cast<int>(width / tileWidth);
            h.block = static_cast<int>(tileWidth);
            for (std::uint64_t i = 0; i < n; ++i) {
                h.offsets.push_back(read(b, static_cast<std::size_t>(offAt + i * offBytes), offBytes));
                h.counts.push_back(read(b, static_cast<std::size_t>(cntAt + i * cntBytes), cntBytes));
            }
        }
        if (cache_) {
            std::vector<std::uint8_t> blob{static_cast<std::uint8_t>(h.missing ? 1 : 0)};
            for (int i = 0; i < 4; ++i) blob.push_back(static_cast<std::uint8_t>(h.across >> (8 * i)));
            for (int i = 0; i < 4; ++i) blob.push_back(static_cast<std::uint8_t>(h.block >> (8 * i)));
            for (std::size_t i = 0; i < h.offsets.size(); ++i) {
                put64(blob, h.offsets[i]);
                put64(blob, h.counts[i]);
            }
            cache_->put(kHeaderKey, kSourceZoom, qx, qy, blob, kNever, true);
        }
    }
    std::lock_guard lock(mutex_);
    headers_[key] = h;
    out = std::move(h);
    return true;
}

CanopySource::Result CanopySource::heights(int x, int y, std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> stored;
    std::int64_t expires = 0;
    if (cache_ && cache_->get(kTileKey, kCanopyZoom, x, y, stored, expires)) {
        if (stored.size() == 1) return Empty;
        if (unpack(stored, static_cast<std::size_t>(kCanopySize) * kCanopySize, out)) return Ready;
    }
    if (offline_) return Failed;
    const int shift = kCanopyZoom - kSourceZoom;
    const int qx = x >> shift, qy = y >> shift;
    Header h;
    if (!header(qx, qy, h)) return Failed;
    bool any = false;
    if (!h.missing && h.across > 0 && h.block > 0) {
        const int sub = 1 << shift;
        const int tilePixels = h.across * h.block / sub;
        const int blocks = std::max(1, tilePixels / h.block);
        const int pool = std::max(1, tilePixels / kCanopySize);
        const std::string url = kBaseUrl + quadkey(qx, qy, kSourceZoom) + ".tif";
        out.assign(static_cast<std::size_t>(kCanopySize) * kCanopySize, 0);
        std::vector<std::uint8_t> raw, pixels;
        for (int by = 0; by < blocks; ++by)
            for (int bx = 0; bx < blocks; ++bx) {
                const int gx = (x & (sub - 1)) * blocks + bx, gy = (y & (sub - 1)) * blocks + by;
                const std::size_t index = static_cast<std::size_t>(gy) * h.across + gx;
                if (index >= h.offsets.size() || h.counts[index] == 0) continue;
                int status = 0;
                if (!range(url, h.offsets[index], h.counts[index], raw, status)) return Failed;
                if (!unpack(raw, static_cast<std::size_t>(h.block) * h.block, pixels)) continue;
                for (int py = 0; py < h.block; ++py) {
                    const int oy = (by * h.block + py) / pool;
                    if (oy >= kCanopySize) break;
                    for (int px = 0; px < h.block; ++px) {
                        const std::uint8_t v = pixels[static_cast<std::size_t>(py) * h.block + px];
                        if (!v || v > 120) continue;
                        const int ox = (bx * h.block + px) / pool;
                        if (ox >= kCanopySize) break;
                        std::uint8_t& o = out[static_cast<std::size_t>(oy) * kCanopySize + ox];
                        if (v > o) o = v;
                        any = true;
                    }
                }
            }
    }
    if (cache_) cache_->put(kTileKey, kCanopyZoom, x, y, any ? pack(out) : std::vector<std::uint8_t>{0}, kNever, true);
    return any ? Ready : Empty;
}

}
