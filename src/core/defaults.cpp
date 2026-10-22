#include "otk/layers.hpp"

#include "core/geoid.hpp"
#include "net/http.hpp"
#include "otk/image.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace otk {

void setAndroidJavaVm(void* vm) { net::setJavaVm(vm); }

double geoidOffset(double lat, double lon) { return geoidHeight(lat, lon); }

namespace {

std::string cacheDirectoryFor(const char* name) {
#if defined(_WIN32)
    if (const char* la = std::getenv("LOCALAPPDATA"); la && *la) return std::string(la) + "/" + name;
#elif defined(__ANDROID__)
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) return std::string(home) + "/Library/Application Support/" + name;
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::string(xdg) + "/" + name;
    if (const char* home = std::getenv("HOME"); home && *home) return std::string(home) + "/.cache/" + name;
#endif
    return {};
}

}

std::string defaultCacheDirectory() {
    std::string dir = cacheDirectoryFor("OTKGlobeEngine");
    std::string old = cacheDirectoryFor("OpenTAKEngine");
    std::error_code ec;
    if (!dir.empty() && !old.empty() && !std::filesystem::exists(std::filesystem::u8path(dir), ec) &&
        std::filesystem::is_directory(std::filesystem::u8path(old), ec))
        std::filesystem::rename(std::filesystem::u8path(old), std::filesystem::u8path(dir), ec);
    return dir;
}

std::vector<ElevationLayer> defaultElevation() {
    ElevationLayer t;
    if (webpSupported()) {
        t.name = "Mapterhorn";
        t.url = "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp";
        t.tileSize = 512;
        t.maxZoom = 16;
    } else {
        t.name = "Terrain Tiles";
        t.url = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";
        t.maxZoom = 15;
    }
    return {t};
}

std::vector<ImageryLayer> defaultLayers() {
    ImageryLayer imagery;
    imagery.name = "World Imagery";
    imagery.url = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}";
    imagery.maxZoom = 19;
    imagery.attribution = "Imagery: Esri, Maxar, Earthstar Geographics";
    return {imagery, polarFillLayer()};
}

std::vector<Basemap> defaultBasemaps() {
    std::vector<Basemap> maps;
    maps.push_back({"Satellite", defaultLayers()});
    std::vector<ImageryLayer> topo = defaultLayers();
    ImageryLayer usgs;
    usgs.name = "USGS Topo";
    usgs.url = "https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/{z}/{y}/{x}";
    usgs.minZoom = 3;
    usgs.maxZoom = 16;
    usgs.attribution = "Topo: USGS The National Map";
    topo.push_back(usgs);
    maps.push_back({"Topographic", topo});
    ImageryLayer osm;
    osm.name = "OpenStreetMap";
    osm.url = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    osm.maxZoom = 19;
    osm.attribution = "\xC2\xA9 OpenStreetMap contributors";
    maps.push_back({"Streets", {osm, polarFillLayer()}});
    ImageryLayer vector;
    vector.name = "OpenFreeMap";
    vector.type = SourceType::Mvt;
    vector.url = "https://tiles.openfreemap.org/planet";
    vector.maxZoom = 14;
    vector.attribution = "OpenFreeMap Â© OpenMapTiles, Â© OpenStreetMap contributors";
    maps.push_back({"Vector", {vector, polarFillLayer()}});
    return maps;
}

ImageryLayer polarFillLayer() {
    ImageryLayer polar;
    polar.name = "NASA Blue Marble Shaded Relief and Bathymetry (polar fill)";
    polar.url = "https://gibs.earthdata.nasa.gov/wmts/epsg4326/best/BlueMarble_ShadedRelief_Bathymetry/default/500m/{z}/{y}/{x}.jpeg";
    polar.srid = 4326;
    polar.tileSize = 512;
    polar.level0Resolution = 0.5625;
    polar.maxZoom = 7;
    polar.polarFill = true;
    polar.attribution = "Polar fill: NASA Blue Marble";
    const float north[3] = {1.79f, 2.27f, 1.26f}, south[3] = {1.02f, 1.03f, 1.075f};
    for (int i = 0; i < 3; ++i) {
        polar.polarTintNorth[i] = north[i];
        polar.polarTintSouth[i] = south[i];
    }
    return polar;
}

}
