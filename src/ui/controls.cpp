#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "ui/controls.hpp"

#include "otk/engine.hpp"

#include "controls.glsl.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace otk {

namespace embedded {
extern const unsigned char label_font[];
extern const std::size_t label_font_size;
}

namespace {

constexpr int kAtlas = 1024;
constexpr std::size_t kMaxVertices = 24000;
constexpr int kSmall = 0;
constexpr int kNormal = 1;
constexpr float kSizes[2] = {11.0f, 13.0f};
constexpr int kAscii = 95;
constexpr int kLatin = 0x180 - 0xA0;
constexpr int kExtra[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026};
constexpr int kGlyphs = kAscii + kLatin + static_cast<int>(sizeof(kExtra) / sizeof(kExtra[0]));
constexpr double kPi = 3.14159265358979323846;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr float kButton = 40.0f;
#else
constexpr float kButton = 34.0f;
#endif

constexpr std::uint32_t rgba(int r, int g, int b, float a) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 |
           static_cast<std::uint32_t>(a * 255.0f + 0.5f) << 24;
}

constexpr std::uint32_t kFace = rgba(255, 255, 255, 0.95f);
constexpr std::uint32_t kFaceHover = rgba(240, 240, 240, 0.97f);
constexpr std::uint32_t kFacePress = rgba(221, 221, 221, 0.98f);
constexpr std::uint32_t kRim = rgba(0, 0, 0, 0.16f);
constexpr std::uint32_t kShade = rgba(0, 0, 0, 0.22f);
constexpr std::uint32_t kInk = rgba(66, 66, 66, 1.0f);
constexpr std::uint32_t kText = rgba(38, 38, 38, 1.0f);
constexpr std::uint32_t kMuted = rgba(118, 118, 118, 1.0f);
constexpr std::uint32_t kAccent = rgba(52, 94, 158, 1.0f);
constexpr std::uint32_t kRowHover = rgba(0, 0, 0, 0.05f);
constexpr std::uint32_t kNorth = rgba(204, 62, 50, 1.0f);
constexpr std::uint32_t kSouth = rgba(150, 150, 150, 1.0f);
constexpr std::uint32_t kStrip = rgba(255, 255, 255, 0.72f);
constexpr std::uint32_t kWhite = rgba(255, 255, 255, 1.0f);

std::uint32_t decode(std::string_view s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra == 0) return 0xFFFD;
    std::uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    return cp;
}

int glyphIndex(std::uint32_t cp) {
    if (cp >= 0x20 && cp < 0x7F) return static_cast<int>(cp - 0x20);
    if (cp >= 0xA0 && cp < 0x180) return kAscii + static_cast<int>(cp - 0xA0);
    for (int k = 0; k < static_cast<int>(sizeof(kExtra) / sizeof(kExtra[0])); ++k)
        if (static_cast<std::uint32_t>(kExtra[k]) == cp) return kAscii + kLatin + k;
    return '?' - 0x20;
}

std::string distanceText(double meters, bool imperial, double& shown) {
    char buf[32];
    auto nice = [](double limit) {
        const double p = std::pow(10.0, std::floor(std::log10(limit)));
        for (double m : {5.0, 2.0, 1.0})
            if (m * p <= limit) return m * p;
        return p;
    };
    if (imperial) {
        const double feet = meters / 0.3048;
        if (feet < 5280.0) {
            const double f = nice(feet);
            shown = f * 0.3048;
            std::snprintf(buf, sizeof(buf), "%.0f ft", f);
        } else {
            const double mi = nice(feet / 5280.0);
            shown = mi * 1609.344;
            std::snprintf(buf, sizeof(buf), mi < 1.0 ? "%.1f mi" : "%.0f mi", mi);
        }
    } else if (meters < 1000.0) {
        const double m = nice(meters);
        shown = m;
        std::snprintf(buf, sizeof(buf), m < 1.0 ? "%.1f m" : "%.0f m", m);
    } else {
        const double km = nice(meters / 1000.0);
        shown = km * 1000.0;
        std::snprintf(buf, sizeof(buf), "%.0f km", km);
    }
    return buf;
}

}

struct Controls::Font {
    stbtt_fontinfo info{};
    std::vector<unsigned char> pixels;
    std::array<std::vector<stbtt_packedchar>, 2> chars;
    std::array<float, 2> scale{};
    sg_image image{};
    sg_view view{};
    float dpi = 0.0f;

