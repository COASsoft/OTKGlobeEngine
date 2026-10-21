#include "render/label_layer.hpp"

#include "core/geo.hpp"
#include "labels/label_data.hpp"

#include "labels.glsl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_set>

namespace otk {

namespace embedded {
extern const unsigned char label_font[];
extern const std::size_t label_font_size;
}

namespace {

constexpr std::size_t kLabelBytes = 2 * 1024 * 1024;
constexpr std::size_t kIconBytes = 256 * 1024;
constexpr std::size_t kMaxBatches = 72;
constexpr int kDecodesPerFrame = 4;
constexpr std::uint32_t kDot = 0xE000;
constexpr int kDotRadius = 10;
constexpr double kOverviewMeters = 15000.0;
constexpr double kCoarseMeters = 2000.0;
constexpr double kCountyMeters = 500.0;
constexpr double kStateMeters = 6500.0;
constexpr int kOverviewClass = 18;
constexpr float kEmToLine = 1.1719f;
constexpr int kMaxLabels = 700;
constexpr std::size_t kMaxCandidates = 9000;
constexpr int kIconSize = 64;
constexpr int kIcons = 4;

enum Tier { Fine = 0, Coarse = 1, Overview = 2 };

struct BorderStyle {
    float width, casing, on, off;
    float color[4];
};

constexpr BorderStyle kBorders[4] = {
    {2.0f, 1.0f, 0.0f, 0.0f, {1.0f, 1.0f, 1.0f, 0.95f}},
    {1.8f, 0.9f, 5.0f, 4.0f, {1.0f, 1.0f, 1.0f, 0.9f}},
    {1.6f, 0.9f, 4.0f, 3.0f, {1.0f, 1.0f, 1.0f, 0.9f}},
    {1.3f, 0.8f, 3.5f, 3.0f, {0.92f, 0.92f, 0.92f, 0.8f}},
};
constexpr float kShade[4] = {0.0f, 0.0f, 0.0f, 0.4f};

struct RoadStyle {
    float zoom[6];
    float width[6];
    float casing;
    float on, off;
    float color[4];
};

constexpr RoadStyle kRoads[12] = {
    {{5.5f, 9, 12, 14, 16, 18}, {1.2f, 1.8f, 2.6f, 3.6f, 6.0f, 11.0f}, 0.7f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{9.5f, 11, 12, 14, 16, 18}, {0.9f, 1.2f, 1.6f, 2.2f, 3.6f, 6.5f}, 0.6f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{6.5f, 9, 12, 14, 16, 18}, {1.0f, 1.6f, 2.3f, 3.2f, 5.5f, 10.0f}, 0.7f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{10.5f, 11, 12, 14, 16, 18}, {0.9f, 1.1f, 1.5f, 2.0f, 3.4f, 6.0f}, 0.6f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{8.5f, 10, 12, 14, 16, 18}, {0.9f, 1.2f, 1.8f, 3.0f, 5.0f, 9.0f}, 0.6f, 0, 0, {0.91f, 0.91f, 0.91f, 0.9f}},
    {{10.0f, 11, 12, 14, 16, 18}, {0.9f, 1.1f, 1.5f, 2.6f, 4.5f, 8.0f}, 0.6f, 0, 0, {0.87f, 0.87f, 0.87f, 0.85f}},
    {{11.0f, 12, 13, 14, 16, 18}, {0.8f, 1.0f, 1.3f, 2.2f, 4.0f, 7.0f}, 0.5f, 0, 0, {0.8f, 0.8f, 0.8f, 0.8f}},
    {{12.5f, 13, 14, 15, 16, 18}, {0.7f, 0.9f, 1.6f, 2.2f, 3.2f, 6.0f}, 0.5f, 0, 0, {0.75f, 0.75f, 0.75f, 0.75f}},
    {{14.5f, 15, 16, 17, 18, 19}, {0.7f, 1.0f, 1.6f, 2.4f, 3.5f, 5.0f}, 0.4f, 0, 0, {0.69f, 0.69f, 0.69f, 0.65f}},
    {{15.0f, 16, 17, 18, 19, 20}, {0.7f, 0.9f, 1.1f, 1.5f, 2.0f, 2.5f}, 0.0f, 3.0f, 2.0f, {0.84f, 0.78f, 0.67f, 0.6f}},
    {{10.0f, 12, 14, 16, 18, 20}, {0.9f, 1.4f, 2.6f, 4.5f, 8.0f, 10.0f}, 0.0f, 4.0f, 3.0f, {0.9f, 0.9f, 0.9f, 0.45f}},
    {{13.0f, 14, 15, 16, 18, 20}, {0.7f, 1.2f, 1.8f, 2.8f, 5.0f, 7.0f}, 0.0f, 3.0f, 3.0f, {0.8f, 0.8f, 0.8f, 0.4f}},
};
constexpr float kRoadShade[4] = {0.08f, 0.08f, 0.08f, 0.55f};

struct LabelVertex {
    float x, y, u, v;
    std::uint32_t fill, halo;
    float haloPx, weight;
};

struct TextStyle {
    float size = 12.0f;
    float tracking = 0.0f;
    std::uint32_t fill = 0xFFFFFFFF;
    std::uint32_t halo = 0xD9101010;
    float haloPx = 1.2f;
    float weight = 0.0f;
    float dot = 0.0f;
    float padding = 3.0f;
};

std::uint32_t rgba(int r, int g, int b, float a) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 |
           static_cast<std::uint32_t>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f)) << 24;
}

TextStyle scaled(TextStyle s, double dpi) {
    const float k = static_cast<float>(dpi);
    s.size *= kEmToLine * k;
    s.tracking *= k;
    s.haloPx *= k;
    s.dot *= k;
    s.padding *= k;
    return s;
}

TextStyle styleFor(const labels::Place& p, double zoom, double dpi) {
    TextStyle s;
    const std::uint32_t dark = rgba(16, 16, 16, 0.85f);
    switch (p.kind) {
    case labels::Kind::Country:
        s = {18.0f, 0.3f, rgba(255, 255, 255, 1.0f), dark, 1.4f, 0.04f, 0.0f, 6.0f};
        break;
    case labels::Kind::State:
        s = {11.0f, 1.0f, rgba(236, 236, 236, 1.0f), rgba(16, 16, 16, 0.8f), 1.2f, 0.01f, 0.0f, 4.0f};
        break;
    case labels::Kind::County:
        s = {10.5f, 0.3f, rgba(214, 214, 214, 1.0f), rgba(16, 16, 16, 0.75f), 1.1f, 0.0f, 0.0f, 4.0f};
        break;
    case labels::Kind::Suburb:
        s = {10.5f, 0.2f, rgba(226, 226, 226, 1.0f), rgba(16, 16, 16, 0.8f), 1.1f, 0.0f, 0.0f, 3.0f};
        break;
    default: {
        const double pop = std::exp2(p.population / 8.0);
        const bool capital = (p.flags & labels::kCapital) != 0;
        float size = capital || pop >= 1e6 ? 14.0f : pop >= 5e5 ? 13.5f : pop >= 1e5 ? 11.5f : pop >= 1e4 ? 11.0f : 10.5f;
        float dot = zoom < 10.5 ? (capital ? 3.2f : 2.6f) : 0.0f;
        s = {size, 0.1f, rgba(255, 255, 255, 1.0f), dark, 1.3f, 0.02f, dot, 3.0f};
        break;
    }
    }
    return scaled(s, dpi);
}

