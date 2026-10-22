#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "render/building_layer.hpp"

#include "core/geo.hpp"

#include "buildings.glsl.h"

#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>

namespace otk {

namespace embedded {
extern const unsigned char building_textures[];
extern const std::size_t building_textures_size;
}

namespace {

constexpr int kZoom = 14;
constexpr double kRadius = 3200.0;
constexpr double kFade = 900.0;
constexpr double kMaxAbove = 6000.0;
constexpr double kFadeAbove = 3500.0;
constexpr int kMaxJobs = 2;
constexpr size_t kBakesPerFrame = 2;
constexpr std::size_t kBakeBytes = 2 * 1024 * 1024;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr int kMaxAtlas = 1024;
constexpr std::size_t kMaxTiles = 20;
#else
constexpr int kMaxAtlas = 2048;
constexpr std::size_t kMaxTiles = 40;
#endif

double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void tileCenter(int x, int y, double& lat, double& lon) {
    const double n = std::ldexp(1.0, kZoom);
    lon = (x + 0.5) / n * 360.0 - 180.0;
    lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * (y + 0.5) / n))) * 180.0 / kPi;
}

void tileOf(double lat, double lon, int& x, int& y) {
    const double n = std::ldexp(1.0, kZoom);
    const double r = std::clamp(lat, -85.0, 85.0) * kPi / 180.0;
    const int max = static_cast<int>(n) - 1;
    x = std::clamp(static_cast<int>(std::floor((lon + 180.0) / 360.0 * n)), 0, max);
    y = std::clamp(static_cast<int>(std::floor((1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / kPi) / 2.0 * n)), 0, max);
}

struct BakeVertex {
    float x, y, u, v;
};

}

BuildingLayer::~BuildingLayer() { shutdown(); }