    bool build(float d) {
        if (!stbtt_InitFont(&info, embedded::label_font, stbtt_GetFontOffsetForIndex(embedded::label_font, 0))) return false;
        pixels.assign(static_cast<size_t>(kAtlas) * kAtlas, 0);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, pixels.data(), kAtlas, kAtlas, 0, 1, nullptr)) return false;
        stbtt_PackSetOversampling(&pc, 1, 1);
        int extra[sizeof(kExtra) / sizeof(kExtra[0])];
        std::copy(std::begin(kExtra), std::end(kExtra), extra);
        bool ok = true;
        for (int s = 0; s < 2; ++s) {
            const float px = std::round(kSizes[s] * d);
            chars[s].assign(kGlyphs, stbtt_packedchar{});
            scale[s] = stbtt_ScaleForMappingEmToPixels(&info, px);
            stbtt_pack_range ranges[3]{};
            ranges[0].font_size = STBTT_POINT_SIZE(px);
            ranges[0].first_unicode_codepoint_in_range = 0x20;
            ranges[0].num_chars = kAscii;
            ranges[0].chardata_for_range = chars[s].data();
            ranges[1].font_size = STBTT_POINT_SIZE(px);
            ranges[1].first_unicode_codepoint_in_range = 0xA0;
            ranges[1].num_chars = kLatin;
            ranges[1].chardata_for_range = chars[s].data() + kAscii;
            ranges[2].font_size = STBTT_POINT_SIZE(px);
            ranges[2].array_of_unicode_codepoints = extra;
            ranges[2].num_chars = static_cast<int>(sizeof(kExtra) / sizeof(kExtra[0]));
            ranges[2].chardata_for_range = chars[s].data() + kAscii + kLatin;
            ok = stbtt_PackFontRanges(&pc, embedded::label_font, 0, ranges, 3) != 0 && ok;
        }
        stbtt_PackEnd(&pc);
        release();
        sg_image_desc id{};
        id.width = kAtlas;
        id.height = kAtlas;
        id.pixel_format = SG_PIXELFORMAT_R8;
        id.data.mip_levels[0] = {pixels.data(), pixels.size()};
        image = sg_make_image(&id);
        sg_view_desc vd{};
        vd.texture.image = image;
        view = sg_make_view(&vd);
        dpi = d;
        return ok;
    }

    void release() {
        if (view.id) sg_destroy_view(view);
        if (image.id) sg_destroy_image(image);
        view = {};
        image = {};
    }

    float kern(std::uint32_t a, std::uint32_t b, int size) const {
        return static_cast<float>(stbtt_GetCodepointKernAdvance(&info, static_cast<int>(a), static_cast<int>(b))) * scale[size];
    }
};

Controls::Controls(Engine& engine) : engine_(engine) {}

Controls::~Controls() = default;

void Controls::setFlags(std::uint32_t flags) {
    flags &= 0x3Fu;
    if (flags == flags_) return;
    flags_ = flags;
    if (!(flags_ & ControlLayers)) panel_ = false;
    if (!(flags_ & ControlAttribution)) creditsOpen_ = false;
    pressId_ = -1;
    pressed_ = {};
    hover_ = {};
    dirty_ = true;
}

void Controls::setPadding(float left, float top, float right, float bottom) {
    const std::array<float, 4> next{std::max(left, 0.0f), std::max(top, 0.0f), std::max(right, 0.0f), std::max(bottom, 0.0f)};
    if (next == padding_) return;
    padding_ = next;
    dirty_ = true;
}

bool Controls::ready() {
    if (failed_) return false;
    if (pipeline_.id) return true;
    shader_ = sg_make_shader(controls_shader_desc(sg_query_backend()));
    sg_pipeline_desc pd{};
    pd.shader = shader_;
    pd.layout.attrs[ATTR_controls_position].format = SG_VERTEXFORMAT_FLOAT2;
    pd.layout.attrs[ATTR_controls_local].format = SG_VERTEXFORMAT_FLOAT2;
    pd.layout.attrs[ATTR_controls_shape_a].format = SG_VERTEXFORMAT_FLOAT4;
    pd.layout.attrs[ATTR_controls_shape_b].format = SG_VERTEXFORMAT_FLOAT4;
    pd.layout.attrs[ATTR_controls_color0].format = SG_VERTEXFORMAT_UBYTE4N;
    pd.layout.attrs[ATTR_controls_color1].format = SG_VERTEXFORMAT_UBYTE4N;
    pd.layout.attrs[ATTR_controls_misc].format = SG_VERTEXFORMAT_FLOAT2;
    pd.colors[0].blend.enabled = true;
    pd.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pd.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pd.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pd.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pd.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pd.depth.write_enabled = false;
    pipeline_ = sg_make_pipeline(&pd);
    sg_buffer_desc bd{};
    bd.usage.vertex_buffer = true;
    bd.usage.write_transient = true;
    bd.size = kMaxVertices * sizeof(Vertex);
    buffer_ = sg_make_buffer(&bd);
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_LINEAR;
    sd.mag_filter = SG_FILTER_LINEAR;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_ = sg_make_sampler(&sd);
    if (sg_query_pipeline_state(pipeline_) != SG_RESOURCESTATE_VALID || sg_query_buffer_state(buffer_) != SG_RESOURCESTATE_VALID) {
        failed_ = true;
        return false;
    }
    return true;
}

