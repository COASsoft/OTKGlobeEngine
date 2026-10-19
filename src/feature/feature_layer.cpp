#include "feature/feature_layer.hpp"

#include "feature.glsl.h"

#include <earcut.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ote {

namespace {

constexpr size_t kOverlayBytes = 4 * 1024 * 1024;
constexpr double kTessellateMeters = 100000.0;
constexpr int kIconSize = 64;
constexpr int kAtlasSize = 2048;

void color4(std::uint32_t argb, float out[4]) {
    out[0] = ((argb >> 16) & 0xFF) / 255.0f;
    out[1] = ((argb >> 8) & 0xFF) / 255.0f;
    out[2] = (argb & 0xFF) / 255.0f;
    out[3] = ((argb >> 24) & 0xFF) / 255.0f;
}

double greatCircle(const Coord& a, const Coord& b) {
    double la1 = a.lat * kDegToRad, la2 = b.lat * kDegToRad, dl = (b.lon - a.lon) * kDegToRad;
    double s = std::sin((la2 - la1) * 0.5), t = std::sin(dl * 0.5);
    double h = s * s + std::cos(la1) * std::cos(la2) * t * t;
    return 2.0 * 6371000.0 * std::asin(std::min(1.0, std::sqrt(h)));
}

std::vector<std::array<double, 2>> tessellate(const std::vector<Coord>& pts) {
    std::vector<std::array<double, 2>> out;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i > 0) {
            double d = greatCircle(pts[i - 1], pts[i]);
            int n = static_cast<int>(d / kTessellateMeters);
            if (n > 0) {
                Vec3 a = geodeticToEcef(pts[i - 1].lat, pts[i - 1].lon, 0), b = geodeticToEcef(pts[i].lat, pts[i].lon, 0);
                for (int k = 1; k <= n; ++k) {
                    double t = static_cast<double>(k) / (n + 1);
                    GeoPoint g = ecefToGeodetic(a * (1 - t) + b * t);
                    double lon = g.lon;
                    while (lon - out.back()[0] > 180) lon -= 360;
                    while (lon - out.back()[0] < -180) lon += 360;
                    out.push_back({lon, g.lat});
                }
            }
        }
        out.push_back({pts[i].lon, pts[i].lat});
    }
    return out;
}

struct Writer {
    std::vector<float>& v;
    void vertex(float x, float y, float z, float u, float w, const float c[4]) {
        v.insert(v.end(), {x, y, z, u, w, c[0], c[1], c[2], c[3]});
    }
};

}

