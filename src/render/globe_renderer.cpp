#include "render/globe_renderer.hpp"

#if defined(__ANDROID__)
#include <android/log.h>
#endif
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#define OTE_GL_MIPS 1
#elif defined(SOKOL_GLCORE) && defined(__linux__)
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#define OTE_GL_MIPS 1
#endif
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#if defined(SOKOL_D3D11)
#include <d3d11.h>
#include <dxgi.h>
#endif
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

#include "feature/grid.hpp"

#include "globe.glsl.h"
#include "mip.glsl.h"
#include "vector.glsl.h"
#include "sky.glsl.h"
#include "surface.glsl.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace ote {

namespace {

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr double budgetMs = 6.0;
constexpr double prefetchBudgetMs = 2.0;
#else
constexpr double budgetMs = 8.0;
constexpr double prefetchBudgetMs = 3.0;
#endif
constexpr int kGrid = 32;
constexpr int kPosts = kGrid + 1;
constexpr int kVertexCount = kPosts * kPosts + 4 * kPosts;
constexpr int kStride = 7;
constexpr int kIndexCount = kGrid * kGrid * 6 + 4 * kGrid * 6;
constexpr int kSurfaceSize = 256;
constexpr int kMinLevel = 2;
constexpr int kMaxLevel = 22;
constexpr int kKeepLevel = 3;
constexpr int kSurfaceRendersPerFrame = 32;
constexpr int kTerrainBuildsPerFrame = 32;
constexpr size_t kUploadsPerFrame = 48;
constexpr size_t kSurfaceBufferBytes = 4 * 1024 * 1024;
constexpr double kRetrySeconds = 30.0;
constexpr int kBaseZoom = 3;
constexpr double kSkirtMax = 20000.0;
constexpr double kUnknownMaxHeight = 4500.0;

double spanDeg(int level) { return 180.0 / static_cast<double>(1 << level); }

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
    if (!budgetSet_) {
        double ram = physicalMemoryMb();
        double vram = videoMemoryMb();
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
        if (ram > 0) setMemoryBudget(static_cast<int>(std::clamp(ram * 0.10, 192.0, 768.0)));
#else
        if (vram > 0) setMemoryBudget(static_cast<int>(std::clamp(vram * 0.35, 256.0, 1536.0)));
        else if (ram > 0) setMemoryBudget(static_cast<int>(std::clamp(ram * 0.03, 256.0, 768.0)));
#endif
    }
    fitPools();
    sg_backend backend = sg_query_backend();
    holeMarker_ = std::getenv("OTE_HOLES") != nullptr;
    if (const char* pp = std::getenv("OTE_PROFILE")) {
        profiling_ = true;
        profilePath_ = pp;
    }
#if defined(OTE_GL_MIPS)
    glMips_ = backend == SG_BACKEND_GLES3 || backend == SG_BACKEND_GLCORE;
    if (const char* v = std::getenv("OTE_BOX_MIPS")) glMips_ = glMips_ && std::atoi(v) == 0;
#endif
    flipSurface_ = backend == SG_BACKEND_D3D11 || backend == SG_BACKEND_METAL_IOS ||
                   backend == SG_BACKEND_METAL_MACOS || backend == SG_BACKEND_METAL_SIMULATOR ||
                   backend == SG_BACKEND_WGPU;

    std::vector<std::uint16_t> idx;
    idx.reserve(kIndexCount);
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i) {
            auto a = static_cast<std::uint16_t>(j * kPosts + i);
            auto b = static_cast<std::uint16_t>(a + 1);
            auto c = static_cast<std::uint16_t>(a + kPosts);
            auto d = static_cast<std::uint16_t>(c + 1);
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    const int skirtBase = kPosts * kPosts;
    auto edge = [&](int e, int i) -> int {
        switch (e) {
        case 0: return i;
        case 1: return kGrid * kPosts + i;
        case 2: return i * kPosts;
        default: return i * kPosts + kGrid;
        }
    };
    for (int e = 0; e < 4; ++e)
        for (int i = 0; i < kGrid; ++i) {
            auto a = static_cast<std::uint16_t>(edge(e, i));
            auto b = static_cast<std::uint16_t>(edge(e, i + 1));
            auto as = static_cast<std::uint16_t>(skirtBase + e * kPosts + i);
            auto bs = static_cast<std::uint16_t>(skirtBase + e * kPosts + i + 1);
            idx.insert(idx.end(), {a, b, bs, a, bs, as});
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
    featureBuffer_ = sg_make_buffer(&vb);
    features_.init();
    models_.init();
    points_.init();

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
    imagerySampler_ = sg_make_sampler(&smp);
    mipSampler_ = sg_make_sampler(&smp);
    smp.mipmap_filter = SG_FILTER_LINEAR;
    smp.max_anisotropy = 8;
    sampler_ = sg_make_sampler(&smp);

    globeShader_ = sg_make_shader(globe_shader_desc(backend));
    surfaceShader_ = sg_make_shader(surface_shader_desc(backend));
    skyShader_ = sg_make_shader(sky_shader_desc(backend));

    sg_pipeline_desc gp{};
    gp.shader = globeShader_;
    gp.layout.attrs[ATTR_globe_position].format = SG_VERTEXFORMAT_FLOAT3;
    gp.layout.attrs[ATTR_globe_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    gp.layout.attrs[ATTR_globe_shade].format = SG_VERTEXFORMAT_FLOAT;
    gp.layout.attrs[ATTR_globe_height].format = SG_VERTEXFORMAT_FLOAT;
    gp.index_type = SG_INDEXTYPE_UINT16;
    gp.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    gp.depth.write_enabled = true;
    gp.cull_mode = SG_CULLMODE_NONE;
    globePipeline_ = sg_make_pipeline(&gp);
    gp.depth.write_enabled = false;
    gp.colors[0].blend.enabled = true;
    gp.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    gp.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    globeBlendPipeline_ = sg_make_pipeline(&gp);

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

    vfillShader_ = sg_make_shader(vfill_shader_desc(backend));
    vlineShader_ = sg_make_shader(vline_shader_desc(backend));
    auto vecPipe = [&](sg_shader shd, bool line) {
        sg_pipeline_desc d{};
        d.shader = shd;
        if (line) {
            d.layout.attrs[ATTR_vline_position].format = SG_VERTEXFORMAT_FLOAT2;
            d.layout.attrs[ATTR_vline_other].format = SG_VERTEXFORMAT_FLOAT2;
            d.layout.attrs[ATTR_vline_extra].format = SG_VERTEXFORMAT_FLOAT2;
            d.layout.attrs[ATTR_vline_color0].format = SG_VERTEXFORMAT_FLOAT4;
        } else {
            d.layout.attrs[ATTR_vfill_position].format = SG_VERTEXFORMAT_FLOAT2;
            d.layout.attrs[ATTR_vfill_color0].format = SG_VERTEXFORMAT_FLOAT4;
        }
        d.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        d.colors[0].blend.enabled = true;
        d.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        d.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        d.depth.pixel_format = SG_PIXELFORMAT_NONE;
        d.sample_count = 1;
        return sg_make_pipeline(&d);
    };
    vfillPipeline_ = vecPipe(vfillShader_, false);
    vlinePipeline_ = vecPipe(vlineShader_, true);

    mipShader_ = sg_make_shader(mip_shader_desc(backend));
    sg_pipeline_desc mp{};
    mp.shader = mipShader_;
    mp.layout.attrs[ATTR_mip_position].format = SG_VERTEXFORMAT_FLOAT2;
    mp.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    mp.depth.pixel_format = SG_PIXELFORMAT_NONE;
    mp.sample_count = 1;
    mipPipeline_ = sg_make_pipeline(&mp);
    sg_image_desc sd{};
    sd.usage.color_attachment = true;
    sd.width = kSurfaceSize;
    sd.height = kSurfaceSize;
    sd.pixel_format = SG_PIXELFORMAT_RGBA8;
    sd.sample_count = 1;
    scratch_ = sg_make_image(&sd);
    sg_view_desc sa{};
    sa.color_attachment.image = scratch_;
    scratchAttachment_ = sg_make_view(&sa);
    sg_view_desc st{};
    st.texture.image = scratch_;
    scratchTexture_ = sg_make_view(&st);

    ready_ = true;
    return true;
}

void GlobeRenderer::shutdown() {
    if (!ready_) return;
    if (sg_isvalid()) {
        for (auto& [k, t] : terrain_) sg_destroy_buffer(t.vbuf);
        for (auto& [k, s] : surface_) dropSurface(s);
        for (auto& s : freeSurfaces_) dropSurface(s);
        for (auto& [k, t] : imagery_) dropImagery(t);
        sg_destroy_pipeline(globePipeline_);
        sg_destroy_pipeline(globeBlendPipeline_);
        sg_destroy_pipeline(surfacePipeline_);
        sg_destroy_pipeline(skyPipeline_);
        sg_destroy_pipeline(mipPipeline_);
        sg_destroy_pipeline(vfillPipeline_);
        sg_destroy_pipeline(vlinePipeline_);
        sg_destroy_shader(vfillShader_);
        sg_destroy_shader(vlineShader_);
        sg_destroy_shader(mipShader_);
        sg_destroy_sampler(mipSampler_);
        sg_destroy_view(scratchAttachment_);
        sg_destroy_view(scratchTexture_);
        sg_destroy_image(scratch_);
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
        sg_destroy_buffer(featureBuffer_);
        for (auto& [id, o] : overlays_) {
            sg_destroy_view(o.view);
            sg_destroy_image(o.image);
        }
        overlays_.clear();
        features_.shutdown();
        tilesets_.clear();
        models_.shutdown();
        points_.shutdown();
    }
    terrain_.clear();
    surface_.clear();
    freeSurfaces_.clear();
    imagery_.clear();
    ready_ = false;
}

void GlobeRenderer::setCacheDirectory(const std::string& dir) {
    std::string path = dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    path += "tiles.sqlite";
    if (cache_.open(path)) {
        loader_.setCache(&cache_);
        if (!layers_.empty()) setLayers(std::vector<ImageryLayer>(layers_));
        if (!elevation_.sources().empty()) setElevation(std::vector<ElevationLayer>(elevation_.sources()));
    }
}

void GlobeRenderer::setMemoryBudget(int megabytes) {
    budgetSet_ = true;
    double mb = std::clamp(megabytes, 96, 8192);
    surfaceCapacity_ = static_cast<size_t>(mb * 0.60 / 0.35);
    imageryCapacity_ = static_cast<size_t>(mb * 0.20 / 0.262);
    terrainCapacity_ = static_cast<size_t>(mb * 0.20 / 0.034);
    freeSurfacePool_ = std::max<size_t>(16, surfaceCapacity_ / 16);
    fitPools();
}

void GlobeRenderer::fitPools() {
    if (!sg_isvalid()) return;
    sg_desc d = sg_query_desc();
    const size_t buffers = static_cast<size_t>(std::max(d.buffer_pool_size, 128));
    const size_t images = static_cast<size_t>(std::max(d.image_pool_size, 128));
    const size_t views = static_cast<size_t>(std::max(d.view_pool_size, 128));
    const size_t perSurface = 2 * kMips + 1;
    size_t imageRoom = images > 1024 ? images - 512 : images / 2;
    size_t viewRoom = views > 4096 ? views - 2048 : views / 2;
    auto surfaceSlots = [&](size_t cap) { return cap + std::max<size_t>(16, cap / 16); };
    double f = 1.0;
    double imagesNeeded = static_cast<double>(surfaceSlots(surfaceCapacity_) + imageryCapacity_);
    double viewsNeeded = static_cast<double>(surfaceSlots(surfaceCapacity_) * perSurface + imageryCapacity_);
    if (imagesNeeded > imageRoom) f = std::min(f, imageRoom / imagesNeeded);
    if (viewsNeeded > viewRoom) f = std::min(f, viewRoom / viewsNeeded);
    if (f < 1.0) {
        surfaceCapacity_ = std::max<size_t>(64, static_cast<size_t>(surfaceCapacity_ * f));
        imageryCapacity_ = std::max<size_t>(48, static_cast<size_t>(imageryCapacity_ * f));
    }
    freeSurfacePool_ = std::max<size_t>(16, surfaceCapacity_ / 16);
    size_t bufferRoom = buffers > 2048 + 2 * imageryCapacity_ ? buffers - 1024 - 2 * imageryCapacity_ : buffers / 2;
    terrainCapacity_ = std::min(terrainCapacity_, std::max<size_t>(256, bufferRoom));
}

void GlobeRenderer::poolPressure(const char* what) {
    ++invalidResources_;
    size_t terrainBefore = terrainCapacity_, surfaceBefore = surfaceCapacity_, imageryBefore = imageryCapacity_;
    if (std::strcmp(what, "buffer") == 0) terrainCapacity_ = std::max<size_t>(256, std::min(terrainCapacity_, terrain_.size()) * 9 / 10);
    else {
        surfaceCapacity_ = std::max<size_t>(64, std::min(surfaceCapacity_, surface_.size()) * 9 / 10);
        imageryCapacity_ = std::max<size_t>(48, std::min(imageryCapacity_, imagery_.size()) * 9 / 10);
        freeSurfacePool_ = std::max<size_t>(16, surfaceCapacity_ / 16);
        while (freeSurfaces_.size() > freeSurfacePool_) {
            dropSurface(freeSurfaces_.back());
            freeSurfaces_.pop_back();
        }
    }
    diag("pool pressure frame %llu: %s creation failed; terrain cap %zu -> %zu surface cap %zu -> %zu imagery cap %zu -> %zu",
         static_cast<unsigned long long>(frame_), what, terrainBefore, terrainCapacity_, surfaceBefore, surfaceCapacity_, imageryBefore,
         imageryCapacity_);
}

std::uint64_t GlobeRenderer::addTileset(const std::string& uri, double maxSse, Vec3* center) {
    auto t = std::make_unique<Tileset3D>(uri, maxSse);
    if (!t->valid()) return 0;
    if (center) *center = t->center();
    std::uint64_t id = nextTileset_++;
    tilesets_[id] = std::move(t);
    busy_ = true;
    return id;
}

void GlobeRenderer::setOffline(bool offline) {
    bool reconnect = offline_ && !offline;
    offline_ = offline;
    loader_.setOffline(offline);
    if (reconnect) {
        failed_.clear();
        for (auto& [k, s] : surface_)
            if (!s.complete) s.missing.clear();
        busy_ = true;
    }
}

void GlobeRenderer::setLayers(const std::vector<ImageryLayer>& layers) {
    layers_ = layers;
    loader_.setLayers(layers);
    matrices_ = loader_.matrices();
    vectorLayers_ = loader_.vectorLayers();
    for (auto& [k, t] : imagery_) dropImagery(t);
    imagery_.clear();
    failed_.clear();
    for (auto& [k, s] : surface_) {
        s.complete = false;
        s.missing.clear();
    }
    usefulLevel_ = kMaxLevel;
    int best = -1;
    bool vectorOnly = true;
    for (size_t l = 0; l < matrices_.size() && l < layers_.size(); ++l) {
        const TileMatrix& m = matrices_[l];
        if (m.levels.empty()) continue;
        if (l < vectorLayers_.size() && vectorLayers_[l]) continue;
        vectorOnly = false;
        double deg = m.levels.back().resolutionX / m.unitsPerDegree();
        if (deg <= 0) continue;
        best = std::max(best, static_cast<int>(std::ceil(std::log2(180.0 / kSurfaceSize / deg))) + 1);
    }
    if (!vectorOnly && best > 0) usefulLevel_ = std::clamp(best, kMinLevel + 1, kMaxLevel);
    ++imageryVersion_;
    busy_ = true;
}

int GlobeRenderer::maxRefine() const {
    bool needsDetail = !features_.empty() || grid_ || !overlays_.empty();
    return needsDetail ? kMaxLevel : usefulLevel_;
}

void GlobeRenderer::setElevation(const std::vector<ElevationLayer>& layers) {
    elevation_.setSources(layers, cache_.isOpen() ? &cache_ : nullptr, &offline_);
    invalidateTerrain();
}

void GlobeRenderer::setTerrainEnabled(bool enabled) {
    if (terrainEnabled_ == enabled) return;
    terrainEnabled_ = enabled;
    invalidateTerrain();
}

void GlobeRenderer::setTerrainExaggeration(double factor) {
    if (!(factor > 0) || factor == exaggeration_) return;
    exaggeration_ = factor;
    invalidateTerrain();
}

void GlobeRenderer::markDirty(const GeoRect& rect) {
    for (auto& [k, s] : surface_) {
        Bounds b = bounds(k);
        if (rect.intersects({b.west, b.south, b.east, b.north})) {
            s.complete = false;
            s.missing.clear();
        }
    }
    busy_ = true;
}

void GlobeRenderer::setGrid(bool enabled) {
    if (grid_ == enabled) return;
    grid_ = enabled;
    markDirty({-180, -90, 180, 90});
}

std::uint64_t GlobeRenderer::addOverlay(const GeoRect& rect, int width, int height, const std::vector<std::uint8_t>& rgba,
                                        float opacity) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4) return 0;
    Overlay o;
    o.rect = rect;
    o.opacity = opacity;
    sg_image_desc d{};
    d.width = width;
    d.height = height;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = {rgba.data(), static_cast<size_t>(width) * height * 4};
    o.image = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = o.image;
    o.view = sg_make_view(&v);
    std::uint64_t id = nextOverlay_++;
    overlays_[id] = o;
    markDirty(rect);
    return id;
}

