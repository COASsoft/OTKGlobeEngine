#include "feature/buildings.hpp"

#include "core/geo.hpp"

#include <earcut.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace otk {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEdge = 1e-7;
constexpr double kBury = 2.0;
constexpr double kOverhang = 0.35;
constexpr double kTargetMpp = 0.3;
constexpr int kPad = 2;
constexpr int kMaxCell = 512;

const BuildingMaterial kMaterials[BuildingTextures] = {
    {30.0f, 30.0f, 0.0f}, {36.0f, 36.0f, 0.0f}, {29.0f, 29.0f, 0.0f}, {45.0f, 45.0f, 0.0f}, {20.0f, 20.0f, 0.0f}, {20.0f, 20.0f, 0.0f},
    {20.0f, 20.0f, 0.0f}, {4.0f, 4.0f, 1.0f},   {5.0f, 5.0f, 1.0f},   {6.0f, 6.0f, 2.0f},   {6.0f, 6.0f, 3.0f},   {6.0f, 6.0f, 0.0f}};

constexpr std::uint32_t rgb(int r, int g, int b) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 | 0xFF000000u;
}

constexpr std::uint32_t kWhite = rgb(255, 255, 255);
constexpr std::uint32_t kPaint[] = {rgb(242, 240, 233), rgb(226, 214, 190), rgb(206, 206, 202), rgb(192, 205, 214),
                                    rgb(198, 206, 186), rgb(234, 224, 182), rgb(212, 192, 162), rgb(224, 208, 200)};

using Ring = std::vector<std::array<double, 2>>;

struct Prepared {
    std::vector<Ring> rings;
    float height = 0.0f;
    float minHeight = 0.0f;
    std::uint32_t colour = 0;
};

struct Point {
    double x, y, z;
    Vec3 up;
    double u, v;
};

struct Obb {
    double cx = 0, cy = 0, ax = 1, ay = 0, halfL = 0, halfW = 0, area = std::numeric_limits<double>::infinity();
};

void toGeo(int z, int x, int y, const std::array<double, 2>& p, double& lat, double& lon) {
    const double n = std::ldexp(1.0, z);
    lon = (x + p[0]) / n * 360.0 - 180.0;
    lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * (y + p[1]) / n))) * 180.0 / kPi;
}

Ring clipRing(const std::vector<std::array<float, 2>>& in) {
    Ring ring;
    ring.reserve(in.size());
    for (const auto& p : in) ring.push_back({p[0], p[1]});
    if (ring.size() >= 2 && ring.front() == ring.back()) ring.pop_back();
    for (int edge = 0; edge < 4 && ring.size() >= 3; ++edge) {
        const int axis = edge / 2;
        const bool lower = edge % 2 == 0;
        const double bound = lower ? 0.0 : 1.0;
        auto inside = [&](const std::array<double, 2>& p) { return lower ? p[axis] >= bound : p[axis] <= bound; };
        Ring out;
        out.reserve(ring.size() + 4);
        for (size_t i = 0; i < ring.size(); ++i) {
            const auto& a = ring[i];
            const auto& b = ring[(i + 1) % ring.size()];
            const bool ia = inside(a), ib = inside(b);
            if (ia) out.push_back(a);
            if (ia != ib) {
                const double t = (bound - a[axis]) / (b[axis] - a[axis]);
                std::array<double, 2> p{a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t};
                p[axis] = bound;
                out.push_back(p);
            }
        }
        ring.swap(out);
    }
    Ring clean;
    clean.reserve(ring.size());
    for (const auto& p : ring)
        if (clean.empty() || std::abs(p[0] - clean.back()[0]) > 1e-9 || std::abs(p[1] - clean.back()[1]) > 1e-9) clean.push_back(p);
    while (clean.size() >= 2 && std::abs(clean.front()[0] - clean.back()[0]) <= 1e-9 && std::abs(clean.front()[1] - clean.back()[1]) <= 1e-9)
        clean.pop_back();
    return clean;
}

std::vector<Prepared> prepare(const std::vector<BuildingShape>& shapes) {
    std::vector<Prepared> out;
    out.reserve(shapes.size());
    for (const BuildingShape& s : shapes) {
        if (s.rings.empty() || !(s.height > s.minHeight)) continue;
        Prepared p;
        p.height = s.height;
        p.minHeight = s.minHeight;
        p.colour = s.colour;
        for (size_t r = 0; r < s.rings.size(); ++r) {
            Ring ring = clipRing(s.rings[r]);
            if (ring.size() < 3) {
                if (r == 0) break;
                continue;
            }
            p.rings.push_back(std::move(ring));
        }
        if (!p.rings.empty()) out.push_back(std::move(p));
    }
    return out;
}

