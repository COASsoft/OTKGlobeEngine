#include "feature/grid.hpp"

#include "ote/mgrs.hpp"

#include <algorithm>
#include <cmath>

namespace ote {

namespace {

constexpr double kD2R = 3.14159265358979323846 / 180.0;

struct Emitter {
    const GeoRect& tile;
    double span, coslat, half;
    float color[4];
    std::vector<float>& verts;

    void push(double lon, double lat) {
        float x = static_cast<float>((lon - tile.west) / span * 2.0 - 1.0);
        float y = static_cast<float>((lat - tile.south) / span * 2.0 - 1.0);
        verts.insert(verts.end(), {x, y, color[0], color[1], color[2], color[3]});
    }

    void segment(double lon0, double lat0, double lon1, double lat1) {
        double ax = lon0 * coslat, ay = lat0, bx = lon1 * coslat, by = lat1;
        double dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) return;
        double nx = -dy / len * half, ny = dx / len * half;
        double q[4][2] = {{ax + nx, ay + ny}, {bx + nx, by + ny}, {bx - nx, by - ny}, {ax - nx, ay - ny}};
        for (int k : {0, 1, 2, 0, 2, 3}) push(q[k][0] / coslat, q[k][1]);
    }
};

}

void buildGrid(const GeoRect& tile, double texelDeg, double dpi, double spacing, std::vector<float>& verts) {
    double span = tile.north - tile.south;
    if (tile.south >= 84.0 || tile.north <= -80.0) return;
    double coslat = std::max(std::cos((tile.south + tile.north) * 0.5 * kD2R), 0.05);
    Emitter gzd{tile, span, coslat, 1.5 * dpi * texelDeg * 0.5, {1.0f, 0.85f, 0.2f, 0.9f}, verts};
    for (double lon = std::ceil(tile.west / 6.0) * 6.0; lon <= tile.east; lon += 6.0)
        gzd.segment(lon, std::max(tile.south, -80.0), lon, std::min(tile.north, 84.0));
    for (double lat = std::ceil(std::max(tile.south, -80.0) / 8.0) * 8.0; lat <= std::min(tile.north, 84.0); lat += 8.0)
        gzd.segment(tile.west, lat, tile.east, lat);

    if (spacing <= 0 || span > 12.0) return;
    Emitter grid{tile, span, coslat, 1.0 * dpi * texelDeg * 0.5, {1.0f, 1.0f, 1.0f, spacing >= 100000.0 ? 0.8f : 0.6f}, verts};
    double south = std::max(tile.south, -80.0), north = std::min(tile.north, 84.0);
    int z0 = static_cast<int>(std::floor((tile.west + 180.0) / 6.0)) + 1;
    int z1 = static_cast<int>(std::floor((tile.east + 180.0 - 1e-9) / 6.0)) + 1;
    for (int zone = std::max(z0, 1); zone <= std::min(z1, 60); ++zone) {
        double zw = (zone - 1) * 6.0 - 180.0, ze = zw + 6.0;
        double w = std::max(tile.west, zw), e = std::min(tile.east, ze);
        if (w >= e) continue;
        double minE = 1e18, maxE = -1e18, minN = 1e18, maxN = -1e18;
        for (double la : {south, (south + north) * 0.5, north})
            for (double lo : {w, (w + e) * 0.5, e}) {
                Utm u = toUtm(la, lo, zone);
                double n = la < 0 ? u.northing - 10000000.0 : u.northing;
                minE = std::min(minE, u.easting);
                maxE = std::max(maxE, u.easting);
                minN = std::min(minN, n);
                maxN = std::max(maxN, n);
            }
        auto toGeo = [&](double easting, double northing, double& lat, double& lon) {
            Utm u;
            u.zone = zone;
            u.north = northing >= 0;
            u.easting = easting;
            u.northing = northing >= 0 ? northing : northing + 10000000.0;
            fromUtm(u, lat, lon);
        };
        const int steps = 16;
        double e0 = std::floor(minE / spacing) * spacing, n0 = std::floor(minN / spacing) * spacing;
        int lines = 0;
        for (double ev = e0; ev <= maxE && lines < 400; ev += spacing, ++lines) {
            double plat = 0, plon = 0;
            bool have = false;
            for (int i = 0; i <= steps; ++i) {
                double nv = minN + (maxN - minN) * i / steps, lat, lon;
                toGeo(ev, nv, lat, lon);
                bool inside = lon >= zw - 1e-9 && lon <= ze + 1e-9;
                if (inside && have) grid.segment(plon, plat, lon, lat);
                plat = lat;
                plon = lon;
                have = inside;
            }
        }
        lines = 0;
        for (double nv = n0; nv <= maxN && lines < 400; nv += spacing, ++lines) {
            double plat = 0, plon = 0;
            bool have = false;
            for (int i = 0; i <= steps; ++i) {
                double ev = minE + (maxE - minE) * i / steps, lat, lon;
                toGeo(ev, nv, lat, lon);
                bool inside = lon >= zw - 1e-9 && lon <= ze + 1e-9;
                if (inside && have) {
                    double a0 = std::clamp(plon, zw, ze), a1 = std::clamp(lon, zw, ze);
                    grid.segment(a0, plat, a1, lat);
                }
                plat = lat;
                plon = lon;
                have = inside;
            }
        }
    }
}

double graticuleStep(double minDeg, bool gars) {
    static const double kLatLon[] = {1.0 / 3600, 2.0 / 3600, 5.0 / 3600, 10.0 / 3600, 30.0 / 3600, 1.0 / 60, 2.0 / 60, 5.0 / 60, 10.0 / 60,
                                     15.0 / 60, 30.0 / 60, 1, 2, 5, 10, 15, 30};
    static const double kGars[] = {5.0 / 60, 15.0 / 60, 30.0 / 60};
    if (gars) {
        for (double s : kGars)
            if (s >= minDeg) return s;
        return 0.0;
    }
    for (double s : kLatLon)
        if (s >= minDeg) return s;
    return 30.0;
}

void buildGraticule(const GeoRect& tile, double texelDeg, double dpi, double stepDeg, bool gars, std::vector<float>& verts) {
    double span = tile.north - tile.south;
    double coslat = std::max(std::cos((tile.south + tile.north) * 0.5 * kD2R), 0.05);
    Emitter major{tile, span, coslat, 1.5 * dpi * texelDeg * 0.5, {1.0f, 0.85f, 0.2f, 0.9f}, verts};
    Emitter minor{tile, span, coslat, 1.0 * dpi * texelDeg * 0.5, {1.0f, 1.0f, 1.0f, 0.7f}, verts};
    double big = gars ? 0.5 : (stepDeg < 1.0 ? 1.0 : stepDeg * 5.0);
    if (gars && stepDeg <= 0) stepDeg = 0.5;
    if (stepDeg <= 0) return;
    if (span / stepDeg > 400) return;
    auto isMajor = [&](double v) { return std::abs(v / big - std::round(v / big)) < 1e-6; };
    double lon0 = std::ceil((tile.west + 180.0) / stepDeg - 1e-9) * stepDeg - 180.0;
    for (double lon = lon0; lon <= tile.east + 1e-12; lon += stepDeg)
        (isMajor(lon + 180.0) ? major : minor).segment(lon, tile.south, lon, tile.north);
    double lat0 = std::ceil((tile.south + 90.0) / stepDeg - 1e-9) * stepDeg - 90.0;
    for (double lat = lat0; lat <= tile.north + 1e-12; lat += stepDeg)
        (isMajor(lat + 90.0) ? major : minor).segment(tile.west, lat, tile.east, lat);
}

}
