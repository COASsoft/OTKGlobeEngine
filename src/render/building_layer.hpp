#pragma once

#include "core/camera.hpp"
#include "feature/buildings.hpp"
#include "raster/vector_tile.hpp"

#include <sokol_gfx.h>

#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace otk {

class BuildingLayer {
public:
    struct Piece {
        sg_view view{};
        float fx0 = 0.0f, fy0 = 0.0f, fx1 = 1.0f, fy1 = 1.0f;
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        int z = -1;
    };
    using TileFn = std::function<int(int x, int y, double priority, std::shared_ptr<const VectorTile>& out)>;
    using HeightsFn = std::function<std::vector<double>(const std::vector<std::pair<double, double>>& latLon)>;
    using ImageryFn = std::function<void(double west, double south, double east, double north, int level, bool request, std::vector<Piece>& out)>;

    BuildingLayer() = default;
    ~BuildingLayer();
    BuildingLayer(const BuildingLayer&) = delete;
    BuildingLayer& operator=(const BuildingLayer&) = delete;

    void shutdown();
    void clear();
    void retry();
    void update(const Camera& cam, const TileFn& tile, const HeightsFn& heights, const ImageryFn& imagery, double exaggeration, bool terrain);
    void bake(const ImageryFn& imagery, bool flip);
    void draw(const Camera& cam, Vec3 light, bool sun);
    bool busy() const { return busy_; }
    int drawn() const { return drawn_; }

private:
    struct Tile {
        int x = 0, y = 0;
        std::future<std::shared_ptr<BuildingMesh>> job;
        bool building = false;
        bool ready = false;
        bool empty = false;
        sg_buffer vbuf{};
        sg_buffer ibuf{};
        int indexCount = 0;
        sg_image atlas{};
        sg_view atlasColor{};
        sg_view atlasTexture{};
        int atlasSize = 0;
        std::vector<RoofCell> cells;
        bool baked = false;
        int bakedLevel = -1;
        int availableLevel = -1;
        int availableMin = -1;
        int target = 16;
        double imageryCheck = 0.0;
        Vec3 origin, east, north, up;
        double appear = -1.0;
        double distance = 0.0;
        std::uint64_t used = 0;
    };

    bool ready();
    void upload(Tile& t, const BuildingMesh& mesh);
    void release(Tile& t);

    std::map<std::pair<int, int>, Tile> tiles_;
    bool initialized_ = false;
    bool failed_ = false;
    sg_shader shader_{};
    sg_shader bakeShader_{};
    sg_pipeline pipeline_{};
    sg_pipeline bakePipeline_{};
    sg_image facades_{};
    sg_view facadeView_{};
    sg_image blank_{};
    sg_view blankView_{};
    sg_sampler facadeSampler_{};
    sg_sampler roofSampler_{};
    sg_buffer bakeBuffer_{};
    std::uint64_t frame_ = 0;
    int jobs_ = 0;
    int drawn_ = 0;
    double fade_ = 1.0;
    bool busy_ = false;
};

}
