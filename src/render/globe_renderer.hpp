#pragma once

#include "core/camera.hpp"
#include "elevation/elevation.hpp"
#include "feature/feature_layer.hpp"
#include "model/model_layer.hpp"
#include "model/pointcloud.hpp"
#include "tiles3d/tileset.hpp"
#include "raster/tile_cache.hpp"
#include "raster/tile_loader.hpp"

#include <sokol_gfx.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace ote {

struct QuadKey {
    int level = 0;
    int x = 0;
    int y = 0;
    bool operator==(const QuadKey& o) const { return level == o.level && x == o.x && y == o.y; }
};

struct QuadKeyHash {
    size_t operator()(const QuadKey& k) const {
        return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(k.level) << 56) ^
                                          (static_cast<std::uint64_t>(k.x) << 28) ^ static_cast<std::uint64_t>(k.y));
    }
};

class GlobeRenderer {
public:
    GlobeRenderer();
    ~GlobeRenderer();

    bool init();
    void shutdown();
    void setLayers(const std::vector<ImageryLayer>& layers);
    const std::vector<ImageryLayer>& layers() const { return layers_; }
    const std::vector<TileMatrix>& matrices() const { return matrices_; }
    void setCacheDirectory(const std::string& dir);
    void setOffline(bool offline);
    void setMemoryBudget(int megabytes);
    void setElevation(const std::vector<ElevationLayer>& layers);
    void setTerrainEnabled(bool enabled);
    void setTerrainExaggeration(double factor);
    void setDepthPerception(bool enabled) { depthPerception_ = enabled; }
    void setHeatmap(bool enabled, double minHeight, double maxHeight, float opacity) {
        heatmap_ = enabled;
        heatMin_ = minHeight;
        heatMax_ = maxHeight;
        heatOpacity_ = opacity;
    }
    void setGrid(bool enabled);
    std::uint64_t addOverlay(const GeoRect& rect, int width, int height, const std::vector<std::uint8_t>& rgba, float opacity);
    bool removeOverlay(std::uint64_t id);
    double elevationAt(double lat, double lon) const;
    TileLoader& loader() { return loader_; }
    FeatureLayer& features() { return features_; }
    ModelLayer& models() { return models_; }
    PointCloudLayer& points() { return points_; }
    std::uint64_t addTileset(const std::string& uri, double maxSse, Vec3* center);
    bool removeTileset(std::uint64_t id) { return tilesets_.erase(id) > 0; }
    const FeatureLayer& features() const { return features_; }
    const TileLoader& loader() const { return loader_; }
    void setImageryScale(double s) { imageryScale_ = s; }
    void setDpiScale(double s) { dpiScale_ = s; }
    void render(Camera& cam, const sg_swapchain* swapchain, const sg_attachments* attachments, int width, int height);
    bool busy() const { return busy_; }
    Stats stats() const { return stats_; }

private:
    struct Bounds {
        double west, south, east, north;
    };
    struct TerrainTile {
        sg_buffer vbuf{};
        Vec3 center;
        float minH = 0;
        float maxH = 0;
        int version = -1;
        bool complete = false;
        std::uint64_t used = 0;
    };
    static constexpr int kMips = 9;
    struct SurfaceTile {
        sg_image image{};
        sg_view attachment{};
        sg_view texture{};
        std::array<sg_view, kMips> levels{};
        std::array<sg_view, kMips> sources{};
        bool complete = false;
        int version = -1;
        std::uint64_t used = 0;
        std::vector<TileId> missing;
    };
    struct ImageryTexture {
        sg_image image{};
        sg_view view{};
        std::uint64_t used = 0;
        std::shared_ptr<VectorTile> vector;
        sg_buffer fill{};
        sg_buffer line{};
        int fillCount = 0;
        int lineCount = 0;
    };
    struct DrawTile {
        QuadKey key;
        double distance = 0;
    };
    struct Need {
        QuadKey key;
        double priority = 0;
    };
    struct NodeInfo {
        bool visible = false;
        bool refine = false;
        bool nearRefine = false;
        double distance = 0;
    };
    struct Fade {
        double start = 0;
        QuadKey from;
    };
    struct SurfaceDraw {
        sg_view texture{};
        float opacity = 1.0f;
        int first = 0;
        int count = 0;
    };
    struct VectorDraw {
        const ImageryTexture* tile = nullptr;
        float xform[4];
    };
    struct SurfaceJob {
        QuadKey key;
        std::vector<SurfaceDraw> draws;
        std::vector<TileId> missing;
        std::vector<VectorDraw> vectors;
        bool background = false;
        int featureFirst = 0;
        int featureCount = 0;
        bool complete = false;
    };