TextStyle markStyle(const TileMark& m, double dpi) {
    const std::uint32_t dark = rgba(16, 16, 16, 0.85f);
    const std::uint32_t blue = rgba(110, 165, 240, 1.0f);
    TextStyle s;
    switch (m.type) {
    case MarkType::Road: s = {10.5f, 0.0f, rgba(255, 255, 255, 1.0f), dark, 1.2f, 0.0f, 0.0f, 2.0f}; break;
    case MarkType::Shield: {
        const bool interstate = m.network == "us-interstate";
        s = {10.5f, 0.0f, interstate ? rgba(255, 255, 255, 1.0f) : rgba(20, 20, 20, 1.0f), rgba(0, 0, 0, 0.0f), 0.0f, 0.05f, 0.0f, 2.0f};
        break;
    }
    case MarkType::Place:
        s = m.rank == 0 ? TextStyle{10.0f, 1.1f, rgba(232, 232, 232, 1.0f), rgba(16, 16, 16, 0.8f), 1.1f, 0.02f, 0.0f, 3.0f}
                        : TextStyle{11.0f, 0.1f, rgba(255, 255, 255, 1.0f), dark, 1.2f, 0.02f, 0.0f, 3.0f};
        break;
    case MarkType::Park: s = {m.rank == 0 ? 12.0f : 11.0f, 0.0f, rgba(150, 215, 150, 1.0f), dark, 1.2f, 0.02f, 0.0f, 3.0f}; break;
    case MarkType::Water:
        s = m.rank == 0 ? TextStyle{13.0f, 1.5f, blue, rgba(16, 16, 16, 0.7f), 1.1f, 0.0f, 0.0f, 4.0f}
                        : TextStyle{11.0f, 0.0f, blue, dark, 1.2f, 0.0f, 0.0f, 3.0f};
        break;
    case MarkType::Waterway: s = {10.5f, 0.2f, blue, dark, 1.2f, 0.0f, 0.0f, 2.0f}; break;
    }
    return scaled(s, dpi);
}

float markZoom(const TileMark& m) {
    static constexpr float kRoad[8] = {12.0f, 12.5f, 13.0f, 13.5f, 14.0f, 15.0f, 16.5f, 16.5f};
    const int r = m.rank;
    switch (m.type) {
    case MarkType::Shield: return r == 0 ? 7.0f : r == 1 ? 8.5f : 10.5f;
    case MarkType::Road: return kRoad[std::min(r, 7)];
    case MarkType::Place: return r == 0 ? 13.0f : r == 1 ? 12.0f : 11.5f;
    case MarkType::Park: return r == 0 ? 6.0f : 14.5f;
    case MarkType::Water: return r == 0 ? 2.5f : r == 1 ? 8.0f : m.zoom <= 10 ? std::max(8.0f, static_cast<float>(m.zoom)) : 12.0f;
    case MarkType::Waterway: return r == 0 ? 12.0f : r == 1 ? 13.5f : 14.5f;
    }
    return 20.0f;
}

float markBase(const TileMark& m) {
    switch (m.type) {
    case MarkType::Shield: return 1.0f;
    case MarkType::Place: return 2.2f;
    case MarkType::Water: return 2.4f;
    case MarkType::Park: return 2.6f;
    case MarkType::Waterway: return 3.0f;
    default: return 3.2f;
    }
}

float stop(const float (&zoom)[6], const float (&value)[6], double z) {
    if (z < zoom[0]) return 0.0f;
    for (int i = 1; i < 6; ++i)
        if (z <= zoom[i]) {
            const double t = (z - zoom[i - 1]) / std::max(1e-6f, zoom[i] - zoom[i - 1]);
            return static_cast<float>(value[i - 1] + (value[i] - value[i - 1]) * t);
        }
    return value[5];
}

float lineWidth(GlyphAtlas& atlas, std::string_view line, float scale, float tracking) {
    float w = 0.0f;
    std::uint32_t prev = 0;
    int n = 0;
    for (std::uint32_t cp : GlyphAtlas::decode(std::string(line))) {
        if (prev) w += atlas.kerning(prev, cp) * scale;
        w += atlas.glyph(cp).advance * scale;
        prev = cp;
        ++n;
    }
    return w + tracking * std::max(n - 1, 0);
}

std::string wrapName(const std::string& name, size_t limit) {
    if (name.size() <= limit || name.find('\n') != std::string::npos) return name;
    size_t best = std::string::npos, score = name.size();
    for (size_t i = name.find(' '); i != std::string::npos; i = name.find(' ', i + 1)) {
        size_t s = std::max(i, name.size() - i - 1);
        if (s < score) {
            score = s;
            best = i;
        }
    }
    if (best == std::string::npos) return name;
    std::string out = name;
    out[best] = '\n';
    return out;
}

double shieldShape(int type, double u, double v) {
    if (type >= 2) {
        const double hx = 0.94, hy = 0.78, r = 0.34;
        const double qx = std::max(std::abs(u) - (hx - r), 0.0), qy = std::max(std::abs(v) - (hy - r), 0.0);
        return std::hypot(qx, qy) - r;
    }
    const double au = std::abs(u);
    double top = type == 0 ? -0.86 + 0.09 * (1.0 - std::abs(au * 2.0 - 1.0)) : -0.86;
    if (v < top) return top - v;
    if (v <= 0.18) return au - 0.9;
    const double t = std::clamp((v - 0.18) / 0.78, 0.0, 1.0);
    const double half = 0.9 * std::pow(1.0 - t, 0.75);
    return au - half;
}