void Controls::shutdown() {
    if (sg_isvalid()) {
        if (font_) font_->release();
        if (pipeline_.id) sg_destroy_pipeline(pipeline_);
        if (shader_.id) sg_destroy_shader(shader_);
        if (buffer_.id) sg_destroy_buffer(buffer_);
        if (sampler_.id) sg_destroy_sampler(sampler_);
    }
    font_.reset();
    pipeline_ = {};
    shader_ = {};
    buffer_ = {};
    sampler_ = {};
}

void Controls::quad(float x0, float y0, float x1, float y1, float ox, float oy, const std::array<float, 4>& a, const std::array<float, 4>& b,
                    std::uint32_t fill, std::uint32_t edge, float kind, float border) {
    if (verts_.size() + 6 > kMaxVertices) return;
    auto v = [&](float x, float y) {
        Vertex r{};
        r.x = x;
        r.y = y;
        r.u = x - ox;
        r.v = y - oy;
        std::copy(a.begin(), a.end(), r.a);
        std::copy(b.begin(), b.end(), r.b);
        r.fill = fill;
        r.edge = edge;
        r.kind = kind;
        r.border = border;
        return r;
    };
    const Vertex p0 = v(x0, y0), p1 = v(x1, y0), p2 = v(x1, y1), p3 = v(x0, y1);
    verts_.insert(verts_.end(), {p0, p1, p2, p0, p2, p3});
}

void Controls::box(float x0, float y0, float x1, float y1, float radius, std::uint32_t fill, std::uint32_t edge, float border) {
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    radius = std::min(radius, std::min(hx, hy));
    quad(x0 - 1, y0 - 1, x1 + 1, y1 + 1, cx, cy, {hx, hy, radius, 0}, {0, 0, 0, 0}, fill, edge, 1.0f, border);
}

void Controls::shadow(float x0, float y0, float x1, float y1, float radius, float blur, std::uint32_t color) {
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    const float pad = blur + 1.0f;
    quad(x0 - pad, y0 - pad, x1 + pad, y1 + pad, cx, cy, {hx, hy, std::min(radius, std::min(hx, hy)), blur}, {0, 0, 0, 0}, color, 0, 4.0f,
         0.0f);
}

void Controls::line(float ax, float ay, float bx, float by, float width, std::uint32_t color) {
    const float r = width * 0.5f;
    const float x0 = std::min(ax, bx) - r - 1, y0 = std::min(ay, by) - r - 1, x1 = std::max(ax, bx) + r + 1, y1 = std::max(ay, by) + r + 1;
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    quad(x0, y0, x1, y1, cx, cy, {ax - cx, ay - cy, bx - cx, by - cy}, {r, 0, 0, 0}, color, 0, 2.0f, 0.0f);
}

void Controls::triangle(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t color) {
    const float x0 = std::min({ax, bx, cx}) - 1, y0 = std::min({ay, by, cy}) - 1, x1 = std::max({ax, bx, cx}) + 1, y1 = std::max({ay, by, cy}) + 1;
    const float ox = (x0 + x1) * 0.5f, oy = (y0 + y1) * 0.5f;
    quad(x0, y0, x1, y1, ox, oy, {ax - ox, ay - oy, bx - ox, by - oy}, {cx - ox, cy - oy, 0, 0}, color, 0, 3.0f, 0.0f);
}

void Controls::circle(float cx, float cy, float r, std::uint32_t fill, std::uint32_t edge, float border) {
    box(cx - r, cy - r, cx + r, cy + r, r, fill, edge, border);
}

