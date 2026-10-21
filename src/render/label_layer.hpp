#pragma once

#include "core/camera.hpp"
#include "feature/feature_layer.hpp"
#include "feature/text.hpp"

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <future>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace otk {

class LabelLayer {
public:
    bool init();
    void shutdown();
    bool available();
    void beginFrame();
    bool prepare(const GeoRect& tile, double texelDeg);
    void drawSurface(const GeoRect& tile, double texelDeg, double dpi, int size);
    void drawLabels(const Camera& cam, const GroundFn& ground, double dpi, const std::vector<GeoRect>& visible, int view);

private:
    struct Batch {
        sg_buffer buffer{};
        int count = 0;
        double west = 0, south = 0, east = 0, north = 0;
        std::uint64_t used = 0;
    };

    void keys(const GeoRect& tile, double texelDeg, std::vector<std::pair<int, int>>& out) const;
    Batch* batch(int tier, int cell, bool load);
    void addDot();
    void uploadAtlas();
    void trim();

    std::unordered_map<int, Batch> batches_;
    std::future<bool> loading_;
    bool loaded_ = false;
    bool usable_ = false;
    int decodes_ = 0;
    std::uint64_t frame_ = 0;
    GlyphAtlas atlas_;
    sg_shader lineShader_{};
    sg_shader labelShader_{};
    sg_pipeline linePipeline_{};
    sg_pipeline labelPipeline_{};
    sg_buffer quad_{};
    std::array<sg_buffer, 4> labelBuffers_{};
    sg_image atlasImage_{};
    sg_view atlasView_{};
    sg_sampler sampler_{};
    std::vector<std::pair<int, int>> scratchKeys_;
    bool ready_ = false;
};

}
