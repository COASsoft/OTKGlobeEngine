#include "raster/tile_matrix.hpp"

#include "core/geo.hpp"

#include <algorithm>
#include <cmath>

namespace ote {

namespace {
constexpr double kMercHalf = 20037508.342789244;
constexpr double kMercR = 6378137.0;
}

TileMatrix TileMatrix::webMercator(int minZoom, int maxZoom, int tileSize) {
    TileMatrix m;
    m.srid = 3857;
    m.originX = -kMercHalf;
    m.originY = kMercHalf;
    m.minZoom = minZoom;
    m.maxZoom = maxZoom;
    m.levels.resize(maxZoom + 1);
    for (int z = 0; z <= maxZoom; ++z) {
        TileLevel& l = m.levels[z];
        l.valid = z >= minZoom;
        l.matrixWidth = l.matrixHeight = 1 << z;
        l.tileWidth = l.tileHeight = tileSize;
        l.resolutionX = l.resolutionY = 2.0 * kMercHalf / (static_cast<double>(tileSize) * (1 << z));
    }
    return m;
}

TileMatrix TileMatrix::geographic(int minZoom, int maxZoom, int tileSize) {
    TileMatrix m;
    m.srid = 4326;
    m.originX = -180.0;
    m.originY = 90.0;
    m.minZoom = minZoom;
    m.maxZoom = maxZoom;
    m.levels.resize(maxZoom + 1);
    for (int z = 0; z <= maxZoom; ++z) {
        TileLevel& l = m.levels[z];
        l.valid = z >= minZoom;
        l.matrixWidth = 2 << z;
        l.matrixHeight = 1 << z;
        l.tileWidth = l.tileHeight = tileSize;
        l.resolutionX = l.resolutionY = 180.0 / (static_cast<double>(tileSize) * (1 << z));
    }
    return m;
}

double TileMatrix::unitsPerDegree() const {
    return srid == 4326 ? 1.0 : kMercR * kDegToRad;
}

void TileMatrix::tileBounds(int z, int x, int y, double& minX, double& minY, double& maxX, double& maxY) const {
    minX = originX + x * spanX(z);
    maxX = minX + spanX(z);
    maxY = originY - y * spanY(z);
    minY = maxY - spanY(z);
}

int TileMatrix::bestLevel(double unitsPerPixel) const {
    int best = -1;
    for (int z = 0; z < static_cast<int>(levels.size()); ++z) {
        if (!levels[z].valid) continue;
        best = z;
        if (levels[z].resolutionX <= unitsPerPixel * 1.0001) break;
    }
    return best;
}

double projectX(int srid, double lon) { return srid == 4326 ? lon : kMercR * lon * kDegToRad; }

double projectY(int srid, double lat) {
    if (srid == 4326) return lat;
    double l = std::clamp(lat, -kMercatorMaxLat, kMercatorMaxLat) * kDegToRad;
    return kMercR * std::log(std::tan(kPi * 0.25 + l * 0.5));
}

double unprojectX(int srid, double x) { return srid == 4326 ? x : x / kMercR * kRadToDeg; }

double unprojectY(int srid, double y) {
    if (srid == 4326) return y;
    return (2.0 * std::atan(std::exp(y / kMercR)) - kPi * 0.5) * kRadToDeg;
}

double maxLatitude(int srid) { return srid == 4326 ? 90.0 : kMercatorMaxLat; }

}