float Controls::text(std::string_view s, float x, float baseline, int size, std::uint32_t color) {
    const Font& f = *font_;
    float pen = std::round(x);
    const float by = std::round(baseline);
    std::uint32_t prev = 0;
    for (size_t i = 0; i < s.size();) {
        const std::uint32_t cp = decode(s, i);
        if (prev) pen += f.kern(prev, cp, size);
        prev = cp;
        const stbtt_packedchar& c = f.chars[size][static_cast<size_t>(glyphIndex(cp))];
        const float gx = std::round(pen);
        if (c.x1 > c.x0 && verts_.size() + 6 <= kMaxVertices) {
            const float x0 = gx + c.xoff, y0 = by + c.yoff, x1 = gx + c.xoff2, y1 = by + c.yoff2;
            const float u0 = c.x0 / static_cast<float>(kAtlas), v0 = c.y0 / static_cast<float>(kAtlas);
            const float u1 = c.x1 / static_cast<float>(kAtlas), v1 = c.y1 / static_cast<float>(kAtlas);
            auto v = [&](float px, float py, float u, float w) {
                Vertex r{};
                r.x = px;
                r.y = py;
                r.u = u;
                r.v = w;
                r.fill = color;
                r.kind = 0.0f;
                return r;
            };
            const Vertex p0 = v(x0, y0, u0, v0), p1 = v(x1, y0, u1, v0), p2 = v(x1, y1, u1, v1), p3 = v(x0, y1, u0, v1);
            verts_.insert(verts_.end(), {p0, p1, p2, p0, p2, p3});
        }
        pen += c.xadvance;
    }
    return pen;
}

float Controls::measure(std::string_view s, int size) const {
    const Font& f = *font_;
    float w = 0.0f;
    std::uint32_t prev = 0;
    for (size_t i = 0; i < s.size();) {
        const std::uint32_t cp = decode(s, i);
        if (prev) w += f.kern(prev, cp, size);
        prev = cp;
        w += f.chars[size][static_cast<size_t>(glyphIndex(cp))].xadvance;
    }
    return w;
}

std::string Controls::fit(const std::string& s, float width, int size) const {
    if (measure(s, size) <= width) return s;
    const std::string dots = "\xE2\x80\xA6";
    std::string out = s;
    while (!out.empty()) {
        size_t cut = out.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.erase(cut);
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (measure(out + dots, size) <= width) return out + dots;
    }
    return dots;
}

void Controls::button(const Target& t, bool active) {
    targets_.push_back(t);
    const bool over = hover_.item == t.item && hover_.index == t.index;
    const bool down = pressId_ >= 0 && pressed_.item == t.item && pressed_.index == t.index && over;
    const float r = std::round(4.0f * dpi_);
    shadow(t.x0, t.y0 + dpi_, t.x1, t.y1 + dpi_, r, 3.0f * dpi_, kShade);
    box(t.x0, t.y0, t.x1, t.y1, r, down || active ? kFacePress : over ? kFaceHover : kFace, kRim, std::max(1.0f, dpi_));
}

Controls::Target Controls::hit(double x, double y) const {
    for (auto it = targets_.rbegin(); it != targets_.rend(); ++it)
        if (x >= it->x0 && x < it->x1 && y >= it->y0 && y < it->y1) return *it;
    return {};
}

bool Controls::pointer(Phase phase, int id, double x, double y) {
    if (!flags_) return false;
    switch (phase) {
    case Hover: {
        const Target t = hit(x, y);
        if (pressId_ < 0 && (t.item != hover_.item || t.index != hover_.index)) {
            hover_ = t;
            dirty_ = true;
        }
        return t.item != Item::None;
    }
    case Wheel:
        return hit(x, y).item != Item::None;
    case Down: {
        const Target t = hit(x, y);
        if (t.item == Item::None) {
            if (panel_ || creditsOpen_) {
                panel_ = false;
                creditsOpen_ = false;
                dirty_ = true;
            }
            return false;
        }
        if (pressId_ >= 0) return true;
        pressId_ = id;
        pressed_ = t;
        hover_ = t;
        dirty_ = true;
        return true;
    }
    case Move: {
        if (id != pressId_) return false;
        const Target t = hit(x, y);
        const bool over = t.item == pressed_.item && t.index == pressed_.index;
        const Target next = over ? t : Target{};
        if (next.item != hover_.item || next.index != hover_.index) {
            hover_ = next;
            dirty_ = true;
        }
        return true;
    }
    case Up: {
        if (id != pressId_) return false;
        const Target t = hit(x, y);
        const Target was = pressed_;
        pressId_ = -1;
        pressed_ = {};
        hover_ = t;
        dirty_ = true;
        if (t.item == was.item && t.index == was.index) activate(t);
        return true;
    }
    case Cancel:
        if (id != pressId_) return false;
        pressId_ = -1;
        pressed_ = {};
        hover_ = {};
        dirty_ = true;
        return true;
    }
    return false;
}

