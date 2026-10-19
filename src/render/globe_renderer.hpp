#pragma once

#include "core/camera.hpp"
#include "raster/tile_cache.hpp"
#include "raster/tile_loader.hpp"

#include <sokol_gfx.h>

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
    void setOffline(bool offline) { loader_.setOffline(offline); }
    TileLoader& loader() { return loader_; }
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
        std::uint64_t used = 0;
    };
    struct SurfaceTile {
        sg_image image{};
        sg_view attachment{};
        sg_view texture{};
        bool complete = false;
        int version = -1;
        std::uint64_t used = 0;
    };
    struct ImageryTexture {
        sg_image image{};
        sg_view view{};
        std::uint64_t used = 0;
    };
    struct DrawTile {
        QuadKey key;
        double distance = 0;
    };
    struct SurfaceDraw {
        sg_view texture{};
        float opacity = 1.0f;
        int first = 0;
        int count = 0;
    };
    struct SurfaceJob {
        QuadKey key;
        std::vector<SurfaceDraw> draws;
        bool complete = false;
    };

    static Bounds bounds(const QuadKey& k);
    void traverse(const Camera& cam, const QuadKey& k, std::vector<DrawTile>& out);
    TerrainTile& terrain(const QuadKey& k);
    SurfaceTile* acquireSurface(const QuadKey& k);
    void processLoads();
    void evict();
    bool buildSurfaceJob(const QuadKey& k, double priority, SurfaceJob* job, std::vector<float>* verts);
    void requestBase();
    ImageryTexture* findTexture(const TileId& id, const TileMatrix& m, double priority, bool& missing, TileId& src);
    double now() const;
    void want(const TileId& id, double priority);

    TileCache cache_;
    TileLoader loader_{6};
    std::vector<ImageryLayer> layers_;
    std::vector<TileMatrix> matrices_;
    std::unordered_map<QuadKey, TerrainTile, QuadKeyHash> terrain_;
    std::unordered_map<QuadKey, SurfaceTile, QuadKeyHash> surface_;
    std::unordered_map<TileId, ImageryTexture, TileIdHash> imagery_;
    std::unordered_map<TileId, double, TileIdHash> failed_;
    std::vector<SurfaceTile> freeSurfaces_;
    std::vector<std::pair<TileId, double>> requests_;

    sg_buffer indexBuffer_{};
    sg_buffer skyBuffer_{};
    sg_buffer surfaceBuffer_{};
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

    std::array<double, 16> frustum_{};
    std::uint64_t frame_ = 0;
    int visits_ = 0;
    FILE* trace_ = nullptr;
    int imageryVersion_ = 0;
    double imageryScale_ = 1.0;
    double dpiScale_ = 1.0;
    bool flipSurface_ = false;
    bool busy_ = true;
    bool ready_ = false;
    Stats stats_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}