std::vector<std::uint8_t> makeIcons() {
    const int w = kIconSize * kIcons, h = kIconSize;
    std::vector<std::uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    for (int type = 0; type < kIcons; ++type)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < kIconSize; ++x) {
                double acc[4] = {0, 0, 0, 0};
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const double u = (x + (sx + 0.5) / 4.0) / kIconSize * 2.0 - 1.0;
                        const double v = (y + (sy + 0.5) / 4.0) / kIconSize * 2.0 - 1.0;
                        if (shieldShape(type, u, v) > 0.0) continue;
                        const bool core = shieldShape(type, u / 0.84, (v - 0.01) / 0.84) <= 0.0;
                        double c[3];
                        if (type == 0) {
                            if (!core) c[0] = c[1] = c[2] = 1.0;
                            else if (v < -0.38) c[0] = 0.85, c[1] = 0.22, c[2] = 0.25;
                            else c[0] = 0.18, c[1] = 0.40, c[2] = 0.75;
                        } else {
                            const double edge = type == 3 ? 0.3 : 0.08;
                            if (!core) c[0] = c[1] = c[2] = edge;
                            else c[0] = c[1] = c[2] = 1.0;
                        }
                        acc[0] += c[0];
                        acc[1] += c[1];
                        acc[2] += c[2];
                        acc[3] += 1.0;
                    }
                const size_t i = (static_cast<size_t>(y) * w + static_cast<size_t>(type) * kIconSize + x) * 4;
                if (acc[3] > 0.0) {
                    px[i + 0] = static_cast<std::uint8_t>(std::lround(acc[0] / acc[3] * 255.0));
                    px[i + 1] = static_cast<std::uint8_t>(std::lround(acc[1] / acc[3] * 255.0));
                    px[i + 2] = static_cast<std::uint8_t>(std::lround(acc[2] / acc[3] * 255.0));
                }
                px[i + 3] = static_cast<std::uint8_t>(std::lround(acc[3] / 16.0 * 255.0));
            }
    return px;
}

int shieldType(const std::string& network) {
    if (network == "us-interstate") return 0;
    if (network == "us-highway") return 1;
    if (network == "us-state") return 2;
    return 3;
}

}

bool LabelLayer::init() {
    if (ready_) return true;
    if (!atlas_.init(embedded::label_font, 6)) return false;
    loading_ = std::async(std::launch::async, [] { return labels::Dataset::get().available(); });
    sg_backend backend = sg_query_backend();
    lineShader_ = sg_make_shader(bline_shader_desc(backend));
    labelShader_ = sg_make_shader(label_shader_desc(backend));
    iconShader_ = sg_make_shader(icon_shader_desc(backend));

    auto blend = [](sg_color_target_state& c) {
        c.blend.enabled = true;
        c.blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        c.blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        c.blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        c.blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    };

    sg_pipeline_desc lp{};
    lp.shader = lineShader_;
    lp.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    lp.layout.attrs[ATTR_bline_corner].buffer_index = 0;
    lp.layout.attrs[ATTR_bline_corner].format = SG_VERTEXFORMAT_FLOAT2;
    lp.layout.attrs[ATTR_bline_seg].buffer_index = 1;
    lp.layout.attrs[ATTR_bline_seg].format = SG_VERTEXFORMAT_FLOAT4;
    lp.layout.attrs[ATTR_bline_info].buffer_index = 1;
    lp.layout.attrs[ATTR_bline_info].format = SG_VERTEXFORMAT_FLOAT2;
    lp.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    blend(lp.colors[0]);
    lp.depth.pixel_format = SG_PIXELFORMAT_NONE;
    lp.sample_count = 1;
    linePipeline_ = sg_make_pipeline(&lp);

    auto overlay = [&](sg_shader shader) {
        sg_pipeline_desc tp{};
        tp.shader = shader;
        tp.layout.attrs[ATTR_label_position].format = SG_VERTEXFORMAT_FLOAT2;
        tp.layout.attrs[ATTR_label_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        tp.layout.attrs[ATTR_label_color0].format = SG_VERTEXFORMAT_UBYTE4N;
        tp.layout.attrs[ATTR_label_color1].format = SG_VERTEXFORMAT_UBYTE4N;
        tp.layout.attrs[ATTR_label_texcoord1].format = SG_VERTEXFORMAT_FLOAT2;
        blend(tp.colors[0]);
        tp.depth.compare = SG_COMPAREFUNC_ALWAYS;
        tp.depth.write_enabled = false;
        return sg_make_pipeline(&tp);
    };
    labelPipeline_ = overlay(labelShader_);
    iconPipeline_ = overlay(iconShader_);

    const float quad[12] = {0, -1, 1, -1, 1, 1, 0, -1, 1, 1, 0, 1};
    sg_buffer_desc qd{};
    qd.usage.vertex_buffer = true;
    qd.data = SG_RANGE(quad);
    quad_ = sg_make_buffer(&qd);

    for (sg_buffer& b : labelBuffers_) {
        sg_buffer_desc bd{};
        bd.usage.vertex_buffer = true;
        bd.usage.write_transient = true;
        bd.size = kLabelBytes;
        b = sg_make_buffer(&bd);
    }
    for (sg_buffer& b : iconBuffers_) {
        sg_buffer_desc bd{};
        bd.usage.vertex_buffer = true;
        bd.usage.write_transient = true;
        bd.size = kIconBytes;
        b = sg_make_buffer(&bd);
    }

    std::vector<std::uint8_t> icons = makeIcons();
    sg_image_desc id{};
    id.width = kIconSize * kIcons;
    id.height = kIconSize;
    id.pixel_format = SG_PIXELFORMAT_RGBA8;
    id.data.mip_levels[0] = {icons.data(), icons.size()};
    iconImage_ = sg_make_image(&id);
    sg_view_desc iv{};
    iv.texture.image = iconImage_;
    iconView_ = sg_make_view(&iv);

    sg_sampler_desc sm{};
    sm.min_filter = SG_FILTER_LINEAR;
    sm.mag_filter = SG_FILTER_LINEAR;
    sm.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sm.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_ = sg_make_sampler(&sm);
    addDot();
    uploadAtlas();
    ready_ = true;
    return true;
}

void LabelLayer::shutdown() {
    if (!ready_) return;
    if (sg_isvalid()) {
        for (auto& [k, b] : batches_)
            if (b.buffer.id) sg_destroy_buffer(b.buffer);
        for (sg_pipeline p : {linePipeline_, labelPipeline_, iconPipeline_}) sg_destroy_pipeline(p);
        for (sg_shader s : {lineShader_, labelShader_, iconShader_}) sg_destroy_shader(s);
        sg_destroy_buffer(quad_);
        for (sg_buffer b : labelBuffers_) sg_destroy_buffer(b);
        for (sg_buffer b : iconBuffers_) sg_destroy_buffer(b);
        if (atlasView_.id) sg_destroy_view(atlasView_);
        if (atlasImage_.id) sg_destroy_image(atlasImage_);
        sg_destroy_view(iconView_);
        sg_destroy_image(iconImage_);
        sg_destroy_sampler(sampler_);
    }
    batches_.clear();
    if (loading_.valid()) loading_.wait();
    ready_ = false;
}

bool LabelLayer::available() {
    if (!ready_) return false;
    if (!loaded_ && loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        usable_ = loading_.get();
        loaded_ = true;
    }
    return loaded_ && usable_;
}

void LabelLayer::beginFrame() {
    ++frame_;
    decodes_ = 0;
    trim();
}

void LabelLayer::addDot() {
    const int pad = atlas_.pad();
    const int n = 2 * (kDotRadius + pad);
    std::vector<std::uint8_t> sdf(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            double dx = x + 0.5 - n * 0.5, dy = y + 0.5 - n * 0.5;
            double d = kDotRadius - std::sqrt(dx * dx + dy * dy);
            sdf[static_cast<size_t>(y) * n + x] = static_cast<std::uint8_t>(std::clamp(128.0 + d * 128.0 / pad, 0.0, 255.0));
        }
    atlas_.place(kDot, n, n, sdf.data(), -n * 0.5f, -n * 0.5f, 0.0f);
}

void LabelLayer::uploadAtlas() {
    if (!atlas_.dirty() && atlasImage_.id) return;
    if (atlasView_.id) sg_destroy_view(atlasView_);
    if (atlasImage_.id) sg_destroy_image(atlasImage_);
    sg_image_desc d{};
    d.width = GlyphAtlas::kSize;
    d.height = GlyphAtlas::kSize;
    d.pixel_format = SG_PIXELFORMAT_R8;
    d.data.mip_levels[0] = {atlas_.pixels().data(), atlas_.pixels().size()};
    atlasImage_ = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = atlasImage_;
    atlasView_ = sg_make_view(&v);
    atlas_.clean();
}

void LabelLayer::trim() {
    while (batches_.size() > kMaxBatches) {
        auto oldest = batches_.end();
        for (auto it = batches_.begin(); it != batches_.end(); ++it)
            if (oldest == batches_.end() || it->second.used < oldest->second.used) oldest = it;
        if (oldest == batches_.end() || oldest->second.used + 2 >= frame_) break;
        if (oldest->second.buffer.id) sg_destroy_buffer(oldest->second.buffer);
        batches_.erase(oldest);
    }
}

void LabelLayer::keys(const GeoRect& tile, double unitDeg, std::vector<std::pair<int, int>>& out) const {
    out.clear();
    const double meters = unitDeg * 111320.0;
    const int tier = meters >= kOverviewMeters ? Overview : meters >= kCoarseMeters ? Coarse : Fine;
    const double size = tier == Fine ? labels::kFineCell : labels::kCoarseCell;
    const int cols = tier == Fine ? labels::kFineCols : labels::kCoarseCols;
    const int rows = tier == Fine ? labels::kFineRows : labels::kCoarseRows;
    const double margin = unitDeg * 4.0 + (tier == Fine ? 0.3 : 1.1);
    const int c0 = std::clamp(static_cast<int>(std::floor((tile.west - margin + 180.0) / size)), 0, cols - 1);
    const int c1 = std::clamp(static_cast<int>(std::floor((tile.east + margin + 180.0) / size)), 0, cols - 1);
    const int r0 = std::clamp(static_cast<int>(std::floor((tile.south - margin + 90.0) / size)), 0, rows - 1);
    const int r1 = std::clamp(static_cast<int>(std::floor((tile.north + margin + 90.0) / size)), 0, rows - 1);
    const labels::Dataset& d = labels::Dataset::get();
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            const int cell = r * cols + c;
            if (tier == Fine ? d.hasFine(cell) : d.hasCoarse(cell)) out.emplace_back(tier, cell);
        }
}