bool FeatureLayer::init() {
    if (ready_) return true;
    if (!atlas_.init()) return false;
    sg_backend backend = sg_query_backend();
    surfaceShader_ = sg_make_shader(fsurf_shader_desc(backend));
    spriteShader_ = sg_make_shader(sprite_shader_desc(backend));
    textShader_ = sg_make_shader(text_shader_desc(backend));
    lineShader_ = sg_make_shader(line3d_shader_desc(backend));
    imageShader_ = sg_make_shader(image_shader_desc(backend));

    auto blend = [](sg_color_target_state& c) {
        c.blend.enabled = true;
        c.blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        c.blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        c.blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        c.blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    };

    sg_pipeline_desc sp{};
    sp.shader = surfaceShader_;
    sp.layout.attrs[ATTR_fsurf_position].format = SG_VERTEXFORMAT_FLOAT2;
    sp.layout.attrs[ATTR_fsurf_color0].format = SG_VERTEXFORMAT_FLOAT4;
    sp.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    blend(sp.colors[0]);
    sp.depth.pixel_format = SG_PIXELFORMAT_NONE;
    sp.sample_count = 1;
    surfacePipeline_ = sg_make_pipeline(&sp);

    auto overlay = [&](sg_shader shd, bool depth) {
        sg_pipeline_desc d{};
        d.shader = shd;
        d.layout.attrs[ATTR_sprite_position].format = SG_VERTEXFORMAT_FLOAT3;
        d.layout.attrs[ATTR_sprite_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        d.layout.attrs[ATTR_sprite_color0].format = SG_VERTEXFORMAT_FLOAT4;
        blend(d.colors[0]);
        d.depth.compare = depth ? SG_COMPAREFUNC_LESS_EQUAL : SG_COMPAREFUNC_ALWAYS;
        d.depth.write_enabled = false;
        return sg_make_pipeline(&d);
    };
    spritePipeline_ = overlay(spriteShader_, false);
    textPipeline_ = overlay(textShader_, false);
    linePipeline_ = overlay(lineShader_, true);
    imagePipeline_ = overlay(imageShader_, false);
    iconPixels_.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4, 0);

    sg_buffer_desc bd{};
    bd.usage.vertex_buffer = true;
    bd.usage.write_transient = true;
    bd.size = kOverlayBytes;
    overlayBuffer_ = sg_make_buffer(&bd);

    sg_sampler_desc sm{};
    sm.min_filter = SG_FILTER_LINEAR;
    sm.mag_filter = SG_FILTER_LINEAR;
    sm.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sm.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_ = sg_make_sampler(&sm);

    std::vector<std::uint8_t> icons(static_cast<size_t>(kIconSize) * 4 * kIconSize * 4, 0);
    for (int s = 0; s < 4; ++s)
        for (int y = 0; y < kIconSize; ++y)
            for (int x = 0; x < kIconSize; ++x) {
                double px = (x + 0.5) / kIconSize * 2 - 1, py = (y + 0.5) / kIconSize * 2 - 1;
                double d = 0;
                if (s == 0) {
                    d = std::sqrt(px * px + py * py) - 0.82;
                } else if (s == 1) {
                    double cy = -0.25;
                    double circle = std::sqrt(px * px + (py - cy) * (py - cy)) - 0.6;
                    double tip = py > cy ? std::abs(px) - (1.0 - py) * 0.6 / (1.0 - cy) : 1.0;
                    d = std::min(circle, std::max(tip, cy - py));
                    if (py > 0.98) d = std::max(d, py - 0.98);
                } else if (s == 2) {
                    d = std::max(std::abs(px), std::abs(py)) - 0.78;
                } else {
                    double e1 = py - 0.75, e2 = -py * 0.5 + px * 0.866 - 0.38, e3 = -py * 0.5 - px * 0.866 - 0.38;
                    d = std::max(e1, std::max(e2, e3));
                }
                double aa = 2.0 / kIconSize;
                double inside = std::clamp(0.5 - d / aa, 0.0, 1.0);
                double outer = std::clamp(0.5 - (d - 0.16) / aa, 0.0, 1.0);
                double white = std::clamp(0.5 - (d + 0.0) / aa, 0.0, 1.0) * (1.0 - std::clamp(0.5 - (d + 0.12) / aa, 0.0, 1.0));
                size_t i = (static_cast<size_t>(y) * kIconSize * 4 + static_cast<size_t>(s) * kIconSize + x) * 4;
                icons[i + 0] = static_cast<std::uint8_t>(inside * 255);
                icons[i + 1] = static_cast<std::uint8_t>(white * 255);
                icons[i + 2] = 0;
                icons[i + 3] = static_cast<std::uint8_t>(outer * 255);
            }
    sg_image_desc id{};
    id.width = kIconSize * 4;
    id.height = kIconSize;
    id.pixel_format = SG_PIXELFORMAT_RGBA8;
    id.data.mip_levels[0] = {icons.data(), icons.size()};
    iconImage_ = sg_make_image(&id);
    sg_view_desc iv{};
    iv.texture.image = iconImage_;
    iconView_ = sg_make_view(&iv);
    uploadText();
    ready_ = true;
    return true;
}

void FeatureLayer::uploadText() {
    if (!atlas_.dirty() && textImage_.id) return;
    if (textView_.id) sg_destroy_view(textView_);
    if (textImage_.id) sg_destroy_image(textImage_);
    sg_image_desc d{};
    d.width = GlyphAtlas::kSize;
    d.height = GlyphAtlas::kSize;
    d.pixel_format = SG_PIXELFORMAT_R8;
    d.data.mip_levels[0] = {atlas_.pixels().data(), atlas_.pixels().size()};
    textImage_ = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = textImage_;
    textView_ = sg_make_view(&v);
    atlas_.clean();
}

