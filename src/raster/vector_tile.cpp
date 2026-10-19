#include "raster/vector_tile.hpp"

#include <earcut.hpp>
#include <miniz.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace ote {

namespace {

constexpr double kPi = 3.14159265358979323846;

}

std::vector<std::uint8_t> gunzip(const std::vector<std::uint8_t>& in) {
    if (in.size() < 18 || in[0] != 0x1F || in[1] != 0x8B) return in;
    size_t pos = 10;
    std::uint8_t flags = in[3];
    if (flags & 4) pos += 2 + (in[10] | (in[11] << 8));
    if (flags & 8)
        while (pos < in.size() && in[pos++]) {
        }
    if (flags & 16)
        while (pos < in.size() && in[pos++]) {
        }
    if (flags & 2) pos += 2;
    if (pos >= in.size()) return {};
    size_t outLen = 0;
    void* out = tinfl_decompress_mem_to_heap(in.data() + pos, in.size() - pos - 8, &outLen, 0);
    if (!out) return {};
    std::vector<std::uint8_t> r(static_cast<std::uint8_t*>(out), static_cast<std::uint8_t*>(out) + outLen);
    mz_free(out);
    return r;
}

namespace {

struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;
    bool ok() const { return p < end; }
    std::uint64_t varint() {
        std::uint64_t v = 0;
        int shift = 0;
        while (p < end) {
            std::uint8_t b = *p++;
            v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift > 63) break;
        }
        return v;
    }
    Reader sub() {
        std::uint64_t n = varint();
        if (n > static_cast<std::uint64_t>(end - p)) n = static_cast<std::uint64_t>(end - p);
        Reader r{p, p + n};
        p += n;
        return r;
    }
    void skip(int wire) {
        if (wire == 0) varint();
        else if (wire == 1) p += std::min<std::ptrdiff_t>(8, end - p);
        else if (wire == 2) sub();
        else if (wire == 5) p += std::min<std::ptrdiff_t>(4, end - p);
        else p = end;
    }
};

struct Rule {
    std::uint32_t color;
    float width;
    bool fill;
};

bool styleFor(std::string_view layer, std::string_view cls, int z, Rule& r) {
    if (layer == "water") return r = {0xFFA0C8F0, 0, true}, true;
    if (layer == "landcover") {
        if (cls == "wood" || cls == "forest") return r = {0xFFADD19E, 0, true}, true;
        if (cls == "grass" || cls == "farmland") return r = {0xFFCDEBB0, 0, true}, true;
        if (cls == "ice" || cls == "glacier") return r = {0xFFF2F8FF, 0, true}, true;
        if (cls == "sand" || cls == "rock") return r = {0xFFEDE4C8, 0, true}, true;
        if (cls == "wetland") return r = {0xFFC8E0D8, 0, true}, true;
        return false;
    }
    if (layer == "landuse") {
        if (cls == "residential") return r = {0xFFE6E1DA, 0, true}, true;
        if (cls == "industrial" || cls == "commercial" || cls == "retail") return r = {0xFFEBDBE8, 0, true}, true;
        if (cls == "cemetery" || cls == "stadium" || cls == "pitch") return r = {0xFFD5ECC9, 0, true}, true;
        return false;
    }
    if (layer == "park") return r = {0xFFC8FACC, 0, true}, true;
    if (layer == "building") return z >= 13 ? (r = {0xFFD9D0C9, 0, true}, true) : false;
    if (layer == "aeroway") return r = {0xFFDADAE0, 1.0f, cls != "runway" && cls != "taxiway"}, true;
    if (layer == "waterway") return r = {0xFFA0C8F0, cls == "river" ? 2.0f : 1.0f, false}, true;
    if (layer == "boundary") return r = {0xFF9E9CAB, 1.0f, false}, true;
    if (layer == "transportation") {
        if (cls == "motorway") return r = {0xFFE892A2, 3.0f, false}, true;
        if (cls == "trunk" || cls == "primary") return r = {0xFFFCD6A4, 2.6f, false}, true;
        if (cls == "secondary" || cls == "tertiary") return r = {0xFFF7FABF, 2.2f, false}, true;
        if (cls == "minor" || cls == "service") return z >= 12 ? (r = {0xFFFFFFFF, 1.4f, false}, true) : false;
        if (cls == "rail" || cls == "transit") return r = {0xFF999999, 1.0f, false}, true;
        if (cls == "path" || cls == "track") return z >= 13 ? (r = {0xFFB0A090, 0.8f, false}, true) : false;
        return false;
    }
    return false;
}

