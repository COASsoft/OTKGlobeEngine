#include "render/globe_renderer.hpp"

#include "globe.glsl.h"
#include "sky.glsl.h"
#include "surface.glsl.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ote {

namespace {

constexpr int kGrid = 32;
constexpr int kSurfaceSize = 256;
constexpr int kMinLevel = 2;
constexpr int kMaxLevel = 22;
constexpr size_t kTerrainCapacity = 2000;
constexpr size_t kSurfaceCapacity = 600;
constexpr size_t kImageryCapacity = 700;
constexpr size_t kFreeSurfacePool = 64;
constexpr int kSurfaceRendersPerFrame = 12;
constexpr size_t kUploadsPerFrame = 16;
constexpr size_t kSurfaceBufferBytes = 4 * 1024 * 1024;
constexpr double kRetrySeconds = 30.0;
constexpr int kBaseZoom = 2;

double spanDeg(int level) { return 180.0 / static_cast<double>(1 << level); }

double mercTileLat(int z, int y) {
    double n = kPi * (1.0 - 2.0 * y / static_cast<double>(1 << z));
    return inverseMercatorY(n);
}

}

GlobeRenderer::GlobeRenderer() = default;

GlobeRenderer::~GlobeRenderer() { shutdown(); }

double GlobeRenderer::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

GlobeRenderer::Bounds GlobeRenderer::bounds(const QuadKey& k) {
    double s = spanDeg(k.level);
    double west = -180.0 + k.x * s;
    double north = 90.0 - k.y * s;
    return {west, north - s, west + s, north};
}

