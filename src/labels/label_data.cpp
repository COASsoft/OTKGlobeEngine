#include "labels/label_data.hpp"

#include "otk/places.hpp"

#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(OTK_LABELS)
namespace otk::embedded {
extern const unsigned char labels_data[];
extern const std::size_t labels_data_size;
}
#endif

namespace otk::labels {

namespace {

struct Cursor {
    const std::uint8_t* p;
    const std::uint8_t* end;

    bool ok() const { return p <= end; }
    std::uint64_t varint() {
        std::uint64_t v = 0;
        int shift = 0;
        while (p < end && shift < 64) {
            std::uint8_t b = *p++;
            v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
            shift += 7;
        }
        p = end + 1;
        return 0;
    }
    std::uint32_t u32() {
        if (end - p < 4) {
            p = end + 1;
            return 0;
        }
        std::uint32_t v = static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
                          static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
        p += 4;
        return v;
    }
    const std::uint8_t* take(std::size_t n) {
        if (static_cast<std::size_t>(end - p) < n) {
            p = end + 1;
            return nullptr;
        }
        const std::uint8_t* r = p;
        p += n;
        return r;
    }
};

std::int64_t unzigzag(std::uint64_t v) { return static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1); }

}

const Dataset& Dataset::get() {
    static const Dataset instance;
    return instance;
}

Dataset::Dataset() {
#if defined(OTK_LABELS)
    data_ = embedded::labels_data;
    size_ = embedded::labels_data_size;
#endif
    if (!data_ || size_ < 12 || std::memcmp(data_, "OTKL", 4) != 0) return;
    Cursor c{data_ + 4, data_ + size_};
    version_ = c.u32();
    if (version_ < 1 || version_ > 2) return;
    std::uint32_t count = c.u32();
    if (!c.ok() || count > 100000) return;
    blocks_.resize(count);
    for (Block& b : blocks_) {
        b.kind = c.u32();
        b.key = c.u32();
        b.offset = c.u32();
        b.size = c.u32();
        b.raw = c.u32();
        if (!c.ok() || static_cast<std::size_t>(b.offset) + b.size > size_) {
            blocks_.clear();
            return;
        }
    }
    if (const Block* b = find(1, 0)) available_ = decodePlaces(*b);
}

const Dataset::Block* Dataset::find(std::uint32_t kind, std::uint32_t key) const {
    auto it = std::lower_bound(blocks_.begin(), blocks_.end(), std::make_pair(kind, key), [](const Block& b, const std::pair<std::uint32_t, std::uint32_t>& k) {
        return b.kind != k.first ? b.kind < k.first : b.key < k.second;
    });
    return it != blocks_.end() && it->kind == kind && it->key == key ? &*it : nullptr;
}

bool Dataset::inflate(const Block& b, std::vector<std::uint8_t>& out) const {
    out.resize(b.raw);
    size_t n = tinfl_decompress_mem_to_mem(out.data(), out.size(), data_ + b.offset, b.size, TINFL_FLAG_PARSE_ZLIB_HEADER);
    return n == b.raw;
}

bool Dataset::hasFine(int key) const { return find(3, static_cast<std::uint32_t>(key)) != nullptr; }

bool Dataset::hasCoarse(int key) const { return find(2, static_cast<std::uint32_t>(key)) != nullptr; }

bool Dataset::fine(int key, Lines& out) const {
    const Block* b = find(3, static_cast<std::uint32_t>(key));
    return b && decodeLines(*b, false, out);
}

bool Dataset::coarse(int key, Lines& out) const {
    const Block* b = find(2, static_cast<std::uint32_t>(key));
    return b && decodeLines(*b, true, out);
}

bool Dataset::decodeLines(const Block& b, bool importance, Lines& out) const {
    std::vector<std::uint8_t> raw;
    if (!inflate(b, raw)) return false;
    Cursor c{raw.data(), raw.data() + raw.size()};
    std::uint64_t runs = c.varint();
    if (!c.ok() || runs > raw.size()) return false;
    out.runs.resize(static_cast<size_t>(runs));
    std::uint64_t total = 0;
    for (Run& r : out.runs) {
        const std::uint8_t* level = c.take(1);
        std::uint64_t n = c.varint();
        if (!level || !c.ok() || n > raw.size()) return false;
        r.level = static_cast<Level>(std::min<std::uint8_t>(*level, 3));
        r.first = static_cast<std::uint32_t>(total);
        r.count = static_cast<std::uint32_t>(n);
        r.left = r.right = -1;
        if (version_ >= 2 && *level == 3) {
            const std::uint64_t left = c.varint(), right = c.varint();
            if (!c.ok()) return false;
            if (left > 0 && left <= places_.size()) r.left = static_cast<std::int32_t>(left - 1);
            if (right > 0 && right <= places_.size()) r.right = static_cast<std::int32_t>(right - 1);
        }
        total += n;
    }
    const size_t n = static_cast<size_t>(total);
    const std::uint8_t* planes = c.take(n * 6);
    if (!planes) return false;
    const std::uint8_t* classes = importance ? c.take(n) : nullptr;
    if (importance && !classes) return false;
    out.xy.resize(n * 2);
    std::int64_t x = 0, y = 0;
    for (size_t i = 0; i < n; ++i) {
        std::uint64_t zx = planes[i] | static_cast<std::uint64_t>(planes[n + i]) << 8 | static_cast<std::uint64_t>(planes[2 * n + i]) << 16;
        std::uint64_t zy = planes[3 * n + i] | static_cast<std::uint64_t>(planes[4 * n + i]) << 8 | static_cast<std::uint64_t>(planes[5 * n + i]) << 16;
        x += unzigzag(zx);
        y += unzigzag(zy);
        out.xy[2 * i] = static_cast<std::int32_t>(x);
        out.xy[2 * i + 1] = static_cast<std::int32_t>(y);
    }
    if (importance) out.importance.assign(classes, classes + n);
    else out.importance.clear();
    return true;
}