void toLonLat(int z, int x, int y, double u, double v, float out[2]) {
    double n = static_cast<double>(1 << z);
    double mx = (x + u) / n, my = (y + v) / n;
    out[0] = static_cast<float>(mx * 360.0 - 180.0);
    out[1] = static_cast<float>(std::atan(std::sinh(kPi * (1.0 - 2.0 * my))) * 180.0 / kPi);
}

}

bool looksLikeVectorTile(const std::vector<std::uint8_t>& d) {
    if (d.size() >= 2 && d[0] == 0x1F && d[1] == 0x8B) return true;
    return !d.empty() && d[0] == 0x1A;
}

std::shared_ptr<VectorTile> decodeVectorTile(const std::vector<std::uint8_t>& raw, int z, int x, int y) {
    std::vector<std::uint8_t> data = gunzip(raw);
    if (data.empty()) return nullptr;
    auto tile = std::make_shared<VectorTile>();
    Reader t{data.data(), data.data() + data.size()};
    while (t.ok()) {
        std::uint64_t key = t.varint();
        if ((key >> 3) != 3 || (key & 7) != 2) {
            t.skip(static_cast<int>(key & 7));
            continue;
        }
        Reader layer = t.sub();
        std::string_view name;
        std::vector<std::string_view> keys;
        std::vector<std::string_view> values;
        std::vector<double> numbers;
        std::vector<Reader> features;
        std::uint32_t extent = 4096;
        while (layer.ok()) {
            std::uint64_t k = layer.varint();
            int field = static_cast<int>(k >> 3), wire = static_cast<int>(k & 7);
            if (field == 1 && wire == 2) {
                Reader s = layer.sub();
                name = std::string_view(reinterpret_cast<const char*>(s.p), static_cast<size_t>(s.end - s.p));
            } else if (field == 2 && wire == 2) {
                features.push_back(layer.sub());
            } else if (field == 3 && wire == 2) {
                Reader s = layer.sub();
                keys.emplace_back(reinterpret_cast<const char*>(s.p), static_cast<size_t>(s.end - s.p));
            } else if (field == 4 && wire == 2) {
                Reader v = layer.sub();
                std::string_view sv;
                double num = 0;
                while (v.ok()) {
                    std::uint64_t vk = v.varint();
                    int vf = static_cast<int>(vk >> 3), vw = static_cast<int>(vk & 7);
                    if (vf == 1 && vw == 2) {
                        Reader s = v.sub();
                        sv = std::string_view(reinterpret_cast<const char*>(s.p), static_cast<size_t>(s.end - s.p));
                    } else if ((vf == 4 || vf == 5) && vw == 0) {
                        num = static_cast<double>(static_cast<std::int64_t>(v.varint()));
                    } else if (vf == 6 && vw == 0) {
                        std::uint64_t raw = v.varint();
                        num = static_cast<double>(static_cast<std::int64_t>((raw >> 1) ^ (~(raw & 1) + 1)));
                    } else if (vf == 3 && vw == 1 && v.end - v.p >= 8) {
                        std::memcpy(&num, v.p, 8);
                        v.p += 8;
                    } else if (vf == 2 && vw == 5 && v.end - v.p >= 4) {
                        float f;
                        std::memcpy(&f, v.p, 4);
                        num = f;
                        v.p += 4;
                    } else {
                        v.skip(vw);
                    }
                }
                values.push_back(sv);
                numbers.push_back(num);
            } else if (field == 5 && wire == 0) {
                extent = static_cast<std::uint32_t>(layer.varint());
            } else {
                layer.skip(wire);
            }
        }
        int classKey = -1, nameKey = -1, latinKey = -1, rankKey = -1, eleKey = -1;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == "class") classKey = static_cast<int>(i);
            else if (keys[i] == "name") nameKey = static_cast<int>(i);
            else if (keys[i] == "name:latin" || keys[i] == "name_en") latinKey = static_cast<int>(i);
            else if (keys[i] == "rank") rankKey = static_cast<int>(i);
            else if (keys[i] == "ele") eleKey = static_cast<int>(i);
        }
        bool labelLayer = name == "place" || name == "mountain_peak" || name == "water_name";
        for (Reader f : features) {
            int type = 0;
            std::string_view cls, label, latin;
            double rank = 30, ele = -1e9;
            Reader geom{nullptr, nullptr};
            while (f.ok()) {
                std::uint64_t k = f.varint();
                int field = static_cast<int>(k >> 3), wire = static_cast<int>(k & 7);
                if (field == 2 && wire == 2) {
                    Reader tags = f.sub();
                    while (tags.ok()) {
                        std::uint64_t tk = tags.varint(), tv = tags.varint();
                        if (tv >= values.size()) continue;
                        if (static_cast<int>(tk) == classKey) cls = values[tv];
                        else if (static_cast<int>(tk) == nameKey) label = values[tv];
                        else if (static_cast<int>(tk) == latinKey) latin = values[tv];
                        else if (static_cast<int>(tk) == rankKey) rank = numbers[tv];
                        else if (static_cast<int>(tk) == eleKey) ele = numbers[tv];
                    }
                } else if (field == 3 && wire == 0) {
                    type = static_cast<int>(f.varint());
                } else if (field == 4 && wire == 2) {
                    geom = f.sub();
                } else {
                    f.skip(wire);
                }
            }
            bool wantLabel = labelLayer;
            if (name == "place") {
                int minZ = cls == "country" || cls == "state" || cls == "city" ? 0 : cls == "town" ? 8 : cls == "village" ? 10 : 12;
                wantLabel = z >= minZ;
            } else if (name == "mountain_peak") {
                wantLabel = z >= 11;
            }
            if (labelLayer && !wantLabel) continue;
            if (wantLabel && type == 1 && geom.p && (!label.empty() || !latin.empty())) {
                Reader g = geom;
                std::uint32_t cmd = static_cast<std::uint32_t>(g.varint());
                if ((cmd & 7) == 1 && g.ok()) {
                    std::uint32_t dx = static_cast<std::uint32_t>(g.varint()), dy = static_cast<std::uint32_t>(g.varint());
                    long px = static_cast<long>((dx >> 1) ^ (~(dx & 1) + 1)), py = static_cast<long>((dy >> 1) ^ (~(dy & 1) + 1));
                    if (px >= 0 && py >= 0 && px <= static_cast<long>(extent) && py <= static_cast<long>(extent)) {
                        VectorLabel vl;
                        float ll[2];
                        toLonLat(z, x, y, static_cast<double>(px) / extent, static_cast<double>(py) / extent, ll);
                        vl.lon = ll[0];
                        vl.lat = ll[1];
                        vl.text = std::string(label.empty() ? latin : label);
                        double base = name == "place" ? (cls == "country" ? 0 : cls == "state" ? 10 : cls == "city" ? 20 : cls == "town" ? 30 : 40)
                                                      : name == "mountain_peak" ? 45 : 50;
                        vl.priority = base + rank;
                        if (name == "place") {
                            vl.size = cls == "city" || cls == "country" ? 15.0f : cls == "town" ? 13.0f : 11.5f;
                            vl.color = 0xFF2A2A2A;
                        } else if (name == "mountain_peak") {
                            vl.size = 11.0f;
                            vl.color = 0xFF6B4A2A;
                            if (ele > -1e8) vl.text += " " + std::to_string(static_cast<int>(std::lround(ele))) + " m";
                        } else {
                            vl.size = 11.5f;
                            vl.color = 0xFF2C5C9C;
                        }
                        tile->labels.push_back(std::move(vl));
                    }
                }
                continue;
            }
            Rule rule;
            if (type < 2 || !geom.p || !styleFor(name, cls, z, rule)) continue;
            std::vector<std::vector<std::array<double, 2>>> rings;
            long cx = 0, cy = 0;
            while (geom.ok()) {
                std::uint32_t cmd = static_cast<std::uint32_t>(geom.varint());
                std::uint32_t id = cmd & 7, count = cmd >> 3;
                if (id == 1 || id == 2) {
                    for (std::uint32_t i = 0; i < count && geom.ok(); ++i) {
                        std::uint32_t dx = static_cast<std::uint32_t>(geom.varint()), dy = static_cast<std::uint32_t>(geom.varint());
                        cx += static_cast<long>((dx >> 1) ^ (~(dx & 1) + 1));
                        cy += static_cast<long>((dy >> 1) ^ (~(dy & 1) + 1));
                        if (id == 1) rings.emplace_back();
                        if (!rings.empty()) rings.back().push_back({static_cast<double>(cx) / extent, static_cast<double>(cy) / extent});
                    }
                } else if (id == 7) {
                    if (!rings.empty() && !rings.back().empty()) rings.back().push_back(rings.back().front());
                }
            }
            if (rule.fill && type == 3) {
                std::vector<std::vector<std::array<double, 2>>> poly;
                auto flush = [&]() {
                    if (poly.empty()) return;
                    VectorShape s;
                    s.color = rule.color;
                    std::vector<std::uint32_t> idx = mapbox::earcut<std::uint32_t>(poly);
                    std::vector<std::array<double, 2>> flat;
                    for (const auto& r : poly) flat.insert(flat.end(), r.begin(), r.end());
                    for (std::uint32_t i : idx) {
                        float ll[2];
                        toLonLat(z, x, y, flat[i][0], flat[i][1], ll);
                        s.triangles.push_back({ll[0], ll[1]});
                    }
                    if (!s.triangles.empty()) tile->fills.push_back(std::move(s));
                    poly.clear();
                };
                for (auto& ring : rings) {
                    double area = 0;
                    for (size_t i = 1; i < ring.size(); ++i)
                        area += ring[i - 1][0] * ring[i][1] - ring[i][0] * ring[i - 1][1];
                    if (area > 0) flush();
                    if (area != 0 && (area > 0 || !poly.empty())) poly.push_back(ring);
                }
                flush();
            } else if (rule.width > 0) {
                VectorShape s;
                s.color = rule.color;
                s.width = rule.width;
                for (auto& ring : rings) {
                    if (ring.size() < 2) continue;
                    s.breaks.push_back(static_cast<std::uint32_t>(s.points.size()));
                    for (auto& p : ring) {
                        float ll[2];
                        toLonLat(z, x, y, p[0], p[1], ll);
                        s.points.push_back({ll[0], ll[1]});
                    }
                }
                if (!s.points.empty()) tile->lines.push_back(std::move(s));
            }
        }
    }
    float ll[2];
    toLonLat(z, x, y, 0.0, 1.0, ll);
    tile->originLon = ll[0];
    tile->originLat = ll[1];
    auto color = [](std::uint32_t argb, float c[4]) {
        c[0] = ((argb >> 16) & 0xFF) / 255.0f;
        c[1] = ((argb >> 8) & 0xFF) / 255.0f;
        c[2] = (argb & 0xFF) / 255.0f;
        c[3] = ((argb >> 24) & 0xFF) / 255.0f;
    };
    float c[4];
    for (const VectorShape& s : tile->fills) {
        color(s.color, c);
        for (const auto& p : s.triangles)
            tile->fillVertices.insert(tile->fillVertices.end(),
                                      {static_cast<float>(p[0] - tile->originLon), static_cast<float>(p[1] - tile->originLat), c[0], c[1], c[2], c[3]});
    }
    for (const VectorShape& s : tile->lines) {
        color(s.color, c);
        for (size_t r = 0; r < s.breaks.size(); ++r) {
            size_t from = s.breaks[r], to = r + 1 < s.breaks.size() ? s.breaks[r + 1] : s.points.size();
            for (size_t i = from; i + 1 < to; ++i) {
                float ax = static_cast<float>(s.points[i][0] - tile->originLon), ay = static_cast<float>(s.points[i][1] - tile->originLat);
                float bx = static_cast<float>(s.points[i + 1][0] - tile->originLon), by = static_cast<float>(s.points[i + 1][1] - tile->originLat);
                if (ax == bx && ay == by) continue;
                float v[4][5] = {{ax, ay, bx, by, 1}, {bx, by, ax, ay, -1}, {bx, by, ax, ay, 1}, {ax, ay, bx, by, -1}};
                for (int k : {0, 1, 2, 0, 2, 3})
                    tile->lineVertices.insert(tile->lineVertices.end(),
                                              {v[k][0], v[k][1], v[k][2], v[k][3], v[k][4], s.width * 0.5f, c[0], c[1], c[2], c[3]});
            }
        }
    }
    tile->fills.clear();
    tile->lines.clear();
    return tile;
}

}