bool GlobeRenderer::init() {
    if (ready_) return true;
    sg_backend backend = sg_query_backend();
    flipSurface_ = backend == SG_BACKEND_D3D11 || backend == SG_BACKEND_METAL_IOS ||
                   backend == SG_BACKEND_METAL_MACOS || backend == SG_BACKEND_METAL_SIMULATOR ||
                   backend == SG_BACKEND_WGPU;

    std::vector<std::uint16_t> idx;
    idx.reserve(kGrid * kGrid * 6);
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i) {
            std::uint16_t a = static_cast<std::uint16_t>(j * (kGrid + 1) + i);
            std::uint16_t b = static_cast<std::uint16_t>(a + 1);
            std::uint16_t c = static_cast<std::uint16_t>(a + kGrid + 1);
            std::uint16_t d = static_cast<std::uint16_t>(c + 1);
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    sg_buffer_desc ib{};
    ib.usage.index_buffer = true;
    ib.data = {idx.data(), idx.size() * sizeof(std::uint16_t)};
    indexBuffer_ = sg_make_buffer(&ib);

    const float tri[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    sg_buffer_desc sb{};
    sb.usage.vertex_buffer = true;
    sb.data = SG_RANGE(tri);
    skyBuffer_ = sg_make_buffer(&sb);

    sg_buffer_desc vb{};
    vb.usage.vertex_buffer = true;
    vb.usage.write_transient = true;
    vb.size = kSurfaceBufferBytes;
    surfaceBuffer_ = sg_make_buffer(&vb);

    const std::uint8_t ocean[4] = {10, 30, 62, 255};
    sg_image_desc fd{};
    fd.width = 1;
    fd.height = 1;
    fd.pixel_format = SG_PIXELFORMAT_RGBA8;
    fd.data.mip_levels[0] = SG_RANGE(ocean);
    fallbackImage_ = sg_make_image(&fd);
    sg_view_desc fv{};
    fv.texture.image = fallbackImage_;
    fallbackView_ = sg_make_view(&fv);

    sg_sampler_desc smp{};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_ = sg_make_sampler(&smp);
    imagerySampler_ = sg_make_sampler(&smp);

    globeShader_ = sg_make_shader(globe_shader_desc(backend));
    surfaceShader_ = sg_make_shader(surface_shader_desc(backend));
    skyShader_ = sg_make_shader(sky_shader_desc(backend));

    sg_pipeline_desc gp{};
    gp.shader = globeShader_;
    gp.layout.attrs[ATTR_globe_position].format = SG_VERTEXFORMAT_FLOAT3;
    gp.layout.attrs[ATTR_globe_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    gp.index_type = SG_INDEXTYPE_UINT16;
    gp.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    gp.depth.write_enabled = true;
    gp.cull_mode = SG_CULLMODE_NONE;
    globePipeline_ = sg_make_pipeline(&gp);

    sg_pipeline_desc sp{};
    sp.shader = surfaceShader_;
    sp.layout.attrs[ATTR_surface_position].format = SG_VERTEXFORMAT_FLOAT2;
    sp.layout.attrs[ATTR_surface_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    sp.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    sp.colors[0].blend.enabled = true;
    sp.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    sp.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    sp.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    sp.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    sp.depth.pixel_format = SG_PIXELFORMAT_NONE;
    sp.sample_count = 1;
    surfacePipeline_ = sg_make_pipeline(&sp);

    sg_pipeline_desc kp{};
    kp.shader = skyShader_;
    kp.layout.attrs[ATTR_sky_position].format = SG_VERTEXFORMAT_FLOAT2;
    kp.depth.compare = SG_COMPAREFUNC_ALWAYS;
    kp.depth.write_enabled = false;
    skyPipeline_ = sg_make_pipeline(&kp);

    ready_ = true;
    return true;
}

void GlobeRenderer::shutdown() {
    if (!ready_) return;
    if (sg_isvalid()) {
        for (auto& [k, t] : terrain_) sg_destroy_buffer(t.vbuf);
        auto dropSurface = [](SurfaceTile& s) {
            sg_destroy_view(s.texture);
            sg_destroy_view(s.attachment);
            sg_destroy_image(s.image);
        };
        for (auto& [k, s] : surface_) dropSurface(s);
        for (auto& s : freeSurfaces_) dropSurface(s);
        for (auto& [k, t] : imagery_) {
            sg_destroy_view(t.view);
            sg_destroy_image(t.image);
        }
        sg_destroy_pipeline(globePipeline_);
        sg_destroy_pipeline(surfacePipeline_);
        sg_destroy_pipeline(skyPipeline_);
        sg_destroy_shader(globeShader_);
        sg_destroy_shader(surfaceShader_);
        sg_destroy_shader(skyShader_);
        sg_destroy_sampler(sampler_);
        sg_destroy_sampler(imagerySampler_);
        sg_destroy_view(fallbackView_);
        sg_destroy_image(fallbackImage_);
        sg_destroy_buffer(indexBuffer_);
        sg_destroy_buffer(skyBuffer_);
        sg_destroy_buffer(surfaceBuffer_);
    }
    terrain_.clear();
    surface_.clear();
    freeSurfaces_.clear();
    imagery_.clear();
    ready_ = false;
}

void GlobeRenderer::setLayers(const std::vector<ImageryLayer>& layers) {
    layers_ = layers;
    loader_.setLayers(layers);
    for (auto& [k, t] : imagery_) {
        sg_destroy_view(t.view);
        sg_destroy_image(t.image);
    }
    imagery_.clear();
    failed_.clear();
    for (auto& [k, s] : surface_) {
        s.complete = false;
        s.version = -1;
    }
    ++imageryVersion_;
    busy_ = true;
}

GlobeRenderer::TerrainTile& GlobeRenderer::terrain(const QuadKey& k) {
    auto it = terrain_.find(k);
    if (it != terrain_.end()) return it->second;
    Bounds b = bounds(k);
    TerrainTile t;
    t.center = geodeticToEcef(0.5 * (b.south + b.north), 0.5 * (b.west + b.east), 0.0);
    std::vector<float> v;
    v.reserve((kGrid + 1) * (kGrid + 1) * 5);
    for (int j = 0; j <= kGrid; ++j) {
        double lat = b.south + (b.north - b.south) * j / kGrid;
        for (int i = 0; i <= kGrid; ++i) {
            double lon = b.west + (b.east - b.west) * i / kGrid;
            Vec3 p = geodeticToEcef(lat, lon, 0.0) - t.center;
            v.insert(v.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                               static_cast<float>(i) / kGrid, static_cast<float>(j) / kGrid});
        }
    }
    sg_buffer_desc d{};
    d.usage.vertex_buffer = true;
    d.data = {v.data(), v.size() * sizeof(float)};
    t.vbuf = sg_make_buffer(&d);
    return terrain_.emplace(k, t).first->second;
}

GlobeRenderer::SurfaceTile* GlobeRenderer::acquireSurface(const QuadKey& k) {
    auto it = surface_.find(k);
    if (it != surface_.end()) return &it->second;
    SurfaceTile s;
    if (!freeSurfaces_.empty()) {
        s = freeSurfaces_.back();
        freeSurfaces_.pop_back();
    } else {
        sg_image_desc d{};
        d.usage.color_attachment = true;
        d.width = kSurfaceSize;
        d.height = kSurfaceSize;
        d.pixel_format = SG_PIXELFORMAT_RGBA8;
        d.sample_count = 1;
        s.image = sg_make_image(&d);
        sg_view_desc a{};
        a.color_attachment.image = s.image;
        s.attachment = sg_make_view(&a);
        sg_view_desc t{};
        t.texture.image = s.image;
        s.texture = sg_make_view(&t);
    }
    s.complete = false;
    s.version = -1;
    return &surface_.emplace(k, s).first->second;
}

void GlobeRenderer::traverse(const Camera& cam, const QuadKey& k, std::vector<DrawTile>& out) {
    ++visits_;
    Bounds b = bounds(k);
    double clat = 0.5 * (b.south + b.north), clon = 0.5 * (b.west + b.east);
    Vec3 c = geodeticToEcef(clat, clon, 0.0);
    double r = 0;
    for (double la : {b.south, clat, b.north})
        for (double lo : {b.west, clon, b.east}) r = std::max(r, length(geodeticToEcef(la, lo, 0.0) - c));
    r *= 1.02;

    Vec3 eye = cam.eye();
    Vec3 rel = c - eye;
    double dist = length(rel);
    if (k.level >= kMinLevel) {
        for (int p = 0; p < 4; ++p) {
            const double* pl = &frustum_[p * 4];
            if (pl[0] * rel.x + pl[1] * rel.y + pl[2] * rel.z + pl[3] < -r) return;
        }
        double eyeR2 = dot(eye, eye), R = kWgs84B;
        double horizonEye = std::sqrt(std::max(0.0, eyeR2 - R * R));
        double outer = length(c) + r;
        double horizonTile = std::sqrt(std::max(0.0, outer * outer - R * R));
        if (dist - r > horizonEye + horizonTile) return;
    }

    double d = std::max(dist - r, 1.0);
    double tileMeters = spanDeg(k.level) * kDegToRad * kWgs84A;
    double texel = tileMeters / kSurfaceSize;
    double threshold = std::max(1.0, dpiScale_ * 0.6) / std::max(imageryScale_, 0.1);
    bool refine = k.level < kMinLevel ||
                  (k.level < kMaxLevel && visits_ < 20000 && texel > cam.pixelSizeAt(d) * threshold);
    if (refine) {
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) traverse(cam, {k.level + 1, k.x * 2 + dx, k.y * 2 + dy}, out);
        return;
    }
    out.push_back({k, dist});
}

void GlobeRenderer::processLoads() {
    for (auto& t : loader_.take(kUploadsPerFrame)) {
        if (t.ok) {
            sg_image_desc d{};
            d.width = t.width;
            d.height = t.height;
            d.pixel_format = SG_PIXELFORMAT_RGBA8;
            d.data.mip_levels[0] = {t.rgba.data(), t.rgba.size()};
            ImageryTexture tex;
            tex.image = sg_make_image(&d);
            sg_view_desc v{};
            v.texture.image = tex.image;
            tex.view = sg_make_view(&v);
            tex.used = frame_;
            auto old = imagery_.find(t.id);
            if (old != imagery_.end()) {
                sg_destroy_view(old->second.view);
                sg_destroy_image(old->second.image);
            }
            imagery_[t.id] = tex;
            failed_.erase(t.id);
        } else {
            failed_[t.id] = t.permanent ? std::numeric_limits<double>::infinity() : now() + kRetrySeconds;
        }
        ++imageryVersion_;
    }
}

void GlobeRenderer::evict() {
    if (imagery_.size() > kImageryCapacity) {
        std::vector<std::pair<std::uint64_t, TileId>> c;
        for (auto& [k, t] : imagery_)
            if (k.z > kBaseZoom && t.used + 1 < frame_) c.push_back({t.used, k});
        std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && imagery_.size() > kImageryCapacity; ++i) {
            auto it = imagery_.find(c[i].second);
            sg_destroy_view(it->second.view);
            sg_destroy_image(it->second.image);
            imagery_.erase(it);
        }
    }
    if (surface_.size() > kSurfaceCapacity) {
        std::vector<std::pair<std::uint64_t, QuadKey>> c;
        for (auto& [k, s] : surface_)
            if (s.used + 1 < frame_) c.push_back({s.used, k});
        std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && surface_.size() > kSurfaceCapacity; ++i) {
            auto it = surface_.find(c[i].second);
            if (freeSurfaces_.size() < kFreeSurfacePool) {
                freeSurfaces_.push_back(it->second);
            } else {
                sg_destroy_view(it->second.texture);
                sg_destroy_view(it->second.attachment);
                sg_destroy_image(it->second.image);
            }
            surface_.erase(it);
        }
    }
    if (terrain_.size() > kTerrainCapacity) {
        std::vector<std::pair<std::uint64_t, QuadKey>> c;
        for (auto& [k, t] : terrain_)
            if (t.used < frame_) c.push_back({t.used, k});
        std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && terrain_.size() > kTerrainCapacity; ++i) {
            auto it = terrain_.find(c[i].second);
            sg_destroy_buffer(it->second.vbuf);
            terrain_.erase(it);
        }
    }
}

