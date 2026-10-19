#pragma once

#include <vector>

namespace ote {

struct TileLevel {
    bool valid = false;
    double resolutionX = 0;
    double resolutionY = 0;
    int matrixWidth = 0;
    int matrixHeight = 0;
    int tileWidth = 256;
    int tileHeight = 256;
};

struct TileMatrix {
    int srid = 3857;
    double originX = 0;
    double originY = 0;
    int minZoom = 0;
    int maxZoom = 0;
    std::vector<TileLevel> levels;

    static TileMatrix webMercator(int minZoom, int maxZoom, int tileSize = 256);
    static TileMatrix geographic(int minZoom, int maxZoom, int tileSize = 256);
    static TileMatrix geographicCustom(int minZoom, int maxZoom, int tileSize, double level0Resolution);

    bool hasLevel(int z) const { return z >= 0 && z < static_cast<int>(levels.size()) && levels[z].valid; }
    double spanX(int z) const { return levels[z].resolutionX * levels[z].tileWidth; }
    double spanY(int z) const { return levels[z].resolutionY * levels[z].tileHeight; }
    double unitsPerDegree() const;
    void tileBounds(int z, int x, int y, double& minX, double& minY, double& maxX, double& maxY) const;
    int bestLevel(double unitsPerPixel) const;
};

double projectX(int srid, double lon);
double projectY(int srid, double lat);
double unprojectX(int srid, double x);
double unprojectY(int srid, double y);
double maxLatitude(int srid);

}