LabelLayer::Batch* LabelLayer::batch(int tier, int cell, bool load) {
    const int id = tier * 100000 + cell;
    auto it = batches_.find(id);
    if (it != batches_.end()) {
        it->second.used = frame_;
        return &it->second;
    }
    if (!load || decodes_ >= kDecodesPerFrame) return nullptr;
    ++decodes_;
    const double size = tier == Fine ? labels::kFineCell : labels::kCoarseCell;
    const int cols = tier == Fine ? labels::kFineCols : labels::kCoarseCols;
    Batch b;
    b.west = -180.0 + (cell % cols) * size;
    b.south = -90.0 + (cell / cols) * size;
    b.east = b.west + size;
    b.north = b.south + size;
    b.used = frame_;
    labels::Lines lines;
    const labels::Dataset& d = labels::Dataset::get();
    if (tier == Fine ? d.fine(cell, lines) : d.coarse(cell, lines)) {
        std::vector<float> inst;
        inst.reserve(lines.xy.size() * 3);
        const int minClass = tier == Overview ? kOverviewClass : 0;
        for (const labels::Run& r : lines.runs) {
            if (r.count < 2) continue;
            const double coslat = std::max(std::cos(lines.xy[2 * r.first + 1] * labels::kUnit * kDegToRad), 0.05);
            double along = 0.0;
            std::int64_t prev = -1;
            for (std::uint32_t i = 0; i < r.count; ++i) {
                const std::uint32_t v = r.first + i;
                const bool keep = i == 0 || i + 1 == r.count || minClass == 0 || lines.importance.size() <= v || lines.importance[v] >= minClass;
                if (!keep) continue;
                if (prev >= 0) {
                    const double ax = lines.xy[2 * prev] * labels::kUnit, ay = lines.xy[2 * prev + 1] * labels::kUnit;
                    const double bx = lines.xy[2 * v] * labels::kUnit, by = lines.xy[2 * v + 1] * labels::kUnit;
                    inst.insert(inst.end(), {static_cast<float>(ax - b.west), static_cast<float>(ay - b.south), static_cast<float>(bx - b.west),
                                             static_cast<float>(by - b.south), static_cast<float>(along), static_cast<float>(r.level)});
                    along += std::hypot((bx - ax) * coslat, by - ay);
                }
                prev = v;
            }
        }
        if (!inst.empty()) {
            sg_buffer_desc bd{};
            bd.usage.vertex_buffer = true;
            bd.data = {inst.data(), inst.size() * sizeof(float)};
            b.buffer = sg_make_buffer(&bd);
            b.count = static_cast<int>(inst.size() / 6);
        }
    }
    return &(batches_[id] = b);
}

bool LabelLayer::prepare(const GeoRect& tile, double unitDeg) {
    if (!available()) return true;
    keys(tile, unitDeg, scratchKeys_);
    bool ok = true;
    for (const auto& [tier, cell] : scratchKeys_)
        if (!batch(tier, cell, true)) ok = false;
    return ok;
}