bool GlobeRenderer::removeOverlay(std::uint64_t id) {
    auto it = overlays_.find(id);
    if (it == overlays_.end()) return false;
    markDirty(it->second.rect);
    sg_destroy_view(it->second.view);
    sg_destroy_image(it->second.image);
    overlays_.erase(it);
    return true;
}

void GlobeRenderer::invalidateTerrain() {
    ++terrainEpoch_;
    for (auto& [k, t] : terrain_) t.complete = false;
    busy_ = true;
}

double GlobeRenderer::elevationAt(double lat, double lon) const {
    if (!terrainEnabled_) return 0.0;
    double h = elevation_.sample(lat, lon);
    return std::isnan(h) ? 0.0 : h * exaggeration_;
}

void GlobeRenderer::buildTerrain(const QuadKey& k, TerrainTile& t, double priority) {
    Bounds b = bounds(k);
    double span = b.north - b.south;
    double cell = span / kGrid;
    std::vector<float> h;
    bool complete = true;
    const int n = kPosts + 2;
    if (terrainEnabled_ && !elevation_.empty()) {
        complete = elevation_.gridFor(b.west - cell, std::max(b.south - cell, -90.0), b.east + cell,
                                      std::min(b.north + cell, 90.0), n, priority, h);
        for (float& v : h) v = std::isnan(v) ? 0.0f : static_cast<float>(v * exaggeration_);
    } else {
        h.assign(static_cast<size_t>(n) * n, 0.0f);
    }
    float minH = std::numeric_limits<float>::max(), maxH = -std::numeric_limits<float>::max();
    for (int j = 1; j <= kPosts; ++j)
        for (int i = 1; i <= kPosts; ++i) {
            float v = h[static_cast<size_t>(j) * n + i];
            minH = std::min(minH, v);
            maxH = std::max(maxH, v);
        }
    double clat = 0.5 * (b.south + b.north), clon = 0.5 * (b.west + b.east);
    t.center = geodeticToEcef(clat, clon, 0.5 * (minH + maxH));
    t.minH = minH;
    t.maxH = maxH;

    std::vector<float> v;
    v.reserve(static_cast<size_t>(kVertexCount) * kStride);
    std::vector<float> shade(static_cast<size_t>(kPosts) * kPosts);
    for (int j = 0; j < kPosts; ++j) {
        double lat = b.south + span * j / kGrid;
        double dy = 111132.954 * cell;
        double dx = std::max(111319.49 * std::cos(lat * kDegToRad) * cell, 1e-3);
        for (int i = 0; i < kPosts; ++i) {
            double lon = b.west + span * i / kGrid;
            size_t c = static_cast<size_t>(j + 1) * n + (i + 1);
            float hc = h[c];
            double nx = (h[c - 1] - h[c + 1]) / (2.0 * dx);
            double ny = (h[c - n] - h[c + n]) / (2.0 * dy);
            double nz = 1.0 / std::sqrt(nx * nx + ny * ny + 1.0);
            shade[static_cast<size_t>(j) * kPosts + i] = static_cast<float>(nz);
            Vec3 p = geodeticToEcef(lat, lon, hc) - t.center;
            v.insert(v.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                               static_cast<float>(i) / kGrid, static_cast<float>(j) / kGrid,
                               static_cast<float>(nz), hc});
        }
    }
    double edgeDev = 0;
    for (int e = 0; e < 4; ++e) {
        auto edge = [&](int i) {
            int gi = e < 2 ? i : (e == 2 ? 0 : kGrid);
            int gj = e < 2 ? (e == 0 ? 0 : kGrid) : i;
            return static_cast<double>(h[static_cast<size_t>(gj + 1) * n + (gi + 1)]);
        };
        for (int stride : {2, 4, 8})
            for (int i = 0; i <= kGrid; ++i) {
                int a = (i / stride) * stride, z = std::min(a + stride, kGrid);
                if (i == a || z == a) continue;
                double f = static_cast<double>(i - a) / (z - a);
                edgeDev = std::max(edgeDev, std::abs(edge(i) - (edge(a) * (1.0 - f) + edge(z) * f)));
            }
    }
    double skirt = std::clamp(std::max({span * 111320.0 * 0.01, static_cast<double>(maxH - minH) * 0.25, edgeDev * 1.5, 1.0}), 1.0, kSkirtMax) *
                   std::max(1.0, exaggeration_);
    for (int e = 0; e < 4; ++e)
        for (int i = 0; i < kPosts; ++i) {
            int gi = e == 0 ? i : e == 1 ? i : e == 2 ? 0 : kGrid;
            int gj = e == 0 ? 0 : e == 1 ? kGrid : i;
            double lat = b.south + span * gj / kGrid, lon = b.west + span * gi / kGrid;
            float hc = h[static_cast<size_t>(gj + 1) * n + (gi + 1)];
            Vec3 p = geodeticToEcef(lat, lon, hc - skirt) - t.center;
            v.insert(v.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                               static_cast<float>(gi) / kGrid, static_cast<float>(gj) / kGrid,
                               shade[static_cast<size_t>(gj) * kPosts + gi], hc});
        }
    if (t.vbuf.id) sg_destroy_buffer(t.vbuf);
    sg_buffer_desc d{};
    d.usage.vertex_buffer = true;
    d.data = {v.data(), v.size() * sizeof(float)};
    t.vbuf = sg_make_buffer(&d);
    if (sg_query_buffer_state(t.vbuf) != SG_RESOURCESTATE_VALID) {
        poolPressure("buffer");
        if (t.vbuf.id) sg_destroy_buffer(t.vbuf);
        t.vbuf = {};
        t.complete = false;
        return;
    }
    t.complete = complete;
    t.version = elevation_.version() + terrainEpoch_ * 1000000;
}

