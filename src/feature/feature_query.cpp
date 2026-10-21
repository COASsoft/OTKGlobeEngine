#include "otk/feature_query.hpp"

#include "core/region.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace otk {

namespace {

constexpr double kMetersPerDegree = 111319.49;

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct Local {
    double lat0, lon0, cos0;
    void project(const Coord& c, double& x, double& y) const {
        double dLon = c.lon - lon0;
        while (dLon > 180.0) dLon -= 360.0;
        while (dLon < -180.0) dLon += 360.0;
        x = dLon * cos0 * kMetersPerDegree;
        y = (c.lat - lat0) * kMetersPerDegree;
    }
};

double segmentDistance(double ax, double ay, double bx, double by) {
    double dx = bx - ax, dy = by - ay;
    double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? std::clamp(-(ax * dx + ay * dy) / len2, 0.0, 1.0) : 0.0;
    double px = ax + dx * t, py = ay + dy * t;
    return std::sqrt(px * px + py * py);
}

bool ringContainsOrigin(const std::vector<Coord>& ring, const Local& local) {
    bool inside = false;
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        double xi, yi, xj, yj;
        local.project(ring[i], xi, yi);
        local.project(ring[j], xj, yj);
        if ((yi > 0) != (yj > 0) && 0 < (xj - xi) * (0 - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

double ringDistance(const std::vector<Coord>& points, bool closed, const Local& local) {
    double best = 1e300;
    if (points.size() == 1) {
        double x, y;
        local.project(points[0], x, y);
        return std::sqrt(x * x + y * y);
    }
    for (size_t i = 1; i < points.size() + (closed ? 1 : 0); ++i) {
        double ax, ay, bx, by;
        local.project(points[i - 1], ax, ay);
        local.project(points[i % points.size()], bx, by);
        best = std::min(best, segmentDistance(ax, ay, bx, by));
    }
    return best;
}

bool withinRadius(const Feature& f, const Coord& center, double radius) {
    Local local{center.lat, center.lon, std::max(std::cos(center.lat * 3.14159265358979323846 / 180.0), 1e-6)};
    for (const Geometry& g : f.geometries) {
        if (g.points.empty()) continue;
        bool polygon = g.type == GeometryType::Polygon && g.points.size() >= 3;
        if (polygon && ringContainsOrigin(g.points, local)) {
            bool inHole = false;
            for (const auto& hole : g.holes)
                if (hole.size() >= 3 && ringContainsOrigin(hole, local)) inHole = true;
            if (!inHole) return true;
        }
        if (ringDistance(g.points, polygon, local) <= radius) return true;
        for (const auto& hole : g.holes)
            if (!hole.empty() && ringDistance(hole, true, local) <= radius) return true;
    }
    return false;
}

}

bool featureRegion(const Feature& feature, Region& out) {
    RegionBuilder b;
    for (const Geometry& g : feature.geometries)
        for (const Coord& c : g.points) b.add(c.lat, c.lon);
    return b.get(out);
}

bool matchesQuery(const Feature& f, const FeatureQuery& q) {
    if (!q.typePrefix.empty() && f.type.rfind(q.typePrefix, 0) != 0) return false;
    if (!q.nameContains.empty() && lowered(f.name).find(lowered(q.nameContains)) == std::string::npos) return false;
    for (const auto& [key, value] : q.attributes) {
        auto it = std::find_if(f.attributes.begin(), f.attributes.end(), [&](const auto& a) { return a.first == key; });
        if (it == f.attributes.end() || it->second != value) return false;
    }
    if (q.activeAt > 0) {
        if (f.start > 0 && q.activeAt < f.start) return false;
        if (f.stale > 0 && q.activeAt >= f.stale) return false;
    }
    if (q.useRegion) {
        Region r;
        if (!featureRegion(f, r) || !regionsIntersect(r, q.region)) return false;
    }
    if (q.useRadius && !withinRadius(f, q.center, q.radiusMeters)) return false;
    return true;
}

std::vector<std::size_t> queryFeatures(const std::vector<Feature>& features, const FeatureQuery& query) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!matchesQuery(features[i], query)) continue;
        out.push_back(i);
        if (query.limit && out.size() >= query.limit) break;
    }
    return out;
}

}