bool FeatureLayer::iconRect(const Image* img, float rect[4]) {
    auto it = atlasRects_.find(img);
    if (it != atlasRects_.end()) {
        for (int i = 0; i < 4; ++i) rect[i] = it->second[i];
        return true;
    }
    if (!img || img->width <= 0 || img->height <= 0) return false;
    int w = std::min(img->width, 96), h = std::min(img->height, 96);
    if (atlasX_ + w + 1 > kAtlasSize) {
        atlasX_ = 0;
        atlasY_ += atlasRow_ + 1;
        atlasRow_ = 0;
    }
    if (atlasY_ + h + 1 > kAtlasSize) return false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sx = x * img->width / w, sy = y * img->height / h;
            const std::uint8_t* s = &img->rgba[(static_cast<size_t>(sy) * img->width + sx) * 4];
            std::uint8_t* d = &iconPixels_[(static_cast<size_t>(atlasY_ + y) * kAtlasSize + atlasX_ + x) * 4];
            std::memcpy(d, s, 4);
        }
    std::array<float, 4> r{static_cast<float>(atlasX_) / kAtlasSize, static_cast<float>(atlasY_) / kAtlasSize,
                           static_cast<float>(atlasX_ + w) / kAtlasSize, static_cast<float>(atlasY_ + h) / kAtlasSize};
    atlasX_ += w + 1;
    atlasRow_ = std::max(atlasRow_, h);
    atlasRects_[img] = r;
    atlasDirty_ = true;
    for (int i = 0; i < 4; ++i) rect[i] = r[i];
    return true;
}

void FeatureLayer::uploadIcons() {
    if (!atlasDirty_ && atlasImage_.id) return;
    if (atlasView_.id) sg_destroy_view(atlasView_);
    if (atlasImage_.id) sg_destroy_image(atlasImage_);
    sg_image_desc d{};
    d.width = kAtlasSize;
    d.height = kAtlasSize;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = {iconPixels_.data(), iconPixels_.size()};
    atlasImage_ = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = atlasImage_;
    atlasView_ = sg_make_view(&v);
    atlasDirty_ = false;
}

void FeatureLayer::shutdown() {
    if (!ready_) return;
    if (sg_isvalid()) {
        for (sg_pipeline p : {surfacePipeline_, spritePipeline_, textPipeline_, linePipeline_}) sg_destroy_pipeline(p);
        for (sg_shader s : {surfaceShader_, spriteShader_, textShader_, lineShader_, imageShader_}) sg_destroy_shader(s);
        sg_destroy_pipeline(imagePipeline_);
        if (atlasView_.id) sg_destroy_view(atlasView_);
        if (atlasImage_.id) sg_destroy_image(atlasImage_);
        sg_destroy_view(textView_);
        sg_destroy_image(textImage_);
        sg_destroy_view(iconView_);
        sg_destroy_image(iconImage_);
        sg_destroy_sampler(sampler_);
        sg_destroy_buffer(overlayBuffer_);
    }
    ready_ = false;
}

void FeatureLayer::prepare(Item& it) {
    it.lines.clear();
    it.triangles.clear();
    it.points.clear();
    GeoRect b{1e9, 1e9, -1e9, -1e9};
    auto grow = [&](double lon, double lat) {
        b.west = std::min(b.west, lon);
        b.east = std::max(b.east, lon);
        b.south = std::min(b.south, lat);
        b.north = std::max(b.north, lat);
    };
    for (const Geometry& g : it.feature.geometries) {
        if (g.type == GeometryType::Point) {
            for (const Coord& c : g.points) {
                it.points.push_back({c.lon, c.lat, c.alt});
                grow(c.lon, c.lat);
            }
            continue;
        }
        std::vector<Coord> ring = g.points;
        if (g.type == GeometryType::Polygon && !ring.empty() &&
            (ring.front().lat != ring.back().lat || ring.front().lon != ring.back().lon))
            ring.push_back(ring.front());
        auto t = tessellate(ring);
        for (auto& p : t) grow(p[0], p[1]);
        it.lines.push_back(t);
        if (g.type == GeometryType::Polygon) {
            std::vector<std::vector<std::array<double, 2>>> poly{t};
            for (const auto& h : g.holes) {
                std::vector<Coord> hr = h;
                if (!hr.empty()) hr.push_back(hr.front());
                auto ht = tessellate(hr);
                it.lines.push_back(ht);
                poly.push_back(ht);
            }
            std::vector<std::uint32_t> idx = mapbox::earcut<std::uint32_t>(poly);
            std::vector<std::array<double, 2>> flat;
            for (const auto& r : poly) flat.insert(flat.end(), r.begin(), r.end());
            for (std::uint32_t i : idx) it.triangles.push_back(flat[i]);
        }
    }
    it.bounds = b;
}