GlobeRenderer::TerrainTile& GlobeRenderer::terrain(const QuadKey& k) {
    auto it = terrain_.find(k);
    if (it != terrain_.end()) {
        if (sg_query_buffer_state(it->second.vbuf) != SG_RESOURCESTATE_VALID) buildTerrain(k, it->second, 0.0);
        return it->second;
    }
    TerrainTile& t = terrain_[k];
    buildTerrain(k, t, 0.0);
    return t;
}

void GlobeRenderer::dropSurface(SurfaceTile& s) {
    sg_destroy_view(s.texture);
    for (int i = 0; i < kMips; ++i) {
        sg_destroy_view(s.levels[i]);
        sg_destroy_view(s.sources[i]);
    }
    sg_destroy_image(s.image);
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
        d.num_mipmaps = kMips;
        s.image = sg_make_image(&d);
        for (int i = 0; i < kMips; ++i) {
            sg_view_desc a{};
            a.color_attachment.image = s.image;
            a.color_attachment.mip_level = i;
            s.levels[i] = sg_make_view(&a);
            sg_view_desc t{};
            t.texture.image = s.image;
            t.texture.mip_levels.base = i;
            t.texture.mip_levels.count = 1;
            s.sources[i] = sg_make_view(&t);
        }
        s.attachment = s.levels[0];
        sg_view_desc t{};
        t.texture.image = s.image;
        s.texture = sg_make_view(&t);
    }
    s.complete = false;
    s.version = -1;
    return &surface_.emplace(k, s).first->second;
}

GlobeRenderer::NodeInfo GlobeRenderer::info(const Camera& cam, const QuadKey& k) const {
    NodeInfo ni;
    Bounds b = bounds(k);
    double minH = 0, maxH = 0;
    auto tt = terrain_.find(k);
    if (tt != terrain_.end() && tt->second.version >= 0) {
        minH = tt->second.minH;
        maxH = tt->second.maxH;
    } else if (terrainEnabled_ && !elevation_.empty()) {
        maxH = kUnknownMaxHeight * exaggeration_;
        for (int up = 1; up <= k.level; ++up) {
            auto at = terrain_.find(QuadKey{k.level - up, k.x >> up, k.y >> up});
            if (at == terrain_.end() || at->second.version < 0) continue;
            double pad = std::max(100.0 * exaggeration_, (at->second.maxH - at->second.minH) * 0.25);
            minH = std::min(0.0, at->second.minH - pad);
            maxH = at->second.maxH + pad;
            break;
        }
    }
    auto cached = spheres_.find(k);
    if (cached == spheres_.end() || cached->second.minH != static_cast<float>(minH) || cached->second.maxH != static_cast<float>(maxH)) {
        double clat = 0.5 * (b.south + b.north), clon = 0.5 * (b.west + b.east);
        Sphere sp;
        sp.center = geodeticToEcef(clat, clon, 0.5 * (minH + maxH));
        for (double la : {b.south, clat, b.north})
            for (double lo : {b.west, clon, b.east}) {
                sp.radius = std::max(sp.radius, length(geodeticToEcef(la, lo, minH) - sp.center));
                sp.radius = std::max(sp.radius, length(geodeticToEcef(la, lo, maxH) - sp.center));
            }
        sp.radius *= 1.02;
        sp.minH = static_cast<float>(minH);
        sp.maxH = static_cast<float>(maxH);
        if (spheres_.size() > 50000) spheres_.clear();
        cached = spheres_.insert_or_assign(k, sp).first;
    }
    Vec3 c = cached->second.center;
    double r = cached->second.radius;
    Vec3 rel = c - eye_;
    double dist = length(rel);
    ni.distance = dist;
    ni.visible = true;
    if (k.level >= kMinLevel) {
        for (int p = 0; p < 4; ++p) {
            const double* pl = &frustum_[p * 4];
            if (pl[0] * rel.x + pl[1] * rel.y + pl[2] * rel.z + pl[3] < -r) {
                ni.visible = false;
                return ni;
            }
        }
        double R = kWgs84B;
        double horizonEye = std::sqrt(std::max(0.0, dot(eye_, eye_) - R * R));
        double outer = length(c) + r;
        double horizonTile = std::sqrt(std::max(0.0, outer * outer - R * R));
        if (dist - r > horizonEye + horizonTile) {
            ni.visible = false;
            return ni;
        }
    }
    double d = std::max(dist - r, 1.0);
    double texel = spanDeg(k.level) * kDegToRad * kWgs84A / kSurfaceSize;
    if (dist > r) {
        double facing = std::abs(dot(rel * (1.0 / dist), normalize(c)));
        texel *= std::max(facing, 0.35);
    }
    double threshold = std::max(1.0, dpiScale_ * 0.6) / std::max(imageryScale_, 0.1);
    double ratio = texel / (cam.pixelSizeAt(d) * threshold);
    bool sticky = wasRefined(k);
    int top = maxRefine();
    ni.refine = k.level < kMinLevel || (k.level < top && visits_ < 20000 && (ratio > 1.0 || (sticky && ratio > 0.8)));
    ni.nearRefine = k.level < top && ratio > 0.7;
    return ni;
}

void GlobeRenderer::explain(const Camera& cam, double lat, double lon, const char* tag) const {
    if (!diagnostics_) return;
    std::string line;
    char buf[160];
    for (int level = 0; level <= 22; ++level) {
        double s = spanDeg(level);
        QuadKey k{level, static_cast<int>(std::floor((lon + 180.0) / s)), static_cast<int>(std::floor((90.0 - lat) / s))};
        NodeInfo ni = info(cam, k);
        auto dr = drawn_.find(k);
        bool drawn = dr != drawn_.end() && dr->second + 1 >= frame_;
        auto tt = terrain_.find(k);
        std::snprintf(buf, sizeof(buf), "L%d/%d/%d %s%s%s%s h[%.0f,%.0f] ", level, k.x, k.y, ni.visible ? "v" : "-", ni.refine ? "r" : "-",
                      ready(k) ? "R" : "-", drawn ? "D" : "-", tt == terrain_.end() ? -1.0 : tt->second.minH,
                      tt == terrain_.end() ? -1.0 : tt->second.maxH);
        line += buf;
        if (!ni.visible || drawn || !ni.refine) break;
    }
    diag("explain %s frame %llu %.5f %.5f: %s", tag, static_cast<unsigned long long>(frame_), lat, lon, line.c_str());
}

int GlobeRenderer::idealImageryZ(const QuadKey& k) const {
    for (size_t l = 0; l < matrices_.size() && l < layers_.size(); ++l) {
        if (layers_[l].polarFill || (l < vectorLayers_.size() && vectorLayers_[l])) continue;
        const TileMatrix& m = matrices_[l];
        if (m.levels.empty()) return -1;
        double target = spanDeg(k.level) / kSurfaceSize * m.unitsPerDegree();
        return m.bestLevel(target);
    }
    return -1;
}

bool GlobeRenderer::wasRefined(const QuadKey& k) const {
    auto it = refined_.find(k);
    return it != refined_.end() && it->second + 2 >= frame_;
}

bool GlobeRenderer::ready(const QuadKey& k) const {
    auto s = surface_.find(k);
    if (s == surface_.end() || !s->second.complete) return false;
    auto t = terrain_.find(k);
    return t != terrain_.end() && t->second.complete;
}

bool GlobeRenderer::progressed(const SurfaceTile& s) const {
    if (s.missing.empty()) return true;
    double t = now();
    for (const TileId& id : s.missing) {
        if (imagery_.count(id)) return true;
        auto f = failed_.find(id);
        if (f != failed_.end() && (f->second == std::numeric_limits<double>::infinity() || f->second <= t)) return true;
    }
    return false;
}

void GlobeRenderer::need(const QuadKey& k, double priority) {
    if (retaining_ > 0) return;
    if (prefetching_) {
        prefetch_.push_back({k, priority * needScale_, true});
        return;
    }
    int depth = 0;
    auto s = surface_.find(k);
    if (s == surface_.end() || !s->second.complete) {
        depth = 1;
        for (int up = 1; up <= 8 && k.level - up >= 0; ++up) {
            auto a = surface_.find({k.level - up, k.x >> up, k.y >> up});
            if (a != surface_.end() && a->second.version >= 0) break;
            ++depth;
        }
    }
    needs_.push_back({k, priority / (1.0 + depth * depth)});
}

void GlobeRenderer::traverse(const Camera& cam, const QuadKey& k, std::vector<DrawTile>& out) {
    ++visits_;
    NodeInfo ni = info(cam, k);
    if (!ni.visible) return;
    QuadKey kids[4];
    for (int i = 0; i < 4; ++i) kids[i] = {k.level + 1, k.x * 2 + (i & 1), k.y * 2 + (i >> 1)};
    if (ni.refine) {
        bool allReady = true;
        bool allDrawable = true;
        NodeInfo ci[4];
        for (int i = 0; i < 4; ++i) {
            ci[i] = info(cam, kids[i]);
            if (!ci[i].visible) continue;
            need(kids[i], ci[i].distance * 2.0);
            if (!ready(kids[i])) allReady = false;
            if (terrain_.find(kids[i]) == terrain_.end()) allDrawable = false;
        }
        if (k.level < kMinLevel) {
            for (int i = 0; i < 4; ++i) {
                if (ci[i].visible) terrain(kids[i]);
                traverse(cam, kids[i], out);
            }
            return;
        }
        bool sticky = !prefetching_ && wasRefined(k);
        if (!allDrawable && sticky) {
            allDrawable = true;
            for (int i = 0; i < 4; ++i) {
                if (!ci[i].visible || terrain_.find(kids[i]) != terrain_.end()) continue;
                if (syncBuilds_ >= 24) {
                    allDrawable = false;
                    break;
                }
                ++syncBuilds_;
                terrain(kids[i]).used = frame_;
            }
        }
        if (allDrawable && (allReady || !ready(k) || sticky)) {
            if (!prefetching_) refined_[k] = frame_;
            for (int i = 0; i < 4; ++i) traverse(cam, kids[i], out);
            return;
        }
    } else if (!ready(k) && k.level < maxRefine()) {
        if (!prefetching_ && wasRefined(k)) {
            bool drawable = true;
            NodeInfo kin[4];
            for (int i = 0; i < 4 && drawable; ++i) {
                kin[i] = info(cam, kids[i]);
                if (kin[i].visible && terrain_.find(kids[i]) == terrain_.end()) drawable = false;
            }
            if (drawable) {
                need(k, ni.distance * 0.5);
                refined_[k] = frame_;
                ++retaining_;
                for (int i = 0; i < 4; ++i) {
                    if (!kin[i].visible) continue;
                    auto d = drawn_.find(kids[i]);
                    bool shown = d != drawn_.end() && d->second + 2 >= frame_;
                    if (shown || wasRefined(kids[i])) traverse(cam, kids[i], out);
                    else out.push_back({kids[i], kin[i].distance, 1});
                }
                --retaining_;
                return;
            }
        }
        bool kidsReady = true;
        bool any = false;
        NodeInfo ci[4];
        for (int i = 0; i < 4 && kidsReady; ++i) {
            ci[i] = info(cam, kids[i]);
            if (!ci[i].visible) continue;
            any = true;
            if (!ready(kids[i])) kidsReady = false;
        }
        if (any && kidsReady) {
            need(k, ni.distance);
            for (int i = 0; i < 4; ++i)
                if (ci[i].visible) out.push_back({kids[i], ci[i].distance});
            return;
        }
    }
    need(k, ni.distance);
    if (k.level > kMinLevel) {
        QuadKey parent{k.level - 1, k.x / 2, k.y / 2};
        prefetch(parent, ni.distance * 3.0);
        if (k.level > kMinLevel + 1) prefetch({k.level - 2, k.x / 4, k.y / 4}, ni.distance * 6.0);
    }
    if (ni.nearRefine && ready(k))
        for (int i = 0; i < 4; ++i) prefetch(kids[i], ni.distance * 4.0);
    out.push_back({k, ni.distance, retaining_ > 0 ? 2 : 0});
}

