#pragma once

#include "ote/tile_source.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ote {

enum class SourceType { Url, Wms, Wmts, MBTiles, GeoPackage, Osmdroid, Gdal, Mvt, PMTiles, Host };

struct ImageryLayer {
    std::string name;
    SourceType type = SourceType::Url;
    std::string url;
    std::string path;
    std::string table;
    std::vector<std::string> servers;
    int minZoom = 0;
    int maxZoom = 19;
    int srid = 3857;
    int tileSize = 256;
    double level0Resolution = 0;
    bool tms = false;
    bool cache = true;
    int expirySeconds = 0;
    float opacity = 1.0f;
    bool polarFill = false;
    float polarTintNorth[3] = {1.0f, 1.0f, 1.0f};
    float polarTintSouth[3] = {1.0f, 1.0f, 1.0f};
    std::string wmsLayers;
    std::string wmsStyles;
    std::string wmsFormat = "image/png";
    std::string wmsVersion = "1.1.1";
    std::string wmsExtra;
    std::string userAgent;
    std::vector<std::pair<std::string, std::string>> headers;
    bool visible = true;
    std::shared_ptr<HostTileSource> source;
};

enum class ElevationType { Terrarium, Directory, Gdal, QuantizedMesh, PMTiles, MBTiles };

enum class HeightEncoding { Terrarium, MapboxRgb };

struct ElevationLayer {
    std::string name;
    ElevationType type = ElevationType::Terrarium;
    std::string url;
    std::string path;
    int minZoom = 0;
    int maxZoom = 15;
    int tileSize = 256;
    HeightEncoding encoding = HeightEncoding::Terrarium;
    bool cache = true;
    bool bathymetry = false;
    std::vector<std::pair<std::string, std::string>> headers;
};

struct Region {
    double west = 0;
    double south = 0;
    double east = 0;
    double north = 0;
};

std::vector<ImageryLayer> defaultLayers();
ImageryLayer polarFillLayer();
std::vector<ElevationLayer> defaultElevation();
std::vector<ImageryLayer> loadMobac(const std::string& xmlOrPath);
std::string defaultCacheDirectory();
void setAndroidJavaVm(void* vm);
bool gdalAvailable();
double geoidOffset(double lat, double lon);

}
