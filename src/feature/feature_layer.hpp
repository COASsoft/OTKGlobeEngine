#pragma once

#include "core/camera.hpp"
#include "feature/text.hpp"
#include "ote/features.hpp"

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ote {

struct GeoRect {
    double west = 0, south = 0, east = 0, north = 0;
    bool intersects(const GeoRect& o) const { return west <= o.east && east >= o.west && south <= o.north && north >= o.south; }
};

using GroundFn = std::function<double(double, double)>;
using PickFn = std::function<bool(double, double, double&, double&)>;

class FeatureLayer {
public:
    bool init();
    void shutdown();

    std::uint64_t add(const Feature& f);
    bool update(std::uint64_t id, const Feature& f);
    bool remove(std::uint64_t id);
    void clear();
    bool get(std::uint64_t id, Feature& out) const;
    std::vector<std::uint64_t> ids() const;
    bool empty() const { return items_.empty(); }
    bool takeDirty(std::vector<GeoRect>& regions);

    void buildSurface(const GeoRect& tile, double texelDeg, double dpi, std::vector<float>& verts);
    void drawSurface(sg_buffer buffer, int first, int count);
    struct ExternalLabel {
        double lat = 0, lon = 0;
        std::string text;
        double priority = 0;
        std::uint32_t color = 0xFF202020;
        float size = 12.0f;
        bool halo = true;
    };
    void drawOverlay(const Camera& cam, const GroundFn& ground, double dpi, bool flip,
                     const std::vector<ExternalLabel>* extra = nullptr);
    std::uint64_t hitTest(const Camera& cam, const GroundFn& ground, const PickFn& pick, double sx, double sy,
                          double radius) const;

private:
    struct Item {
        Feature feature;
        GeoRect bounds;
        std::vector<std::vector<std::array<double, 2>>> lines;
        std::vector<std::array<double, 2>> triangles;
        std::vector<std::array<double, 3>> points;
    };

    void prepare(Item& it);
    static bool draped(const Item& it) {
        return it.feature.altitudeMode == AltitudeMode::ClampToGround && (!it.lines.empty() || !it.triangles.empty());
    }
    const std::vector<std::uint64_t>& ordered();
    double altitude(const Item& it, double lat, double lon, double alt, const GroundFn& ground) const;
    void uploadText();
    void uploadIcons();
    bool iconRect(const Image* img, float rect[4]);

    std::unordered_map<std::uint64_t, Item> items_;
    std::vector<std::uint64_t> sorted_;
    bool sortDirty_ = true;
    bool atlasFull_ = false;
    std::uint64_t nextId_ = 1;
    std::vector<GeoRect> dirty_;
    GlyphAtlas atlas_;

    sg_image textImage_{};
    sg_view textView_{};
    sg_image iconImage_{};
    sg_view iconView_{};
    sg_sampler sampler_{};
    sg_shader surfaceShader_{};
    sg_shader spriteShader_{};
    sg_shader textShader_{};
    sg_shader lineShader_{};
    sg_shader imageShader_{};
    sg_pipeline imagePipeline_{};
    sg_image atlasImage_{};
    sg_view atlasView_{};
    std::vector<std::uint8_t> iconPixels_;
    std::unordered_map<const Image*, std::array<float, 4>> atlasRects_;
    std::vector<std::shared_ptr<const Image>> atlasRefs_;
    int atlasX_ = 0, atlasY_ = 0, atlasRow_ = 0;
    bool atlasDirty_ = true;
    sg_pipeline surfacePipeline_{};
    sg_pipeline spritePipeline_{};
    sg_pipeline textPipeline_{};
    sg_pipeline linePipeline_{};
    sg_buffer overlayBuffer_{};
    bool ready_ = false;
};

}