std::uint64_t FeatureLayer::add(const Feature& f) {
    std::uint64_t id = f.id ? f.id : nextId_++;
    nextId_ = std::max(nextId_, id + 1);
    Item& it = items_[id];
    it.feature = f;
    it.feature.id = id;
    prepare(it);
    dirty_.push_back(it.bounds);
    return id;
}

bool FeatureLayer::update(std::uint64_t id, const Feature& f) {
    auto it = items_.find(id);
    if (it == items_.end()) return false;
    dirty_.push_back(it->second.bounds);
    it->second.feature = f;
    it->second.feature.id = id;
    prepare(it->second);
    dirty_.push_back(it->second.bounds);
    return true;
}

bool FeatureLayer::remove(std::uint64_t id) {
    auto it = items_.find(id);
    if (it == items_.end()) return false;
    dirty_.push_back(it->second.bounds);
    items_.erase(it);
    return true;
}

void FeatureLayer::clear() {
    for (auto& [k, v] : items_) dirty_.push_back(v.bounds);
    items_.clear();
}

bool FeatureLayer::get(std::uint64_t id, Feature& out) const {
    auto it = items_.find(id);
    if (it == items_.end()) return false;
    out = it->second.feature;
    return true;
}

std::vector<std::uint64_t> FeatureLayer::ids() const {
    std::vector<std::uint64_t> out;
    for (auto& [k, v] : items_) out.push_back(k);
    return out;
}

bool FeatureLayer::takeDirty(std::vector<GeoRect>& regions) {
    if (dirty_.empty()) return false;
    regions.swap(dirty_);
    dirty_.clear();
    return true;
}

void FeatureLayer::buildSurface(const GeoRect& tile, double texelDeg, double dpi, std::vector<float>& verts) {
    double span = tile.north - tile.south;
    double coslat = std::max(std::cos((tile.south + tile.north) * 0.5 * kDegToRad), 0.05);
    auto ndc = [&](double lon, double lat, float out[2]) {
        out[0] = static_cast<float>((lon - tile.west) / span * 2.0 - 1.0);
        out[1] = static_cast<float>((lat - tile.south) / span * 2.0 - 1.0);
    };
    auto push = [&](double lon, double lat, const float c[4]) {
        float p[2];
        ndc(lon, lat, p);
        verts.insert(verts.end(), {p[0], p[1], c[0], c[1], c[2], c[3]});
    };
    for (auto& [id, it] : items_) {
        const Feature& f = it.feature;
        if (!f.visible || f.altitudeMode != AltitudeMode::ClampToGround) continue;
        double half = f.style.strokeWidth * dpi * texelDeg * 0.5;
        GeoRect grown{tile.west - half * 2 / coslat, tile.south - half * 2, tile.east + half * 2 / coslat, tile.north + half * 2};
        if (!it.bounds.intersects(grown)) continue;
        if ((f.style.fillColor >> 24) != 0 && !it.triangles.empty()) {
            float c[4];
            color4(f.style.fillColor, c);
            for (size_t i = 0; i + 2 < it.triangles.size(); i += 3) {
                const auto &a = it.triangles[i], &b = it.triangles[i + 1], &d = it.triangles[i + 2];
                GeoRect tb{std::min({a[0], b[0], d[0]}), std::min({a[1], b[1], d[1]}), std::max({a[0], b[0], d[0]}),
                           std::max({a[1], b[1], d[1]})};
                if (!tb.intersects(tile)) continue;
                push(a[0], a[1], c);
                push(b[0], b[1], c);
                push(d[0], d[1], c);
            }
        }
        if ((f.style.strokeColor >> 24) == 0 || f.style.strokeWidth <= 0) continue;
        float c[4];
        color4(f.style.strokeColor, c);
        for (const auto& line : it.lines)
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                const auto& a = line[i];
                const auto& b = line[i + 1];
                GeoRect sb{std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[0], b[0]), std::max(a[1], b[1])};
                if (!sb.intersects(grown)) continue;
                double ax = a[0] * coslat, ay = a[1], bx = b[0] * coslat, by = b[1];
                double dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0) continue;
                double nx = -dy / len * half, ny = dx / len * half;
                double ex = dx / len * half, ey = dy / len * half;
                double q[4][2] = {{ax + nx - ex, ay + ny - ey}, {bx + nx + ex, by + ny + ey}, {bx - nx + ex, by - ny + ey},
                                  {ax - nx - ex, ay - ny - ey}};
                int order[6] = {0, 1, 2, 0, 2, 3};
                for (int k : order) push(q[k][0] / coslat, q[k][1], c);
            }
    }
}