void LabelLayer::drawRoads(const std::vector<RoadBatch>& batches, const GeoRect& tile, double unitDeg, double dpi) {
    if (!ready_ || batches.empty()) return;
    const double lat = (tile.south + tile.north) * 0.5;
    const double coslat = std::max(std::cos(lat * kDegToRad), 0.05);
    const double zoom = std::log2(156543.034 * std::max(std::cos(lat * kDegToRad), 1e-3) / std::max(unitDeg * 111320.0 * dpi, 1e-6));
    bline_params_t p{};
    p.frame[0] = static_cast<float>(coslat);
    p.frame[1] = static_cast<float>(unitDeg);
    p.frame[2] = static_cast<float>(dpi);
    std::memcpy(p.shade, kRoadShade, sizeof(p.shade));
    bool any = false;
    for (int i = 0; i < 12; ++i) {
        const RoadStyle& s = kRoads[i];
        const float w = stop(s.zoom, s.width, zoom);
        any = any || w > 0.0f;
        p.styles[4 + i][0] = w;
        p.styles[4 + i][1] = w > 0.0f ? s.casing : 0.0f;
        p.styles[4 + i][2] = s.on;
        p.styles[4 + i][3] = s.off;
        std::memcpy(p.colors[4 + i], s.color, sizeof(p.colors[4 + i]));
    }
    if (!any) return;
    sg_apply_pipeline(linePipeline_);
    for (int pass = 0; pass < 2; ++pass) {
        p.frame[3] = static_cast<float>(pass);
        for (const RoadBatch& b : batches) {
            if (b.count <= 0 || sg_query_buffer_state(b.buffer) != SG_RESOURCESTATE_VALID) continue;
            sg_bindings bind{};
            bind.vertex_buffers[0] = quad_;
            bind.vertex_buffers[1] = b.buffer;
            sg_apply_bindings(&bind);
            std::memcpy(p.xform, b.xform, sizeof(p.xform));
            sg_apply_uniforms(UB_bline_params, SG_RANGE(p));
            sg_draw(0, 6, b.count);
        }
    }
}

void LabelLayer::drawSurface(const GeoRect& tile, double unitDeg, double dpi) {
    if (!available()) return;
    keys(tile, unitDeg, scratchKeys_);
    if (scratchKeys_.empty()) return;
    const double span = tile.north - tile.south;
    if (span <= 0.0) return;
    const double coslat = std::max(std::cos((tile.south + tile.north) * 0.5 * kDegToRad), 0.05);
    const double meters = unitDeg * 111320.0;
    bline_params_t p{};
    p.frame[0] = static_cast<float>(coslat);
    p.frame[1] = static_cast<float>(unitDeg);
    p.frame[2] = static_cast<float>(dpi);
    p.frame[3] = 2.0f;
    std::memcpy(p.shade, kShade, sizeof(p.shade));
    for (int l = 0; l < 4; ++l) {
        const BorderStyle& s = kBorders[l];
        const bool hidden = (l == 3 && meters > kCountyMeters) || (l == 2 && meters > kStateMeters);
        p.styles[l][0] = hidden ? 0.0f : s.width;
        p.styles[l][1] = s.casing;
        p.styles[l][2] = s.on;
        p.styles[l][3] = s.off;
        std::memcpy(p.colors[l], s.color, sizeof(p.colors[l]));
    }
    bool applied = false;
    for (const auto& [tier, cell] : scratchKeys_) {
        Batch* b = batch(tier, cell, false);
        if (!b || b->count <= 0 || sg_query_buffer_state(b->buffer) != SG_RESOURCESTATE_VALID) continue;
        if (!applied) {
            sg_apply_pipeline(linePipeline_);
            applied = true;
        }
        sg_bindings bind{};
        bind.vertex_buffers[0] = quad_;
        bind.vertex_buffers[1] = b->buffer;
        sg_apply_bindings(&bind);
        p.xform[0] = static_cast<float>(2.0 / span);
        p.xform[1] = static_cast<float>(2.0 / span);
        p.xform[2] = static_cast<float>((b->west - tile.west) / span * 2.0 - 1.0);
        p.xform[3] = static_cast<float>((b->south - tile.south) / span * 2.0 - 1.0);
        sg_apply_uniforms(UB_bline_params, SG_RANGE(p));
        sg_draw(0, 6, b->count);
    }
}