void GlobeRenderer::want(const TileId& id, double priority) {
    requests_.emplace_back(id, priority);
}

void GlobeRenderer::requestBase() {
    double t = now();
    const int layerCount = static_cast<int>(layers_.size());
    for (int l = 0; l < layerCount; ++l) {
        const int first = std::max(layers_[l].minZoom, 0);
        for (int z = first; z <= kBaseZoom; ++z) {
            const int n = 1 << z;
            for (int i = 0; i < n * n; ++i) {
                TileId id{l, z, i % n, i / n};
                if (imagery_.find(id) != imagery_.end()) continue;
                auto f = failed_.find(id);
                if (f != failed_.end() && f->second > t) continue;
                want(id, -1.0 + z * 0.01);
            }
        }
    }
}

bool GlobeRenderer::buildSurfaceJob(const QuadKey& k, double priority, SurfaceJob* job, std::vector<float>* verts) {
    Bounds b = bounds(k);
    double span = b.north - b.south;
    double t = now();
    bool complete = true;
    double south = std::max(b.south, -kMercatorMaxLat), north = std::min(b.north, kMercatorMaxLat);
    if (south >= north) return true;

    for (int l = 0; l < static_cast<int>(layers_.size()); ++l) {
        const ImageryLayer& layer = layers_[l];
        int z = std::min(k.level + 1, layer.maxZoom);
        if (z < layer.minZoom) continue;
        int n = 1 << z;
        double eps = 1e-9;
        int x0 = std::clamp(static_cast<int>(std::floor((b.west + 180.0) / 360.0 * n)), 0, n - 1);
        int x1 = std::clamp(static_cast<int>(std::floor((b.east + 180.0) / 360.0 * n - eps)), 0, n - 1);
        int y0 = std::clamp(static_cast<int>(std::floor((1.0 - mercatorY(north) / kPi) * 0.5 * n)), 0, n - 1);
        int y1 = std::clamp(static_cast<int>(std::floor((1.0 - mercatorY(south) / kPi) * 0.5 * n - eps)), 0, n - 1);
        for (int ty = y0; ty <= y1; ++ty)
            for (int tx = x0; tx <= x1; ++tx) {
                TileId id{l, z, tx, ty};
                ImageryTexture* tex = nullptr;
                TileId src = id;
                auto it = imagery_.find(id);
                if (it != imagery_.end()) {
                    tex = &it->second;
                } else {
                    auto f = failed_.find(id);
                    bool failedNow = f != failed_.end();
                    if (!failedNow || f->second <= t) {
                        complete = false;
                        want(id, priority);
                    } else if (f->second != std::numeric_limits<double>::infinity()) {
                        complete = false;
                    }
                    for (int dz = 1; dz <= z; ++dz) {
                        TileId a{l, z - dz, tx >> dz, ty >> dz};
                        auto ai = imagery_.find(a);
                        if (ai != imagery_.end()) {
                            tex = &ai->second;
                            src = a;
                            break;
                        }
                    }
                }
                if (!tex) continue;
                tex->used = frame_;
                if (!job) continue;

                int sn = 1 << src.z;
                double sw = src.x * 360.0 / sn - 180.0, se = (src.x + 1) * 360.0 / sn - 180.0;
                double snorth = mercTileLat(src.z, src.y), ssouth = mercTileLat(src.z, src.y + 1);
                double my0 = mercatorY(snorth), my1 = mercatorY(ssouth);

                double tw = tx * 360.0 / n - 180.0, te = (tx + 1) * 360.0 / n - 180.0;
                double tn = mercTileLat(z, ty), ts = mercTileLat(z, ty + 1);
                double w = std::max(b.west, tw), e = std::min(b.east, te);
                double s = std::max(south, ts), nn = std::min(north, tn);
                if (w >= e || s >= nn) continue;

                int rows = z < 9 ? 8 : 1;
                int first = static_cast<int>(verts->size() / 4);
                auto emit = [&](double lon, double lat) {
                    float px = static_cast<float>((lon - b.west) / span * 2.0 - 1.0);
                    float py = static_cast<float>((lat - b.south) / span * 2.0 - 1.0);
                    float u = static_cast<float>((lon - sw) / (se - sw));
                    float v = static_cast<float>((my0 - mercatorY(lat)) / (my0 - my1));
                    verts->insert(verts->end(), {px, py, u, v});
                };
                for (int r = 0; r < rows; ++r) {
                    double la0 = s + (nn - s) * r / rows, la1 = s + (nn - s) * (r + 1) / rows;
                    emit(w, la0);
                    emit(e, la0);
                    emit(e, la1);
                    emit(w, la0);
                    emit(e, la1);
                    emit(w, la1);
                }
                job->draws.push_back({tex->view, layer.opacity, first, rows * 6});
            }
    }
    if (job) job->complete = complete;
    return complete;
}