void GlobeRenderer::processLoads() {
    auto taken = loader_.take(kUploadsPerFrame);
    int uploaded = 0;
    size_t keep = taken.size();
    for (size_t i = 0; i < taken.size(); ++i) {
        if (uploaded >= 4 && elapsedMs() > budgetMs * 0.3) {
            keep = i;
            break;
        }
        ++uploaded;
        auto& t = taken[i];
        if (t.ok && t.vector) {
            ImageryTexture tex;
            tex.vector = t.vector;
            tex.used = frame_;
            if (!t.vector->fillVertices.empty()) {
                sg_buffer_desc d{};
                d.usage.vertex_buffer = true;
                d.data = {t.vector->fillVertices.data(), t.vector->fillVertices.size() * sizeof(float)};
                tex.fill = sg_make_buffer(&d);
                tex.fillCount = static_cast<int>(t.vector->fillVertices.size() / 6);
            }
            if (!t.vector->lineVertices.empty()) {
                sg_buffer_desc d{};
                d.usage.vertex_buffer = true;
                d.data = {t.vector->lineVertices.data(), t.vector->lineVertices.size() * sizeof(float)};
                tex.line = sg_make_buffer(&d);
                tex.lineCount = static_cast<int>(t.vector->lineVertices.size() / 10);
            }
            t.vector->fillVertices.clear();
            t.vector->fillVertices.shrink_to_fit();
            t.vector->lineVertices.clear();
            t.vector->lineVertices.shrink_to_fit();
            auto old = imagery_.find(t.id);
            if (old != imagery_.end()) dropImagery(old->second);
            imagery_[t.id] = tex;
            failed_.erase(t.id);
        } else if (t.ok) {
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
            if (sg_query_view_state(tex.view) != SG_RESOURCESTATE_VALID) {
                poolPressure("imagery texture");
                sg_destroy_view(tex.view);
                sg_destroy_image(tex.image);
                failed_[t.id] = now() + 0.5;
                continue;
            }
            auto old = imagery_.find(t.id);
            if (old != imagery_.end()) dropImagery(old->second);
            imagery_[t.id] = tex;
            failed_.erase(t.id);
        } else {
            failed_[t.id] = t.permanent ? std::numeric_limits<double>::infinity() : now() + kRetrySeconds;
            auto old = imagery_.find(t.id);
            if (old != imagery_.end() && t.permanent) {
                dropImagery(old->second);
                imagery_.erase(old);
                if (t.id.layer < static_cast<int>(matrices_.size())) {
                    const TileMatrix& m = matrices_[t.id.layer];
                    double x0, y0, x1, y1;
                    m.tileBounds(t.id.z, t.id.x, t.id.y, x0, y0, x1, y1);
                    markDirty({unprojectX(m.srid, x0), unprojectY(m.srid, y0), unprojectX(m.srid, x1), unprojectY(m.srid, y1)});
                }
            }
        }
        ++imageryVersion_;
    }
    for (size_t i = taken.size(); i-- > keep;) loader_.giveBack(taken[i]);
    stats_.uploads = uploaded;
    stats_.imageryLoads += uploaded;
}

void GlobeRenderer::evict() {
    if (imagery_.size() > imageryCapacity_) {
        std::vector<std::pair<std::uint64_t, TileId>> c;
        for (auto& [k, t] : imagery_)
            if (k.z > kBaseZoom && t.used + 1 < frame_) c.push_back({t.used, k});
        std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && imagery_.size() > imageryCapacity_; ++i) {
            auto it = imagery_.find(c[i].second);
            dropImagery(it->second);
            imagery_.erase(it);
            ++stats_.imageryEvictions;
        }
    }
    if (surface_.size() > surfaceCapacity_) {
        std::vector<std::pair<std::uint64_t, QuadKey>> c;
        for (auto& [k, s] : surface_)
            if (k.level > kKeepLevel && s.used + 1 < frame_) c.push_back({s.used, k});
        std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && surface_.size() > surfaceCapacity_; ++i) {
            auto it = surface_.find(c[i].second);
            if (freeSurfaces_.size() < freeSurfacePool_) freeSurfaces_.push_back(it->second);
            else dropSurface(it->second);
            ++stats_.surfaceEvictions;
            surface_.erase(it);
        }
    }
    if (terrain_.size() > terrainCapacity_) {
        std::vector<std::pair<std::uint64_t, QuadKey>> c;
        for (auto& [k, t] : terrain_)
            if (k.level > kKeepLevel && t.used + 1 < frame_) c.push_back({t.used, k});
        std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && terrain_.size() > terrainCapacity_; ++i) {
            auto it = terrain_.find(c[i].second);
            sg_destroy_buffer(it->second.vbuf);
            terrain_.erase(it);
            ++stats_.terrainEvictions;
        }
    }
}

void GlobeRenderer::want(const TileId& id, double priority) { requests_.emplace_back(id, priority); }

void GlobeRenderer::requestBase() {
    double t = now();
    for (int l = 0; l < static_cast<int>(matrices_.size()); ++l) {
        const TileMatrix& m = matrices_[l];
        const int top = std::min(kBaseZoom, static_cast<int>(m.levels.size()) - 1);
        for (int z = 0; z <= top; ++z) {
            if (!m.hasLevel(z)) continue;
            const int w = m.levels[z].matrixWidth, h = m.levels[z].matrixHeight;
            if (w * h > 64) break;
            bool fill = l < static_cast<int>(layers_.size()) && layers_[l].polarFill;
            for (int i = 0; i < w * h; ++i) {
                if (fill && i / w != 0 && i / w != h - 1) continue;
                TileId id{l, z, i % w, i / w};
                if (imagery_.find(id) != imagery_.end()) continue;
                auto f = failed_.find(id);
                if (f != failed_.end() && f->second > t) continue;
                want(id, -1.0 + z * 0.01);
            }
        }
    }
}

GlobeRenderer::ImageryTexture* GlobeRenderer::findTexture(const TileId& id, const TileMatrix& m, double priority,
                                                          bool& missing, TileId& src) {
    missing = false;
    src = id;
    auto it = imagery_.find(id);
    if (it != imagery_.end()) return &it->second;
    const double t = now();
    const double cx = m.originX + (id.x + 0.5) * m.spanX(id.z);
    const double cy = m.originY - (id.y + 0.5) * m.spanY(id.z);
    auto ancestor = [&](int z) {
        if (z == id.z) return id;
        return TileId{id.layer, z, static_cast<int>(std::floor((cx - m.originX) / m.spanX(z))),
                      static_cast<int>(std::floor((m.originY - cy) / m.spanY(z)))};
    };
    auto unavailable = [&](const TileId& k) {
        auto f = failed_.find(k);
        return f != failed_.end() && f->second == std::numeric_limits<double>::infinity();
    };
    int target = -1;
    for (int z = id.z; z >= 0; --z) {
        if (z != id.z && !m.hasLevel(z)) continue;
        TileId k = ancestor(z);
        if (imagery_.count(k) || !unavailable(k)) {
            target = z;
            break;
        }
    }
    if (target >= 0) {
        TileId k = ancestor(target);
        auto ti = imagery_.find(k);
        if (ti != imagery_.end()) {
            src = k;
            return &ti->second;
        }
        missing = true;
        auto f = failed_.find(k);
        if (f == failed_.end() || f->second <= t) want(k, priority);
    }
    TileId coarse{-1, 0, 0, 0};
    for (int z = (target >= 0 ? target : id.z) - 1; z >= 0; --z) {
        if (!m.hasLevel(z)) continue;
        TileId a = ancestor(z);
        auto ai = imagery_.find(a);
        if (ai != imagery_.end()) {
            if (missing && coarse.layer >= 0) want(coarse, priority * 0.5);
            src = a;
            return &ai->second;
        }
        if (z == target - 3) {
            auto fa = failed_.find(a);
            if (fa == failed_.end() || fa->second <= t) coarse = a;
        }
    }
    if (missing && coarse.layer >= 0) want(coarse, priority * 0.5);
    return nullptr;
}

void GlobeRenderer::dropImagery(ImageryTexture& t) {
    if (t.image.id) {
        sg_destroy_view(t.view);
        sg_destroy_image(t.image);
    }
    if (t.fill.id) sg_destroy_buffer(t.fill);
    if (t.line.id) sg_destroy_buffer(t.line);
    t = ImageryTexture{};
}