void LabelLayer::drawLabels(const Camera& cam, const GroundFn& ground, double dpi, const std::vector<GeoRect>& visible,
                            const std::vector<const VectorTile*>& overlay, int view) {
    if (!ready_ || visible.empty() || cam.width <= 0 || cam.height <= 0) return;
    const bool bundled = available();
    if (!bundled && overlay.empty()) return;
    const labels::Dataset* data = bundled ? &labels::Dataset::get() : nullptr;
    if (atlas_.full()) {
        atlas_.reset();
        addDot();
    }
    const double W = cam.width, H = cam.height;
    const Vec3 eye = cam.eye();
    const GeoPoint eyeGeo = ecefToGeodetic(eye);
    const Mat4& vp = cam.viewProjRel();
    auto project = [&](const Vec3& p, double& sx, double& sy) {
        Vec3 r = p - eye;
        Vec4 c = vp * Vec4{r.x, r.y, r.z, 1.0};
        if (c.w <= 1e-6) return false;
        sx = (c.x / c.w * 0.5 + 0.5) * W;
        sy = (0.5 - c.y / c.w * 0.5) * H;
        return true;
    };
    auto zoomAt = [&](double lat, double dist) {
        const double mpp = std::max(cam.pixelSizeAt(std::max(dist, 1.0)) * dpi, 1e-6);
        return std::log2(156543.034 * std::max(std::cos(lat * kDegToRad), 1e-3) / mpp);
    };

    struct Candidate {
        const labels::Place* place = nullptr;
        const TileMark* mark = nullptr;
        double height = 0.0;
        float x = 0, y = 0, zoom = 0, priority = 0;
    };
    std::vector<Candidate> candidates;
    std::unordered_set<std::string_view> names;

    if (bundled) {
        std::vector<int> cells;
        for (const GeoRect& r : visible) {
            const int c0 = std::clamp(static_cast<int>(std::floor((r.west + 180.0) / labels::kPlaceCell)), 0, labels::kPlaceCols - 1);
            const int c1 = std::clamp(static_cast<int>(std::floor((r.east + 180.0) / labels::kPlaceCell)), 0, labels::kPlaceCols - 1);
            const int r0 = std::clamp(static_cast<int>(std::floor((r.south + 90.0) / labels::kPlaceCell)), 0, labels::kPlaceRows - 1);
            const int r1 = std::clamp(static_cast<int>(std::floor((r.north + 90.0) / labels::kPlaceCell)), 0, labels::kPlaceRows - 1);
            for (int row = r0; row <= r1; ++row)
                for (int col = c0; col <= c1; ++col) cells.push_back(row * labels::kPlaceCols + col);
        }
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        for (int cell : cells) {
            const double south = -90.0 + (cell / labels::kPlaceCols) * labels::kPlaceCell;
            const double west = -180.0 + (cell % labels::kPlaceCols) * labels::kPlaceCell;
            const double plat = std::clamp(eyeGeo.lat, south, south + labels::kPlaceCell);
            const double plon = std::clamp(eyeGeo.lon, west, west + labels::kPlaceCell);
            const double cellZoom = zoomAt(plat, length(geodeticToEcef(plat, plon, 0.0) - eye)) + 0.25;
            const int band = static_cast<int>(std::floor(cellZoom));
            for (std::uint32_t i = data->cellBegin(cell); i < data->cellEnd(cell) && candidates.size() < kMaxCandidates; ++i) {
                const labels::Place& p = data->places()[i];
                if (p.minZoom / 10 > band) break;
                const double minz = p.minZoom * 0.1;
                if (minz > cellZoom) continue;
                Vec3 e = geodeticToEcef(p.lat, p.lon, 0.0);
                if (dot(normalize(e), eye - e) <= 0.0) continue;
                double sx, sy;
                if (!project(e, sx, sy) || sx < -200 || sy < -100 || sx > W + 200 || sy > H + 100) continue;
                const double h = ground(p.lat, p.lon);
                e = geodeticToEcef(p.lat, p.lon, h);
                if (!project(e, sx, sy) || sx < -40 || sy < -20 || sx > W + 40 || sy > H + 20) continue;
                const double z = zoomAt(p.lat, length(e - eye));
                if (z < minz || (p.maxZoom != 255 && z >= p.maxZoom * 0.1)) continue;
                float base = 0.8f;
                switch (p.kind) {
                case labels::Kind::Country: base = 0.0f; break;
                case labels::Kind::State: base = 1.4f; break;
                case labels::Kind::Suburb: base = 3.0f; break;
                case labels::Kind::County: base = 4.0f; break;
                default: break;
                }
                Candidate c;
                c.place = &p;
                c.height = h;
                c.x = static_cast<float>(sx);
                c.y = static_cast<float>(sy);
                c.zoom = static_cast<float>(z);
                c.priority = base + static_cast<float>(minz);
                candidates.push_back(c);
                names.insert(data->name(p));
            }
        }
    }

    for (const VectorTile* tile : overlay) {
        if (candidates.size() >= kMaxCandidates) break;
        for (const TileMark& m : tile->marks) {
            if (m.points.empty() || m.text.empty()) continue;
            if (m.type == MarkType::Place && m.rank == 1 && names.count(m.text)) continue;
            const auto& anchor = m.points[m.points.size() / 2];
            Vec3 e = geodeticToEcef(anchor[1], anchor[0], 0.0);
            if (dot(normalize(e), eye - e) <= 0.0) continue;
            double sx, sy;
            if (!project(e, sx, sy) || sx < -400 || sy < -300 || sx > W + 400 || sy > H + 300) continue;
            const float minz = markZoom(m);
            const double z = zoomAt(anchor[1], length(e - eye));
            if (z < minz) continue;
            const double h = ground(anchor[1], anchor[0]);
            e = geodeticToEcef(anchor[1], anchor[0], h);
            if (!project(e, sx, sy)) continue;
            const bool line = m.type == MarkType::Road || m.type == MarkType::Waterway || m.type == MarkType::Shield;
            if (!line && (sx < -40 || sy < -20 || sx > W + 40 || sy > H + 20)) continue;
            Candidate c;
            c.mark = &m;
            c.height = h;
            c.x = static_cast<float>(sx);
            c.y = static_cast<float>(sy);
            c.zoom = static_cast<float>(z);
            c.priority = markBase(m) + minz;
            candidates.push_back(c);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });

    constexpr int kGrid = 64;
    const int gw = static_cast<int>(W / kGrid) + 1, gh = static_cast<int>(H / kGrid) + 1;
    std::vector<std::vector<std::array<float, 4>>> grid(static_cast<size_t>(gw) * gh);
    auto cellsOf = [&](const std::array<float, 4>& b, auto&& fn) {
        const int x0 = std::clamp(static_cast<int>(b[0] / kGrid), 0, gw - 1), x1 = std::clamp(static_cast<int>(b[2] / kGrid), 0, gw - 1);
        const int y0 = std::clamp(static_cast<int>(b[1] / kGrid), 0, gh - 1), y1 = std::clamp(static_cast<int>(b[3] / kGrid), 0, gh - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (fn(grid[static_cast<size_t>(y) * gw + x])) return true;
        return false;
    };
    auto free = [&](const std::array<float, 4>& b) {
        return !cellsOf(b, [&](const std::vector<std::array<float, 4>>& list) {
            for (const auto& o : list)
                if (b[0] < o[2] && o[0] < b[2] && b[1] < o[3] && o[1] < b[3]) return true;
            return false;
        });
    };
    auto take = [&](const std::array<float, 4>& b) {
        cellsOf(b, [&](std::vector<std::array<float, 4>>& list) {
            list.push_back(b);
            return false;
        });
    };
    std::unordered_map<std::string, std::vector<std::array<float, 2>>> placedText;
    auto repeated = [&](const std::string& key, float x, float y, float spacing) {
        auto it = placedText.find(key);
        if (it == placedText.end()) return false;
        for (const auto& q : it->second)
            if (std::hypot(q[0] - x, q[1] - y) < spacing) return true;
        return false;
    };

    std::vector<LabelVertex> verts, icons;
    verts.reserve(8192);
    auto nx = [&](double x) { return static_cast<float>(x / W * 2.0 - 1.0); };
    auto ny = [&](double y) { return static_cast<float>(1.0 - y / H * 2.0); };
    auto quadTo = [&](std::vector<LabelVertex>& out, const float (&px)[4], const float (&py)[4], float u0, float v0, float u1, float v1, std::uint32_t fill,
                      std::uint32_t halo, float haloPx, float weight) {
        const LabelVertex a{nx(px[0]), ny(py[0]), u0, v0, fill, halo, haloPx, weight};
        const LabelVertex b{nx(px[1]), ny(py[1]), u1, v0, fill, halo, haloPx, weight};
        const LabelVertex c{nx(px[2]), ny(py[2]), u1, v1, fill, halo, haloPx, weight};
        const LabelVertex d{nx(px[3]), ny(py[3]), u0, v1, fill, halo, haloPx, weight};
        out.insert(out.end(), {a, b, c, a, c, d});
    };
    auto glyphQuad = [&](float x0, float y0, float x1, float y1, const GlyphAtlas::Glyph& g, const TextStyle& s) {
        const float px[4] = {x0, x1, x1, x0}, py[4] = {y0, y0, y1, y1};
        quadTo(verts, px, py, g.u0, g.v0, g.u1, g.v1, s.fill, s.halo, s.haloPx, s.weight);
    };
    auto emitLine = [&](std::string_view text, float pen, float baseline, float scale, const TextStyle& s, float cx, float cy, float ca, float sa) {
        std::uint32_t prev = 0;
        for (std::uint32_t cp : GlyphAtlas::decode(std::string(text))) {
            const GlyphAtlas::Glyph& g = atlas_.glyph(cp);
            if (prev) pen += atlas_.kerning(prev, cp) * scale;
            prev = cp;
            if (g.u1 > g.u0) {
                const float lx[4] = {pen + g.x0 * scale, pen + g.x1 * scale, pen + g.x1 * scale, pen + g.x0 * scale};
                const float ly[4] = {baseline + g.y0 * scale, baseline + g.y0 * scale, baseline + g.y1 * scale, baseline + g.y1 * scale};
                float px[4], py[4];
                for (int k = 0; k < 4; ++k) {
                    px[k] = cx + lx[k] * ca - ly[k] * sa;
                    py[k] = cy + lx[k] * sa + ly[k] * ca;
                }
                quadTo(verts, px, py, g.u0, g.v0, g.u1, g.v1, s.fill, s.halo, s.haloPx, s.weight);
            }
            pen += g.advance * scale + s.tracking;
        }
    };

    int placed = 0;
    std::vector<std::string_view> lines;
    std::vector<float> widths;
    std::vector<std::array<double, 3>> screen;
    for (const Candidate& c : candidates) {
        if (placed >= kMaxLabels || (verts.size() + 600) * sizeof(LabelVertex) > kLabelBytes) break;
        const TileMark* m = c.mark;
        const TextStyle s = m ? markStyle(*m, dpi) : styleFor(*c.place, c.zoom, dpi);
        const float scale = s.size / GlyphAtlas::kBase;
        const float em = s.size / kEmToLine;

        if (m && (m->type == MarkType::Road || m->type == MarkType::Waterway)) {
            if (repeated(m->text, c.x, c.y, 260.0f * static_cast<float>(dpi))) continue;
            screen.clear();
            bool ok = true;
            for (const auto& p : m->points) {
                double sx, sy;
                if (!project(geodeticToEcef(p[1], p[0], c.height), sx, sy)) {
                    ok = false;
                    break;
                }
                screen.push_back({sx, sy, 0.0});
            }
            if (!ok || screen.size() < 2) continue;
            for (size_t i = 1; i < screen.size(); ++i)
                screen[i][2] = screen[i - 1][2] + std::hypot(screen[i][0] - screen[i - 1][0], screen[i][1] - screen[i - 1][1]);
            const float w = lineWidth(atlas_, m->text, scale, s.tracking);
            const double total = screen.back()[2];
            const double need = w + 8.0 * dpi;
            if (total < need) continue;
            auto at = [&](double d, double& x, double& y) {
                size_t i = 1;
                while (i + 1 < screen.size() && screen[i][2] < d) ++i;
                const double seg = std::max(screen[i][2] - screen[i - 1][2], 1e-9);
                const double t = std::clamp((d - screen[i - 1][2]) / seg, 0.0, 1.0);
                x = screen[i - 1][0] + (screen[i][0] - screen[i - 1][0]) * t;
                y = screen[i - 1][1] + (screen[i][1] - screen[i - 1][1]) * t;
            };
            bool found = false;
            double cx = 0, cy = 0, angle = 0;
            for (int k = 0; k < 7 && !found; ++k) {
                const double offset = (k == 0 ? 0.0 : ((k + 1) / 2) * need * 0.6 * (k % 2 ? 1.0 : -1.0));
                const double mid = total * 0.5 + offset;
                const double d0 = mid - need * 0.5, d1 = mid + need * 0.5;
                if (d0 < 0.0 || d1 > total) continue;
                double x0, y0, x1, y1;
                at(d0, x0, y0);
                at(d1, x1, y1);
                const double cl = std::hypot(x1 - x0, y1 - y0);
                if (cl < need * 0.9) continue;
                double worst = 0.0;
                for (const auto& q : screen) {
                    if (q[2] <= d0 || q[2] >= d1) continue;
                    const double dd = std::abs((x1 - x0) * (y0 - q[1]) - (x0 - q[0]) * (y1 - y0)) / cl;
                    worst = std::max(worst, dd);
                }
                if (worst > 3.0 * dpi) continue;
                cx = (x0 + x1) * 0.5;
                cy = (y0 + y1) * 0.5;
                angle = std::atan2(y1 - y0, x1 - x0);
                found = true;
            }
            if (!found || cx < 0 || cy < 0 || cx > W || cy > H) continue;
            if (angle > kPi * 0.5) angle -= kPi;
            if (angle < -kPi * 0.5) angle += kPi;
            const float ca = static_cast<float>(std::cos(angle)), sa = static_cast<float>(std::sin(angle));
            const float hh = em * 1.2f;
            const int pieces = std::max(1, static_cast<int>(std::ceil(w / hh)));
            std::vector<std::array<float, 4>> boxes;
            for (int i = 0; i < pieces; ++i) {
                const float along = -w * 0.5f + (i + 0.5f) * w / pieces;
                const float px = static_cast<float>(cx) + along * ca, py = static_cast<float>(cy) + along * sa;
                const float r = std::max(w / pieces, hh) * 0.5f + s.padding;
                boxes.push_back({px - r, py - r, px + r, py + r});
            }
            bool clear = true;
            for (const auto& b : boxes) clear = clear && free(b);
            if (!clear) continue;
            for (const auto& b : boxes) take(b);
            placedText[m->text].push_back({static_cast<float>(cx), static_cast<float>(cy)});
            ++placed;
            emitLine(m->text, -w * 0.5f, em * 0.36f, scale, s, static_cast<float>(cx), static_cast<float>(cy), ca, sa);
            continue;
        }

        if (m && m->type == MarkType::Shield) {
            const std::string key = "#" + m->network + m->text;
            if (repeated(key, c.x, c.y, 220.0f * static_cast<float>(dpi))) continue;
            if (c.x < 0 || c.y < 0 || c.x > W || c.y > H) continue;
            const int type = shieldType(m->network);
            const float h = 20.0f * static_cast<float>(dpi);
            const float tw = lineWidth(atlas_, m->text, scale, s.tracking);
            const float w = std::max(h * (type <= 1 ? 1.0f : 1.15f), tw + 7.0f * static_cast<float>(dpi));
            const std::array<float, 4> box{c.x - w * 0.5f - s.padding, c.y - h * 0.5f - s.padding, c.x + w * 0.5f + s.padding, c.y + h * 0.5f + s.padding};
            if (!free(box)) continue;
            take(box);
            placedText[key].push_back({c.x, c.y});
            ++placed;
            const float u0 = (type * kIconSize + 0.5f) / (kIconSize * kIcons), u1 = ((type + 1) * kIconSize - 0.5f) / (kIconSize * kIcons);
            const float px[4] = {c.x - w * 0.5f, c.x + w * 0.5f, c.x + w * 0.5f, c.x - w * 0.5f};
            const float py[4] = {c.y - h * 0.5f, c.y - h * 0.5f, c.y + h * 0.5f, c.y + h * 0.5f};
            quadTo(icons, px, py, u0, 0.5f / kIconSize, u1, 1.0f - 0.5f / kIconSize, rgba(255, 255, 255, 1.0f), 0, 0.0f, 0.0f);
            const float shift = type <= 1 ? h * 0.1f : 0.0f;
            emitLine(m->text, -tw * 0.5f, em * 0.36f + shift, scale, s, c.x, c.y, 1.0f, 0.0f);
            continue;
        }

        std::string text;
        if (m) {
            text = m->type == MarkType::Park ? wrapName(m->text, 14) : m->text;
            if (repeated(text, c.x, c.y, 500.0f * static_cast<float>(dpi))) continue;
        } else {
            text = std::string(data->name(*c.place));
        }
        lines.clear();
        widths.clear();
        const std::string_view name = text;
        for (size_t start = 0;;) {
            size_t end = name.find('\n', start);
            lines.push_back(name.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        float w = 0.0f;
        for (std::string_view l : lines) {
            widths.push_back(lineWidth(atlas_, l, scale, s.tracking));
            w = std::max(w, widths.back());
        }
        const float pitch = em * 1.2f;
        const float n = static_cast<float>(lines.size());
        const float top = -(n - 1.0f) * pitch * 0.5f - em * 0.72f;
        const float bottom = (n - 1.0f) * pitch * 0.5f + em * 0.24f;
        std::array<float, 4> box{};
        float textX = 0.0f;
        float cx = c.x, cy = c.y;
        bool ok = false;
        if (s.dot > 0.0f) {
            const float gap = s.dot + 3.5f * static_cast<float>(dpi);
            const std::array<float, 4> dotBox{c.x - s.dot - 1, c.y - s.dot - 1, c.x + s.dot + 1, c.y + s.dot + 1};
            for (int side = 0; side < 2 && !ok; ++side) {
                textX = side == 0 ? c.x + gap : c.x - gap - w;
                box = {std::min(textX, dotBox[0]) - s.padding, c.y + top - s.padding, std::max(textX + w, dotBox[2]) + s.padding,
                       c.y + bottom + s.padding};
                ok = box[0] >= 0 && box[2] <= W && box[1] >= 0 && box[3] <= H && free(box);
            }
        } else {
            const bool area = c.place && (c.place->kind == labels::Kind::Country || c.place->kind == labels::Kind::State ||
                                          c.place->kind == labels::Kind::County);
            const float h = bottom - top;
            const float shifts[5][2] = {{0.0f, 0.0f}, {0.0f, -0.9f * h}, {0.0f, 0.9f * h}, {-0.3f * w, 0.0f}, {0.3f * w, 0.0f}};
            for (int k = 0; k < (area ? 5 : 1) && !ok; ++k) {
                cx = c.x + shifts[k][0];
                cy = c.y + shifts[k][1];
                textX = cx - w * 0.5f;
                box = {textX - s.padding, cy + top - s.padding, textX + w + s.padding, cy + bottom + s.padding};
                ok = box[0] >= -w * 0.25f && box[2] <= W + w * 0.25f && box[1] >= 0 && box[3] <= H && free(box);
            }
        }
        if (!ok) continue;
        take(box);
        if (m) placedText[text].push_back({c.x, c.y});
        ++placed;
        if (s.dot > 0.0f) {
            const GlyphAtlas::Glyph& g = atlas_.glyph(kDot);
            if (g.u1 > g.u0) {
                const float k = s.dot / kDotRadius;
                TextStyle ds = s;
                ds.fill = rgba(242, 242, 242, 1.0f);
                ds.halo = rgba(24, 24, 24, 0.75f);
                ds.haloPx = 1.0f * static_cast<float>(dpi);
                ds.weight = 0.0f;
                glyphQuad(c.x + g.x0 * k, c.y + g.y0 * k, c.x + g.x1 * k, c.y + g.y1 * k, g, ds);
            }
        }
        for (size_t li = 0; li < lines.size(); ++li) {
            const float pen = s.dot > 0.0f ? textX : cx - widths[li] * 0.5f;
            const float baseline = cy - (n - 1.0f) * pitch * 0.5f + static_cast<float>(li) * pitch + em * 0.36f;
            emitLine(lines[li], pen, baseline, scale, s, 0.0f, 0.0f, 1.0f, 0.0f);
        }
    }
    uploadAtlas();
    const size_t slot = static_cast<size_t>(std::clamp(view, 0, 3));
    if (!icons.empty()) {
        if (icons.size() * sizeof(LabelVertex) > kIconBytes) icons.resize(kIconBytes / sizeof(LabelVertex) / 6 * 6);
        sg_write_buffer_desc wd{};
        wd.src.data = {icons.data(), icons.size() * sizeof(LabelVertex)};
        wd.dst.buffer = iconBuffers_[slot];
        wd.size = icons.size() * sizeof(LabelVertex);
        sg_write_buffer_transient(&wd);
        sg_apply_pipeline(iconPipeline_);
        sg_bindings b{};
        b.vertex_buffers[0] = iconBuffers_[slot];
        b.views[VIEW_tex] = iconView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_draw(0, static_cast<int>(icons.size()), 1);
    }
    if (verts.empty()) return;
    sg_write_buffer_desc wd{};
    wd.src.data = {verts.data(), verts.size() * sizeof(LabelVertex)};
    wd.dst.buffer = labelBuffers_[slot];
    wd.size = verts.size() * sizeof(LabelVertex);
    sg_write_buffer_transient(&wd);
    sg_apply_pipeline(labelPipeline_);
    sg_bindings b{};
    b.vertex_buffers[0] = labelBuffers_[slot];
    b.views[VIEW_tex] = atlasView_;
    b.samplers[SMP_smp] = sampler_;
    sg_apply_bindings(&b);
    sg_draw(0, static_cast<int>(verts.size()), 1);
}

}