void FeatureLayer::drawSurface(sg_buffer buffer, int first, int count) {
    if (count <= 0) return;
    sg_apply_pipeline(surfacePipeline_);
    sg_bindings b{};
    b.vertex_buffers[0] = buffer;
    sg_apply_bindings(&b);
    sg_draw(first, count, 1);
}

double FeatureLayer::altitude(const Item& it, double lat, double lon, double alt, const GroundFn& ground) const {
    switch (it.feature.altitudeMode) {
    case AltitudeMode::Absolute: return alt;
    case AltitudeMode::RelativeToGround: return ground(lat, lon) + alt;
    default: return ground(lat, lon);
    }
}

void FeatureLayer::drawOverlay(const Camera& cam, const GroundFn& ground, double dpi, bool flip,
                               const std::vector<ExternalLabel>* extra) {
    if (!ready_ || (items_.empty() && (!extra || extra->empty()))) return;
    const double W = cam.width, H = cam.height;
    Vec3 eye = cam.eye();
    const Mat4& vp = cam.viewProjRel();
    auto project = [&](Vec3 p, double& sx, double& sy, double& z) {
        Vec3 r = p - eye;
        Vec4 c = vp * Vec4{r.x, r.y, r.z, 1.0};
        if (c.w <= 1e-6) return false;
        sx = (c.x / c.w * 0.5 + 0.5) * W;
        sy = (0.5 - c.y / c.w * 0.5) * H;
        z = c.z / c.w;
        return true;
    };
    auto visible = [&](Vec3 p) { return dot(normalize(p), normalize(eye - p)) > -0.05; };
    auto nx = [&](double x) { return static_cast<float>(x / W * 2.0 - 1.0); };
    auto ny = [&](double y) { return static_cast<float>(1.0 - y / H * 2.0); };

    std::vector<float> lines, icons, text, images;
    Writer lw{lines}, iw{icons}, tw{text}, mw{images};

    struct Label {
        std::string text;
        double x, y, offset, priority;
        float color[4];
        float size;
        double margin = 0;
    };
    std::vector<Label> labels;

    for (auto& [id, it] : items_) {
        const Feature& f = it.feature;
        if (!f.visible) continue;
        if (f.altitudeMode != AltitudeMode::ClampToGround && (f.style.strokeColor >> 24) && f.style.strokeWidth > 0) {
            float c[4];
            color4(f.style.strokeColor, c);
            double half = f.style.strokeWidth * dpi * 0.5;
            for (const Geometry& g : f.geometries) {
                if (g.type == GeometryType::Point) continue;
                for (size_t i = 0; i + 1 < g.points.size(); ++i) {
                    const Coord &a = g.points[i], &b = g.points[i + 1];
                    double ax, ay, az, bx, by, bz;
                    if (!project(geodeticToEcef(a.lat, a.lon, altitude(it, a.lat, a.lon, a.alt, ground)), ax, ay, az) ||
                        !project(geodeticToEcef(b.lat, b.lon, altitude(it, b.lat, b.lon, b.alt, ground)), bx, by, bz))
                        continue;
                    double dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-3) continue;
                    double ox = -dy / len * half, oy = dx / len * half;
                    float za = static_cast<float>(az), zb = static_cast<float>(bz);
                    lw.vertex(nx(ax + ox), ny(ay + oy), za, 0, 0, c);
                    lw.vertex(nx(bx + ox), ny(by + oy), zb, 0, 0, c);
                    lw.vertex(nx(bx - ox), ny(by - oy), zb, 0, 0, c);
                    lw.vertex(nx(ax + ox), ny(ay + oy), za, 0, 0, c);
                    lw.vertex(nx(bx - ox), ny(by - oy), zb, 0, 0, c);
                    lw.vertex(nx(ax - ox), ny(ay - oy), za, 0, 0, c);
                }
            }
        }
        float lc[4];
        color4(f.style.labelColor, lc);
        float size = static_cast<float>(14.0 * f.style.labelScale * dpi);
        for (const auto& p : it.points) {
            Vec3 e = geodeticToEcef(p[1], p[0], altitude(it, p[1], p[0], p[2], ground));
            double sx, sy, sz;
            if (!visible(e) || !project(e, sx, sy, sz) || sx < -64 || sy < -64 || sx > W + 64 || sy > H + 64) continue;
            double s = 22.0 * f.style.iconScale * dpi;
            double offset = 0;
            float rect[4];
            if (f.style.iconImage && iconRect(f.style.iconImage.get(), rect)) {
                if (std::find(atlasRefs_.begin(), atlasRefs_.end(), f.style.iconImage) == atlasRefs_.end())
                    atlasRefs_.push_back(f.style.iconImage);
                float c[4];
                color4(f.style.iconColor, c);
                double aspect = static_cast<double>(f.style.iconImage->width) / std::max(f.style.iconImage->height, 1);
                double hh = s * 1.4, ww = hh * aspect;
                float x0 = nx(sx - ww * 0.5), x1 = nx(sx + ww * 0.5), y0 = ny(sy - hh * 0.5), y1 = ny(sy + hh * 0.5);
                mw.vertex(x0, y0, 0, rect[0], rect[1], c);
                mw.vertex(x1, y0, 0, rect[2], rect[1], c);
                mw.vertex(x1, y1, 0, rect[2], rect[3], c);
                mw.vertex(x0, y0, 0, rect[0], rect[1], c);
                mw.vertex(x1, y1, 0, rect[2], rect[3], c);
                mw.vertex(x0, y1, 0, rect[0], rect[3], c);
                offset = hh * 0.5 + 2;
            } else if (f.style.icon != IconShape::Hidden) {
                int shape = f.style.icon == IconShape::Circle ? 0 : f.style.icon == IconShape::Pin ? 1 : f.style.icon == IconShape::Square ? 2 : 3;
                double cx = sx, cy = f.style.icon == IconShape::Pin ? sy - s * 0.5 : sy;
                float c[4];
                color4(f.style.iconColor, c);
                float u0 = shape * 0.25f, u1 = u0 + 0.25f;
                float x0 = nx(cx - s * 0.5), x1 = nx(cx + s * 0.5), y0 = ny(cy - s * 0.5), y1 = ny(cy + s * 0.5);
                iw.vertex(x0, y0, 0, u0, 0, c);
                iw.vertex(x1, y0, 0, u1, 0, c);
                iw.vertex(x1, y1, 0, u1, 1, c);
                iw.vertex(x0, y0, 0, u0, 0, c);
                iw.vertex(x1, y1, 0, u1, 1, c);
                iw.vertex(x0, y1, 0, u0, 1, c);
                offset = f.style.icon == IconShape::Pin ? 4 : s * 0.5 + 2;
            }
            if (f.style.showLabel && !f.name.empty())
                labels.push_back({f.name, sx, sy, offset, length(e - eye), {lc[0], lc[1], lc[2], lc[3]}, size});
        }
        if (f.style.showLabel && !f.name.empty() && it.points.empty() && !it.lines.empty()) {
            const auto& line = it.lines[0];
            double lat, lon;
            if (!it.triangles.empty()) {
                double sx = 0, sy = 0;
                for (const auto& t : it.triangles) {
                    sx += t[0];
                    sy += t[1];
                }
                lon = sx / it.triangles.size();
                lat = sy / it.triangles.size();
            } else {
                const auto& m = line[line.size() / 2];
                lon = m[0];
                lat = m[1];
            }
            double alt = 0;
            if (f.altitudeMode != AltitudeMode::ClampToGround && !f.geometries.empty() && !f.geometries[0].points.empty()) {
                const auto& pts = f.geometries[0].points;
                alt = pts[pts.size() / 2].alt;
            }
            Vec3 e = geodeticToEcef(lat, lon, altitude(it, lat, lon, alt, ground));
            double sx, sy, sz;
            if (visible(e) && project(e, sx, sy, sz) && sx > 0 && sy > 0 && sx < W && sy < H)
                labels.push_back({f.name, sx, sy, -size * 0.5, length(e - eye) * 4.0, {lc[0], lc[1], lc[2], lc[3]}, size});
        }
    }

    if (extra) {
        for (const ExternalLabel& l : *extra) {
            Vec3 e = geodeticToEcef(l.lat, l.lon, ground(l.lat, l.lon));
            double sx, sy, sz;
            if (!visible(e) || !project(e, sx, sy, sz) || sx < 0 || sy < 0 || sx > W || sy > H) continue;
            float c[4];
            color4(l.color, c);
            double size = l.size * dpi;
            labels.push_back({l.text, sx, sy, -size * 0.6, 1e12 + l.priority * 1e9 + length(e - eye), {c[0], c[1], c[2], c[3]},
                              static_cast<float>(size), 28.0 * dpi});
        }
    }
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.priority < b.priority; });
    std::vector<std::array<double, 4>> placed;
    for (const Label& l : labels) {
        double w = atlas_.measure(l.text, l.size) + 4, h = l.size * 1.25;
        double candidates[3] = {0, w * 0.5 + 10 + w * 0.5, -(w + 10)};
        int tries = l.margin > 0 ? 1 : 3;
        bool ok = false;
        double x0 = 0, y0 = l.y + l.offset;
        for (int ci = 0; ci < tries; ++ci) {
            double shift = candidates[ci];
            x0 = l.x - w * 0.5 + shift;
            std::array<double, 4> r{x0 - l.margin, y0 - l.margin * 0.5, x0 + w + l.margin, y0 + h + l.margin * 0.5};
            bool hit = false;
            for (const auto& p : placed)
                if (r[0] < p[2] && r[2] > p[0] && r[1] < p[3] && r[3] > p[1]) {
                    hit = true;
                    break;
                }
            if (!hit) {
                placed.push_back(r);
                ok = true;
                break;
            }
        }
        if (!ok) continue;
        double scale = l.size / GlyphAtlas::kBase;
        double pen = x0 + 2, base = y0 + atlas_.ascent() * scale;
        std::uint32_t prev = 0;
        for (std::uint32_t cp : GlyphAtlas::decode(l.text)) {
            const GlyphAtlas::Glyph& g = atlas_.glyph(cp);
            if (prev) pen += atlas_.kerning(prev, cp) * scale;
            prev = cp;
            if (g.u1 > g.u0) {
                float gx0 = nx(pen + g.x0 * scale), gx1 = nx(pen + g.x1 * scale);
                float gy0 = ny(base + g.y0 * scale), gy1 = ny(base + g.y1 * scale);
                tw.vertex(gx0, gy0, 0, g.u0, g.v0, l.color);
                tw.vertex(gx1, gy0, 0, g.u1, g.v0, l.color);
                tw.vertex(gx1, gy1, 0, g.u1, g.v1, l.color);
                tw.vertex(gx0, gy0, 0, g.u0, g.v0, l.color);
                tw.vertex(gx1, gy1, 0, g.u1, g.v1, l.color);
                tw.vertex(gx0, gy1, 0, g.u0, g.v1, l.color);
            }
            pen += g.advance * scale;
        }
    }
    uploadText();
    uploadIcons();

    std::vector<float> all;
    all.reserve(lines.size() + icons.size() + images.size() + text.size());
    all.insert(all.end(), lines.begin(), lines.end());
    all.insert(all.end(), icons.begin(), icons.end());
    all.insert(all.end(), images.begin(), images.end());
    all.insert(all.end(), text.begin(), text.end());
    if (all.empty()) return;
    if (all.size() * sizeof(float) > kOverlayBytes) all.resize(kOverlayBytes / sizeof(float) / 9 / 6 * 9 * 6);
    sg_write_buffer_desc wd{};
    wd.src.data = {all.data(), all.size() * sizeof(float)};
    wd.dst.buffer = overlayBuffer_;
    wd.size = all.size() * sizeof(float);
    sg_write_buffer_transient(&wd);

    int nl = static_cast<int>(lines.size() / 9), ni = static_cast<int>(icons.size() / 9);
    int nm = static_cast<int>(images.size() / 9);
    int nt = static_cast<int>(all.size() / 9) - nl - ni - nm;
    (void)flip;
    if (nl > 0) {
        sg_apply_pipeline(linePipeline_);
        sg_bindings b{};
        b.vertex_buffers[0] = overlayBuffer_;
        b.views[VIEW_tex] = iconView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_draw(0, nl, 1);
    }
    if (ni > 0) {
        sg_apply_pipeline(spritePipeline_);
        sg_bindings b{};
        b.vertex_buffers[0] = overlayBuffer_;
        b.views[VIEW_tex] = iconView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_draw(nl, ni, 1);
    }
    if (nm > 0) {
        sg_apply_pipeline(imagePipeline_);
        sg_bindings b{};
        b.vertex_buffers[0] = overlayBuffer_;
        b.views[VIEW_tex] = atlasView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_draw(nl + ni, nm, 1);
    }
    if (nt > 0) {
        sg_apply_pipeline(textPipeline_);
        sg_bindings b{};
        b.vertex_buffers[0] = overlayBuffer_;
        b.views[VIEW_tex] = textView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_draw(nl + ni + nm, nt, 1);
    }
}