bool BuildingLayer::ready() {
    if (failed_) return false;
    if (initialized_) return true;
    initialized_ = true;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(embedded::building_textures, static_cast<int>(embedded::building_textures_size), &w, &h, &n, 4);
    if (!px || w <= 0 || h < w * BuildingTextures) {
        if (px) stbi_image_free(px);
        failed_ = true;
        return false;
    }
    const int layers = BuildingTextures;
    std::vector<std::vector<unsigned char>> mips;
    mips.emplace_back(px, px + static_cast<size_t>(w) * w * 4 * layers);
    stbi_image_free(px);
    for (int s = w; s > 1;) {
        const int t = s / 2;
        const std::vector<unsigned char>& prev = mips.back();
        std::vector<unsigned char> next(static_cast<size_t>(t) * t * 4 * layers);
        for (int l = 0; l < layers; ++l)
            for (int yy = 0; yy < t; ++yy)
                for (int xx = 0; xx < t; ++xx)
                    for (int c = 0; c < 4; ++c) {
                        auto at = [&](int ax, int ay) { return static_cast<int>(prev[((static_cast<size_t>(l) * s + ay) * s + ax) * 4 + c]); };
                        next[((static_cast<size_t>(l) * t + yy) * t + xx) * 4 + c] =
                            static_cast<unsigned char>((at(2 * xx, 2 * yy) + at(2 * xx + 1, 2 * yy) + at(2 * xx, 2 * yy + 1) + at(2 * xx + 1, 2 * yy + 1) + 2) / 4);
                    }
        mips.push_back(std::move(next));
        s = t;
    }
    sg_image_desc fd{};
    fd.type = SG_IMAGETYPE_ARRAY;
    fd.width = w;
    fd.height = w;
    fd.num_slices = layers;
    fd.num_mipmaps = static_cast<int>(mips.size());
    fd.pixel_format = SG_PIXELFORMAT_RGBA8;
    for (size_t i = 0; i < mips.size(); ++i) fd.data.mip_levels[i] = {mips[i].data(), mips[i].size()};
    facades_ = sg_make_image(&fd);
    sg_view_desc fv{};
    fv.texture.image = facades_;
    facadeView_ = sg_make_view(&fv);

    const unsigned char gray[4] = {108, 108, 105, 255};
    sg_image_desc bd{};
    bd.width = 1;
    bd.height = 1;
    bd.pixel_format = SG_PIXELFORMAT_RGBA8;
    bd.data.mip_levels[0] = {gray, sizeof(gray)};
    blank_ = sg_make_image(&bd);
    sg_view_desc bv{};
    bv.texture.image = blank_;
    blankView_ = sg_make_view(&bv);

    sg_sampler_desc fs{};
    fs.min_filter = SG_FILTER_LINEAR;
    fs.mag_filter = SG_FILTER_LINEAR;
    fs.mipmap_filter = SG_FILTER_LINEAR;
    fs.wrap_u = SG_WRAP_REPEAT;
    fs.wrap_v = SG_WRAP_REPEAT;
    fs.max_anisotropy = 4;
    facadeSampler_ = sg_make_sampler(&fs);
    sg_sampler_desc rs{};
    rs.min_filter = SG_FILTER_LINEAR;
    rs.mag_filter = SG_FILTER_LINEAR;
    rs.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    rs.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    roofSampler_ = sg_make_sampler(&rs);

    const sg_backend backend = sg_query_backend();
    shader_ = sg_make_shader(building_shader_desc(backend));
    bakeShader_ = sg_make_shader(roofbake_shader_desc(backend));
    sg_pipeline_desc pd{};
    pd.shader = shader_;
    pd.layout.attrs[ATTR_building_position].format = SG_VERTEXFORMAT_FLOAT3;
    pd.layout.attrs[ATTR_building_normal].format = SG_VERTEXFORMAT_FLOAT3;
    pd.layout.attrs[ATTR_building_texcoord0].format = SG_VERTEXFORMAT_FLOAT4;
    pd.layout.attrs[ATTR_building_color0].format = SG_VERTEXFORMAT_UBYTE4N;
    pd.layout.attrs[ATTR_building_texcoord1].format = SG_VERTEXFORMAT_FLOAT2;
    pd.index_type = SG_INDEXTYPE_UINT32;
    pd.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pd.depth.write_enabled = true;
    pd.cull_mode = SG_CULLMODE_NONE;
    pipeline_ = sg_make_pipeline(&pd);
    sg_pipeline_desc kd{};
    kd.shader = bakeShader_;
    kd.layout.attrs[ATTR_roofbake_position].format = SG_VERTEXFORMAT_FLOAT2;
    kd.layout.attrs[ATTR_roofbake_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    kd.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    kd.depth.pixel_format = SG_PIXELFORMAT_NONE;
    kd.sample_count = 1;
    bakePipeline_ = sg_make_pipeline(&kd);
    sg_buffer_desc kb{};
    kb.usage.vertex_buffer = true;
    kb.usage.write_transient = true;
    kb.size = kBakeBytes;
    bakeBuffer_ = sg_make_buffer(&kb);
    if (sg_query_pipeline_state(pipeline_) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(bakePipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_image_state(facades_) != SG_RESOURCESTATE_VALID) {
        failed_ = true;
        return false;
    }
    return true;
}

void BuildingLayer::release(Tile& t) {
    if (sg_isvalid()) {
        if (t.vbuf.id) sg_destroy_buffer(t.vbuf);
        if (t.ibuf.id) sg_destroy_buffer(t.ibuf);
        if (t.atlasColor.id) sg_destroy_view(t.atlasColor);
        if (t.atlasTexture.id) sg_destroy_view(t.atlasTexture);
        if (t.atlas.id) sg_destroy_image(t.atlas);
    }
    t.vbuf = {};
    t.ibuf = {};
    t.atlasColor = {};
    t.atlasTexture = {};
    t.atlas = {};
    t.ready = false;
    t.baked = false;
}

void BuildingLayer::clear() {
    for (auto& [key, t] : tiles_) {
        if (t.building && t.job.valid()) t.job.wait();
        release(t);
    }
    tiles_.clear();
    jobs_ = 0;
}

void BuildingLayer::retry() {
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        if (it->second.empty && !it->second.building) it = tiles_.erase(it);
        else ++it;
    }
}

void BuildingLayer::shutdown() {
    clear();
    if (initialized_ && sg_isvalid()) {
        for (sg_pipeline p : {pipeline_, bakePipeline_})
            if (p.id) sg_destroy_pipeline(p);
        for (sg_shader s : {shader_, bakeShader_})
            if (s.id) sg_destroy_shader(s);
        for (sg_view v : {facadeView_, blankView_})
            if (v.id) sg_destroy_view(v);
        for (sg_image i : {facades_, blank_})
            if (i.id) sg_destroy_image(i);
        for (sg_sampler s : {facadeSampler_, roofSampler_})
            if (s.id) sg_destroy_sampler(s);
        if (bakeBuffer_.id) sg_destroy_buffer(bakeBuffer_);
    }
    pipeline_ = bakePipeline_ = {};
    shader_ = bakeShader_ = {};
    facadeView_ = blankView_ = {};
    facades_ = blank_ = {};
    facadeSampler_ = roofSampler_ = {};
    bakeBuffer_ = {};
    initialized_ = false;
    failed_ = false;
}

void BuildingLayer::upload(Tile& t, const BuildingMesh& mesh) {
    sg_buffer_desc vd{};
    vd.usage.vertex_buffer = true;
    vd.data = {mesh.vertices.data(), mesh.vertices.size() * sizeof(BuildingVertex)};
    t.vbuf = sg_make_buffer(&vd);
    sg_buffer_desc id{};
    id.usage.index_buffer = true;
    id.data = {mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t)};
    t.ibuf = sg_make_buffer(&id);
    t.indexCount = static_cast<int>(mesh.indices.size());
    enuFrame(mesh.lat, mesh.lon, t.east, t.north, t.up);
    t.origin = geodeticToEcef(mesh.lat, mesh.lon, 0.0);
    if (mesh.atlas > 0 && !mesh.cells.empty()) {
        sg_image_desc ad{};
        ad.usage.color_attachment = true;
        ad.width = mesh.atlas;
        ad.height = mesh.atlas;
        ad.pixel_format = SG_PIXELFORMAT_RGBA8;
        ad.sample_count = 1;
        t.atlas = sg_make_image(&ad);
        sg_view_desc cv{};
        cv.color_attachment.image = t.atlas;
        t.atlasColor = sg_make_view(&cv);
        sg_view_desc tv{};
        tv.texture.image = t.atlas;
        t.atlasTexture = sg_make_view(&tv);
        t.atlasSize = mesh.atlas;
        t.cells = mesh.cells;
    }
}