double signedArea(const std::vector<Point>& ring) {
    double a = 0.0;
    for (size_t i = 0; i < ring.size(); ++i) {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % ring.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

Obb fitObb(const std::vector<Point>& ring) {
    Obb best;
    for (size_t i = 0; i < ring.size(); ++i) {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % ring.size()];
        double dx = q.x - p.x, dy = q.y - p.y;
        const double len = std::hypot(dx, dy);
        if (len < 1e-6) continue;
        dx /= len;
        dy /= len;
        double minS = std::numeric_limits<double>::infinity(), maxS = -minS, minT = minS, maxT = -minS;
        for (const Point& r : ring) {
            const double s = r.x * dx + r.y * dy, t = -r.x * dy + r.y * dx;
            minS = std::min(minS, s);
            maxS = std::max(maxS, s);
            minT = std::min(minT, t);
            maxT = std::max(maxT, t);
        }
        const double area = (maxS - minS) * (maxT - minT);
        if (area >= best.area) continue;
        const double cs = (minS + maxS) * 0.5, ct = (minT + maxT) * 0.5, hs = (maxS - minS) * 0.5, ht = (maxT - minT) * 0.5;
        best.cx = cs * dx - ct * dy;
        best.cy = cs * dy + ct * dx;
        best.area = area;
        if (hs >= ht) {
            best.ax = dx;
            best.ay = dy;
            best.halfL = hs;
            best.halfW = ht;
        } else {
            best.ax = -dy;
            best.ay = dx;
            best.halfL = ht;
            best.halfW = hs;
        }
    }
    return best;
}

std::uint32_t hashRing(const Ring& r) {
    std::uint32_t h = 2166136261u;
    auto mix = [&](std::int64_t v) {
        for (int i = 0; i < 8; ++i) {
            h = (h ^ static_cast<std::uint32_t>(v & 0xFF)) * 16777619u;
            v >>= 8;
        }
    };
    mix(std::llround(r[0][0] * 1e6));
    mix(std::llround(r[0][1] * 1e6));
    mix(static_cast<std::int64_t>(r.size()));
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

std::uint32_t blend(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t out = 0xFF000000u;
    for (int c = 0; c < 3; ++c) {
        const float ca = static_cast<float>((a >> (8 * c)) & 0xFF), cb = static_cast<float>((b >> (8 * c)) & 0xFF);
        out |= static_cast<std::uint32_t>(std::clamp(ca + (cb - ca) * t, 0.0f, 255.0f) + 0.5f) << (8 * c);
    }
    return out;
}

struct BridgePart {
    std::vector<std::array<double, 2>> pts;
    bool openA = false, openB = false;
    double width = 8.0;
};

double tileMeters(int z, int y) {
    const double n = std::ldexp(1.0, z);
    const double lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * (y + 0.5) / n)));
    return 2.0 * kPi * 6378137.0 * std::cos(lat) / n;
}

bool clipUnit(double& x0, double& y0, double& x1, double& y1, double& t0, double& t1) {
    t0 = 0.0;
    t1 = 1.0;
    const double dx = x1 - x0, dy = y1 - y0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x0, 1.0 - x0, y0, 1.0 - y0};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-15) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) return false;
            t0 = std::max(t0, r);
        } else {
            if (r < t0) return false;
            t1 = std::min(t1, r);
        }
    }
    const double ax = x0 + dx * t0, ay = y0 + dy * t0, bx = x0 + dx * t1, by = y0 + dy * t1;
    x0 = ax;
    y0 = ay;
    x1 = bx;
    y1 = by;
    return t1 > t0;
}

bool onEdge(const std::array<double, 2>& p) { return p[0] < kEdge || p[0] > 1.0 - kEdge || p[1] < kEdge || p[1] > 1.0 - kEdge; }

std::vector<BridgePart> prepareBridges(const std::vector<BridgeShape>& bridges, double meters) {
    std::vector<BridgePart> out;
    const double step = 12.0 / meters;
    for (const BridgeShape& b : bridges) {
        std::vector<std::vector<std::array<double, 2>>> parts;
        std::vector<std::array<double, 2>> cur;
        auto flush = [&]() {
            if (cur.size() >= 2) parts.push_back(cur);
            cur.clear();
        };
        for (size_t i = 1; i < b.line.size(); ++i) {
            double x0 = b.line[i - 1][0], y0 = b.line[i - 1][1], x1 = b.line[i][0], y1 = b.line[i][1], t0, t1;
            if (!clipUnit(x0, y0, x1, y1, t0, t1)) {
                flush();
                continue;
            }
            if (!cur.empty() && (std::abs(cur.back()[0] - x0) > 1e-12 || std::abs(cur.back()[1] - y0) > 1e-12)) flush();
            if (cur.empty()) cur.push_back({x0, y0});
            cur.push_back({x1, y1});
            if (t1 < 1.0) flush();
        }
        flush();
        for (const auto& part : parts) {
            BridgePart bp;
            bp.width = b.width;
            bp.openA = onEdge(part.front());
            bp.openB = onEdge(part.back());
            bp.pts.push_back(part.front());
            double total = 0.0;
            for (size_t i = 1; i < part.size(); ++i) {
                const auto& a = part[i - 1];
                const auto& c = part[i];
                const double len = std::hypot(c[0] - a[0], c[1] - a[1]);
                total += len;
                const int count = std::max(1, static_cast<int>(std::ceil(len / step)));
                for (int k = 1; k <= count; ++k) bp.pts.push_back({a[0] + (c[0] - a[0]) * k / count, a[1] + (c[1] - a[1]) * k / count});
            }
            if (total * meters >= 3.0) out.push_back(std::move(bp));
        }
    }
    return out;
}

bool onBoundary(const Point& a, const Point& b) {
    for (double bound : {0.0, 1.0}) {
        if (std::abs(a.u - bound) < kEdge && std::abs(b.u - bound) < kEdge) return true;
        if (std::abs(a.v - bound) < kEdge && std::abs(b.v - bound) < kEdge) return true;
    }
    return false;
}

}

const BuildingMaterial& buildingMaterial(int texture) { return kMaterials[std::clamp(texture, 0, BuildingTextures - 1)]; }

std::vector<std::array<double, 2>> buildingSamplePoints(const std::vector<BuildingShape>& shapes, const std::vector<BridgeShape>& bridges,
                                                        int z, int x, int y) {
    std::vector<std::array<double, 2>> out;
    for (const Prepared& b : prepare(shapes))
        for (const auto& p : b.rings.front()) {
            double lat, lon;
            toGeo(z, x, y, p, lat, lon);
            out.push_back({lat, lon});
        }
    for (const BridgePart& bp : prepareBridges(bridges, tileMeters(z, y)))
        for (const auto& p : bp.pts) {
            double lat, lon;
            toGeo(z, x, y, p, lat, lon);
            out.push_back({lat, lon});
        }
    return out;
}