void GlobeRenderer::render(Camera& cam, const sg_swapchain* swapchain, const sg_attachments* attachments, int width,
                           int height) {
    if (!ready_) return;
    if (!trace_)
        if (const char* path = std::getenv("OTE_TRACE")) trace_ = std::fopen(path, "w");
    auto mark = [&](const char* m) {
        if (trace_) {
            std::fprintf(trace_, "%s %.3f lat %g lon %g gsd %g tilt %g az %g w %d h %d\n", m, now(), cam.focus.lat, cam.focus.lon, cam.gsd, cam.tilt, cam.azimuth, cam.width, cam.height);
            std::fflush(trace_);
        }
    };
    mark("begin");
    ++frame_;
    requests_.clear();
    processLoads();
    mark("loads");
    evict();
    mark("evict");
    requestBase();
    mark("base");

    cam.width = std::max(width, 1);
    cam.height = std::max(height, 1);
    sg_backend backend = sg_query_backend();
    cam.zeroToOneDepth = backend != SG_BACKEND_GLCORE && backend != SG_BACKEND_GLES3;
    cam.update();
    mark("camera");

    const Mat4& m = cam.viewProjRel();
    for (int p = 0; p < 4; ++p) {
        int row = p / 2;
        double sign = (p % 2 == 0) ? 1.0 : -1.0;
        double a = m.at(3, 0) + sign * m.at(row, 0);
        double b = m.at(3, 1) + sign * m.at(row, 1);
        double c = m.at(3, 2) + sign * m.at(row, 2);
        double d = m.at(3, 3) + sign * m.at(row, 3);
        double len = std::sqrt(a * a + b * b + c * c);
        frustum_[p * 4 + 0] = a / len;
        frustum_[p * 4 + 1] = b / len;
        frustum_[p * 4 + 2] = c / len;
        frustum_[p * 4 + 3] = d / len;
    }

    std::vector<DrawTile> tiles;
    visits_ = 0;
    auto tTraverse = now();
    traverse(cam, {0, 0, 0}, tiles);
    traverse(cam, {0, 1, 0}, tiles);
    std::sort(tiles.begin(), tiles.end(), [](const DrawTile& a, const DrawTile& b) { return a.distance < b.distance; });

    std::vector<std::pair<int, size_t>> work;
    for (size_t i = 0; i < tiles.size(); ++i) {
        auto it = surface_.find(tiles[i].key);
        if (it == surface_.end()) {
            work.push_back({0, i});
        } else {
            it->second.used = frame_;
            if (!it->second.complete && it->second.version != imageryVersion_) work.push_back({1, i});
            else if (!it->second.complete) buildSurfaceJob(tiles[i].key, tiles[i].distance, nullptr, nullptr);
        }
    }
    std::stable_sort(work.begin(), work.end(), [](auto& a, auto& b) { return a.first < b.first; });

    if (trace_) {
        std::fprintf(trace_, "frame %llu visits %d tiles %zu work %zu traverse %.4f eye %.0f near %.1f far %.0f\n",
                     static_cast<unsigned long long>(frame_), visits_, tiles.size(), work.size(), now() - tTraverse,
                     cam.eyeAltitude(), cam.nearPlane(), cam.farPlane());
        std::fflush(trace_);
    }
    std::vector<SurfaceJob> jobs;
    std::vector<float> verts;
    int renders = 0;
    for (auto& [kind, i] : work) {
        if (renders >= kSurfaceRendersPerFrame || verts.size() * sizeof(float) > kSurfaceBufferBytes / 2) {
            buildSurfaceJob(tiles[i].key, tiles[i].distance, nullptr, nullptr);
            continue;
        }
        SurfaceJob job;
        job.key = tiles[i].key;
        buildSurfaceJob(job.key, tiles[i].distance, &job, &verts);
        jobs.push_back(std::move(job));
        ++renders;
    }
    {
        std::sort(requests_.begin(), requests_.end(), [](const auto& a, const auto& b) {
            const TileId& x = a.first;
            const TileId& y = b.first;
            if (x.layer != y.layer) return x.layer < y.layer;
            if (x.z != y.z) return x.z < y.z;
            if (x.x != y.x) return x.x < y.x;
            if (x.y != y.y) return x.y < y.y;
            return a.second < b.second;
        });
        requests_.erase(std::unique(requests_.begin(), requests_.end(),
                                    [](const auto& a, const auto& b) { return a.first == b.first; }),
                        requests_.end());
        loader_.submit(requests_);
    }
    if (verts.size() * sizeof(float) > kSurfaceBufferBytes) verts.resize(kSurfaceBufferBytes / sizeof(float));

    if (!verts.empty()) {
        sg_write_buffer_desc wd{};
        wd.src.data = {verts.data(), verts.size() * sizeof(float)};
        wd.dst.buffer = surfaceBuffer_;
        wd.size = verts.size() * sizeof(float);
        sg_write_buffer_transient(&wd);
    }

    for (auto& job : jobs) {
        SurfaceTile* s = acquireSurface(job.key);
        s->used = frame_;
        sg_pass pass{};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {10 / 255.0f, 30 / 255.0f, 62 / 255.0f, 1.0f};
        pass.attachments.colors[0] = s->attachment;
        sg_begin_pass(&pass);
        if (!job.draws.empty()) {
            sg_apply_pipeline(surfacePipeline_);
            for (auto& d : job.draws) {
                if ((d.first + d.count) * 4 * sizeof(float) > kSurfaceBufferBytes) continue;
                sg_bindings bind{};
                bind.vertex_buffers[0] = surfaceBuffer_;
                bind.views[VIEW_tex] = d.texture;
                bind.samplers[SMP_smp] = imagerySampler_;
                sg_apply_bindings(&bind);
                surface_fs_params_t fsp{};
                fsp.tint[3] = d.opacity;
                sg_apply_uniforms(UB_surface_fs_params, SG_RANGE(fsp));
                sg_draw(d.first, d.count, 1);
            }
        }
        sg_end_pass();
        s->complete = job.complete;
        s->version = imageryVersion_;
    }

    sg_pass main{};
    main.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    main.action.colors[0].clear_value = {0, 0, 0, 1};
    main.action.depth.load_action = SG_LOADACTION_CLEAR;
    main.action.depth.clear_value = 1.0f;
    if (swapchain) main.swapchain = *swapchain;
    if (attachments) main.attachments = *attachments;
    sg_begin_pass(&main);

    sg_apply_pipeline(skyPipeline_);
    sg_bindings sb{};
    sb.vertex_buffers[0] = skyBuffer_;
    sg_apply_bindings(&sb);
    sky_fs_params_t sky{};
    toFloat(cam.invViewProjRel(), sky.inv_vp);
    Vec3 center = -cam.eye();
    sky.center[0] = static_cast<float>(center.x);
    sky.center[1] = static_cast<float>(center.y);
    sky.center[2] = static_cast<float>(center.z);
    sky.radii[0] = 6356752.0f;
    sky.radii[1] = 6356752.0f + 100000.0f;
    sky.radii[2] = cam.eyeAltitude() < 100000.0 ? 1.0f : 0.0f;
    sg_apply_uniforms(UB_sky_fs_params, SG_RANGE(sky));
    sg_draw(0, 3, 1);

    sg_apply_pipeline(globePipeline_);
    globe_fs_params_t gfs{};
    gfs.flags[0] = flipSurface_ ? 1.0f : 0.0f;
    sg_apply_uniforms(UB_globe_fs_params, SG_RANGE(gfs));
    int maxLevel = 0;
    for (auto& dt : tiles) {
        TerrainTile& tt = terrain(dt.key);
        tt.used = frame_;
        maxLevel = std::max(maxLevel, dt.key.level);
        sg_view view = fallbackView_;
        float uvx[4] = {1, 1, 0, 0};
        for (int k = 0; k <= dt.key.level; ++k) {
            QuadKey a{dt.key.level - k, dt.key.x >> k, dt.key.y >> k};
            auto it = surface_.find(a);
            if (it == surface_.end() || it->second.version < 0) continue;
            it->second.used = frame_;
            view = it->second.texture;
            double sc = 1.0 / (1 << k);
            int rx = dt.key.x - (a.x << k), ry = dt.key.y - (a.y << k);
            uvx[0] = static_cast<float>(sc);
            uvx[1] = static_cast<float>(sc);
            uvx[2] = static_cast<float>(rx * sc);
            uvx[3] = static_cast<float>(((1 << k) - 1 - ry) * sc);
            break;
        }
        Mat4 mvp = cam.viewProjRel() * translation(tt.center - cam.eye());
        globe_vs_params_t vsp{};
        toFloat(mvp, vsp.mvp);
        std::memcpy(vsp.uv_xform, uvx, sizeof(uvx));
        sg_bindings bind{};
        bind.vertex_buffers[0] = tt.vbuf;
        bind.index_buffer = indexBuffer_;
        bind.views[VIEW_tex] = view;
        bind.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&bind);
        sg_apply_uniforms(UB_globe_vs_params, SG_RANGE(vsp));
        sg_draw(0, kGrid * kGrid * 6, 1);
    }
    sg_end_pass();

    bool pendingSurface = false;
    for (auto& dt : tiles) {
        auto it = surface_.find(dt.key);
        if (it == surface_.end() || !it->second.complete) {
            pendingSurface = true;
            break;
        }
    }
    stats_.terrainTiles = static_cast<int>(tiles.size());
    stats_.surfaceRenders = renders;
    stats_.imageryTextures = static_cast<int>(imagery_.size());
    stats_.pendingRequests = loader_.pending();
    stats_.maxLevel = maxLevel;
    busy_ = pendingSurface || stats_.pendingRequests > 0 || renders > 0;
}

}