void BuildingLayer::update(const Camera& cam, const TileFn& tile, const HeightsFn& heights, const ImageryFn& imagery, double exaggeration,
                           bool terrain) {
    ++frame_;
    busy_ = false;
    const double now = seconds();
    for (auto& [key, t] : tiles_) {
        if (!t.building || !t.job.valid() || t.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        std::shared_ptr<BuildingMesh> mesh = t.job.get();
        t.building = false;
        --jobs_;
        if (!mesh || mesh->indices.empty() || !ready()) {
            t.empty = true;
            continue;
        }
        upload(t, *mesh);
        t.ready = true;
        t.appear = now;
    }
    const double above = cam.eyeAboveGround();
    fade_ = std::clamp((kMaxAbove - above) / (kMaxAbove - kFadeAbove), 0.0, 1.0);
    std::vector<std::pair<double, std::pair<int, int>>> candidates;
    if (fade_ > 0.0) {
        const Vec3 eye = cam.eye();
        const GeoPoint eyeGround = ecefToGeodetic(eye);
        std::set<std::pair<int, int>> seen;
        auto around = [&](double lat, double lon) {
            int cx, cy;
            tileOf(lat, lon, cx, cy);
            const double width = 40075016.686 * std::max(std::cos(lat * kPi / 180.0), 0.05) / std::ldexp(1.0, kZoom);
            const int r = static_cast<int>(std::ceil(kRadius / width)) + 1;
            const int count = 1 << kZoom;
            for (int y = std::max(cy - r, 0); y <= std::min(cy + r, count - 1); ++y)
                for (int x = cx - r; x <= cx + r; ++x) {
                    const int wx = ((x % count) + count) % count;
                    if (!seen.insert({wx, y}).second) continue;
                    double clat, clon;
                    tileCenter(wx, y, clat, clon);
                    const double d = std::max(length(geodeticToEcef(clat, clon, 0.0) - eye) - width * 0.71, 0.0);
                    if (d < kRadius) candidates.push_back({d, {wx, y}});
                }
        };
        around(cam.focus.lat, cam.focus.lon);
        around(eyeGround.lat, eyeGround.lon);
        std::sort(candidates.begin(), candidates.end());
    }
    for (const auto& [d, key] : candidates) {
        auto it = tiles_.find(key);
        if (it != tiles_.end()) {
            Tile& t = it->second;
            t.used = frame_;
            t.distance = d;
            if (t.ready || t.building || t.empty) continue;
        }
        if (jobs_ >= kMaxJobs) {
            busy_ = true;
            continue;
        }
        std::shared_ptr<const VectorTile> vt;
        const int state = tile(key.first, key.second, d, vt);
        if (state == 0) {
            busy_ = true;
            continue;
        }
        Tile& t = tiles_[key];
        t.x = key.first;
        t.y = key.second;
        t.used = frame_;
        t.distance = d;
        if (state < 0 || !vt || (vt->buildings.empty() && vt->bridges.empty())) {
            t.empty = true;
            continue;
        }
        t.building = true;
        ++jobs_;
        busy_ = true;
        std::vector<BuildingShape> shapes = vt->buildings;
        std::vector<BridgeShape> bridges = vt->bridges;
        const int x = key.first, y = key.second;
        t.job = std::async(std::launch::async, [shapes = std::move(shapes), bridges = std::move(bridges), x, y, heights, exaggeration, terrain]() {
            const std::vector<std::array<double, 2>> points = buildingSamplePoints(shapes, bridges, kZoom, x, y);
            std::vector<double> h;
            if (terrain && !points.empty() && heights) {
                std::vector<std::pair<double, double>> ll;
                ll.reserve(points.size());
                for (const auto& p : points) ll.emplace_back(p[0], p[1]);
                h = heights(ll);
                for (double& v : h)
                    if (std::isfinite(v)) v *= exaggeration;
            } else {
                h.assign(points.size(), 0.0);
            }
            return std::make_shared<BuildingMesh>(buildBuildingMesh(shapes, bridges, kZoom, x, y, h, kMaxAtlas));
        });
    }
    if (tiles_.size() > kMaxTiles) {
        std::vector<std::pair<std::uint64_t, std::pair<int, int>>> old;
        for (const auto& [key, t] : tiles_)
            if (!t.building && t.used + 1 < frame_) old.push_back({t.used, key});
        std::sort(old.begin(), old.end());
        for (const auto& [used, key] : old) {
            if (tiles_.size() <= kMaxTiles) break;
            release(tiles_[key]);
            tiles_.erase(key);
        }
    }
    for (auto& [key, t] : tiles_) {
        if (t.used != frame_) continue;
        if (t.building) busy_ = true;
        if (!t.ready) continue;
        if (now - t.appear < 0.7) busy_ = true;
        if (t.atlasSize <= 0 || !imagery) continue;
        t.target = t.distance < 700.0 ? 18 : t.distance < 1500.0 ? 17 : 16;
        if (now - t.imageryCheck >= 0.5) {
            t.imageryCheck = now;
            int sum = 0, low = 99;
            std::vector<Piece> pieces;
            for (const RoofCell& c : t.cells) {
                imagery(c.west, c.south, c.east, c.north, t.target, true, pieces);
                int z = pieces.empty() ? 0 : 99;
                for (const Piece& p : pieces) z = std::min(z, p.z);
                sum += z;
                low = std::min(low, z);
            }
            t.availableLevel = sum;
            t.availableMin = low == 99 ? 0 : low;
        }
        if ((!t.baked || t.availableMin < t.target - 1) && now - t.appear < 12.0) busy_ = true;
    }
}

void BuildingLayer::bake(const ImageryFn& imagery, bool flip) {
    if (!initialized_ || failed_ || !imagery) return;
    const double now = seconds();
    std::vector<Tile*> todo;
    for (auto& [key, t] : tiles_) {
        if (!t.ready || t.atlasSize <= 0 || t.used + 1 < frame_ || t.availableLevel < 0) continue;
        if (!t.baked) {
            if (t.availableMin >= t.target - 1 || now - t.appear > 4.0) todo.push_back(&t);
            continue;
        }
        if (t.availableLevel > t.bakedLevel) todo.push_back(&t);
    }
    if (todo.empty()) return;
    std::sort(todo.begin(), todo.end(), [](const Tile* a, const Tile* b) { return a->distance < b->distance; });
    if (todo.size() > kBakesPerFrame) todo.resize(kBakesPerFrame);
    struct Range {
        sg_view view;
        int first, count;
    };
    struct Quad {
        sg_view view;
        float x0, y0, x1, y1, u0, v0, u1, v1;
    };
    std::vector<BakeVertex> verts;
    std::vector<std::vector<Range>> ranges(todo.size());
    std::vector<int> levels(todo.size(), 0);
    const size_t cap = kBakeBytes / sizeof(BakeVertex);
    std::vector<Piece> pieces;
    for (size_t k = 0; k < todo.size(); ++k) {
        const Tile& t = *todo[k];
        const float s = static_cast<float>(t.atlasSize);
        auto ndcY = [&](float py) { return flip ? 1.0f - 2.0f * py / s : -1.0f + 2.0f * py / s; };
        std::vector<Quad> quads;
        quads.reserve(t.cells.size());
        for (const RoofCell& c : t.cells) {
            imagery(c.west, c.south, c.east, c.north, t.target, false, pieces);
            int low = pieces.empty() ? 0 : 99;
            for (const Piece& p : pieces) {
                if (!p.view.id) continue;
                low = std::min(low, p.z);
                const float px0 = static_cast<float>(c.x) + p.fx0 * static_cast<float>(c.w), px1 = static_cast<float>(c.x) + p.fx1 * static_cast<float>(c.w);
                const float py0 = static_cast<float>(c.y) + p.fy0 * static_cast<float>(c.h), py1 = static_cast<float>(c.y) + p.fy1 * static_cast<float>(c.h);
                quads.push_back({p.view, px0 / s * 2.0f - 1.0f, ndcY(py0), px1 / s * 2.0f - 1.0f, ndcY(py1), p.u0, p.v0, p.u1, p.v1});
            }
            levels[k] += low == 99 ? 0 : low;
        }
        std::sort(quads.begin(), quads.end(), [](const Quad& a, const Quad& b) { return a.view.id < b.view.id; });
        for (const Quad& q : quads) {
            if (verts.size() + 6 > cap) break;
            if (ranges[k].empty() || ranges[k].back().view.id != q.view.id) ranges[k].push_back({q.view, static_cast<int>(verts.size()), 0});
            const BakeVertex a{q.x0, q.y0, q.u0, q.v0}, b{q.x1, q.y0, q.u1, q.v0}, c{q.x1, q.y1, q.u1, q.v1}, d{q.x0, q.y1, q.u0, q.v1};
            verts.insert(verts.end(), {a, b, c, a, c, d});
            ranges[k].back().count += 6;
        }
    }
    if (!verts.empty()) {
        sg_write_buffer_desc wd{};
        wd.src.data = {verts.data(), verts.size() * sizeof(BakeVertex)};
        wd.dst.buffer = bakeBuffer_;
        wd.size = verts.size() * sizeof(BakeVertex);
        sg_write_buffer_transient(&wd);
    }
    for (size_t k = 0; k < todo.size(); ++k) {
        Tile& t = *todo[k];
        sg_pass pass{};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0.42f, 0.42f, 0.41f, 1.0f};
        pass.attachments.colors[0] = t.atlasColor;
        sg_begin_pass(&pass);
        if (!ranges[k].empty()) {
            sg_apply_pipeline(bakePipeline_);
            for (const Range& r : ranges[k]) {
                sg_bindings b{};
                b.vertex_buffers[0] = bakeBuffer_;
                b.views[VIEW_tex] = r.view;
                b.samplers[SMP_smp] = roofSampler_;
                sg_apply_bindings(&b);
                sg_draw(r.first, r.count, 1);
            }
        }
        sg_end_pass();
        t.baked = true;
        t.bakedLevel = levels[k];
        t.availableLevel = std::min(t.availableLevel, levels[k]);
    }
}