BuildingMesh buildBuildingMesh(const std::vector<BuildingShape>& shapes, const std::vector<BridgeShape>& bridges, int z, int x, int y,
                               const std::vector<double>& heights, int maxAtlas) {
    BuildingMesh mesh;
    const std::vector<Prepared> all = prepare(shapes);
    toGeo(z, x, y, {0.5, 0.5}, mesh.lat, mesh.lon);
    Vec3 E, N, U;
    enuFrame(mesh.lat, mesh.lon, E, N, U);
    const Vec3 origin = geodeticToEcef(mesh.lat, mesh.lon, 0.0);
    auto toEnu = [&](const Vec3& p) {
        const Vec3 d = p - origin;
        return Vec3{dot(d, E), dot(d, N), dot(d, U)};
    };
    std::vector<double> valid;
    for (double h : heights)
        if (std::isfinite(h)) valid.push_back(h);
    double fallback = 0.0;
    if (!valid.empty()) {
        std::nth_element(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(valid.size() / 2), valid.end());
        fallback = valid[valid.size() / 2];
    }

    struct Roof {
        double minX = std::numeric_limits<double>::infinity(), minY = minX, maxX = -minX, maxY = -minX;
        std::vector<std::uint32_t> verts;
    };
    std::vector<Roof> roofs;
    roofs.reserve(all.size());
    size_t sample = 0;

    auto vertex = [&](const Vec3& p, const Vec3& n, float u, float v, float w, float layer, std::uint32_t tint, float base, float kind) {
        BuildingVertex bv{};
        bv.position[0] = static_cast<float>(p.x);
        bv.position[1] = static_cast<float>(p.y);
        bv.position[2] = static_cast<float>(p.z);
        bv.normal[0] = static_cast<float>(n.x);
        bv.normal[1] = static_cast<float>(n.y);
        bv.normal[2] = static_cast<float>(n.z);
        bv.texcoord[0] = u;
        bv.texcoord[1] = v;
        bv.texcoord[2] = w;
        bv.texcoord[3] = layer;
        bv.tint = tint;
        bv.base = base;
        bv.kind = kind;
        mesh.vertices.push_back(bv);
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    };
    auto quad = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
        mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
    };

    for (const Prepared& b : all) {
        std::vector<std::vector<Point>> rings;
        double ground = std::numeric_limits<double>::infinity();
        for (size_t r = 0; r < b.rings.size(); ++r) {
            std::vector<Point> ring;
            ring.reserve(b.rings[r].size());
            for (const auto& p : b.rings[r]) {
                double lat, lon;
                toGeo(z, x, y, p, lat, lon);
                Vec3 e2, n2, u2;
                enuFrame(lat, lon, e2, n2, u2);
                const Vec3 local = toEnu(geodeticToEcef(lat, lon, 0.0));
                ring.push_back({local.x, local.y, local.z, Vec3{dot(u2, E), dot(u2, N), dot(u2, U)}, p[0], p[1]});
                if (r == 0) {
                    if (sample < heights.size() && std::isfinite(heights[sample])) ground = std::min(ground, heights[sample]);
                    ++sample;
                }
            }
            const double area = signedArea(ring);
            if ((r == 0 && area < 0.0) || (r > 0 && area > 0.0)) std::reverse(ring.begin(), ring.end());
            rings.push_back(std::move(ring));
        }
        if (!std::isfinite(ground)) ground = fallback;
        double area = 0.0;
        for (const auto& ring : rings) area += signedArea(ring);
        if (area < 1.0) continue;

        const std::uint32_t hash = hashRing(b.rings.front());
        const Obb box = fitObb(rings.front());
        const double rect = box.area > 0.0 ? area / box.area : 0.0;
        const bool unknown = std::abs(b.height - 10.0f) < 0.01f && b.minHeight <= 0.0f;
        double top = b.height;
        if (unknown) {
            if (area < 250.0) top = hash % 10 < 6 ? 3.8 : 6.6;
            else if (area < 700.0) top = 6.8;
            else if (area < 2500.0) top = 8.5;
            else top = 9.5;
        }
        const double bottom = b.minHeight > 0.0f ? b.minHeight : -kBury;
        {
            if (mesh.heights.empty()) mesh.heights.assign(static_cast<std::size_t>(kBuildingHeightGrid) * kBuildingHeightGrid, 0);
            const Ring& outer = b.rings.front();
            double minX = 1.0, minY = 1.0, maxX = 0.0, maxY = 0.0;
            for (const auto& p : outer) {
                minX = std::min(minX, p[0]);
                minY = std::min(minY, p[1]);
                maxX = std::max(maxX, p[0]);
                maxY = std::max(maxY, p[1]);
            }
            const int g = kBuildingHeightGrid;
            const int i0 = std::clamp(static_cast<int>(minX * g), 0, g - 1), i1 = std::clamp(static_cast<int>(maxX * g), 0, g - 1);
            const int j0 = std::clamp(static_cast<int>(minY * g), 0, g - 1), j1 = std::clamp(static_cast<int>(maxY * g), 0, g - 1);
            const std::uint8_t value = static_cast<std::uint8_t>(std::clamp(top, 0.0, 255.0));
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    const double cx = (i + 0.5) / g, cy = (j + 0.5) / g;
                    bool in = i0 == i1 || j0 == j1;
                    for (size_t a = 0, c = outer.size() - 1; !in && a < outer.size(); c = a++) {
                        const auto& pa = outer[a];
                        const auto& pc = outer[c];
                        if ((pa[1] > cy) != (pc[1] > cy) && cx < (pc[0] - pa[0]) * (cy - pa[1]) / (pc[1] - pa[1]) + pa[0]) in = !in;
                    }
                    std::uint8_t& cell = mesh.heights[static_cast<std::size_t>(j) * g + i];
                    if (in && value > cell) cell = value;
                }
        }
        const std::uint32_t pick = hash >> 8;
        int texture = WallConcrete;
        std::uint32_t tint = kWhite;
        bool house = false;
        if (top >= 40.0) {
            constexpr int towers[] = {FacadeGlass, FacadeGlassFine, FacadeOffice};
            texture = towers[pick % 3];
        } else if (top >= 15.0) {
            constexpr int mids[] = {FacadeOffice, FacadeGrid, FacadeBrick, FacadeStone, FacadeBrickDark};
            texture = mids[pick % 5];
        } else if (area > 2500.0) {
            texture = pick % 2 ? WallMetal : WallConcrete;
            tint = kPaint[(hash >> 20) % 8];
        } else if (area < 400.0 && top <= 9.0) {
            house = true;
            texture = pick % 5 < 3 ? WallSiding : WallStucco;
            tint = kPaint[(hash >> 16) % 8];
        } else {
            constexpr int lows[] = {FacadeBrick, FacadeBrickDark, WallConcrete, WallStucco, FacadeStone};
            texture = lows[pick % 5];
            if (texture == WallStucco || texture == WallConcrete) tint = kPaint[(hash >> 16) % 8];
        }
        if (b.colour) tint = blend(tint, b.colour | 0xFF000000u, texture >= WallSiding ? 0.7f : 0.3f);
        const bool pitched = house && rect > 0.78 && rings.front().size() <= 16 && box.halfW >= 2.0 && b.minHeight <= 0.0f;

        double cz = 0.0;
        for (const Point& p : rings.front()) cz += p.z;
        cz /= static_cast<double>(rings.front().size());
        const float base = static_cast<float>(cz + ground);
        const float layer = static_cast<float>(texture);

        for (const auto& ring : rings)
            for (size_t i = 0; i < ring.size(); ++i) {
                const Point& p = ring[i];
                const Point& q = ring[(i + 1) % ring.size()];
                if (onBoundary(p, q)) continue;
                const double len = std::hypot(q.x - p.x, q.y - p.y);
                if (len < 0.05) continue;
                const Vec3 n = normalize(Vec3{(q.y - p.y) / len, -(q.x - p.x) / len, 0.0});
                auto at = [&](const Point& pt, double h) { return Vec3{pt.x + pt.up.x * (ground + h), pt.y + pt.up.y * (ground + h), pt.z + pt.up.z * (ground + h)}; };
                const float fl = static_cast<float>(len);
                const std::uint32_t a = vertex(at(p, bottom), n, 0.0f, static_cast<float>(bottom), fl, layer, tint, base, 0.0f);
                const std::uint32_t c = vertex(at(q, bottom), n, fl, static_cast<float>(bottom), fl, layer, tint, base, 0.0f);
                const std::uint32_t d = vertex(at(q, top), n, fl, static_cast<float>(top), fl, layer, tint, base, 0.0f);
                const std::uint32_t e = vertex(at(p, top), n, 0.0f, static_cast<float>(top), fl, layer, tint, base, 0.0f);
                quad(a, c, d, e);
            }

        Roof roof;
        const Vec3 up{0.0, 0.0, 1.0};
        auto grow = [&](double px, double py) {
            roof.minX = std::min(roof.minX, px);
            roof.minY = std::min(roof.minY, py);
            roof.maxX = std::max(roof.maxX, px);
            roof.maxY = std::max(roof.maxY, py);
        };
        auto roofVertex = [&](const Vec3& p, const Vec3& n) {
            const std::uint32_t idx = vertex(p, n, static_cast<float>(p.x), static_cast<float>(p.y), -1.0f, static_cast<float>(RoofShingles), kWhite, base, 1.0f);
            roof.verts.push_back(idx);
            grow(p.x, p.y);
            return idx;
        };
        if (pitched) {
            const double ridge = std::clamp(0.36 * 2.0 * box.halfW, 1.4, 4.2);
            const double L = box.halfL + kOverhang, W = box.halfW + kOverhang;
            const double drop = ridge * kOverhang / W;
            const double eave = cz + ground + top - drop;
            const double apex = eave + ridge;
            auto P = [&](double s, double t, double h) { return Vec3{box.cx + s * box.ax - t * box.ay, box.cy + s * box.ay + t * box.ax, h}; };
            auto face = [&](std::initializer_list<Vec3> pts) {
                std::vector<Vec3> v(pts);
                const Vec3 n = normalize(cross(v[1] - v[0], v[2] - v[0]));
                std::vector<std::uint32_t> ids;
                for (const Vec3& q : v) ids.push_back(roofVertex(q, n.z < 0.0 ? -n : n));
                for (size_t k = 1; k + 1 < ids.size(); ++k) mesh.indices.insert(mesh.indices.end(), {ids[0], ids[k], ids[k + 1]});
            };
            const bool hip = (hash >> 4) % 5 < 2;
            const double inset = hip ? std::max(L - W, 0.0) : L;
            const Vec3 A = P(-L, W, eave), B = P(L, W, eave), C = P(L, -W, eave), D = P(-L, -W, eave);
            const Vec3 R1 = P(-inset, 0.0, apex), R2 = P(inset, 0.0, apex);
            if (inset > 1e-3) {
                face({A, B, R2, R1});
                face({C, D, R1, R2});
            } else {
                face({A, B, R2});
                face({C, D, R1});
            }
            if (hip) {
                face({D, A, R1});
                face({B, C, R2});
            } else {
                const double Lw = box.halfL, Ww = box.halfW;
                for (int sgn : {-1, 1}) {
                    const Vec3 n = Vec3{box.ax * sgn, box.ay * sgn, 0.0};
                    const Vec3 g0 = P(sgn * Lw, -Ww * sgn, cz + ground + top), g1 = P(sgn * Lw, Ww * sgn, cz + ground + top);
                    const Vec3 g2 = P(sgn * Lw, 0.0, apex);
                    const float w = static_cast<float>(2.0 * Ww);
                    const std::uint32_t i0 = vertex(g0, n, 0.0f, static_cast<float>(top), w, layer, tint, base, 0.0f);
                    const std::uint32_t i1 = vertex(g1, n, w, static_cast<float>(top), w, layer, tint, base, 0.0f);
                    const std::uint32_t i2 = vertex(g2, n, w * 0.5f, static_cast<float>(top + (apex - (cz + ground + top))), w, layer, tint, base, 0.0f);
                    mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
                }
            }
        } else {
            std::vector<std::vector<std::array<double, 2>>> poly;
            std::vector<const Point*> flat;
            for (const auto& ring : rings) {
                std::vector<std::array<double, 2>> pr;
                for (const Point& p : ring) {
                    pr.push_back({p.x, p.y});
                    flat.push_back(&p);
                }
                poly.push_back(std::move(pr));
            }
            const std::vector<std::uint32_t> tri = mapbox::earcut<std::uint32_t>(poly);
            std::vector<std::uint32_t> ids;
            ids.reserve(flat.size());
            for (const Point* p : flat)
                ids.push_back(roofVertex(Vec3{p->x + p->up.x * (ground + top), p->y + p->up.y * (ground + top), p->z + p->up.z * (ground + top)}, up));
            for (size_t k = 0; k + 2 < tri.size(); k += 3) mesh.indices.insert(mesh.indices.end(), {ids[tri[k]], ids[tri[k + 1]], ids[tri[k + 2]]});
        }
        if (!roof.verts.empty()) roofs.push_back(std::move(roof));
        ++mesh.buildings;
    }

    const std::uint32_t concrete = rgb(212, 210, 203);
    const float concreteLayer = static_cast<float>(WallConcrete);
    for (const BridgePart& bp : prepareBridges(bridges, tileMeters(z, y))) {
        const size_t n = bp.pts.size();
        std::vector<Point> pts;
        pts.reserve(n);
        std::vector<double> ground(n, fallback);
        for (size_t i = 0; i < n; ++i) {
            double lat, lon;
            toGeo(z, x, y, bp.pts[i], lat, lon);
            Vec3 e2, n2, u2;
            enuFrame(lat, lon, e2, n2, u2);
            const Vec3 local = toEnu(geodeticToEcef(lat, lon, 0.0));
            pts.push_back({local.x, local.y, local.z, Vec3{dot(u2, E), dot(u2, N), dot(u2, U)}, bp.pts[i][0], bp.pts[i][1]});
            if (sample < heights.size() && std::isfinite(heights[sample])) ground[i] = heights[sample];
            ++sample;
        }
        std::vector<double> dist(n, 0.0);
        for (size_t i = 1; i < n; ++i) dist[i] = dist[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        const double total = dist.back();
        if (total < 3.0) continue;
        const double highest = *std::max_element(ground.begin(), ground.end());
        const double a0 = bp.openA ? highest : ground.front(), a1 = bp.openB ? highest : ground.back();
        std::vector<double> deck(n);
        for (size_t i = 0; i < n; ++i) {
            const double line = a0 + (a1 - a0) * dist[i] / total;
            const double edge = std::min(bp.openA ? 1e9 : dist[i], bp.openB ? 1e9 : total - dist[i]);
            const double r = total > 20.0 ? std::clamp(edge / 25.0, 0.0, 1.0) : 0.0;
            deck[i] = std::max(line, ground[i] + 5.5 * r * r * (3.0 - 2.0 * r));
        }
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<double> next = deck;
            for (size_t i = 1; i + 1 < n; ++i) next[i] = (deck[i - 1] + 2.0 * deck[i] + deck[i + 1]) / 4.0;
            deck.swap(next);
        }
        std::vector<Vec3> left(n), along(n);
        for (size_t i = 0; i < n; ++i) {
            const Point& a = pts[i == 0 ? 0 : i - 1];
            const Point& c = pts[i + 1 < n ? i + 1 : n - 1];
            double dx = c.x - a.x, dy = c.y - a.y;
            const double len = std::hypot(dx, dy);
            if (len < 1e-9) {
                dx = 1.0;
                dy = 0.0;
            } else {
                dx /= len;
                dy /= len;
            }
            along[i] = Vec3{dx, dy, 0.0};
            left[i] = Vec3{-dy, dx, 0.0};
        }
        const double half = bp.width * 0.5;
        constexpr double kThick = 1.1, kParapet = 0.95;
        auto at = [&](size_t i, double offset, double h) {
            const Point& p = pts[i];
            return Vec3{p.x + left[i].x * offset + p.up.x * h, p.y + left[i].y * offset + p.up.y * h, p.z + p.up.z * h};
        };
        auto wall = [&](const Vec3& p, const Vec3& nrm, double u, double v) {
            return vertex(p, nrm, static_cast<float>(u), static_cast<float>(v), 0.0f, concreteLayer, concrete, static_cast<float>(p.z), 0.0f);
        };
        size_t c0 = 0;
        while (c0 + 1 < n) {
            size_t c1 = c0 + 1;
            while (c1 + 1 < n && dist[c1] - dist[c0] < 60.0) ++c1;
            Roof roof;
            std::vector<std::uint32_t> rowL, rowR;
            for (size_t i = c0; i <= c1; ++i) {
                for (int sideSign : {1, -1}) {
                    const Vec3 p = at(i, sideSign * half, deck[i]);
                    const std::uint32_t idx = vertex(p, Vec3{0.0, 0.0, 1.0}, static_cast<float>(p.x), static_cast<float>(p.y), -1.0f,
                                                     static_cast<float>(RoofShingles), kWhite, static_cast<float>(p.z), 1.0f);
                    roof.verts.push_back(idx);
                    roof.minX = std::min(roof.minX, p.x);
                    roof.minY = std::min(roof.minY, p.y);
                    roof.maxX = std::max(roof.maxX, p.x);
                    roof.maxY = std::max(roof.maxY, p.y);
                    (sideSign > 0 ? rowL : rowR).push_back(idx);
                }
            }
            for (size_t k = 0; k + 1 < rowL.size(); ++k) quad(rowL[k], rowR[k], rowR[k + 1], rowL[k + 1]);
            roofs.push_back(std::move(roof));
            c0 = c1;
        }
        for (size_t i = 0; i + 1 < n; ++i) {
            for (int sideSign : {1, -1}) {
                const Vec3 nrm = left[i] * static_cast<double>(sideSign);
                const double off = sideSign * half;
                const std::uint32_t a = wall(at(i, off, deck[i] - kThick), nrm, dist[i], 0.0);
                const std::uint32_t b = wall(at(i + 1, off, deck[i + 1] - kThick), nrm, dist[i + 1], 0.0);
                const std::uint32_t c = wall(at(i + 1, off, deck[i + 1] + kParapet), nrm, dist[i + 1], kThick + kParapet);
                const std::uint32_t d = wall(at(i, off, deck[i] + kParapet), nrm, dist[i], kThick + kParapet);
                quad(a, b, c, d);
            }
            const Vec3 down{0.0, 0.0, -1.0};
            quad(wall(at(i, half, deck[i] - kThick), down, dist[i], 0.0), wall(at(i, -half, deck[i] - kThick), down, dist[i], bp.width),
                 wall(at(i + 1, -half, deck[i + 1] - kThick), down, dist[i + 1], bp.width), wall(at(i + 1, half, deck[i + 1] - kThick), down, dist[i + 1], 0.0));
        }
        for (int end = 0; end < 2; ++end) {
            if (end == 0 ? bp.openA : bp.openB) continue;
            const size_t i = end == 0 ? 0 : n - 1;
            const Vec3 nrm = along[i] * (end == 0 ? -1.0 : 1.0);
            quad(wall(at(i, half, deck[i] - kThick), nrm, 0.0, 0.0), wall(at(i, -half, deck[i] - kThick), nrm, bp.width, 0.0),
                 wall(at(i, -half, deck[i] + kParapet), nrm, bp.width, kThick + kParapet), wall(at(i, half, deck[i] + kParapet), nrm, 0.0, kThick + kParapet));
        }
        double lastPier = -1e9;
        for (size_t i = 0; i < n; ++i) {
            if (dist[i] < 10.0 || dist[i] > total - 10.0 || dist[i] - lastPier < 35.0) continue;
            const double low = ground[i] - 1.0, high = deck[i] - kThick;
            if (high - ground[i] < 3.5) continue;
            lastPier = dist[i];
            const double ha = 0.8, hc = std::max(1.0, half * 0.55);
            const Point& p = pts[i];
            auto corner = [&](double sa, double sc, double h) {
                return Vec3{p.x + along[i].x * sa + left[i].x * sc + p.up.x * h, p.y + along[i].y * sa + left[i].y * sc + p.up.y * h, p.z + p.up.z * h};
            };
            const double sx[4] = {-ha, ha, ha, -ha}, sy[4] = {-hc, -hc, hc, hc};
            for (int k = 0; k < 4; ++k) {
                const int m = (k + 1) % 4;
                const Vec3 mid{(sx[k] + sx[m]) * 0.5, (sy[k] + sy[m]) * 0.5, 0.0};
                const Vec3 nrm = normalize(along[i] * mid.x + left[i] * mid.y);
                const double w = std::hypot(sx[m] - sx[k], sy[m] - sy[k]);
                quad(wall(corner(sx[k], sy[k], low), nrm, 0.0, 0.0), wall(corner(sx[m], sy[m], low), nrm, w, 0.0),
                     wall(corner(sx[m], sy[m], high), nrm, w, high - low), wall(corner(sx[k], sy[k], high), nrm, 0.0, high - low));
            }
        }
    }

    if (roofs.empty() || maxAtlas <= 0) return mesh;
    struct Size {
        int w, h;
    };
    std::vector<Size> sizes(roofs.size());
    std::vector<int> order(roofs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    auto pack = [&](int atlas, double mpp, std::vector<RoofCell>& cells) {
        for (size_t i = 0; i < roofs.size(); ++i) {
            const double wm = roofs[i].maxX - roofs[i].minX, hm = roofs[i].maxY - roofs[i].minY;
            sizes[i].w = std::clamp(static_cast<int>(std::ceil(wm / mpp)), 1, kMaxCell);
            sizes[i].h = std::clamp(static_cast<int>(std::ceil(hm / mpp)), 1, kMaxCell);
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) { return sizes[a].h != sizes[b].h ? sizes[a].h > sizes[b].h : a < b; });
        cells.assign(roofs.size(), RoofCell{});
        int cx = 0, cy = 0, row = 0;
        for (int i : order) {
            const int w = sizes[i].w + 2 * kPad, h = sizes[i].h + 2 * kPad;
            if (w > atlas || h > atlas) return false;
            if (cx + w > atlas) {
                cx = 0;
                cy += row;
                row = 0;
            }
            if (cy + h > atlas) return false;
            cells[i].x = cx;
            cells[i].y = cy;
            cells[i].w = w;
            cells[i].h = h;
            cx += w;
            row = std::max(row, h);
        }
        return true;
    };
    std::vector<RoofCell> cells;
    double mpp = kTargetMpp;
    int atlas = 0;
    for (int size = 256; size <= maxAtlas && !atlas; size *= 2)
        if (pack(size, mpp, cells)) atlas = size;
    for (int tries = 0; !atlas && tries < 40; ++tries) {
        mpp *= 1.2;
        if (pack(maxAtlas, mpp, cells)) atlas = maxAtlas;
    }
    if (!atlas) return mesh;
    mesh.atlas = atlas;
    for (size_t i = 0; i < roofs.size(); ++i) {
        const Roof& r = roofs[i];
        RoofCell& cell = cells[i];
        const double wm = std::max(r.maxX - r.minX, 1e-3), hm = std::max(r.maxY - r.minY, 1e-3);
        const double sx = (cell.w - 2 * kPad) / wm, sy = (cell.h - 2 * kPad) / hm;
        for (std::uint32_t idx : r.verts) {
            BuildingVertex& v = mesh.vertices[idx];
            const double px = v.texcoord[0], py = v.texcoord[1];
            v.texcoord[0] = static_cast<float>((cell.x + kPad + (px - r.minX) * sx) / atlas);
            v.texcoord[1] = static_cast<float>((cell.y + kPad + (r.maxY - py) * sy) / atlas);
        }
        const double padX = kPad / sx, padY = kPad / sy;
        const GeoPoint sw = ecefToGeodetic(origin + E * (r.minX - padX) + N * (r.minY - padY));
        const GeoPoint ne = ecefToGeodetic(origin + E * (r.maxX + padX) + N * (r.maxY + padY));
        cell.west = sw.lon;
        cell.south = sw.lat;
        cell.east = ne.lon;
        cell.north = ne.lat;
    }
    mesh.cells = std::move(cells);
    return mesh;
}