void Controls::activate(const Target& t) {
    switch (t.item) {
    case Item::ZoomIn:
        engine_.input().zoomAnimated(width_ * 0.5, height_ * 0.5, 0.5, 0.25);
        break;
    case Item::ZoomOut:
        engine_.input().zoomAnimated(width_ * 0.5, height_ * 0.5, 2.0, 0.25);
        break;
    case Item::Compass: {
        View v = engine_.view();
        v.azimuth = 0.0;
        engine_.flyTo(v, 0.45);
        break;
    }
    case Item::Tilt: {
        View v = engine_.view();
        const bool flat = !engine_.is3D() || v.tilt < 1.0;
        if (!engine_.is3D()) engine_.set3D(true);
        v.tilt = flat ? 50.0 : 0.0;
        engine_.flyTo(v, 0.6);
        break;
    }
    case Item::Layers:
        panel_ = !panel_;
        creditsOpen_ = false;
        break;
    case Item::Basemap:
        engine_.setBasemap(t.index);
        break;
    case Item::Labels:
        engine_.setLabelsEnabled(!engine_.labelsEnabled());
        break;
    case Item::Buildings:
        engine_.setBuildingsEnabled(!engine_.buildingsEnabled());
        break;
    case Item::Contours:
        engine_.setContours(!engine_.contoursEnabled(), engine_.contourInterval(), engine_.contourUnit());
        break;
    case Item::Terrain:
        engine_.setTerrainEnabled(!engine_.terrainEnabled());
        break;
    case Item::Download:
        if (!downloadId_ || engine_.downloadStatus(downloadId_).finished) downloadId_ = engine_.downloadLabels(engine_.viewRegion());
        break;
    case Item::Scale:
        imperial_ = !imperial_;
        break;
    case Item::Credits:
        creditsOpen_ = !creditsOpen_;
        panel_ = false;
        break;
    default:
        break;
    }
    dirty_ = true;
}

void Controls::drawZoom() {
    const Target& in = targets_[targets_.size() - 2];
    const Target& out = targets_.back();
    const float r = std::round(4.0f * dpi_);
    shadow(in.x0, in.y0 + dpi_, out.x1, out.y1 + dpi_, r, 3.0f * dpi_, kShade);
    box(in.x0, in.y0, out.x1, out.y1, r, kFace, kRim, std::max(1.0f, dpi_));
    for (const Target* t : {&in, &out}) {
        const bool over = hover_.item == t->item;
        if (!over) continue;
        const bool down = pressId_ >= 0 && pressed_.item == t->item;
        box(t->x0 + dpi_, t->y0 + dpi_, t->x1 - dpi_, t->y1 - dpi_, r - dpi_, down ? kFacePress : kFaceHover);
    }
    const float stroke = std::max(1.5f, 1.7f * dpi_);
    const float half = 6.0f * dpi_;
    const float cx = (in.x0 + in.x1) * 0.5f;
    const float iy = (in.y0 + in.y1) * 0.5f, oy = (out.y0 + out.y1) * 0.5f;
    line(cx - half, iy, cx + half, iy, stroke, kInk);
    line(cx, iy - half, cx, iy + half, stroke, kInk);
    line(cx - half, oy, cx + half, oy, stroke, kInk);
    line(in.x0 + 7.0f * dpi_, in.y1, in.x1 - 7.0f * dpi_, in.y1, std::max(1.0f, dpi_), kRim);
}

void Controls::drawCompass(double azimuth) {
    const Target& t = targets_.back();
    const float cx = (t.x0 + t.x1) * 0.5f, cy = (t.y0 + t.y1) * 0.5f;
    const double a = -azimuth * kPi / 180.0;
    const float ca = static_cast<float>(std::cos(a)), sa = static_cast<float>(std::sin(a));
    auto at = [&](float x, float y, float& ox, float& oy) {
        ox = cx + x * ca - y * sa;
        oy = cy + x * sa + y * ca;
    };
    const float len = 10.5f * dpi_, wide = 3.6f * dpi_;
    float nx, ny, sx, sy, lx, ly, rx, ry;
    at(0, -len, nx, ny);
    at(0, len, sx, sy);
    at(-wide, 0, lx, ly);
    at(wide, 0, rx, ry);
    triangle(nx, ny, rx, ry, lx, ly, kNorth);
    triangle(sx, sy, lx, ly, rx, ry, kSouth);
    circle(cx, cy, 1.4f * dpi_, kWhite);
}

void Controls::drawTilt(bool flat) {
    const Target& t = targets_.back();
    const char* label = flat ? "3D" : "2D";
    const float w = measure(label, kNormal);
    const float cx = (t.x0 + t.x1) * 0.5f, cy = (t.y0 + t.y1) * 0.5f;
    text(label, cx - w * 0.5f, cy + 4.6f * dpi_, kNormal, kInk);
}