bool GlobeRenderer::buildSurfaceJob(const QuadKey& k, double priority, SurfaceJob* job, std::vector<float>* verts,
                                    std::vector<float>* fverts, double texelDeg) {
    Bounds b = bounds(k);
    double span = b.north - b.south;
    bool complete = true;
    double coverLat = 0;
    bool anyCover = false;
    for (int l = 0; l < static_cast<int>(matrices_.size()) && l < static_cast<int>(layers_.size()); ++l)
        if (!layers_[l].polarFill) {
            coverLat = std::max(coverLat, maxLatitude(matrices_[l].srid));
            anyCover = true;
        }
    for (int l = 0; l < static_cast<int>(matrices_.size()) && l < static_cast<int>(layers_.size()); ++l) {
      const TileMatrix& m = matrices_[l];
      double maxLat = maxLatitude(m.srid);
      double bandS[2], bandN[2];
      int bands = 0;
      if (layers_[l].polarFill && anyCover) {
          if (coverLat >= maxLat) continue;
          if (b.north > coverLat) {
              bandS[bands] = std::max(b.south, coverLat);
              bandN[bands++] = std::min(b.north, maxLat);
          }
          if (b.south < -coverLat) {
              bandS[bands] = std::max(b.south, -maxLat);
              bandN[bands++] = std::min(b.north, -coverLat);
          }
      } else {
          bandS[bands] = std::max(b.south, -maxLat);
          bandN[bands++] = std::min(b.north, maxLat);
      }
      for (int band = 0; band < bands; ++band) {
        double south = bandS[band], north = bandN[band];
        if (south >= north) continue;
        double target = span / kSurfaceSize * m.unitsPerDegree();
        int z = m.bestLevel(target);
        if (z < 0 || m.levels[z].resolutionX < target * 0.125) continue;
        std::vector<TileRange> ranges = regionRanges(m, {b.west, south, b.east, north}, z, z);
        if (ranges.empty()) continue;
        const TileRange& r = ranges[0];
        int rows = (m.srid == 3857 && z < 9) ? 8 : 1;
        for (int ty = r.y0; ty <= r.y1; ++ty)
            for (int tx = r.x0; tx <= r.x1; ++tx) {
                TileId id{l, z, tx, ty};
                bool missing = false;
                TileId src;
                ImageryTexture* tex = findTexture(id, m, priority, missing, src);
                if (missing) {
                    complete = false;
                    if (job) job->missing.push_back(id);
                }
                if (!tex) {
                    if (job && !layers_[l].polarFill && !(l < static_cast<int>(vectorLayers_.size()) && vectorLayers_[l])) ++job->hollow;
                    continue;
                }
                tex->used = frame_;
                if (!job) continue;
                if (l == 0) {
                    job->imageryZ = z;
                    job->sourceZ = std::min(job->sourceZ, src.z);
                }
                if (tex->vector) {
                    if (l == 0) job->background = true;
                    VectorDraw vd;
                    vd.tile = tex;
                    vd.xform[0] = static_cast<float>(2.0 / span);
                    vd.xform[1] = static_cast<float>(2.0 / span);
                    vd.xform[2] = static_cast<float>((tex->vector->originLon - b.west) * 2.0 / span - 1.0);
                    vd.xform[3] = static_cast<float>((tex->vector->originLat - b.south) * 2.0 / span - 1.0);
                    bool seen = false;
                    for (const auto& e : job->vectors) seen = seen || e.tile == tex;
                    if (!seen) job->vectors.push_back(vd);
                    continue;
                }

                double sMinX, sMinY, sMaxX, sMaxY, tMinX, tMinY, tMaxX, tMaxY;
                m.tileBounds(src.z, src.x, src.y, sMinX, sMinY, sMaxX, sMaxY);
                m.tileBounds(z, tx, ty, tMinX, tMinY, tMaxX, tMaxY);
                double w = std::max(b.west, unprojectX(m.srid, tMinX));
                double e = std::min(b.east, unprojectX(m.srid, tMaxX));
                double s = std::max(south, unprojectY(m.srid, tMinY));
                double nn = std::min(north, unprojectY(m.srid, tMaxY));
                if (w >= e || s >= nn) continue;

                int first = static_cast<int>(verts->size() / 4);
                auto emit = [&](double lon, double lat) {
                    float px = static_cast<float>((lon - b.west) / span * 2.0 - 1.0);
                    float py = static_cast<float>((lat - b.south) / span * 2.0 - 1.0);
                    float u = static_cast<float>((projectX(m.srid, lon) - sMinX) / (sMaxX - sMinX));
                    float v = static_cast<float>((sMaxY - projectY(m.srid, lat)) / (sMaxY - sMinY));
                    verts->insert(verts->end(), {px, py, u, v});
                };
                for (int rr = 0; rr < rows; ++rr) {
                    double la0 = s + (nn - s) * rr / rows, la1 = s + (nn - s) * (rr + 1) / rows;
                    emit(w, la0);
                    emit(e, la0);
                    emit(e, la1);
                    emit(w, la0);
                    emit(e, la1);
                    emit(w, la1);
                }
                SurfaceDraw sd{tex->view, layers_[l].opacity, first, rows * 6};
                if (layers_[l].polarFill) {
                    const float* t = north > 0 ? layers_[l].polarTintNorth : layers_[l].polarTintSouth;
                    for (int c = 0; c < 3; ++c) sd.tint[c] = t[c];
                }
                job->draws.push_back(sd);
            }
      }
    }
    if (job) {
        for (const auto& [id, o] : overlays_) {
            const GeoRect& r = o.rect;
            double w = std::max(b.west, r.west), e = std::min(b.east, r.east);
            double s = std::max(b.south, r.south), nn = std::min(b.north, r.north);
            if (w >= e || s >= nn) continue;
            int first = static_cast<int>(verts->size() / 4);
            auto emit = [&](double lon, double lat) {
                float px = static_cast<float>((lon - b.west) / span * 2.0 - 1.0);
                float py = static_cast<float>((lat - b.south) / span * 2.0 - 1.0);
                float u = static_cast<float>((lon - r.west) / (r.east - r.west));
                float v = static_cast<float>((r.north - lat) / (r.north - r.south));
                verts->insert(verts->end(), {px, py, u, v});
            };
            emit(w, s);
            emit(e, s);
            emit(e, nn);
            emit(w, s);
            emit(e, nn);
            emit(w, nn);
            job->draws.push_back({o.view, o.opacity, first, 6});
        }
        job->complete = complete;
    }
    return complete;
}

void GlobeRenderer::generateMips(const std::vector<sg_image>& images) {
#if defined(OTE_GL_MIPS)
    for (const sg_image& img : images) {
        sg_gl_image_info info = sg_gl_query_image_info(img);
        GLuint tex = info.tex[info.active_slot];
        if (!tex) continue;
        glBindTexture(GL_TEXTURE_2D, tex);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sg_reset_state_cache();
#else
    (void)images;
#endif
}

double GlobeRenderer::videoMemoryMb() {
#if defined(_WIN32) && defined(SOKOL_D3D11)
    auto* dev = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    if (!dev) return 0;
    IDXGIDevice* dxgi = nullptr;
    if (FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi)))) return 0;
    double mb = 0;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgi->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc)) && desc.DedicatedVideoMemory > 64ull * 1048576ull)
            mb = static_cast<double>(desc.DedicatedVideoMemory) / 1048576.0;
        adapter->Release();
    }
    dxgi->Release();
    return mb;
#elif defined(OTE_GL_MIPS) && !defined(SOKOL_GLES3)
    GLint kb = 0;
    while (glGetError() != GL_NO_ERROR) {
    }
    glGetIntegerv(0x9047, &kb);
    if (glGetError() != GL_NO_ERROR || kb <= 0) {
        GLint info[4] = {0, 0, 0, 0};
        glGetIntegerv(0x87FC, info);
        if (glGetError() != GL_NO_ERROR) return 0;
        kb = info[0];
    }
    return kb > 0 ? kb / 1024.0 : 0;
#else
    return 0;
#endif
}

double GlobeRenderer::physicalMemoryMb() {
#if defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) return static_cast<double>(m.ullTotalPhys) / 1048576.0;
    return 0;
#elif defined(__APPLE__)
    std::uint64_t bytes = 0;
    size_t len = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) return static_cast<double>(bytes) / 1048576.0;
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && size > 0 ? static_cast<double>(pages) * static_cast<double>(size) / 1048576.0 : 0;
#endif
}

void GlobeRenderer::diag(const char* fmt, ...) const {
    if (!diagnostics_) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "OpenTAKEngine", buf);
#else
    if (const char* path = std::getenv("OTE_DIAG")) {
        if (FILE* f = std::fopen(path, "a")) {
            std::fputs(buf, f);
            std::fputc(10, f);
            std::fclose(f);
        }
    } else {
        std::fprintf(stderr, "%s\n", buf);
    }
#endif
}

void GlobeRenderer::explainCollapse(const Camera& cam, const std::vector<DrawTile>& tiles) {
    const DrawTile* coarse = nullptr;
    for (const DrawTile& t : tiles)
        if (!coarse || t.key.level < coarse->key.level) coarse = &t;
    if (!coarse) {
        diag("collapse frame %llu: no tiles drawn gsd %.2f", static_cast<unsigned long long>(frame_), cam.gsd);
        return;
    }
    auto state = [&](const QuadKey& k, char* out, size_t n) {
        auto s = surface_.find(k);
        auto t = terrain_.find(k);
        NodeInfo ni = info(cam, k);
        std::snprintf(out, n, "L%d/%d/%d vis%d ref%d surf%s terr%s", k.level, k.x, k.y, ni.visible ? 1 : 0, ni.refine ? 1 : 0,
                      s == surface_.end() ? "-" : s->second.complete ? "C" : s->second.version < 0 ? "0" : "p",
                      t == terrain_.end() ? "-" : t->second.complete ? "C" : "p");
    };
    char a[160], kid[4][160];
    state(coarse->key, a, sizeof(a));
    for (int i = 0; i < 4; ++i)
        state({coarse->key.level + 1, coarse->key.x * 2 + (i & 1), coarse->key.y * 2 + (i >> 1)}, kid[i], sizeof(kid[i]));
    diag("collapse frame %llu: drawn %zu maxL %d -> coarsest %s | kids %s ; %s ; %s ; %s | gsd %.2f prevGsd %.2f",
         static_cast<unsigned long long>(frame_), tiles.size(), prevMaxLevel_, a, kid[0], kid[1], kid[2], kid[3], cam.gsd, prevGsd_);
}

void GlobeRenderer::profileMark(int section) {
    if (!profiling_) return;
    double t = elapsedMs();
    if (section > 0) profileAcc_[section - 1] += t - profileLast_;
    else profileAcc_[7] += t;
    profileLast_ = t;
    if (section == 6 && ++profileFrames_ >= 120) {
        if (FILE* f = std::fopen(profilePath_.c_str(), "a")) {
            const char* names[8] = {"traverse", "overview+admit", "terrain", "work", "surface", "main", "overview-only", "pre-traverse"};
            for (int i = 0; i < 8; ++i) std::fprintf(f, "%s %.3f ", names[i], profileAcc_[i] / profileFrames_);
            std::fputc(10, f);
            std::fclose(f);
        }
        for (double& a : profileAcc_) a = 0;
        profileFrames_ = 0;
    }
}