std::vector<std::array<double, 2>> canopySamplePoints(int z, int x, int y) {
    std::vector<std::array<double, 2>> out;
    out.reserve(kCanopyGround * kCanopyGround);
    for (int j = 0; j < kCanopyGround; ++j)
        for (int i = 0; i < kCanopyGround; ++i) {
            double lat, lon;
            toGeo(z, x, y, {i / double(kCanopyGround - 1), j / double(kCanopyGround - 1)}, lat, lon);
            out.push_back({lat, lon});
        }
    return out;
}

BuildingMesh buildCanopyMesh(const std::vector<std::uint8_t>& canopy, int size, int maxTrees, int z, int x, int y,
                             const std::vector<double>& ground, int atlas) {
    BuildingMesh mesh;
    if (size <= 2 || maxTrees <= 0 || canopy.size() < static_cast<std::size_t>(size) * size) return mesh;
    toGeo(z, x, y, {0.5, 0.5}, mesh.lat, mesh.lon);
    Vec3 E, N, U;
    enuFrame(mesh.lat, mesh.lon, E, N, U);
    const Vec3 origin = geodeticToEcef(mesh.lat, mesh.lon, 0.0);
    double fallback = 0.0;
    for (double g : ground)
        if (std::isfinite(g)) {
            fallback = g;
            break;
        }
    auto groundAt = [&](double fx, double fy) {
        if (ground.size() < static_cast<std::size_t>(kCanopyGround) * kCanopyGround) return fallback;
        const double gx = std::clamp(fx, 0.0, 1.0) * (kCanopyGround - 1), gy = std::clamp(fy, 0.0, 1.0) * (kCanopyGround - 1);
        const int ix = std::min(static_cast<int>(gx), kCanopyGround - 2), iy = std::min(static_cast<int>(gy), kCanopyGround - 2);
        const double tx = gx - ix, ty = gy - iy;
        auto at = [&](int a, int b) {
            const double v = ground[static_cast<std::size_t>(b) * kCanopyGround + a];
            return std::isfinite(v) ? v : fallback;
        };
        return (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty) + (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
    };
    double latA, lonA, latB, lonB;
    toGeo(z, x, y, {0.0, 0.5}, latA, lonA);
    toGeo(z, x, y, {1.0, 0.5}, latB, lonB);
    const double tileMeters = length(geodeticToEcef(latA, lonA, 0.0) - geodeticToEcef(latB, lonB, 0.0));
    const double cell = tileMeters / size;
    auto h = [&](int i, int j) { return static_cast<int>(canopy[static_cast<std::size_t>(j) * size + i]); };

    struct Top {
        int height;
        int i, j;
    };
    std::vector<Top> tops;
    constexpr int kMinTree = 4;
    for (int j = 1; j < size - 1; ++j)
        for (int i = 1; i < size - 1; ++i) {
            const int v = h(i, j);
            if (v < kMinTree) continue;
            const double crown = std::clamp(0.2 * v + 1.0, 2.0, 7.0);
            const int r = std::max(1, static_cast<int>(std::lround(crown / cell)));
            bool top = true;
            for (int dj = -r; dj <= r && top; ++dj)
                for (int di = -r; di <= r && top; ++di) {
                    if (!di && !dj) continue;
                    const int a = i + di, b = j + dj;
                    if (a < 0 || b < 0 || a >= size || b >= size) continue;
                    const int o = h(a, b);
                    if (o > v || (o == v && (dj < 0 || (dj == 0 && di < 0)))) top = false;
                }
            if (top) tops.push_back({v, i, j});
        }
    if (tops.empty()) return mesh;
    if (static_cast<int>(tops.size()) > maxTrees) {
        std::nth_element(tops.begin(), tops.begin() + maxTrees, tops.end(), [](const Top& a, const Top& b) { return a.height > b.height; });
        tops.resize(static_cast<std::size_t>(maxTrees));
    }

    constexpr int kSides = 7;
    const double rings[][2] = {{0.62, 0.78}, {0.97, 0.3}, {0.9, -0.25}, {0.5, -0.8}};
    for (const Top& t : tops) {
        const double fx = (t.i + 0.5) / size, fy = (t.j + 0.5) / size;
        double lat, lon;
        toGeo(z, x, y, {fx, fy}, lat, lon);
        const double base = groundAt(fx, fy);
        const double height = t.height;
        const double radius = std::clamp(0.2 * height + 1.0, 2.0, 7.0) * (0.85 + 0.3 * ((t.i * 7 + t.j * 13) % 10) / 10.0);
        const double bottom = std::max(1.5, height * 0.3);
        const double half = (height - bottom) * 0.5;
        const double middle = bottom + half;
        const Vec3 d = geodeticToEcef(lat, lon, base + middle) - origin;
        const Vec3 c{dot(d, E), dot(d, N), dot(d, U)};
        constexpr double kPi_ = std::numbers::pi;
        const double phase = (t.i * 31 + t.j * 17) % 360 * kPi_ / 180.0;
        auto vertex = [&](double ex, double ny, double uz) {
            const Vec3 p{c.x + ex * radius, c.y + ny * radius, c.z + uz * half};
            const Vec3 n = normalize(Vec3{ex / radius, ny / radius, uz / std::max(half, 0.5)});
            BuildingVertex v{};
            v.position[0] = static_cast<float>(p.x);
            v.position[1] = static_cast<float>(p.y);
            v.position[2] = static_cast<float>(p.z);
            v.normal[0] = static_cast<float>(n.x);
            v.normal[1] = static_cast<float>(n.y);
            v.normal[2] = static_cast<float>(n.z);
            v.texcoord[0] = static_cast<float>(std::clamp(fx + ex * radius * 0.8 / tileMeters, 0.0, 1.0));
            v.texcoord[1] = static_cast<float>(std::clamp(fy - ny * radius * 0.8 / tileMeters, 0.0, 1.0));
            v.tint = 0xFFFFFFFFu;
            v.base = static_cast<float>(c.z - half - bottom);
            v.kind = 1.0f;
            mesh.vertices.push_back(v);
            return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
        };
        const std::uint32_t apex = vertex(0.0, 0.0, 1.0);
        std::uint32_t ring[4][kSides];
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < kSides; ++k) {
                const double a = phase + 2.0 * std::numbers::pi * k / kSides;
                const double wobble = 1.0 + 0.08 * std::sin(3.0 * a + phase);
                ring[r][k] = vertex(std::cos(a) * rings[r][0] * wobble, std::sin(a) * rings[r][0] * wobble, rings[r][1]);
            }
        const std::uint32_t foot = vertex(0.0, 0.0, -1.0);
        const double trunk = std::clamp(0.15 + 0.012 * height, 0.15, 0.6);
        const double groundZ = -(bottom + half) / std::max(half, 0.5) - 0.3;
        std::uint32_t low[4], high[4];
        for (int k = 0; k < 4; ++k) {
            const double a = phase + kPi_ * 0.5 * k;
            const double ex = std::cos(a) * trunk / radius, ny = std::sin(a) * trunk / radius;
            low[k] = vertex(ex, ny, groundZ);
            high[k] = vertex(ex, ny, -0.2);
            for (std::uint32_t idx : {low[k], high[k]}) {
                BuildingVertex& v = mesh.vertices[idx];
                v.normal[0] = static_cast<float>(std::cos(a));
                v.normal[1] = static_cast<float>(std::sin(a));
                v.normal[2] = 0.0f;
                v.texcoord[0] = static_cast<float>(fx);
                v.texcoord[1] = static_cast<float>(fy);
            }
        }
        for (int k = 0; k < 4; ++k) {
            const int n = (k + 1) % 4;
            mesh.indices.insert(mesh.indices.end(), {low[k], low[n], high[n], low[k], high[n], high[k]});
        }
        for (int k = 0; k < kSides; ++k) {
            const int n = (k + 1) % kSides;
            mesh.indices.insert(mesh.indices.end(), {apex, ring[0][k], ring[0][n]});
            for (int r = 0; r < 3; ++r) mesh.indices.insert(mesh.indices.end(), {ring[r][k], ring[r + 1][k], ring[r + 1][n], ring[r][k], ring[r + 1][n], ring[r][n]});
            mesh.indices.insert(mesh.indices.end(), {ring[3][k], foot, ring[3][n]});
        }
    }
    mesh.buildings = static_cast<int>(tops.size());
    constexpr int kSplit = 8;
    const int span = atlas / kSplit;
    for (int j = 0; j < kSplit; ++j)
        for (int i = 0; i < kSplit; ++i) {
            RoofCell c;
            toGeo(z, x, y, {static_cast<double>(i) / kSplit, static_cast<double>(j) / kSplit}, c.north, c.west);
            toGeo(z, x, y, {static_cast<double>(i + 1) / kSplit, static_cast<double>(j + 1) / kSplit}, c.south, c.east);
            c.x = i * span;
            c.y = j * span;
            c.w = span;
            c.h = span;
            mesh.cells.push_back(c);
        }
    mesh.atlas = atlas;
    return mesh;
}

}