void Controls::drawLayersButton() {
    const Target& t = targets_.back();
    const float cx = (t.x0 + t.x1) * 0.5f, cy = (t.y0 + t.y1) * 0.5f;
    const float u = dpi_, s = std::max(1.5f, 1.6f * dpi_);
    const float w = 7.5f * u;
    line(cx - w, cy - 2.0f * u, cx, cy - 6.0f * u, s, kInk);
    line(cx, cy - 6.0f * u, cx + w, cy - 2.0f * u, s, kInk);
    line(cx + w, cy - 2.0f * u, cx, cy + 2.0f * u, s, kInk);
    line(cx, cy + 2.0f * u, cx - w, cy - 2.0f * u, s, kInk);
    line(cx - w, cy + 2.0f * u, cx, cy + 6.0f * u, s, kInk);
    line(cx, cy + 6.0f * u, cx + w, cy + 2.0f * u, s, kInk);
}

void Controls::drawPanel() {
    const Target& anchor = targets_.back();
    const float u = dpi_;
    const float row = std::round(28.0f * u), pad = std::round(8.0f * u), width = std::round(196.0f * u);
    const std::vector<Basemap> maps = engine_.basemaps();
    const int current = engine_.basemap();
    const std::string source = engine_.labelsSource();
    const bool download = engine_.labelsEnabled() && (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0);
    const float header = std::round(22.0f * u);
    float height = pad + (maps.empty() ? 0.0f : header + row * static_cast<float>(maps.size()) + pad) + header + row * 3.0f +
                   (download ? pad + row : 0.0f) + pad;
    const float right = anchor.x0 - std::round(8.0f * u);
    const float left = right - width;
    float top = anchor.y0;
    const bool strip = (flags_ & (ControlScale | ControlAttribution)) != 0;
    const float floor = bottom_ - std::round(10.0f * u) - (strip ? std::round(18.0f * u) : 0.0f);
    if (top + height > floor) top = floor - height;
    top = std::max(top, top_ + std::round(10.0f * u));
    const float r = std::round(6.0f * u);
    shadow(left, top + u, right, top + height + u, r, 5.0f * u, kShade);
    box(left, top, right, top + height, r, rgba(255, 255, 255, 0.97f), kRim, std::max(1.0f, u));
    targets_.push_back({left, top, right, top + height, Item::Panel, -1});
    float y = top + pad;
    auto rowTarget = [&](Item item, int index) {
        const Target t{left + std::round(4.0f * u), y, right - std::round(4.0f * u), y + row, item, index};
        targets_.push_back(t);
        if (hover_.item == item && hover_.index == index) box(t.x0, t.y0, t.x1, t.y1, std::round(4.0f * u), kRowHover);
        return t;
    };
    auto heading = [&](const char* s) {
        text(s, left + pad + 4.0f * u, y + 15.0f * u, kSmall, kMuted);
        y += header;
    };
    const float labelX = left + pad + 28.0f * u;
    const float textWidth = right - labelX - pad;
    if (!maps.empty()) {
        heading("Map");
        for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
            const Target t = rowTarget(Item::Basemap, i);
            const float cy = (t.y0 + t.y1) * 0.5f, cx = left + pad + 13.0f * u;
            const bool on = i == current;
            circle(cx, cy, 6.5f * u, kWhite, on ? kAccent : kMuted, std::max(1.2f, 1.5f * u));
            if (on) circle(cx, cy, 3.2f * u, kAccent);
            text(fit(maps[i].name, textWidth, kNormal), labelX, cy + 4.6f * u, kNormal, kText);
            y += row;
        }
        y += pad;
        line(left + pad, y - pad * 0.5f, right - pad, y - pad * 0.5f, std::max(1.0f, u), kRim);
    }
    heading("Show");
    const struct {
        Item item;
        const char* name;
        bool on;
    } toggles[3] = {{Item::Labels, "Labels", engine_.labelsEnabled()},
                    // {Item::Buildings, "3D buildings", engine_.buildingsEnabled()},
                    {Item::Contours, "Contours", engine_.contoursEnabled()},
                    {Item::Terrain, "3D terrain", engine_.terrainEnabled()}};
    for (const auto& tg : toggles) {
        const Target t = rowTarget(tg.item, -1);
        const float cy = (t.y0 + t.y1) * 0.5f, cx = left + pad + 13.0f * u, h = 6.5f * u;
        if (tg.on) {
            box(cx - h, cy - h, cx + h, cy + h, 2.5f * u, kAccent);
            const float s = std::max(1.5f, 1.8f * u);
            line(cx - 3.6f * u, cy + 0.2f * u, cx - 1.0f * u, cy + 2.8f * u, s, kWhite);
            line(cx - 1.0f * u, cy + 2.8f * u, cx + 3.8f * u, cy - 2.6f * u, s, kWhite);
        } else {
            box(cx - h, cy - h, cx + h, cy + h, 2.5f * u, kWhite, kMuted, std::max(1.2f, 1.5f * u));
        }
        text(tg.name, labelX, cy + 4.6f * u, kNormal, kText);
        y += row;
    }
    if (download) {
        y += pad;
        line(left + pad, y - pad * 0.5f, right - pad, y - pad * 0.5f, std::max(1.0f, u), kRim);
        const Target t = rowTarget(Item::Download, -1);
        const float cy = (t.y0 + t.y1) * 0.5f;
        std::string label = "Download roads here";
        std::uint32_t color = kAccent;
        if (downloadId_) {
            const DownloadStatus st = engine_.downloadStatus(downloadId_);
            if (!st.finished) {
                const double pct = st.total > 0 ? 100.0 * static_cast<double>(st.done + st.failed) / static_cast<double>(st.total) : 0.0;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Downloading roads %.0f%%", pct);
                label = buf;
                color = kMuted;
            } else if (st.done > 0) {
                label = "Roads saved, download again";
            }
        }
        text(fit(label, right - left - 2.0f * pad - 8.0f * u, kNormal), left + pad + 4.0f * u, cy + 4.6f * u, kNormal, color);
        y += row;
    }
}