std::uint64_t FeatureLayer::hitTest(const Camera& cam, const GroundFn& ground, const PickFn& pick, double sx, double sy,
                                    double radius) const {
    std::uint64_t best = 0;
    double bestD = radius;
    Vec3 eye = cam.eye();
    auto screen = [&](double lat, double lon, double alt, double& x, double& y) {
        Vec3 p = geodeticToEcef(lat, lon, alt);
        if (dot(normalize(p), eye - p) < 0) return false;
        return cam.ecefToScreen(p, x, y);
    };
    double pickLat = 0, pickLon = 0;
    bool picked = pick(sx, sy, pickLat, pickLon);
    for (const auto& [id, it] : items_) {
        if (!it.feature.visible) continue;
        for (const auto& p : it.points) {
            double x, y;
            if (screen(p[1], p[0], altitude(it, p[1], p[0], p[2], ground), x, y)) {
                double d = std::hypot(x - sx, y - sy) * 0.5;
                if (d < bestD) {
                    bestD = d;
                    best = id;
                }
            }
        }
        for (const auto& line : it.lines) {
            double px = 0, py = 0;
            bool havePrev = false;
            for (const auto& q : line) {
                double x, y;
                bool ok = screen(q[1], q[0], ground(q[1], q[0]), x, y);
                if (ok && havePrev) {
                    double dx = x - px, dy = y - py, l2 = dx * dx + dy * dy;
                    double t = l2 > 0 ? std::clamp(((sx - px) * dx + (sy - py) * dy) / l2, 0.0, 1.0) : 0.0;
                    double d = std::hypot(px + t * dx - sx, py + t * dy - sy);
                    if (d < bestD) {
                        bestD = d;
                        best = id;
                    }
                }
                px = x;
                py = y;
                havePrev = ok;
            }
        }
        if (picked && !it.triangles.empty() && bestD >= radius) {
            for (size_t i = 0; i + 2 < it.triangles.size(); i += 3) {
                const auto &a = it.triangles[i], &b = it.triangles[i + 1], &c = it.triangles[i + 2];
                auto side = [&](const std::array<double, 2>& p, const std::array<double, 2>& q) {
                    return (q[0] - p[0]) * (pickLat - p[1]) - (q[1] - p[1]) * (pickLon - p[0]);
                };
                double s1 = side(a, b), s2 = side(b, c), s3 = side(c, a);
                if ((s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0)) {
                    best = id;
                    bestD = radius * 0.99;
                    break;
                }
            }
        }
    }
    return best;
}

}
