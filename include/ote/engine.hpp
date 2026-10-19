#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ote/features.hpp"
#include "ote/mgrs.hpp"

struct sg_swapchain;
struct sg_attachments;
struct sg_desc;

namespace ote {

enum class SourceType { Url, Wms, Wmts, MBTiles, GeoPackage, Osmdroid, Gdal, Mvt };

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
    bool tms = false;
    bool cache = true;
    float opacity = 1.0f;
    std::string wmsLayers;
    std::string wmsStyles;
    std::string wmsFormat = "image/png";
    std::string wmsVersion = "1.1.1";
    std::string wmsExtra;
    std::string userAgent;
};

enum class ElevationType { Terrarium, Directory, Gdal };

struct ElevationLayer {
    std::string name;
    ElevationType type = ElevationType::Terrarium;
    std::string url;
    std::string path;
    int maxZoom = 15;
    bool cache = true;
    bool bathymetry = false;
};

struct View {
    double lat = 20.0;
    double lon = -40.0;
    double alt = 0.0;
    double gsd = 20000.0;
    double azimuth = 0.0;
    double tilt = 0.0;
};

struct Stats {
    int terrainTiles = 0;
    int surfaceRenders = 0;
    int imageryTextures = 0;
    int pendingRequests = 0;
    int maxLevel = 0;
    int incompleteTiles = 0;
};

struct DownloadStatus {
    std::int64_t total = 0;
    std::int64_t done = 0;
    std::int64_t failed = 0;
    bool finished = false;
    bool cancelled = false;
};

struct ImageOverlay {
    double north = 0;
    double south = 0;
    double east = 0;
    double west = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    float opacity = 1.0f;
};

struct LineOfSight {
    bool visible = true;
    Coord obstruction;
    double distance = 0;
};

struct ProfileSample {
    double distance = 0;
    double lat = 0;
    double lon = 0;
    double elevation = 0;
};

struct Region {
    double west = 0;
    double south = 0;
    double east = 0;
    double north = 0;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init();
    void shutdown();

    void setViewport(int width, int height, float dpiScale);
    void setLayers(const std::vector<ImageryLayer>& layers);
    std::vector<ImageryLayer> layers() const;
    void setImageryScale(double scale);
    void setCacheDirectory(const std::string& dir);
    void setOffline(bool offline);
    void setMemoryBudget(int megabytes);
    void setElevation(const std::vector<ElevationLayer>& layers);
    void setTerrainEnabled(bool enabled);
    void setTerrainExaggeration(double factor);
    void setDepthPerception(bool enabled);
    void setElevationHeatmap(bool enabled, double minHeight = 0, double maxHeight = 4500, float opacity = 0.55f);
    std::vector<ProfileSample> elevationProfile(const std::vector<Coord>& path, int samples) const;
    double elevationAt(double lat, double lon) const;

    View view() const;
    void setView(const View& v);

    bool pick(double sx, double sy, double& lat, double& lon) const;
    void pan(double x0, double y0, double x1, double y1);
    void zoomAt(double sx, double sy, double factor);
    void rotate(double degrees);
    void tiltBy(double degrees);
    void flyTo(const View& target, double seconds = 2.0);
    void setFirstPerson(bool enabled, double heightAboveGround = 2.0);
    bool firstPerson() const;
    void walk(double forwardMeters, double rightMeters);
    void look(double headingDegrees, double pitchDegrees);
    void set3D(bool enabled);
    bool is3D() const;
    bool animating() const;

    std::uint64_t addFeature(const Feature& feature);
    bool updateFeature(std::uint64_t id, const Feature& feature);
    bool removeFeature(std::uint64_t id);
    void clearFeatures();
    bool feature(std::uint64_t id, Feature& out) const;
    std::vector<std::uint64_t> featureIds() const;
    std::vector<std::uint64_t> loadFeatures(const std::string& path);
    std::uint64_t hitTest(double sx, double sy, double radiusPx = 16.0) const;

    std::uint64_t addModel(const std::string& path, const ModelPlacement& placement);
    bool updateModel(std::uint64_t id, const ModelPlacement& placement);
    bool removeModel(std::uint64_t id);
    std::uint64_t addTileset(const std::string& pathOrUrl, double maxScreenSpaceError = 16.0);
    bool removeTileset(std::uint64_t id);
    bool tilesetCenter(std::uint64_t id, double& lat, double& lon, double& alt) const;

    void setGridEnabled(bool enabled);
    std::uint64_t addImageOverlay(const ImageOverlay& overlay);
    bool removeImageOverlay(std::uint64_t id);
    LineOfSight lineOfSight(const Coord& from, double fromHeight, const Coord& to, double toHeight) const;
    std::uint64_t computeViewshed(const Coord& observer, double heightAboveGround, double radiusMeters, int resolution = 256);

    int downloadRegion(int layer, const Region& region, int minZoom, int maxZoom);
    std::int64_t countRegionTiles(int layer, const Region& region, int minZoom, int maxZoom) const;
    DownloadStatus downloadStatus(int id) const;
    void cancelDownload(int id);

    void frame(const sg_swapchain& swapchain);
    void frameOffscreen(const sg_attachments& attachments, int width, int height);
    bool needsRedraw() const;
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<ImageryLayer> defaultLayers();
std::vector<ElevationLayer> defaultElevation();
std::vector<ImageryLayer> loadMobac(const std::string& xmlOrPath);
void configureGfx(sg_desc& desc);
void setAndroidJavaVm(void* vm);
bool gdalAvailable();
double geoidOffset(double lat, double lon);

}