bool Dataset::decodePlaces(const Block& b) {
    std::vector<std::uint8_t> raw;
    if (!inflate(b, raw)) return false;
    Cursor c{raw.data(), raw.data() + raw.size()};
    std::uint32_t count = c.u32();
    if (!c.ok() || count > raw.size()) return false;
    starts_.resize(kPlaceCols * kPlaceRows + 1);
    for (std::uint32_t& s : starts_) s = std::min(c.u32(), count);
    const std::uint8_t* streams[8] = {};
    std::size_t lengths[8] = {};
    for (int i = 0; i < 8; ++i) {
        lengths[i] = c.u32();
        streams[i] = c.take(lengths[i]);
        if (!streams[i]) return false;
    }
    for (int i : {0, 1, 2, 3, 6})
        if (lengths[i] != count) return false;
    places_.resize(count);
    Cursor lat{streams[4], streams[4] + lengths[4]}, lon{streams[5], streams[5] + lengths[5]}, names{streams[7], streams[7] + lengths[7]};
    names_.reserve(lengths[7]);
    std::int64_t qlat = 0, qlon = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        Place& p = places_[i];
        p.kind = static_cast<Kind>(std::min<std::uint8_t>(streams[0][i], 4));
        p.flags = streams[1][i];
        p.minZoom = streams[2][i];
        p.maxZoom = streams[3][i];
        p.population = streams[6][i];
        qlat += unzigzag(lat.varint());
        qlon += unzigzag(lon.varint());
        p.lat = static_cast<float>(qlat * kUnit);
        p.lon = static_cast<float>(qlon * kUnit);
        std::uint64_t len = names.varint();
        const std::uint8_t* s = names.take(static_cast<size_t>(len));
        if (!lat.ok() || !lon.ok() || !s || len > 0xFFFF) return false;
        p.name = static_cast<std::uint32_t>(names_.size());
        p.length = static_cast<std::uint16_t>(len);
        names_.append(reinterpret_cast<const char*>(s), static_cast<size_t>(len));
    }
    return true;
}

}

namespace otk {

bool placesAvailable() { return labels::Dataset::get().available(); }

std::vector<Place> placesNear(double lat, double lon, double radiusMeters, std::size_t limit, const std::vector<PlaceKind>& kinds) {
    std::vector<Place> out;
    const labels::Dataset& d = labels::Dataset::get();
    if (!d.available() || limit == 0) return out;
    constexpr double kRad = 3.14159265358979323846 / 180.0;
    radiusMeters = std::clamp(radiusMeters, 1.0, 2.0e7);
    double dlat = radiusMeters / 111320.0;
    double dlon = dlat / std::max(std::cos(lat * kRad), 0.01);
    int r0 = std::clamp(static_cast<int>(std::floor((lat - dlat + 90.0) / labels::kPlaceCell)), 0, labels::kPlaceRows - 1);
    int r1 = std::clamp(static_cast<int>(std::floor((lat + dlat + 90.0) / labels::kPlaceCell)), 0, labels::kPlaceRows - 1);
    int span = dlon >= 180.0 ? labels::kPlaceCols : static_cast<int>(std::ceil(2.0 * dlon / labels::kPlaceCell)) + 1;
    int c0 = static_cast<int>(std::floor((lon - dlon + 180.0) / labels::kPlaceCell));
    for (int r = r0; r <= r1; ++r)
        for (int k = 0; k < std::min(span, labels::kPlaceCols); ++k) {
            int col = ((c0 + k) % labels::kPlaceCols + labels::kPlaceCols) % labels::kPlaceCols;
            int cell = r * labels::kPlaceCols + col;
            for (std::uint32_t i = d.cellBegin(cell); i < d.cellEnd(cell); ++i) {
                const labels::Place& p = d.places()[i];
                PlaceKind kind = static_cast<PlaceKind>(p.kind);
                if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) continue;
                double la1 = lat * kRad, la2 = p.lat * kRad, dl = (p.lon - lon) * kRad;
                double s = std::sin((la2 - la1) * 0.5), t = std::sin(dl * 0.5);
                double h = s * s + std::cos(la1) * std::cos(la2) * t * t;
                double dist = 2.0 * 6371008.8 * std::asin(std::min(1.0, std::sqrt(h)));
                if (dist > radiusMeters) continue;
                Place q;
                q.name = std::string(d.name(p));
                std::replace(q.name.begin(), q.name.end(), '\n', ' ');
                q.lat = p.lat;
                q.lon = p.lon;
                q.kind = kind;
                q.minZoom = p.minZoom / 10.0;
                q.distanceMeters = dist;
                q.capital = (p.flags & labels::kCapital) != 0;
                out.push_back(std::move(q));
            }
        }
    std::sort(out.begin(), out.end(), [](const Place& a, const Place& b) { return a.distanceMeters < b.distanceMeters; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

}