void Controls::drawStrip(double metersPerPixel) {
    const float u = dpi_;
    const float h = std::round(18.0f * u);
    const float W = right_, H = bottom_;
    const float y0 = H - h, baseline = H - std::round(5.0f * u);
    float x = W - std::round(8.0f * u);
    float left = x;
    struct Piece {
        std::string text;
        float x;
    };
    std::vector<Piece> pieces;
    float barX0 = 0, barX1 = 0;
    std::string scaleText;
    if ((flags_ & ControlScale) && metersPerPixel > 0 && std::isfinite(metersPerPixel)) {
        double shown = 0;
        scaleText = distanceText(metersPerPixel * 90.0 * u, imperial_, shown);
        const float bar = static_cast<float>(shown / metersPerPixel);
        barX1 = x;
        barX0 = x - bar;
        const float tw = measure(scaleText, kSmall);
        const float tx = barX0 - std::round(5.0f * u) - tw;
        targets_.push_back({tx - 4.0f * u, y0, barX1 + 4.0f * u, H, Item::Scale, -1});
        pieces.push_back({scaleText, tx});
        left = tx;
        x = tx - std::round(12.0f * u);
    }
    std::vector<std::string> credits;
    if (flags_ & ControlAttribution) credits = engine_.credits();
    std::string joined;
    for (const auto& c : credits) {
        if (c.empty()) continue;
        if (!joined.empty()) joined += " \xC2\xB7 ";
        joined += c;
    }
    float creditsX = 0;
    if (!joined.empty()) {
        const float room = std::max(60.0f * u, std::min(x - 8.0f * u, W * 0.7f));
        const std::string shown = fit(joined, room, kSmall);
        const float tw = measure(shown, kSmall);
        creditsX = x - tw;
        targets_.push_back({creditsX - 4.0f * u, y0, x + 4.0f * u, H, Item::Credits, -1});
        pieces.push_back({shown, creditsX});
        left = std::min(left, creditsX);
    }
    if (pieces.empty()) return;
    box(left - std::round(8.0f * u), y0, W + (W >= static_cast<float>(width_) ? 2.0f : 0.0f), H + (H >= static_cast<float>(height_) ? 2.0f : 0.0f),
        0.0f, kStrip);
    for (const auto& p : pieces) text(p.text, p.x, baseline, kSmall, kText);
    if (barX1 > barX0) {
        const float s = std::max(1.0f, 1.3f * u);
        const float by = baseline - std::round(1.0f * u);
        line(barX0, by, barX1, by, s, kText);
        line(barX0, by, barX0, by - 5.0f * u, s, kText);
        line(barX1, by, barX1, by - 5.0f * u, s, kText);
    }
}

