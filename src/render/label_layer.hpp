#pragma once

#include "core/camera.hpp"
#include "feature/feature_layer.hpp"
#include "raster/vector_tile.hpp"

#include <sokol_gfx.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace otk {

struct RoadBatch {
    sg_buffer buffer{};
    int count = 0;
    float xform[4] = {};
};

struct ContourBucket {
    int first = 0;
    int count = 0;
    GeoRect bounds;
};

struct ContourSet {
    GeoRect bounds;
    double meters = 0.0;
    std::vector<float> segments;
    std::vector<ContourBucket> buckets;
    std::shared_ptr<VectorTile> marks;
};

class LabelLayer {
public:
    LabelLayer();
    ~LabelLayer();
    LabelLayer(const LabelLayer&) = delete;
    LabelLayer& operator=(const LabelLayer&) = delete;

    bool init();
    void shutdown();
    bool available();
    bool animating() const;
    void beginFrame();
    bool prepare(const GeoRect& tile, double unitDeg);
    void drawRoads(const std::vector<RoadBatch>& batches, const GeoRect& tile, double unitDeg, double dpi);
    void drawSurface(const GeoRect& tile, double unitDeg, double dpi);
    void drawBorders3D(const Camera& cam, const GroundFn& ground, double dpi, const std::vector<GeoRect>& visible, int version);
    void beginRoads3D(const Camera& cam, double dpi);
    void drawRoads3D(sg_buffer buffer, int count, const Vec3& origin, const float clip[4], int pass);
    static void buildLines3D(const std::vector<float>& inst, double originLat, double originLon, const GroundFn& ground, std::vector<float>& out);
    void setContours(std::shared_ptr<const ContourSet> set);
    void drawContours(const GeoRect& tile, double unitDeg, double dpi);
    void drawLabels(const Camera& cam, const GroundFn& ground, double dpi, const std::vector<GeoRect>& visible,
                    const std::vector<const VectorTile*>& overlay, int view, bool places, const GroundFn& above = {});

private:
    struct Batch {
        sg_buffer buffer{};
        int count = 0;
        double west = 0, south = 0, east = 0, north = 0;
        std::uint64_t used = 0;
        std::vector<float> inst;
        sg_buffer buffer3d{};
        int count3d = 0;
        int version3d = -1;
        std::chrono::steady_clock::time_point built3d{};
    };
    struct Text;

    void keys(const GeoRect& tile, double unitDeg, std::vector<std::pair<int, int>>& out) const;
    Batch* batch(int tier, int cell, bool load);
    void uploadAtlas();
    void trim();

    std::unordered_map<int, Batch> batches_;
    std::unique_ptr<Text> text_;
    std::future<bool> loading_;
    bool loaded_ = false;
    bool usable_ = false;
    int decodes_ = 0;
    std::uint64_t frame_ = 0;
    sg_shader lineShader_{};
    sg_shader line3dShader_{};
    sg_shader labelShader_{};
    sg_shader iconShader_{};
    sg_pipeline linePipeline_{};
    sg_pipeline line3dPipeline_{};
    float road3dStyles_[16][4] = {};
    float road3dColors_[16][4] = {};
    float road3dView_[4] = {};
    float road3dDash_ = 0.0f;
    Mat4 road3dViewProj_{};
    Vec3 road3dEye_{};
    bool road3dAny_ = false;
    int road3dPass_ = -1;
    sg_pipeline labelPipeline_{};
    sg_pipeline iconPipeline_{};
    sg_buffer quad_{};
    std::array<sg_buffer, 4> labelBuffers_{};
    std::array<sg_buffer, 4> iconBuffers_{};
    sg_image atlasImage_{};
    sg_view atlasView_{};
    sg_image iconImage_{};
    sg_view iconView_{};
    sg_sampler sampler_{};
    std::vector<std::pair<int, int>> scratchKeys_;
    std::shared_ptr<const ContourSet> contours_;
    const ContourSet* contoursUploaded_ = nullptr;
    sg_buffer contourBuffer_{};
    bool ready_ = false;
};

}