void BuildingLayer::draw(const Camera& cam, Vec3 light, bool sun) {
    drawn_ = 0;
    if (!initialized_ || failed_ || fade_ <= 0.0) return;
    const double now = seconds();
    building_fs_params_t fs{};
    for (int i = 0; i < BuildingTextures && i < 12; ++i) {
        const BuildingMaterial& m = buildingMaterial(i);
        fs.sizes[i][0] = m.width;
        fs.sizes[i][1] = m.height;
        fs.sizes[i][2] = m.windows;
    }
    bool applied = false;
    for (auto& [key, t] : tiles_) {
        if (!t.ready || t.used + 1 < frame_) continue;
        const double appear = std::clamp((now - t.appear) / 0.6, 0.0, 1.0);
        const double grow = std::min(std::clamp((kRadius - t.distance) / kFade, 0.0, 1.0), appear * appear * (3.0 - 2.0 * appear)) * fade_;
        if (grow <= 0.01) continue;
        Mat4 rot = Mat4::identity();
        rot.at(0, 0) = t.east.x;
        rot.at(1, 0) = t.east.y;
        rot.at(2, 0) = t.east.z;
        rot.at(0, 1) = t.north.x;
        rot.at(1, 1) = t.north.y;
        rot.at(2, 1) = t.north.z;
        rot.at(0, 2) = t.up.x;
        rot.at(1, 2) = t.up.y;
        rot.at(2, 2) = t.up.z;
        building_vs_params_t vs{};
        toFloat(cam.viewProjRel() * translation(t.origin - cam.eye()) * rot, vs.mvp);
        vs.params[0] = static_cast<float>(grow);
        Vec3 l = sun ? normalize(light) : normalize(t.east * -0.45 + t.north * 0.55 + t.up * 0.7);
        float intensity = 0.46f;
        if (sun) {
            const double elevation = std::asin(std::clamp(dot(l, t.up), -1.0, 1.0)) * 180.0 / kPi;
            intensity *= static_cast<float>(std::clamp((elevation + 2.0) / 7.0, 0.0, 1.0));
        }
        fs.light[0] = static_cast<float>(dot(l, t.east));
        fs.light[1] = static_cast<float>(dot(l, t.north));
        fs.light[2] = static_cast<float>(dot(l, t.up));
        fs.light[3] = intensity;
        fs.shading[0] = sun ? 0.52f : 0.64f;
        fs.shading[1] = t.atlasSize > 0 ? 1.0f : 0.0f;
        if (!applied) {
            sg_apply_pipeline(pipeline_);
            applied = true;
        }
        sg_bindings b{};
        b.vertex_buffers[0] = t.vbuf;
        b.index_buffer = t.ibuf;
        b.views[VIEW_facades] = facadeView_;
        b.views[VIEW_roofs] = t.baked ? t.atlasTexture : blankView_;
        b.samplers[SMP_facade_smp] = facadeSampler_;
        b.samplers[SMP_roof_smp] = roofSampler_;
        sg_apply_bindings(&b);
        sg_apply_uniforms(UB_building_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_building_fs_params, SG_RANGE(fs));
        sg_draw(0, t.indexCount, 1);
        ++drawn_;
    }
}

}