    static Bounds bounds(const QuadKey& k);
    NodeInfo info(const Camera& cam, const QuadKey& k) const;
    bool ready(const QuadKey& k) const;
    bool progressed(const SurfaceTile& s) const;
    void need(const QuadKey& k, double priority);
    void prefetch(const QuadKey& k, double priority) { prefetch_.push_back({k, priority}); }
    void traverse(const Camera& cam, const QuadKey& k, std::vector<DrawTile>& out);
    TerrainTile& terrain(const QuadKey& k);
    void buildTerrain(const QuadKey& k, TerrainTile& t, double priority);
    SurfaceTile* acquireSurface(const QuadKey& k);
    static void dropSurface(SurfaceTile& s);
    void processLoads();
    void evict();
    bool buildSurfaceJob(const QuadKey& k, double priority, SurfaceJob* job, std::vector<float>* verts,
                         std::vector<float>* fverts = nullptr, double texelDeg = 0);
    static void dropImagery(ImageryTexture& t);
    void requestBase();
    void want(const TileId& id, double priority);
    ImageryTexture* findTexture(const TileId& id, const TileMatrix& m, double priority, bool& missing, TileId& src);
    void invalidateTerrain();
    void markDirty(const GeoRect& rect);
    double now() const;

    TileCache cache_;
    TileLoader loader_{3, 8};
    ElevationManager elevation_{4};
    FeatureLayer features_;
    ModelLayer models_;
    PointCloudLayer points_;
    std::unordered_map<std::uint64_t, std::unique_ptr<Tileset3D>> tilesets_;
    std::uint64_t nextTileset_ = 1;
    std::atomic<bool> offline_{false};
    std::vector<ImageryLayer> layers_;
    std::vector<TileMatrix> matrices_;
    std::vector<bool> vectorLayers_;
    std::unordered_map<QuadKey, TerrainTile, QuadKeyHash> terrain_;
    std::unordered_map<QuadKey, SurfaceTile, QuadKeyHash> surface_;
    std::unordered_map<TileId, ImageryTexture, TileIdHash> imagery_;
    std::unordered_map<TileId, double, TileIdHash> failed_;
    std::vector<SurfaceTile> freeSurfaces_;
    std::vector<std::pair<TileId, double>> requests_;
    std::vector<Need> needs_;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    size_t surfaceCapacity_ = 400, imageryCapacity_ = 244, terrainCapacity_ = 1500, freeSurfacePool_ = 25;
#else
    size_t surfaceCapacity_ = 800, imageryCapacity_ = 488, terrainCapacity_ = 3000, freeSurfacePool_ = 50;
#endif
    std::unordered_map<QuadKey, Fade, QuadKeyHash> fades_;
    double needScale_ = 1.0;
    bool prefetching_ = false;
    std::vector<Need> prefetch_;
    bool viewComplete_ = false;
    std::unordered_map<QuadKey, std::uint64_t, QuadKeyHash> drawn_;
    struct Overlay {
        GeoRect rect;
        sg_image image{};
        sg_view view{};
        float opacity = 1.0f;
    };
    std::unordered_map<std::uint64_t, Overlay> overlays_;
    std::uint64_t nextOverlay_ = 1;
    bool grid_ = false;
    double gridSpacing_ = 0;

    sg_buffer indexBuffer_{};
    sg_buffer skyBuffer_{};
    sg_buffer surfaceBuffer_{};
    sg_buffer featureBuffer_{};
    sg_image fallbackImage_{};
    sg_view fallbackView_{};
    sg_sampler sampler_{};
    sg_sampler imagerySampler_{};
    sg_shader globeShader_{};
    sg_shader surfaceShader_{};
    sg_shader skyShader_{};
    sg_pipeline globePipeline_{};
    sg_pipeline surfacePipeline_{};
    sg_pipeline skyPipeline_{};
    sg_shader vfillShader_{};
    sg_shader vlineShader_{};
    sg_pipeline vfillPipeline_{};
    sg_pipeline vlinePipeline_{};
    sg_shader mipShader_{};
    sg_pipeline mipPipeline_{};
    sg_sampler mipSampler_{};
    sg_image scratch_{};
    sg_view scratchAttachment_{};
    sg_view scratchTexture_{};

    std::array<double, 16> frustum_{};
    Vec3 eye_;
    double pixelScale_ = 1.0;
    std::uint64_t frame_ = 0;
    int visits_ = 0;
    FILE* trace_ = nullptr;
    int imageryVersion_ = 0;
    int terrainEpoch_ = 0;
    double imageryScale_ = 1.0;
    double dpiScale_ = 1.0;
    double exaggeration_ = 1.0;
    bool terrainEnabled_ = true;
    bool depthPerception_ = true;
    bool heatmap_ = false;
    double heatMin_ = 0, heatMax_ = 4500;
    float heatOpacity_ = 0.55f;
    bool flipSurface_ = false;
    bool busy_ = true;
    bool ready_ = false;
    Stats stats_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}
