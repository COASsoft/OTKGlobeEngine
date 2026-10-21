#include "render/label_layer.hpp"

#include "core/geo.hpp"
#include "labels/label_data.hpp"

#include "labels.glsl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string_view>

namespace otk {

namespace embedded {
extern const unsigned char label_font[];
extern const std::size_t label_font_size;
}

namespace {

constexpr std::size_t kLabelBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxBatches = 72;
constexpr int kDecodesPerFrame = 4;
constexpr std::uint32_t kDot = 0xE000;
constexpr int kDotRadius = 10;
constexpr double kOverviewMeters = 15000.0;
constexpr double kCoarseMeters = 2000.0;
constexpr double kCountyMeters = 500.0;
constexpr int kOverviewClass = 18;
constexpr float kEmToLine = 1.1719f;
constexpr int kMaxLabels = 600;
constexpr std::size_t kMaxCandidates = 8000;

enum Tier { Fine = 0, Coarse = 1, Overview = 2 };

struct LineStyle {
    float width, on, off;
    float color[4];
};

constexpr LineStyle kLines[4] = {
    {1.3f, 0.0f, 0.0f, {1.0f, 1.0f, 1.0f, 0.85f}},
    {1.2f, 4.0f, 3.0f, {1.0f, 1.0f, 1.0f, 0.7f}},
    {1.1f, 2.5f, 2.5f, {1.0f, 1.0f, 1.0f, 0.75f}},
    {1.0f, 2.0f, 3.0f, {1.0f, 1.0f, 1.0f, 0.45f}},
};

struct Vertex {
    float x, y, u, v;
    std::uint32_t fill, halo;
    float haloPx, weight;
};

struct Style {
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

Style styleFor(const labels::Place& p, double zoom, double dpi) {
    Style s;
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
    const float k = static_cast<float>(dpi);
    s.size *= kEmToLine * k;
    s.tracking *= k;
    s.haloPx *= k;
    s.dot *= k;
    s.padding *= k;
    return s;
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

}

bool LabelLayer::init() {
    if (ready_) return true;
    if (!atlas_.init(embedded::label_font, 6)) return false;
    loading_ = std::async(std::launch::async, [] { return labels::Dataset::get().available(); });
    sg_backend backend = sg_query_backend();
    lineShader_ = sg_make_shader(bline_shader_desc(backend));
    labelShader_ = sg_make_shader(label_shader_desc(backend));

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

    sg_pipeline_desc tp{};
    tp.shader = labelShader_;
    tp.layout.attrs[ATTR_label_position].format = SG_VERTEXFORMAT_FLOAT2;
    tp.layout.attrs[ATTR_label_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    tp.layout.attrs[ATTR_label_color0].format = SG_VERTEXFORMAT_UBYTE4N;
    tp.layout.attrs[ATTR_label_color1].format = SG_VERTEXFORMAT_UBYTE4N;
    tp.layout.attrs[ATTR_label_texcoord1].format = SG_VERTEXFORMAT_FLOAT2;
    blend(tp.colors[0]);
    tp.depth.compare = SG_COMPAREFUNC_ALWAYS;
    tp.depth.write_enabled = false;
    labelPipeline_ = sg_make_pipeline(&tp);

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
        sg_destroy_pipeline(linePipeline_);
        sg_destroy_pipeline(labelPipeline_);
        sg_destroy_shader(lineShader_);
        sg_destroy_shader(labelShader_);
        sg_destroy_buffer(quad_);
        for (sg_buffer b : labelBuffers_) sg_destroy_buffer(b);
        if (atlasView_.id) sg_destroy_view(atlasView_);
        if (atlasImage_.id) sg_destroy_image(atlasImage_);
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

void LabelLayer::keys(const GeoRect& tile, double texelDeg, std::vector<std::pair<int, int>>& out) const {
    out.clear();
    const double meters = texelDeg * 111320.0;
    const int tier = meters >= kOverviewMeters ? Overview : meters >= kCoarseMeters ? Coarse : Fine;
    const double size = tier == Fine ? labels::kFineCell : labels::kCoarseCell;
    const int cols = tier == Fine ? labels::kFineCols : labels::kCoarseCols;
    const int rows = tier == Fine ? labels::kFineRows : labels::kCoarseRows;
    const double margin = texelDeg * 4.0;
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

bool LabelLayer::prepare(const GeoRect& tile, double texelDeg) {
    if (!available()) return true;
    keys(tile, texelDeg, scratchKeys_);
    bool ok = true;
    for (const auto& [tier, cell] : scratchKeys_)
        if (!batch(tier, cell, true)) ok = false;
    return ok;
}

void LabelLayer::drawSurface(const GeoRect& tile, double texelDeg, double dpi, int size) {
    if (!available()) return;
    keys(tile, texelDeg, scratchKeys_);
    if (scratchKeys_.empty()) return;
    const double span = tile.north - tile.south;
    if (span <= 0.0) return;
    const double coslat = std::max(std::cos((tile.south + tile.north) * 0.5 * kDegToRad), 0.05);
    const double meters = texelDeg * 111320.0;
    bool applied = false;
    for (const auto& [tier, cell] : scratchKeys_) {
        Batch* b = batch(tier, cell, false);
        if (!b || b->count <= 0 || sg_query_buffer_state(b->buffer) != SG_RESOURCESTATE_VALID) continue;
        const double x0 = (std::max(b->west, tile.west) - tile.west) / span * size;
        const double x1 = (std::min(b->east, tile.east) - tile.west) / span * size;
        const double y0 = (tile.north - std::min(b->north, tile.north)) / span * size;
        const double y1 = (tile.north - std::max(b->south, tile.south)) / span * size;
        const int sx = static_cast<int>(std::lround(x0)), sy = static_cast<int>(std::lround(y0));
        const int sw = static_cast<int>(std::lround(x1)) - sx, sh = static_cast<int>(std::lround(y1)) - sy;
        if (sw <= 0 || sh <= 0) continue;
        if (!applied) {
            sg_apply_pipeline(linePipeline_);
            applied = true;
        }
        sg_apply_scissor_rect(sx, sy, sw, sh, true);
        sg_bindings bind{};
        bind.vertex_buffers[0] = quad_;
        bind.vertex_buffers[1] = b->buffer;
        sg_apply_bindings(&bind);
        bline_params_t p{};
        p.xform[0] = static_cast<float>(2.0 / span);
        p.xform[1] = static_cast<float>(2.0 / span);
        p.xform[2] = static_cast<float>((b->west - tile.west) / span * 2.0 - 1.0);
        p.xform[3] = static_cast<float>((b->south - tile.south) / span * 2.0 - 1.0);
        p.frame[0] = static_cast<float>(coslat);
        p.frame[1] = static_cast<float>(texelDeg);
        p.frame[2] = static_cast<float>(dpi);
        for (int l = 0; l < 4; ++l) {
            const LineStyle& s = kLines[l];
            p.width[l] = l == 3 && meters > kCountyMeters ? 0.0f : s.width;
            p.dash_on[l] = s.on;
            p.dash_off[l] = s.off;
        }
        std::memcpy(p.color0, kLines[0].color, sizeof(p.color0));
        std::memcpy(p.color1, kLines[1].color, sizeof(p.color1));
        std::memcpy(p.color2, kLines[2].color, sizeof(p.color2));
        std::memcpy(p.color3, kLines[3].color, sizeof(p.color3));
        sg_apply_uniforms(UB_bline_params, SG_RANGE(p));
        sg_draw(0, 6, b->count);
    }
    if (applied) sg_apply_scissor_rect(0, 0, size, size, true);
}

void LabelLayer::drawLabels(const Camera& cam, const GroundFn& ground, double dpi, const std::vector<GeoRect>& visible, int view) {
    if (!available() || visible.empty() || cam.width <= 0 || cam.height <= 0) return;
    const labels::Dataset& d = labels::Dataset::get();
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

    struct Candidate {
        std::uint32_t index;
        float x, y, zoom, priority;
    };
    std::vector<Candidate> candidates;
    for (int cell : cells) {
        const double south = -90.0 + (cell / labels::kPlaceCols) * labels::kPlaceCell;
        const double west = -180.0 + (cell % labels::kPlaceCols) * labels::kPlaceCell;
        const double plat = std::clamp(eyeGeo.lat, south, south + labels::kPlaceCell);
        const double plon = std::clamp(eyeGeo.lon, west, west + labels::kPlaceCell);
        const double cellZoom = zoomAt(plat, length(geodeticToEcef(plat, plon, 0.0) - eye)) + 0.25;
        const int band = static_cast<int>(std::floor(cellZoom));
        for (std::uint32_t i = d.cellBegin(cell); i < d.cellEnd(cell) && candidates.size() < kMaxCandidates; ++i) {
            const labels::Place& p = d.places()[i];
            if (p.minZoom / 10 > band) break;
            const double minz = p.minZoom * 0.1;
            if (minz > cellZoom) continue;
            Vec3 e = geodeticToEcef(p.lat, p.lon, 0.0);
            if (dot(normalize(e), eye - e) <= 0.0) continue;
            double sx, sy;
            if (!project(e, sx, sy) || sx < -200 || sy < -100 || sx > W + 200 || sy > H + 100) continue;
            e = geodeticToEcef(p.lat, p.lon, ground(p.lat, p.lon));
            if (!project(e, sx, sy) || sx < -40 || sy < -20 || sx > W + 40 || sy > H + 20) continue;
            const double z = zoomAt(p.lat, length(e - eye));
            if (z < minz || (p.maxZoom != 255 && z >= p.maxZoom * 0.1)) continue;
            float base = 1.5f;
            switch (p.kind) {
            case labels::Kind::Country: base = 0.0f; break;
            case labels::Kind::State: base = 1.0f; break;
            case labels::Kind::Suburb: base = 3.0f; break;
            case labels::Kind::County: base = 4.0f; break;
            default: break;
            }
            candidates.push_back({i, static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(z), base + static_cast<float>(minz)});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.index < b.index;
    });

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

    std::vector<Vertex> verts;
    verts.reserve(8192);
    auto nx = [&](double x) { return static_cast<float>(x / W * 2.0 - 1.0); };
    auto ny = [&](double y) { return static_cast<float>(1.0 - y / H * 2.0); };
    auto quad = [&](float x0, float y0, float x1, float y1, const GlyphAtlas::Glyph& g, const Style& s) {
        const Vertex a{nx(x0), ny(y0), g.u0, g.v0, s.fill, s.halo, s.haloPx, s.weight};
        const Vertex b{nx(x1), ny(y0), g.u1, g.v0, s.fill, s.halo, s.haloPx, s.weight};
        const Vertex c{nx(x1), ny(y1), g.u1, g.v1, s.fill, s.halo, s.haloPx, s.weight};
        const Vertex e{nx(x0), ny(y1), g.u0, g.v1, s.fill, s.halo, s.haloPx, s.weight};
        verts.insert(verts.end(), {a, b, c, a, c, e});
    };

    int placed = 0;
    std::vector<std::string_view> lines;
    std::vector<float> widths;
    for (const Candidate& c : candidates) {
        if (placed >= kMaxLabels || (verts.size() + 600) * sizeof(Vertex) > kLabelBytes) break;
        const labels::Place& p = d.places()[c.index];
        const Style s = styleFor(p, c.zoom, dpi);
        const float scale = s.size / GlyphAtlas::kBase;
        const std::string_view name = d.name(p);
        lines.clear();
        widths.clear();
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
        const float em = s.size / kEmToLine;
        const float pitch = em * 1.2f;
        const float n = static_cast<float>(lines.size());
        const float top = -(n - 1.0f) * pitch * 0.5f - em * 0.72f;
        const float bottom = (n - 1.0f) * pitch * 0.5f + em * 0.24f;
        std::array<float, 4> box{};
        float textX = 0.0f;
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
            textX = c.x - w * 0.5f;
            box = {textX - s.padding, c.y + top - s.padding, textX + w + s.padding, c.y + bottom + s.padding};
            ok = box[0] >= -w * 0.25f && box[2] <= W + w * 0.25f && box[1] >= 0 && box[3] <= H && free(box);
        }
        if (!ok) continue;
        take(box);
        ++placed;
        if (s.dot > 0.0f) {
            const GlyphAtlas::Glyph& g = atlas_.glyph(kDot);
            if (g.u1 > g.u0) {
                const float k = s.dot / kDotRadius;
                Style ds = s;
                ds.fill = rgba(242, 242, 242, 1.0f);
                ds.halo = rgba(24, 24, 24, 0.75f);
                ds.haloPx = 1.0f * static_cast<float>(dpi);
                ds.weight = 0.0f;
                quad(c.x + g.x0 * k, c.y + g.y0 * k, c.x + g.x1 * k, c.y + g.y1 * k, g, ds);
            }
        }
        for (size_t li = 0; li < lines.size(); ++li) {
            float pen = s.dot > 0.0f ? textX : c.x - widths[li] * 0.5f;
            const float baseline = c.y - (n - 1.0f) * pitch * 0.5f + static_cast<float>(li) * pitch + em * 0.36f;
            std::uint32_t prev = 0;
            for (std::uint32_t cp : GlyphAtlas::decode(std::string(lines[li]))) {
                const GlyphAtlas::Glyph& g = atlas_.glyph(cp);
                if (prev) pen += atlas_.kerning(prev, cp) * scale;
                prev = cp;
                if (g.u1 > g.u0)
                    quad(pen + g.x0 * scale, baseline + g.y0 * scale, pen + g.x1 * scale, baseline + g.y1 * scale, g, s);
                pen += g.advance * scale + s.tracking;
            }
        }
    }
    uploadAtlas();
    if (verts.empty()) return;
    sg_buffer buffer = labelBuffers_[static_cast<size_t>(std::clamp(view, 0, 3))];
    sg_write_buffer_desc wd{};
    wd.src.data = {verts.data(), verts.size() * sizeof(Vertex)};
    wd.dst.buffer = buffer;
    wd.size = verts.size() * sizeof(Vertex);
    sg_write_buffer_transient(&wd);
    sg_apply_pipeline(labelPipeline_);
    sg_bindings b{};
    b.vertex_buffers[0] = buffer;
    b.views[VIEW_tex] = atlasView_;
    b.samplers[SMP_smp] = sampler_;
    sg_apply_bindings(&b);
    sg_draw(0, static_cast<int>(verts.size()), 1);
}

}