void Controls::drawCredits() {
    if (!creditsOpen_ || !(flags_ & ControlAttribution)) return;
    const std::vector<std::string> credits = engine_.credits();
    if (credits.empty()) return;
    const float u = dpi_;
    const float W = right_, H = bottom_;
    const float pad = std::round(8.0f * u), lineH = std::round(16.0f * u);
    const bool cluster = (flags_ & (ControlZoom | ControlCompass | ControlTilt | ControlLayers)) != 0;
    const float bx1 = cluster ? W - std::round(10.0f * u) - std::round(kButton * u) - std::round(8.0f * u) : W - std::round(8.0f * u);
    const float maxW = std::max(bx1 - 2.0f * pad - std::round(8.0f * u), 40.0f * u);
    float w = 0.0f;
    std::vector<std::string> rows;
    for (const auto& c : credits) {
        if (c.empty()) continue;
        rows.push_back(fit(c, maxW, kSmall));
        w = std::max(w, measure(rows.back(), kSmall));
    }
    if (rows.empty()) return;
    const float bx0 = bx1 - w - 2.0f * pad;
    const float by1 = H - std::round(18.0f * u) - std::round(6.0f * u);
    const float by0 = by1 - lineH * static_cast<float>(rows.size()) - pad * 1.5f;
    const float r = std::round(5.0f * u);
    shadow(bx0, by0 + u, bx1, by1 + u, r, 4.0f * u, kShade);
    box(bx0, by0, bx1, by1, r, rgba(255, 255, 255, 0.97f), kRim, std::max(1.0f, u));
    targets_.push_back({bx0, by0, bx1, by1, Item::Credits, -1});
    float ly = by0 + pad * 0.75f + lineH - 4.0f * u;
    for (const auto& row : rows) {
        text(row, bx0 + pad, ly, kSmall, kText);
        ly += lineH;
    }
}

void Controls::draw(int width, int height, double dpi) {
    dirty_ = false;
    if (!flags_ || width <= 0 || height <= 0 || !ready()) return;
    const float d = static_cast<float>(std::clamp(dpi, 0.5, 4.0));
    if (!font_ || font_->dpi != d) {
        if (!font_) font_ = std::make_unique<Font>();
        if (!font_->build(d) && !font_->image.id) {
            failed_ = true;
            return;
        }
    }
    dpi_ = d;
    width_ = width;
    height_ = height;
    verts_.clear();
    targets_.clear();
    const float u = dpi_;
    const View view = engine_.view();
    const bool strip = (flags_ & (ControlScale | ControlAttribution)) != 0;
    const float size = std::round(kButton * u), gap = std::round(8.0f * u), margin = std::round(10.0f * u);
    top_ = std::round(padding_[1] * u);
    right_ = static_cast<float>(width) - std::round(padding_[2] * u);
    bottom_ = static_cast<float>(height) - std::round(padding_[3] * u);
    const float right = right_ - margin;
    float bottom = bottom_ - margin - (strip ? std::round(18.0f * u) : 0.0f);
    auto next = [&](float h) {
        const float top = bottom - h;
        bottom = top - gap;
        return top;
    };
    if (strip) drawStrip(view.gsd);
    if (flags_ & ControlZoom) {
        const float top = next(size * 2.0f);
        targets_.push_back({right - size, top, right, top + size, Item::ZoomIn, -1});
        targets_.push_back({right - size, top + size, right, top + 2.0f * size, Item::ZoomOut, -1});
        drawZoom();
    }
    if (flags_ & ControlCompass) {
        const float top = next(size);
        button({right - size, top, right, top + size, Item::Compass, -1});
        drawCompass(view.azimuth);
    }
    if (flags_ & ControlTilt) {
        const float top = next(size);
        button({right - size, top, right, top + size, Item::Tilt, -1});
        drawTilt(!engine_.is3D() || view.tilt < 1.0);
    }
    if (flags_ & ControlLayers) {
        const float top = next(size);
        button({right - size, top, right, top + size, Item::Layers, -1}, panel_);
        drawLayersButton();
        if (panel_) drawPanel();
    }
    drawCredits();
    if (verts_.empty()) return;
    sg_write_buffer_desc wd{};
    wd.src.data = {verts_.data(), verts_.size() * sizeof(Vertex)};
    wd.dst.buffer = buffer_;
    wd.size = verts_.size() * sizeof(Vertex);
    sg_write_buffer_transient(&wd);
    sg_apply_pipeline(pipeline_);
    sg_bindings b{};
    b.vertex_buffers[0] = buffer_;
    b.views[VIEW_tex] = font_->view;
    b.samplers[SMP_smp] = sampler_;
    sg_apply_bindings(&b);
    controls_params_t params{};
    params.screen[0] = 2.0f / static_cast<float>(width);
    params.screen[1] = 2.0f / static_cast<float>(height);
    sg_apply_uniforms(UB_controls_params, SG_RANGE(params));
    sg_draw(0, static_cast<int>(verts_.size()), 1);
}

}