void GlobeRenderer::render(Camera& cam, const sg_swapchain* swapchain, const sg_attachments* attachments, int width,
                           int height) {
    if (!ready_) return;
    frameStart_ = std::chrono::steady_clock::now();
    profileStart_ = frameStart_;
    profileLast_ = 0;
    if (!trace_)
        if (const char* path = std::getenv("OTE_TRACE")) trace_ = std::fopen(path, "w");
    ++frame_;
    requests_.clear();
    needs_.clear();
    prefetch_.clear();
    elevation_.beginFrame();
    processLoads();
    bool elevationChanged = elevation_.processResults(32);
    {
        std::vector<GeoRect> regions;
        if (features_.takeDirty(regions)) {
            for (auto& [k, s] : surface_) {
                Bounds b = bounds(k);
                GeoRect r{b.west, b.south, b.east, b.north};
                for (const GeoRect& g : regions)
                    if (g.intersects(r)) {
                        s.complete = false;
                        s.missing.clear();
                        break;
                    }
            }
        }
    }
    evict();
    requestBase();

    cam.width = std::max(width, 1);
    cam.height = std::max(height, 1);
    sg_backend backend = sg_query_backend();
    cam.zeroToOneDepth = backend != SG_BACKEND_GLCORE && backend != SG_BACKEND_GLES3;
    cam.update();
    eye_ = cam.eye();
    if (grid_) {
        double s = cam.gsd * 70.0 * dpiScale_;
        double spacing = s <= 1000.0 ? 1000.0 : s <= 10000.0 ? 10000.0 : s <= 100000.0 ? 100000.0 : 0.0;
        if (spacing != gridSpacing_) {
            gridSpacing_ = spacing;
            markDirty({-180, -90, 180, 90});
        }
    }

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
    syncBuilds_ = 0;
    if (refined_.size() > 20000)
        for (auto it = refined_.begin(); it != refined_.end();) it = it->second + 3 < frame_ ? refined_.erase(it) : std::next(it);
    terrain({0, 0, 0});
    terrain({0, 1, 0});
    profileMark(0);
    traverse(cam, {0, 0, 0}, tiles);
    traverse(cam, {0, 1, 0}, tiles);
    profileMark(1);
    auto overview = [&](double gsdScale, double scale) {
        Camera wide = cam;
        int side = static_cast<int>(std::hypot(static_cast<double>(cam.width), static_cast<double>(cam.height)));
        wide.width = side;
        wide.height = side;
        wide.gsd = cam.gsd * gsdScale;
        wide.update();
        std::array<double, 16> saved = frustum_;
        Vec3 savedEye = eye_;
        eye_ = wide.eye();
        const Mat4& wm = wide.viewProjRel();
        for (int p = 0; p < 4; ++p) {
            int row = p / 2;
            double sign = (p % 2 == 0) ? 1.0 : -1.0;
            double a = wm.at(3, 0) + sign * wm.at(row, 0), b = wm.at(3, 1) + sign * wm.at(row, 1);
            double c = wm.at(3, 2) + sign * wm.at(row, 2), d = wm.at(3, 3) + sign * wm.at(row, 3);
            double len = std::sqrt(a * a + b * b + c * c);
            frustum_[p * 4 + 0] = a / len;
            frustum_[p * 4 + 1] = b / len;
            frustum_[p * 4 + 2] = c / len;
            frustum_[p * 4 + 3] = d / len;
        }
        std::vector<DrawTile> ignored;
        needScale_ = scale;
        prefetching_ = true;
        int savedVisits = visits_;
        visits_ = 0;
        traverse(wide, {0, 0, 0}, ignored);
        traverse(wide, {0, 1, 0}, ignored);
        visits_ = savedVisits;
        needScale_ = 1.0;
        prefetching_ = false;
        frustum_ = saved;
        eye_ = savedEye;
    };
    {
        double camKey[6] = {cam.focus.lat, cam.focus.lon, cam.gsd, cam.azimuth, cam.tilt, static_cast<double>(cam.width * 100000 + cam.height)};
        bool moved = false;
        for (int i = 0; i < 6; ++i) moved = moved || camKey[i] != lastOverviewKey_[i];
        size_t own = prefetch_.size();
        double overviewStart = elapsedMs();
        double tnowZoom = now();
        if (zoomClock_ > 0 && tnowZoom > zoomClock_) {
            double rate = (std::log2(std::max(cam.gsd, 1e-6)) - std::log2(std::max(zoomGsd_, 1e-6))) / (tnowZoom - zoomClock_);
            double a = std::clamp((tnowZoom - zoomClock_) / 0.25, 0.0, 1.0);
            zoomRate_ = zoomRate_ + (rate - zoomRate_) * a;
        }
        zoomClock_ = tnowZoom;
        zoomGsd_ = cam.gsd;
        bool zoomingOut = zoomRate_ > 0.3;
        urgentPrefetch_ = zoomingOut;
        overviewReused_ = !(moved || frame_ % 8 == 0 || overviewCache_.empty());
        if (!overviewReused_) {
            overview(2.0, zoomingOut ? 1.2 : 3.0);
            overview(4.0, zoomingOut ? 2.0 : 6.0);
            if (zoomRate_ > 1.5) overview(8.0, 3.0);

            overviewCache_.assign(prefetch_.begin() + static_cast<std::ptrdiff_t>(own), prefetch_.end());
            for (int i = 0; i < 6; ++i) lastOverviewKey_[i] = camKey[i];
        } else {
            prefetch_.insert(prefetch_.end(), overviewCache_.begin(), overviewCache_.end());
        }
        if (profiling_) profileAcc_[6] += elapsedMs() - overviewStart;
    }
    std::sort(tiles.begin(), tiles.end(), [](const DrawTile& a, const DrawTile& b) { return a.distance < b.distance; });

    if (overviewReused_ && !admittedCache_.empty()) {
        needs_.insert(needs_.end(), admittedCache_.begin(), admittedCache_.end());
        stats_.prefetched = static_cast<int>(admittedCache_.size());
    } else {
        size_t firstAdmitted = needs_.size();
        auto keyLess = [](const QuadKey& a, const QuadKey& b) {
            if (a.level != b.level) return a.level < b.level;
            if (a.x != b.x) return a.x < b.x;
            return a.y < b.y;
        };
        std::vector<QuadKey> seen;
        seen.reserve(needs_.size());
        for (const Need& n : needs_) seen.push_back(n.key);
        std::sort(seen.begin(), seen.end(), keyLess);
        seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
        std::sort(prefetch_.begin(), prefetch_.end(), [&](const Need& a, const Need& b) {
            if (!(a.key == b.key)) return keyLess(a.key, b.key);
            return a.priority < b.priority;
        });
        prefetch_.erase(std::unique(prefetch_.begin(), prefetch_.end(), [](const Need& a, const Need& b) { return a.key == b.key; }),
                        prefetch_.end());
        size_t activeImagery = 0;
        for (const auto& [id, t] : imagery_)
            if (t.used + 1 >= frame_) ++activeImagery;
        double imageryRoom = static_cast<double>(imageryCapacity_) * 0.85 - static_cast<double>(activeImagery);
        double surfaceRoom = std::min(static_cast<double>(surfaceCapacity_) * 0.92 - static_cast<double>(seen.size()),
                                      2.5 * static_cast<double>(seen.size()) + 64.0);
        int freshBudget = std::clamp(static_cast<int>(imageryRoom / 2.0), 4, urgentPrefetch_ ? 96 : 32);
        int fresh = 0;
        size_t admitted = 0;
        std::sort(prefetch_.begin(), prefetch_.end(), [](const Need& a, const Need& b) { return a.priority < b.priority; });
        for (const Need& n : prefetch_) {
            if (static_cast<double>(admitted) >= surfaceRoom) break;
            if (std::binary_search(seen.begin(), seen.end(), n.key, keyLess)) continue;
            auto sf = surface_.find(n.key);
            bool done = sf != surface_.end() && sf->second.complete;
            if (!done) {
                if (fresh >= freshBudget) continue;
                ++fresh;
            }
            needs_.push_back(n);
            ++admitted;
        }
        stats_.prefetched = static_cast<int>(admitted);
        admittedCache_.assign(needs_.begin() + static_cast<std::ptrdiff_t>(firstAdmitted), needs_.end());
    }
    int baseSurfaceLevel = 3;
    while (baseSurfaceLevel > 0 && static_cast<size_t>(2 * ((1 << (2 * (baseSurfaceLevel + 1))) - 1) / 3) > surfaceCapacity_ / 4) --baseSurfaceLevel;
    for (int level = 0; level <= baseSurfaceLevel; ++level)
        for (int y = 0; y < (1 << level); ++y)
            for (int x = 0; x < (2 << level); ++x) {
                QuadKey k{level, x, y};
                auto sf = surface_.find(k);
                if (sf != surface_.end() && sf->second.complete) sf->second.used = frame_;
                else needs_.push_back({k, 1e-3 * level, true});
            }
    std::sort(needs_.begin(), needs_.end(), [](const Need& a, const Need& b) {
        if (!(a.key == b.key)) {
            if (a.key.level != b.key.level) return a.key.level < b.key.level;
            if (a.key.x != b.key.x) return a.key.x < b.key.x;
            return a.key.y < b.key.y;
        }
        return a.priority < b.priority;
    });
    needs_.erase(std::unique(needs_.begin(), needs_.end(), [](const Need& a, const Need& b) { return a.key == b.key; }),
                 needs_.end());
    std::sort(needs_.begin(), needs_.end(), [](const Need& a, const Need& b) { return a.priority < b.priority; });

    profileMark(2);
    int builds = 0;
    int currentTerrain = elevation_.version() + terrainEpoch_ * 1000000;
    for (const Need& n : needs_) {
        auto it = terrain_.find(n.key);
        if (it == terrain_.end()) {
            if (builds < kTerrainBuildsPerFrame && elapsedMs() < budgetMs * 0.6) {
                TerrainTile& t = terrain_[n.key];
                buildTerrain(n.key, t, n.priority);
                t.used = frame_;
                ++builds;
            }
            continue;
        }
        it->second.used = frame_;
        if (!it->second.complete && it->second.version != currentTerrain && builds < kTerrainBuildsPerFrame &&
            elapsedMs() < budgetMs * 0.6) {
            buildTerrain(n.key, it->second, n.priority);
            ++builds;
        } else if (!it->second.complete && terrainEnabled_ && !elevation_.empty()) {
            std::vector<float> scratch;
            Bounds b = bounds(n.key);
            double cell = (b.north - b.south) / kGrid;
            elevation_.gridFor(b.west - cell, std::max(b.south - cell, -90.0), b.east + cell,
                               std::min(b.north + cell, 90.0), kPosts + 2, n.priority, scratch, false);
        }
    }
    elevation_.submit();

    profileMark(3);
    struct Work {
        double priority;
        size_t index;
        bool render;
        bool prefetch;
    };
    double visibleDone = -1;
    std::vector<Work> work;
    for (size_t i = 0; i < needs_.size(); ++i) {
        auto it = surface_.find(needs_[i].key);
        bool pf = needs_[i].prefetch;
        if (it == surface_.end()) {
            work.push_back({needs_[i].priority, i, true, pf});
        } else {
            it->second.used = frame_;
            if (!it->second.complete && progressed(it->second)) work.push_back({needs_[i].priority * 1.5, i, true, pf});
            else if (!it->second.complete) work.push_back({needs_[i].priority, i, false, pf});
        }
    }
    std::stable_sort(work.begin(), work.end(), [](const Work& a, const Work& b) {
        if (a.prefetch != b.prefetch) return !a.prefetch;
        return a.priority < b.priority;
    });
    int rasterLayers = 0;
    for (size_t l = 0; l < matrices_.size() && l < layers_.size(); ++l)
        if (!(l < vectorLayers_.size() && vectorLayers_[l])) ++rasterLayers;
    const int maxTouched = std::clamp(static_cast<int>(imageryCapacity_ * 0.8 / (2.5 * std::max(rasterLayers, 1))), 16, 128);
    int touched = 0;
    int hollowSkipped = 0;

    std::vector<SurfaceJob> jobs;
    std::vector<float> verts;
    std::vector<float> fverts;
    std::unordered_map<QuadKey, double, QuadKeyHash> jobTexel;
    int renders = 0;
    for (const Work& w : work) {
        size_t i = w.index;
        bool speculative = w.prefetch;
        if (speculative) {
            if (touched >= maxTouched) break;
            ++touched;
            if (visibleDone < 0) visibleDone = elapsedMs();
        }
        double specBudget = prefetchBudgetMs * (urgentPrefetch_ ? 3.0 : 1.0);
        bool overBudget = renders > 0 && (elapsedMs() > budgetMs || (speculative && elapsedMs() - visibleDone > specBudget));
        if (!w.render || renders >= kSurfaceRendersPerFrame || verts.size() * sizeof(float) > kSurfaceBufferBytes / 2 || overBudget) {
            buildSurfaceJob(needs_[i].key, needs_[i].priority, nullptr, nullptr);
            continue;
        }
        SurfaceJob job;
        job.key = needs_[i].key;
        size_t vertsBefore = verts.size();
        Bounds jb = bounds(job.key);
        Vec3 jc = geodeticToEcef(0.5 * (jb.south + jb.north), 0.5 * (jb.west + jb.east), 0.0);
        double jr = (jb.north - jb.south) * kDegToRad * kWgs84A * 0.75;
        double jd = std::max(length(jc - cam.eye()) - jr, cam.eyeAboveGround());
        double texelDeg = std::min((jb.north - jb.south) / kSurfaceSize, cam.pixelSizeAt(jd) / 111320.0);
        size_t vectorStart = fverts.size();
        buildSurfaceJob(job.key, needs_[i].priority, &job, &verts, nullptr, texelDeg);
        if (job.hollow > 0) {
            verts.resize(vertsBefore);
            ++hollowSkipped;
            continue;
        }
        job.featureFirst = static_cast<int>(vectorStart / 6);
        job.featureCount = 0;
        jobTexel[job.key] = texelDeg;
        if ((!features_.empty() || grid_) && fverts.size() * sizeof(float) < kSurfaceBufferBytes / 2) {
            Bounds b = jb;
            size_t before = vectorStart;
            features_.buildSurface({b.west, b.south, b.east, b.north}, texelDeg, dpiScale_, fverts);
            if (grid_) buildGrid({b.west, b.south, b.east, b.north}, texelDeg, dpiScale_, gridSpacing_, fverts);
            job.featureFirst = static_cast<int>(before / 6);
            job.featureCount = static_cast<int>((fverts.size() - before) / 6);
        }
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

    if (fverts.size() * sizeof(float) > kSurfaceBufferBytes) fverts.resize(kSurfaceBufferBytes / sizeof(float) / 6 * 6);
    if (!fverts.empty()) {
        sg_write_buffer_desc wd{};
        wd.src.data = {fverts.data(), fverts.size() * sizeof(float)};
        wd.dst.buffer = featureBuffer_;
        wd.size = fverts.size() * sizeof(float);
        sg_write_buffer_transient(&wd);
    }

    profileMark(4);
    std::vector<sg_image> mipQueue;
    for (auto& job : jobs) {
        SurfaceTile* s = acquireSurface(job.key);
        if (sg_query_view_state(s->texture) != SG_RESOURCESTATE_VALID || sg_query_view_state(s->levels[0]) != SG_RESOURCESTATE_VALID) {
            poolPressure("surface");
            dropSurface(*s);
            surface_.erase(job.key);
            continue;
        }
        s->used = frame_;
        bool jobComplete = job.complete;
        sg_pass pass{};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        if (job.background) pass.action.colors[0].clear_value = {242 / 255.0f, 239 / 255.0f, 233 / 255.0f, 1.0f};
        else pass.action.colors[0].clear_value = {10 / 255.0f, 30 / 255.0f, 62 / 255.0f, 1.0f};
        pass.attachments.colors[0] = glMips_ ? s->levels[0] : scratchAttachment_;
        sg_begin_pass(&pass);
        if (!job.draws.empty()) {
            sg_apply_pipeline(surfacePipeline_);
            for (auto& d : job.draws) {
                if ((d.first + d.count) * 4 * sizeof(float) > kSurfaceBufferBytes) continue;
                if (sg_query_view_state(d.texture) != SG_RESOURCESTATE_VALID) {
                    ++invalidResources_;
                    jobComplete = false;
                    continue;
                }
                sg_bindings bind{};
                bind.vertex_buffers[0] = surfaceBuffer_;
                bind.views[VIEW_tex] = d.texture;
                bind.samplers[SMP_smp] = imagerySampler_;
                sg_apply_bindings(&bind);
                surface_fs_params_t fsp{};
                fsp.tint[0] = d.tint[0];
                fsp.tint[1] = d.tint[1];
                fsp.tint[2] = d.tint[2];
                fsp.tint[3] = d.opacity;
                sg_apply_uniforms(UB_surface_fs_params, SG_RANGE(fsp));
                sg_draw(d.first, d.count, 1);
            }
        }
        if (!job.vectors.empty()) {
            Bounds jb = bounds(job.key);
            double coslat = std::max(std::cos((jb.south + jb.north) * 0.5 * kDegToRad), 0.05);
            double texel = jobTexel.count(job.key) ? jobTexel[job.key] : (jb.north - jb.south) / kSurfaceSize;
            for (int pass = 0; pass < 2; ++pass) {
                sg_apply_pipeline(pass == 0 ? vfillPipeline_ : vlinePipeline_);
                for (const VectorDraw& vd : job.vectors) {
                    const ImageryTexture* t = vd.tile;
                    if (pass == 0 ? !t->fill.id : !t->line.id) continue;
                    if (sg_query_buffer_state(pass == 0 ? t->fill : t->line) != SG_RESOURCESTATE_VALID) {
                        ++invalidResources_;
                        jobComplete = false;
                        continue;
                    }
                    sg_bindings vb{};
                    vb.vertex_buffers[0] = pass == 0 ? t->fill : t->line;
                    sg_apply_bindings(&vb);
                    vector_params_t vp{};
                    std::memcpy(vp.xform, vd.xform, sizeof(vd.xform));
                    vp.line[0] = static_cast<float>(coslat);
                    vp.line[1] = static_cast<float>(texel * dpiScale_);
                    sg_apply_uniforms(UB_vector_params, SG_RANGE(vp));
                    sg_draw(0, pass == 0 ? t->fillCount : t->lineCount, 1);
                }
            }
        }
        if (job.featureCount > 0 && (job.featureFirst + job.featureCount) * 6 * sizeof(float) <= kSurfaceBufferBytes)
            features_.drawSurface(featureBuffer_, job.featureFirst, job.featureCount);
        sg_end_pass();
        if (glMips_) {
            mipQueue.push_back(s->image);
            s->complete = jobComplete;
            s->version = imageryVersion_;
            s->missing = std::move(job.missing);
            s->imageryZ = job.imageryZ;
            s->sourceZ = job.sourceZ;
            continue;
        }
        for (int level = 0; level < kMips; ++level) {
            sg_pass mp{};
            mp.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
            mp.attachments.colors[0] = s->levels[level];
            sg_begin_pass(&mp);
            sg_apply_pipeline(mipPipeline_);
            sg_bindings mb{};
            mb.vertex_buffers[0] = skyBuffer_;
            mb.views[VIEW_tex] = scratchTexture_;
            mb.samplers[SMP_smp] = mipSampler_;
            sg_apply_bindings(&mb);
            mip_params_t mpp{};
            float block = static_cast<float>(1 << level);
            mpp.params[0] = flipSurface_ ? 1.0f : 0.0f;
            mpp.params[1] = block;
            mpp.params[2] = std::min(block, 16.0f);
            mpp.params[3] = 1.0f / kSurfaceSize;
            sg_apply_uniforms(UB_mip_params, SG_RANGE(mpp));
            sg_draw(0, 3, 1);
            sg_end_pass();
        }
        s->complete = jobComplete;
        s->version = imageryVersion_;
        s->missing = std::move(job.missing);
        s->imageryZ = job.imageryZ;
        s->sourceZ = job.sourceZ;
    }

    if (!mipQueue.empty()) generateMips(mipQueue);

    profileMark(5);
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
    sky.radii[3] = holeMarker_ ? 1.0f : 0.0f;
    sg_apply_uniforms(UB_sky_fs_params, SG_RANGE(sky));
    sg_draw(0, 3, 1);

    bool xray = terrainAlpha_ < 0.999f;
    bool tilesBusy = false;
    auto drawObjects = [&]() {
        models_.draw(cam, [this](double lat, double lon) { return elevationAt(lat, lon); });
        points_.draw(cam, dpiScale_);
        for (auto& [id, t] : tilesets_) {
            t->update(cam, frustum_, frame_);
            t->draw(cam, models_);
            tilesBusy = tilesBusy || t->busy();
        }
    };
    if (xray) drawObjects();
    sg_apply_pipeline(xray ? globeBlendPipeline_ : globePipeline_);
    globe_fs_params_t gfs{};
    gfs.flags[0] = flipSurface_ ? 1.0f : 0.0f;
    gfs.flags[1] = depthPerception_ && terrainEnabled_ ? 1.0f : 0.0f;
    gfs.flags[3] = terrainAlpha_;
    gfs.heat[0] = static_cast<float>(heatMin_);
    gfs.heat[1] = static_cast<float>(1.0 / std::max(heatMax_ - heatMin_, 1.0));
    gfs.heat[2] = heatmap_ ? heatOpacity_ : 0.0f;
    sg_apply_uniforms(UB_globe_fs_params, SG_RANGE(gfs));
    int maxLevel = 0;
    int incomplete = 0;
    int blank = 0;
    int coarse = 0;
    int maxDepth = 0;
    int deepLogged = 0;
    int coverageOnly = 0, retained = 0;
    for (const DrawTile& dt : tiles) {
        if (dt.kind == 1) ++coverageOnly;
        if (dt.kind == 2) ++retained;
    }
    bool fading = false;
    double tnow = now();
    auto source = [&](const QuadKey& key, int minUp, sg_view& view, float uvx[4]) {
        int bestK = -1;
        int bestZ = -1000;
        SurfaceTile* best = nullptr;
        for (int k = std::max(0, minUp); k <= key.level; ++k) {
            QuadKey a{key.level - k, key.x >> k, key.y >> k};
            auto it = surface_.find(a);
            if (it == surface_.end() || it->second.version < 0) continue;
            int z = it->second.complete ? std::max(it->second.sourceZ, it->second.imageryZ) : it->second.sourceZ;
            if (z > bestZ) {
                bestZ = z;
                bestK = k;
                best = &it->second;
            }
            if (it->second.complete) break;
        }
        if (!best) return QuadKey{-1, 0, 0};
        best->used = frame_;
        view = best->texture;
        double sc = 1.0 / (1 << bestK);
        QuadKey a{key.level - bestK, key.x >> bestK, key.y >> bestK};
        int rx = key.x - (a.x << bestK), ry = key.y - (a.y << bestK);
        uvx[0] = static_cast<float>(sc);
        uvx[1] = static_cast<float>(sc);
        uvx[2] = static_cast<float>(rx * sc);
        uvx[3] = static_cast<float>(((1 << bestK) - 1 - ry) * sc);
        return a;
    };
    if (diagnostics_ && frame_ % 90 == 45 && !tiles.empty()) {
        const DrawTile& probe = tiles.front();
        sg_view pv{};
        float pu[4];
        QuadKey ps = source(probe.key, 0, pv, pu);
        auto os = surface_.find(probe.key);
        auto ss = ps.level >= 0 ? surface_.find(ps) : surface_.end();
        diag("probe frame %llu: L%d/%d/%d kind %d src L%d own %s v%d imgZ %d srcZ %d missing %zu | src surf imgZ %d srcZ %d | failed %zu imagery %zu",
             static_cast<unsigned long long>(frame_), probe.key.level, probe.key.x, probe.key.y, probe.kind, ps.level,
             os == surface_.end() ? "-" : os->second.complete ? "C" : "p", os == surface_.end() ? -9 : os->second.version,
             os == surface_.end() ? -1 : os->second.imageryZ, os == surface_.end() ? -1 : os->second.sourceZ,
             os == surface_.end() ? size_t(0) : os->second.missing.size(), ss == surface_.end() ? -1 : ss->second.imageryZ,
             ss == surface_.end() ? -1 : ss->second.sourceZ, failed_.size(), imagery_.size());
    }
    for (auto& dt : tiles) {
        auto prev = drawn_.find(dt.key);
        if (prev == drawn_.end() || prev->second + 1 < frame_) {
            for (int up = 1; up <= 4 && dt.key.level - up >= 0; ++up) {
                QuadKey a{dt.key.level - up, dt.key.x >> up, dt.key.y >> up};
                auto pa = drawn_.find(a);
                if (pa != drawn_.end() && pa->second + 1 >= frame_) {
                    fades_[dt.key] = {tnow, a};
                    break;
                }
            }
        }
    }
    for (auto& dt : tiles) drawn_[dt.key] = frame_;
    if (drawn_.size() > 20000)
        for (auto it = drawn_.begin(); it != drawn_.end();) it = it->second + 2 < frame_ ? drawn_.erase(it) : std::next(it);
    for (auto& dt : tiles) {
        TerrainTile& tt = terrain(dt.key);
        tt.used = frame_;
        maxLevel = std::max(maxLevel, dt.key.level);
        sg_view view = fallbackView_;
        float uvx[4] = {1, 1, 0, 0};
        QuadKey src = source(dt.key, 0, view, uvx);
        if (src.level < 0) ++blank;
        else {
            auto ownSurface = surface_.find(src);
            int depth = 0;
            int ideal = idealImageryZ(dt.key);
            if (ownSurface != surface_.end()) {
                const SurfaceTile& st = ownSurface->second;
                int effective = st.sourceZ;
                if (ideal >= 0 && effective < 99) depth = std::max(0, ideal - effective);
                else depth = dt.key.level - src.level + (st.complete ? 0 : 1);
            }
            if (depth >= 2) ++coarse;
            maxDepth = std::max(maxDepth, depth);
            if (diagnostics_ && depth >= 3 && frame_ % 90 == 0 && deepLogged < 6) {
                ++deepLogged;
                bool needed = false;
                for (const Need& n : needs_)
                    if (n.key == dt.key) {
                        needed = true;
                        break;
                    }
                auto os = surface_.find(dt.key);
                auto ot = terrain_.find(dt.key);
                diag("deep frame %llu: L%d/%d/%d src L%d depth %d kind %s needed %d sticky %d surf %s missing %zu terr %s | needs %zu imagery %zu/%zu pending %d",
                     static_cast<unsigned long long>(frame_), dt.key.level, dt.key.x, dt.key.y, src.level, depth,
                     dt.kind == 1 ? "coverage" : dt.kind == 2 ? "retained" : "normal", needed ? 1 : 0, wasRefined(dt.key) ? 1 : 0,
                     os == surface_.end() ? "-" : os->second.complete ? "C" : os->second.version < 0 ? "0" : "p",
                     os == surface_.end() ? size_t(0) : os->second.missing.size(),
                     ot == terrain_.end() ? "-" : ot->second.complete ? "C" : "p", needs_.size(), imagery_.size(), imageryCapacity_,
                     loader_.pending());
            }
        }
        auto own = surface_.find(dt.key);
        if (own == surface_.end() || !own->second.complete) ++incomplete;
        sg_view view2 = view;
        float uvx2[4] = {uvx[0], uvx[1], uvx[2], uvx[3]};
        float blend = 1.0f;
        auto fd = fades_.find(dt.key);
        if (fd != fades_.end()) {
            double t = (tnow - fd->second.start) / 0.35;
            if (t >= 1.0 || src.level < 0) {
                fades_.erase(fd);
            } else {
                QuadKey from = fd->second.from;
                int up = dt.key.level - from.level;
                QuadKey got = source(dt.key, up, view2, uvx2);
                if (got.level >= 0 && got.level < src.level) {
                    blend = static_cast<float>(t * t * (3 - 2 * t));
                    fading = true;
                }
            }
        }
        if (sg_query_buffer_state(tt.vbuf) != SG_RESOURCESTATE_VALID) {
            ++invalidResources_;
            continue;
        }
        if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
            ++invalidResources_;
            view = fallbackView_;
        }
        if (sg_query_view_state(view2) != SG_RESOURCESTATE_VALID) {
            ++invalidResources_;
            view2 = view;
        }
        Mat4 mvp = cam.viewProjRel() * translation(tt.center - cam.eye());
        globe_vs_params_t vsp{};
        toFloat(mvp, vsp.mvp);
        std::memcpy(vsp.uv_xform, uvx, sizeof(uvx));
        std::memcpy(vsp.uv_xform2, uvx2, sizeof(uvx2));
        gfs.flags[2] = blend;
        sg_apply_uniforms(UB_globe_fs_params, SG_RANGE(gfs));
        sg_bindings bind{};
        bind.vertex_buffers[0] = tt.vbuf;
        bind.index_buffer = indexBuffer_;
        bind.views[VIEW_tex] = view;
        bind.views[VIEW_tex2] = view2;
        bind.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&bind);
        sg_apply_uniforms(UB_globe_vs_params, SG_RANGE(vsp));
        sg_draw(0, xray ? kGrid * kGrid * 6 : kIndexCount, 1);
    }
    if (!xray) drawObjects();
    std::vector<FeatureLayer::ExternalLabel> vectorLabels;
    if (!vectorLayers_.empty()) {
        std::unordered_set<TileId, TileIdHash> seen;
        for (const auto& dt : tiles) {
            Bounds b = bounds(dt.key);
            for (int l = 0; l < static_cast<int>(matrices_.size()) && l < static_cast<int>(vectorLayers_.size()); ++l) {
                if (!vectorLayers_[l]) continue;
                const TileMatrix& m = matrices_[l];
                double target = (b.north - b.south) / kSurfaceSize * m.unitsPerDegree();
                int z = m.bestLevel(target);
                if (z < 0) continue;
                double clat = std::clamp(0.5 * (b.south + b.north), -84.0, 84.0), clon = 0.5 * (b.west + b.east);
                for (int zz = z; zz >= 0; --zz) {
                    if (!m.hasLevel(zz)) continue;
                    TileId id{l, zz, static_cast<int>(std::floor((projectX(m.srid, clon) - m.originX) / m.spanX(zz))),
                              static_cast<int>(std::floor((m.originY - projectY(m.srid, clat)) / m.spanY(zz)))};
                    auto it = imagery_.find(id);
                    if (it == imagery_.end() || !it->second.vector) continue;
                    if (!seen.insert(id).second) break;
                    for (const VectorLabel& vl : it->second.vector->labels)
                        vectorLabels.push_back({vl.lat, vl.lon, vl.text, vl.priority, vl.color, vl.size, true});
                    break;
                }
            }
        }
    }
    features_.drawOverlay(cam, [this](double lat, double lon) { return elevationAt(lat, lon); }, dpiScale_, flipSurface_,
                          &vectorLabels);
    sg_end_pass();

    bool pending = false;
    for (const Need& n : needs_)
        if (!ready(n.key)) {
            pending = true;
            break;
        }
    if (diagnostics_ && prevGsd_ > 0 && maxLevel + 2 < prevMaxLevel_ && std::abs(std::log2(cam.gsd / prevGsd_)) < 1.0)
        explainCollapse(cam, tiles);
    prevMaxLevel_ = maxLevel;
    prevGsd_ = cam.gsd;
    stats_.terrainTiles = static_cast<int>(tiles.size());
    profileMark(6);
    stats_.surfaceRenders = renders;
    stats_.hollowSkipped = hollowSkipped;
    stats_.placeholders = loader_.placeholders();
    stats_.terrainBuilds = builds;
    stats_.imageryTextures = static_cast<int>(imagery_.size());
    stats_.imageryPending = loader_.pending();
    stats_.elevationPending = elevation_.pending();
    stats_.pendingRequests = stats_.imageryPending + stats_.elevationPending;
    stats_.maxLevel = maxLevel;
    stats_.incompleteTiles = incomplete;
    stats_.blankTiles = blank;
    stats_.coarseTiles = coarse;
    stats_.coverageOnlyTiles = coverageOnly;
    stats_.retainedTiles = retained;
    stats_.maxFallbackDepth = maxDepth;
    stats_.surfaces = static_cast<int>(surface_.size());
    stats_.freeSurfaces = static_cast<int>(freeSurfaces_.size());
    stats_.invalidResources = invalidResources_;
    stats_.terrainMeshes = static_cast<int>(terrain_.size());
    stats_.gpuMegabytes = (surface_.size() + freeSurfaces_.size()) * (kSurfaceSize * kSurfaceSize * 4.0 * 4.0 / 3.0) / 1048576.0 +
                          imagery_.size() * (256.0 * 256.0 * 4.0) / 1048576.0 + terrain_.size() * (kVertexCount * kStride * 4.0) / 1048576.0;
    viewComplete_ = incomplete == 0;
    busy_ = pending || fading || tilesBusy || stats_.pendingRequests > 0 || renders > 0 || builds > 0 || elevationChanged;
    if (trace_) {
        int waitImagery = 0, waitTerrain = 0, unrendered = 0;
        for (auto& dt : tiles) {
            auto sf = surface_.find(dt.key);
            auto tf = terrain_.find(dt.key);
            if (sf == surface_.end()) ++unrendered;
            else if (!sf->second.complete) ++waitImagery;
            if (tf == terrain_.end() || !tf->second.complete) ++waitTerrain;
        }
        std::fprintf(trace_,
                     "frame %llu tiles %zu needs %zu work %zu renders %d builds %d loader %d elev %d incomplete %d "
                     "waitimg %d unrendered %d waitterrain %d L%d gsd %.1f\n",
                     static_cast<unsigned long long>(frame_), tiles.size(), needs_.size(), work.size(), renders, builds,
                     loader_.pending(), elevation_.pending(), incomplete, waitImagery, unrendered, waitTerrain, maxLevel,
                     cam.gsd);
        if (std::getenv("OTE_TRACE_TILES")) {
            GeoPoint eg = ecefToGeodetic(cam.eye());
            std::fprintf(trace_, "  eye %.5f %.5f alt %.1f ground %.1f\n", eg.lat, eg.lon, eg.alt, elevationAt(eg.lat, eg.lon));
            for (size_t i = 0; i < tiles.size() && i < 6; ++i) {
                Bounds tb = bounds(tiles[i].key);
                std::fprintf(trace_, "  draw L%d %d,%d dist %.0f bounds %.4f %.4f %.4f %.4f\n", tiles[i].key.level, tiles[i].key.x,
                             tiles[i].key.y, tiles[i].distance, tb.west, tb.south, tb.east, tb.north);
            }
        }
        std::fprintf(trace_, "  loader %s\n", loader_.debug().c_str());
        int shown = 0;
        for (auto& dt : tiles) {
            auto sf = surface_.find(dt.key);
            if (sf == surface_.end() || sf->second.complete || shown >= 2) continue;
            ++shown;
            std::fprintf(trace_, "  tile L%d %d,%d missing", dt.key.level, dt.key.x, dt.key.y);
            for (const TileId& m : sf->second.missing) {
                auto f = failed_.find(m);
                std::fprintf(trace_, " [%d/%d/%d %s]", m.z, m.x, m.y,
                             imagery_.count(m) ? "have" : f != failed_.end() ? (std::isinf(f->second) ? "perm" : "retry") : "wait");
            }
            std::fprintf(trace_, "\n");
        }
        std::fflush(trace_);
    }
}

}
