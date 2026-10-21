#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_activity.h>
#endif
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define SOKOL_APP_IMPL
#define SOKOL_GLUE_IMPL
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include "ote/engine.hpp"
#include "ote/formats.hpp"
#include "shot.hpp"
#include "watchdog.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

void parseArgs(int argc, char** argv);
void loadScript(const char* path);
bool runScript();
#if defined(_WIN32)
void installPointerInput(const void* hwnd, ote::InputController* input);
#endif

namespace {

struct ViewerState {
    ote::Engine engine;
    float mouseX = 0, mouseY = 0;
    int preset = 0;
    ote::View startView;
    bool offscreen = false;
    std::string shotPath;
    std::string filmDir;
    double filmEvery = 0.25;
    double filmNext = 0.0;
    int filmFrame = 0;
    double shotTimeout = 60.0;
    double shotElapsed = 0.0;
    int settledFrames = 0;
    int frames = 0;
    sg_image shotColor{};
    sg_image shotDepth{};
    sg_view shotColorView{};
    sg_view shotDepthView{};
    std::string cacheDir;
    bool offline = false;
    std::vector<ote::ImageryLayer> customLayers;
    bool hasDownload = false;
    ote::Region download;
    int downloadMin = 0, downloadMax = 0;
    int downloadId = 0;
    std::vector<std::string> imports;
    int gridType = 0;
    bool slope = false;
    double contourInterval = 0;
    bool contoursDone = false;
    std::vector<std::array<double, 3>> bullseyes;
    std::vector<std::array<double, 4>> rings;
    std::vector<std::array<double, 4>> rbLines;
    bool rbStarted = false;
    double rbLat = 0, rbLon = 0;
    bool hasExport = false;
    ote::Region exportRegion;
    int exportMin = 0, exportMax = 0;
    std::string exportPath;
    int cacheLimit = 0;
    std::vector<std::string> featureFiles;
    std::vector<std::string> openPaths;
    std::vector<ote::ImageryLayer> addedLayers;
    std::vector<ote::ElevationLayer> elevation;
    std::string status;
    double statusTime = 0;
    bool viewGiven = false;
    std::vector<std::string> tilesets;
    double stereo = -1.0;
    double sunTime = -1.0;
    std::vector<std::string> pointClouds;
    std::vector<std::pair<std::string, ote::ModelPlacement>> models;
    std::string selected;
    bool hasHit = false;
    double hitX = 0, hitY = 0;
    bool terrain = true;
    bool grid = false;
    bool heat = false;
    std::uint64_t viewshed = 0;
    bool shotViewshed = false;
    bool zoomTest = false;
    int bench = 0;
    bool mouseDown = false;
    sapp_mousebutton mouseButton = SAPP_MOUSEBUTTON_LEFT;
    bool diag = false;
    bool holes = false;
    long long scriptFrames = 0, scriptIncomplete = 0, scriptCoarse = 0, scriptMaxDepth = 0, scriptLoads0 = -1, scriptLoads = 0;
    int holeChecks = 0;
    int holeFrames = 0;
    int holeMax = 0;
    double zoomStartAfter = 0;
    struct ScriptStep {
        double at = 0;
        std::string op;
        double v[6] = {};
    };
    std::vector<ScriptStep> script;
    size_t scriptNext = 0;
    bool dragging = false;
    ScriptStep drag;
    int perfFrames = 0;
    double perfFrameMs = 0, perfFrameMax = 0, perfCpuMs = 0, perfCpuMax = 0;
    long long perfRenders = 0, perfBuilds = 0, perfUploads = 0;
    int perfIncompleteFrames = 0, perfIncompleteMax = 0, perfBlankMax = 0, perfCoarseFrames = 0, perfMaxDepth = 0;
    float xray = 1.0f;
    int benchDone = 0;
    double benchTotal = 0, benchMax = 0;
    int memory = 0;
    bool shotFirstPerson = false;
    double fpHeading = 0, fpPitch = 0, fpHeight = 2.0;
    ote::View zoomTarget;
    double zoomSeconds = 4.0;
    bool zoomStarted = false;
    long long zoomFrames = 0, zoomIncomplete = 0, zoomMaxIncomplete = 0, zoomBadFrames = 0, zoomCoarseFrames = 0, zoomMaxDepth = 0, zoomBlankFrames = 0;
    bool depth = true;
    double exaggeration = 1.0;
    std::string elevationDir;
    std::string gdalDem;
    std::string quantizedMesh;
    int width = 1600;
    int height = 900;
    double titleTimer = 0;
};

ViewerState* state = nullptr;
FILE* logFile = nullptr;

void openPath(const std::string& path);

void fileLog(const char* tag, uint32_t level, uint32_t item, const char* msg, uint32_t line, const char* file, void*) {
    if (!logFile) return;
    std::fprintf(logFile, "[%s] level %u item %u line %u %s: %s\n", tag ? tag : "", level, item, line, file ? file : "",
                 msg ? msg : "");
    std::fflush(logFile);
}

std::vector<ote::ImageryLayer> preset(int i) {
    std::vector<ote::ImageryLayer> layers = ote::defaultLayers();
    if (i == 1) {
        ote::ImageryLayer topo;
        topo.name = "USGS Topo";
        topo.url = "https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/{z}/{y}/{x}";
        topo.minZoom = 3;
        topo.maxZoom = 16;
        layers.push_back(topo);
    } else if (i == 3) {
        ote::ImageryLayer v;
        v.name = "OpenFreeMap";
        v.type = ote::SourceType::Mvt;
        v.url = "https://tiles.openfreemap.org/planet";
        v.maxZoom = 14;
        layers = {v, ote::polarFillLayer()};
    } else if (i == 2) {
        ote::ImageryLayer osm;
        osm.name = "OpenStreetMap";
        osm.url = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
        osm.maxZoom = 19;
        layers = {osm, ote::polarFillLayer()};
    }
    return layers;
}

void init() {
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    if (const char* lp = std::getenv("OTE_LOG")) {
        logFile = std::fopen(lp, "w");
        desc.logger.func = fileLog;
    }
    ote::configureGfx(desc);
    if (const char* wd = std::getenv("OTE_WATCHDOG")) watchdogStart(wd);
    sg_setup(&desc);
    state->engine.init();
    state->engine.input().onTap([](double x, double y, int taps, ote::PointerType) {
        if (taps != 1) return;
        ote::Feature f;
        std::uint64_t id = state->engine.hitTest(x, y, 16.0 * sapp_dpi_scale());
        state->selected = id && state->engine.feature(id, f) ? (f.name.empty() ? "feature " + std::to_string(id) : f.name) : "";
    });
#if defined(_WIN32)
    installPointerInput(sapp_win32_get_hwnd(), &state->engine.input());
#endif
    state->engine.setViewport(sapp_width(), sapp_height(), sapp_dpi_scale());
#if defined(__ANDROID__)
    {
        auto* activity = static_cast<const ANativeActivity*>(sapp_android_get_native_activity());
        ote::setAndroidJavaVm(activity->vm);
        if (state->cacheDir.empty() && activity->internalDataPath) state->cacheDir = activity->internalDataPath;
        if (activity->externalDataPath) {
            std::string path = std::string(activity->externalDataPath) + "/args.txt";
            if (FILE* f = std::fopen(path.c_str(), "r")) {
                std::vector<std::string> tokens{"ote_viewer"};
                char buf[1024];
                while (std::fscanf(f, "%1023s", buf) == 1) tokens.push_back(buf);
                std::fclose(f);
                std::vector<char*> args;
                for (auto& t : tokens) args.push_back(t.data());
                parseArgs(static_cast<int>(args.size()), args.data());
                __android_log_print(ANDROID_LOG_INFO, "OpenTAKEngine", "args %zu from %s", tokens.size() - 1, path.c_str());
            }
        }
    }
#endif
    std::string cache = state->cacheDir;
    if (cache.empty()) {
        if (const char* la = std::getenv("LOCALAPPDATA")) cache = std::string(la) + "/OpenTAKEngine";
        else if (const char* home = std::getenv("HOME")) cache = std::string(home) + "/.cache/OpenTAKEngine";
    }
    if (!cache.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(cache, ec);
        state->engine.setCacheDirectory(cache);
    }
    state->engine.setOffline(state->offline);
    if (state->memory > 0) state->engine.setMemoryBudget(state->memory);
    {
        std::vector<ote::ElevationLayer>& elev = state->elevation;
        if (!state->elevationDir.empty()) {
            ote::ElevationLayer d;
            d.type = ote::ElevationType::Directory;
            d.path = state->elevationDir;
            d.maxZoom = 14;
            elev.push_back(d);
        }
        if (!state->quantizedMesh.empty()) {
            ote::ElevationLayer q;
            q.type = ote::ElevationType::QuantizedMesh;
            q.url = state->quantizedMesh;
            q.maxZoom = 14;
            elev.push_back(q);
        }
        if (!state->gdalDem.empty()) {
            ote::ElevationLayer g;
            g.type = ote::ElevationType::Gdal;
            g.path = state->gdalDem;
            elev.push_back(g);
        }
        for (auto& e : ote::defaultElevation()) elev.push_back(e);
        state->engine.setElevation(elev);
        state->engine.setTerrainEnabled(state->terrain);
        state->engine.setDiagnostics(state->diag);
        state->engine.setTerrainExaggeration(state->exaggeration);
    }
    state->engine.setLayers(state->customLayers.empty() ? preset(state->preset) : state->customLayers);
    for (const auto& f : state->featureFiles) state->engine.loadFeatures(f);
    state->engine.setGridEnabled(state->grid);
    state->engine.setElevationHeatmap(state->heat);
    state->engine.setTerrainTransparency(state->xray);

    for (const auto& [path, placement] : state->models) state->engine.addModel(path, placement);
    for (const auto& t : state->tilesets) state->engine.addTileset(t);
    if (state->sunTime >= 0) state->engine.setSunLighting(true, state->sunTime);
    for (const auto& p : state->pointClouds) state->engine.addPointCloud(p);
    if (state->cacheLimit > 0) state->engine.setCacheLimit(state->cacheLimit);
    state->engine.setGridType(static_cast<ote::GridType>(state->gridType));
    state->engine.setTerrainSlope(state->slope);
    for (const auto& b : state->bullseyes)
        for (const ote::Feature& f : ote::bullseye({b[0], b[1], 0}, b[2])) state->engine.addFeature(f);
    for (const auto& r : state->rings)
        for (const ote::Feature& f : ote::rangeRings({r[0], r[1], 0}, r[2], static_cast<int>(r[3]), 8)) state->engine.addFeature(f);
    for (const auto& r : state->rbLines)
        for (const ote::Feature& f : ote::rangeBearingLine({r[0], r[1], 0}, {r[2], r[3], 0})) state->engine.addFeature(f);
    for (const auto& path : state->imports) {
        std::string err;
        long long n = state->engine.importTiles(0, path, &err);
        std::printf("import %s: %lld tiles%s%s\n", path.c_str(), n, err.empty() ? "" : " ", err.c_str());
    }
    if (state->hasDownload)
        state->downloadId = state->engine.downloadRegion(0, state->download, state->downloadMin, state->downloadMax);
    state->engine.setView(state->startView);
    if (state->shotFirstPerson) {
        state->engine.setFirstPerson(true, state->fpHeight);
        state->engine.look(state->fpHeading, state->fpPitch);
    }
    for (const auto& p : state->openPaths) openPath(p);
    if (!state->shotPath.empty()) {
        sg_environment env = sglue_environment();
        sg_image_desc c{};
        c.usage.color_attachment = true;
        c.width = state->width;
        c.height = state->height;
        c.pixel_format = env.defaults.color_format;
        c.sample_count = 1;
        state->shotColor = sg_make_image(&c);
        sg_image_desc dd{};
        dd.usage.depth_stencil_attachment = true;
        dd.width = state->width;
        dd.height = state->height;
        dd.pixel_format = env.defaults.depth_format;
        dd.sample_count = 1;
        state->shotDepth = sg_make_image(&dd);
        sg_view_desc cv{};
        cv.color_attachment.image = state->shotColor;
        state->shotColorView = sg_make_view(&cv);
        sg_view_desc dv{};
        dv.depth_stencil_attachment.image = state->shotDepth;
        state->shotDepthView = sg_make_view(&dv);
    }
#if defined(_WIN32)
    if (state->offscreen) {
        HWND h = static_cast<HWND>(const_cast<void*>(sapp_win32_get_hwnd()));
        SetWindowPos(h, HWND_BOTTOM, -6000, 0, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(h, SW_SHOWNOACTIVATE);
    }
#endif
}

void checkHoles() {
    std::vector<unsigned char> px;
    if (!readImageRgba(state->shotColor, state->width, state->height,
#if defined(SOKOL_D3D11)
                       sg_d3d11_device(), sg_d3d11_device_context(),
#else
                       nullptr, nullptr,
#endif
                       px))
        return;
    int blocks = 0, hx = -1, hy = -1;
    for (int by = 0; by + 8 <= state->height; by += 8)
        for (int bx = 0; bx + 8 <= state->width; bx += 8) {
            int n = 0;
            for (int y = by; y < by + 8; ++y)
                for (int x = bx; x < bx + 8; ++x) {
                    const unsigned char* p = &px[(static_cast<size_t>(y) * state->width + x) * 4];
                    if (p[0] > 240 && p[1] < 16 && p[2] > 240) ++n;
                }
            if (n > 32) {
                ++blocks;
                hx = bx + 4;
                hy = by + 4;
            }
        }
    ++state->holeChecks;
    if (blocks == 0) return;
    ++state->holeFrames;
    ote::View v = state->engine.view();
    if (const char* dp = std::getenv("OTE_DIAG"))
        if (FILE* df = std::fopen(dp, "a")) {
            ote::Stats hs = state->engine.stats();
            std::fprintf(df, "holes frame %d t=%.2f blocks %d at %d,%d tiles %d L%d cov %d ret %d inc %d hollow %d ph %d surf %d free %d alt %.1f | view %.5f %.5f gsd %.2f tilt %.1f az %.1f\n", state->frames, state->shotElapsed, blocks, hx, hy, hs.terrainTiles,
                         hs.maxLevel, hs.coverageOnlyTiles, hs.retainedTiles, hs.incompleteTiles, hs.hollowSkipped, hs.placeholders, hs.surfaces, hs.freeSurfaces, v.alt, v.lat, v.lon, v.gsd, v.tilt, v.azimuth);
            std::fclose(df);
        }
    if (blocks > state->holeMax) {
        state->holeMax = blocks;
        writePng((state->shotPath + ".holes.png").c_str(), state->width, state->height, px);
    }
}

void frame() {
    watchdogBeat();
    if (!state->shotPath.empty()) {
        state->engine.setViewport(state->width, state->height, 1.0f);
        sg_attachments att{};
        att.colors[0] = state->shotColorView;
        att.depth_stencil = state->shotDepthView;
        auto benchStart = std::chrono::steady_clock::now();
        if (state->stereo >= 0) state->engine.frameStereoOffscreen(att, state->width, state->height, state->stereo);
        else state->engine.frameOffscreen(att, state->width, state->height);
        sg_commit();
        double benchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - benchStart).count();
        ++state->frames;
        state->shotElapsed += sapp_frame_duration();
        if (state->holes && state->frames % 3 == 0) checkHoles();
        if (!state->filmDir.empty() && state->shotElapsed >= state->filmNext) {
            state->filmNext = state->shotElapsed + state->filmEvery;
            std::vector<unsigned char> px;
            if (readImageRgba(state->shotColor, state->width, state->height,
#if defined(SOKOL_D3D11)
                              sg_d3d11_device(), sg_d3d11_device_context(),
#else
                              nullptr, nullptr,
#endif
                              px)) {
                const int w = state->width / 2, h = state->height / 2;
                std::vector<unsigned char> half(static_cast<size_t>(w) * h * 4);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        for (int c = 0; c < 4; ++c) {
                            const size_t a = (static_cast<size_t>(2 * y) * state->width + 2 * x) * 4 + c;
                            const size_t row = static_cast<size_t>(state->width) * 4;
                            half[(static_cast<size_t>(y) * w + x) * 4 + c] =
                                static_cast<unsigned char>((px[a] + px[a + 4] + px[a + row] + px[a + row + 4] + 2) / 4);
                        }
                char name[64];
                std::snprintf(name, sizeof(name), "/frame-%04d.png", state->filmFrame);
                writePng((state->filmDir + name).c_str(), w, h, half);
                if (FILE* f = std::fopen((state->filmDir + "/film.csv").c_str(), state->filmFrame == 0 ? "w" : "a")) {
                    if (state->filmFrame == 0) std::fprintf(f, "frame,ms,step,verb,pending,wants,settled\n");
                    std::fprintf(f, "%d,%.0f,0,settle,%d,%d,0\n", state->filmFrame, state->shotElapsed * 1000.0,
                                 state->engine.stats().pendingRequests, state->engine.needsRedraw() ? 1 : 0);
                    std::fclose(f);
                }
                ++state->filmFrame;
            }
        }
        if (state->shotViewshed && !state->viewshed && state->shotElapsed > 1.0 && !state->engine.needsRedraw()) {
            ote::View v = state->engine.view();
            state->viewshed = state->engine.computeViewshed({v.lat, v.lon, 0}, 2.0, 5000.0);
        }
        if (state->contourInterval > 0 && !state->contoursDone && state->shotElapsed > 1.0 && !state->engine.needsRedraw()) {
            state->engine.addContours(state->engine.viewRegion(), state->contourInterval);
            state->contoursDone = true;
        }
        state->settledFrames = state->engine.needsRedraw() ? 0 : state->settledFrames + 1;
        if (state->zoomTest) {
            bool startNow = state->zoomStartAfter > 0 ? state->shotElapsed > state->zoomStartAfter
                                                      : (state->shotElapsed > 1.0 && !state->engine.needsRedraw());
            if (!state->zoomStarted && startNow) {
                state->engine.flyTo(state->zoomTarget, state->zoomSeconds);
                state->zoomStarted = true;
            } else if (state->zoomStarted && state->engine.animating()) {
                int inc = state->engine.stats().incompleteTiles;
                ++state->zoomFrames;
                state->zoomIncomplete += inc;
                state->zoomMaxIncomplete = std::max<long long>(state->zoomMaxIncomplete, inc);
                if (state->engine.stats().coarseTiles > 0) ++state->zoomCoarseFrames;
                if (state->engine.stats().blankTiles > 0) ++state->zoomBlankFrames;
                state->zoomMaxDepth = std::max<long long>(state->zoomMaxDepth, state->engine.stats().maxFallbackDepth);
                if (inc > 0) ++state->zoomBadFrames;
            }
        }
        bool scripting = runScript();
        if (scripting && state->scriptNext > 0) {
            ote::Stats ss = state->engine.stats();
            if (state->scriptLoads0 < 0) state->scriptLoads0 = ss.imageryLoads;
            state->scriptLoads = ss.imageryLoads - state->scriptLoads0;
            ++state->scriptFrames;
            if (ss.incompleteTiles > 0) ++state->scriptIncomplete;
            if (ss.coarseTiles > 0) ++state->scriptCoarse;
            state->scriptMaxDepth = std::max<long long>(state->scriptMaxDepth, ss.maxFallbackDepth);
        }
        bool zooming = scripting || (state->zoomTest && (!state->zoomStarted || state->engine.animating()));
        if (state->bench > 0 && state->shotElapsed > 1.0 && state->settledFrames > 10 && !zooming) {
            state->engine.rotate(0.05);
            ++state->benchDone;
            state->benchTotal += benchMs;
            state->benchMax = std::max(state->benchMax, benchMs);
        }
        zooming = zooming || (state->bench > 0 && state->benchDone < state->bench);
        bool downloading = (state->downloadId && !state->engine.downloadStatus(state->downloadId).finished) || zooming;
        bool settled = state->shotElapsed > 1.0 && state->settledFrames > 10 && !downloading &&
                       (!state->shotViewshed || state->viewshed) && (state->contourInterval <= 0 || state->contoursDone);
        if (settled || state->shotElapsed >= state->shotTimeout) {
            bool ok = saveImagePng(state->shotColor, state->width, state->height,
#if defined(SOKOL_D3D11)
                                   sg_d3d11_device(), sg_d3d11_device_context(),
#else
                                   nullptr, nullptr,
#endif
                                   state->shotPath.c_str());
            ote::Stats s = state->engine.stats();
            std::string logPath = state->shotPath + ".txt";
            if (FILE* f = std::fopen(logPath.c_str(), "w")) {
                std::fprintf(f, "resources invalid=%d terrain=%d surfaces=%d free=%d imagery=%d gpu_mb=%.0f\n", s.invalidResources, s.terrainMeshes, s.surfaces,
                             s.freeSurfaces, s.imageryTextures, s.gpuMegabytes);
                std::fprintf(f, "timing frame_p50=%.2f frame_p95=%.2f frame_p99=%.2f render_p95=%.2f render_p99=%.2f draws=%d\n", s.frameMsP50,
                             s.frameMsP95, s.frameMsP99, s.renderMsP95, s.renderMsP99, s.drawCalls);
                std::fprintf(f, "tilesets drawn=%d failed=%d\n", s.tilesetTiles, s.tilesetFailed);
                for (const std::string& c : state->engine.credits()) std::fprintf(f, "credit %s\n", c.c_str());
                if (state->scriptFrames > 0)
                    std::fprintf(f, "script frames=%lld incomplete_frames=%lld coarse_frames=%lld max_depth=%lld imagery_loads=%lld\n", state->scriptFrames,
                                 state->scriptIncomplete, state->scriptCoarse, state->scriptMaxDepth, state->scriptLoads);
                std::fprintf(f, "evictions surfaces=%lld imagery=%lld terrain=%lld loads=%lld\n", s.surfaceEvictions, s.imageryEvictions, s.terrainEvictions,
                             s.imageryLoads);
                if (state->holes) std::fprintf(f, "holes checked=%d frames=%d max_blocks=%d\n", state->holeChecks, state->holeFrames, state->holeMax);
                std::fprintf(f, "%s frames=%d seconds=%.1f settled=%d tiles=%d L%d imagery=%d pending=%d img_pending=%d elev_pending=%d\n",
                             ok ? "saved" : "failed", state->frames, state->shotElapsed, settled ? 1 : 0,
                             s.terrainTiles, s.maxLevel, s.imageryTextures, s.pendingRequests, s.imageryPending, s.elevationPending);
                std::fprintf(f, "coverage blank=%d incomplete=%d coarse=%d deficit=%d coverage_only=%d retained=%d\n", s.blankTiles,
                             s.incompleteTiles, s.coarseTiles, s.maxFallbackDepth, s.coverageOnlyTiles, s.retainedTiles);
                ote::View fv = state->engine.view();
                std::fprintf(f, "focus %.5f %.5f elevation %.1f", fv.lat, fv.lon, state->engine.elevationAt(fv.lat, fv.lon));
                std::fputc(10, f);
                if (state->bench > 0)
                    std::fprintf(f, "bench frames=%d avg_ms=%.2f max_ms=%.2f\n", state->benchDone,
                                 state->benchDone ? state->benchTotal / state->benchDone : 0.0, state->benchMax);
                if (state->zoomTest)
                    std::fprintf(f, "zoom frames=%lld frames_with_incomplete=%lld avg_incomplete=%.2f max_incomplete=%lld\n",
                                 state->zoomFrames, state->zoomBadFrames,
                                 state->zoomFrames ? static_cast<double>(state->zoomIncomplete) / state->zoomFrames : 0.0,
                                 state->zoomMaxIncomplete);
                if (state->zoomTest)
                    std::fprintf(f, "zoom coarse_frames=%lld max_fallback_depth=%lld blank_frames=%lld\n", state->zoomCoarseFrames, state->zoomMaxDepth, state->zoomBlankFrames);
                std::fprintf(f, "features %zu viewshed %llu", state->engine.featureIds().size(),
                             static_cast<unsigned long long>(state->viewshed));
                std::fputc(10, f);
                if (state->hasHit) {
                    ote::Feature hf;
                    std::uint64_t hid = state->engine.hitTest(state->hitX, state->hitY, 16.0);
                    std::fprintf(f, "hit %llu %s", static_cast<unsigned long long>(hid),
                                 hid && state->engine.feature(hid, hf) ? hf.name.c_str() : "-");
                    std::fputc(10, f);
                }
                if (state->downloadId) {
                    ote::DownloadStatus d = state->engine.downloadStatus(state->downloadId);
                    std::fprintf(f, "download total=%lld done=%lld failed=%lld finished=%d\n",
                                 static_cast<long long>(d.total), static_cast<long long>(d.done),
                                 static_cast<long long>(d.failed), d.finished ? 1 : 0);
                }
                if (state->hasExport) {
                    std::string err;
                    long long n = state->engine.exportTiles(0, state->exportRegion, state->exportMin, state->exportMax, state->exportPath, &err);
                    std::fprintf(f, "export tiles=%lld %s\n", n, err.c_str());
                }
                {
                    ote::Stats net = state->engine.stats();
                    std::fprintf(f, "network unreachable_hosts=%d stale_tiles=%lld cache_mb=%.1f\n", net.unreachableHosts, net.staleTiles,
                                 static_cast<double>(state->engine.cacheBytes()) / 1048576.0);
                }
                std::fclose(f);
            }
            sapp_quit();
        }
        return;
    }
    state->engine.setViewport(sapp_width(), sapp_height(), sapp_dpi_scale());
    if (state->stereo >= 0) state->engine.frameStereo(sglue_swapchain(), state->stereo);
    else state->engine.frame(sglue_swapchain());
    sg_commit();
    {
        ote::Stats ps = state->engine.stats();
        double ft = sapp_frame_duration() * 1000.0;
        state->perfFrames++;
        state->perfFrameMs += ft;
        state->perfFrameMax = std::max(state->perfFrameMax, ft);
        state->perfCpuMs += ps.cpuMs;
        state->perfCpuMax = std::max(state->perfCpuMax, ps.cpuMs);
        state->perfRenders += ps.surfaceRenders;
        state->perfIncompleteMax = std::max(state->perfIncompleteMax, ps.incompleteTiles);
        state->perfBlankMax = std::max(state->perfBlankMax, ps.blankTiles);
        if (ps.incompleteTiles > 0) state->perfIncompleteFrames++;
        if (ps.coarseTiles > 0) state->perfCoarseFrames++;
        state->perfMaxDepth = std::max(state->perfMaxDepth, ps.maxFallbackDepth);
        state->perfBuilds += ps.terrainBuilds;
        state->perfUploads += ps.uploads;
        if (state->perfFrames >= 30) {
            char line[768];
            ote::View pv = state->engine.view();
            std::snprintf(line, sizeof(line),
                          "perf frame avg %.1f max %.1f ms | cpu avg %.2f max %.2f | surf %.1f terr %.1f up %.1f | tiles %d L%d | "
                          "incomplete frames %d max %d blank max %d coarse frames %d depth %d pre %d hollow %d ph %d | surfaces %d+%d img %d terr %d est %.0fMB | evict s %lld i %lld t %lld loads %lld | view %.4f %.4f gsd %.1f az %.0f tilt %.0f",
                          state->perfFrameMs / state->perfFrames, state->perfFrameMax, state->perfCpuMs / state->perfFrames,
                          state->perfCpuMax, state->perfRenders / double(state->perfFrames), state->perfBuilds / double(state->perfFrames),
                          state->perfUploads / double(state->perfFrames), ps.terrainTiles, ps.maxLevel, state->perfIncompleteFrames,
                          state->perfIncompleteMax, state->perfBlankMax, state->perfCoarseFrames, state->perfMaxDepth, ps.prefetched, ps.hollowSkipped, ps.placeholders, ps.surfaces, ps.freeSurfaces, ps.imageryTextures, ps.terrainMeshes, ps.gpuMegabytes, ps.surfaceEvictions, ps.imageryEvictions,
                          ps.terrainEvictions, ps.imageryLoads, pv.lat, pv.lon, pv.gsd, pv.azimuth, pv.tilt);
            state->perfIncompleteFrames = state->perfIncompleteMax = state->perfBlankMax = 0;
            state->perfCoarseFrames = state->perfMaxDepth = 0;
#if defined(__ANDROID__)
            __android_log_write(ANDROID_LOG_INFO, "OpenTAKEngine", line);
#else
            if (const char* pf = std::getenv("OTE_PERF")) {
                if (FILE* pfile = std::fopen(pf, "a")) {
                    std::fputs(line, pfile);
                    std::fputc(10, pfile);
                    std::fclose(pfile);
                }
            }
#endif
            state->perfFrames = 0;
            state->perfFrameMs = state->perfFrameMax = state->perfCpuMs = state->perfCpuMax = 0;
            state->perfRenders = state->perfBuilds = state->perfUploads = 0;
        }
    }
    state->titleTimer += sapp_frame_duration();
    state->statusTime = std::max(0.0, state->statusTime - sapp_frame_duration());
    if (state->titleTimer > 0.5) {
        state->titleTimer = 0;
        ote::Stats s = state->engine.stats();
        ote::View v = state->engine.view();
        char title[256];
        std::snprintf(title, sizeof(title),
                      "OpenTAKEngine  %.5f, %.5f  gsd %.2f m/px  tilt %.0f  az %.0f  |  tiles %d  L%d  imagery %d  pending %d  %s",
                      v.lat, v.lon, v.gsd, v.tilt, v.azimuth, s.terrainTiles, s.maxLevel, s.imageryTextures,
                      s.pendingRequests, state->statusTime > 0 ? state->status.c_str() : state->selected.c_str());
        sapp_set_window_title(title);
    }
}

void cleanup() {
    state->engine.shutdown();
    sg_shutdown();
    delete state;
    state = nullptr;
}

ote::MouseButton mouseButton(sapp_mousebutton b) {
    switch (b) {
    case SAPP_MOUSEBUTTON_LEFT:
        return ote::MouseButton::Left;
    case SAPP_MOUSEBUTTON_RIGHT:
        return ote::MouseButton::Right;
    case SAPP_MOUSEBUTTON_MIDDLE:
        return ote::MouseButton::Middle;
    default:
        return ote::MouseButton::NoButton;
    }
}

int touchId(const sapp_touchpoint& t) { return static_cast<int>(t.identifier & 0x3fffffff) + 1; }

ote::PointerType touchType(const sapp_touchpoint& t) {
    return t.android_tooltype == SAPP_ANDROIDTOOLTYPE_STYLUS ? ote::PointerType::Pen : ote::PointerType::Touch;
}

bool mapKey(sapp_keycode k, ote::Key& out) {
    switch (k) {
    case SAPP_KEYCODE_LEFT:
    case SAPP_KEYCODE_A:
        out = ote::Key::Left;
        return true;
    case SAPP_KEYCODE_RIGHT:
    case SAPP_KEYCODE_D:
        out = ote::Key::Right;
        return true;
    case SAPP_KEYCODE_UP:
    case SAPP_KEYCODE_W:
        out = ote::Key::Up;
        return true;
    case SAPP_KEYCODE_DOWN:
    case SAPP_KEYCODE_S:
        out = ote::Key::Down;
        return true;
    case SAPP_KEYCODE_EQUAL:
    case SAPP_KEYCODE_KP_ADD:
        out = ote::Key::ZoomIn;
        return true;
    case SAPP_KEYCODE_MINUS:
    case SAPP_KEYCODE_KP_SUBTRACT:
        out = ote::Key::ZoomOut;
        return true;
    case SAPP_KEYCODE_Q:
        out = ote::Key::RotateLeft;
        return true;
    case SAPP_KEYCODE_E:
        out = ote::Key::RotateRight;
        return true;
    case SAPP_KEYCODE_PAGE_UP:
        out = ote::Key::TiltUp;
        return true;
    case SAPP_KEYCODE_PAGE_DOWN:
        out = ote::Key::TiltDown;
        return true;
    case SAPP_KEYCODE_N:
        out = ote::Key::ResetNorth;
        return true;
    case SAPP_KEYCODE_T:
        out = ote::Key::ResetTilt;
        return true;
    default:
        return false;
    }
}

void event(const sapp_event* e) {
    ote::Engine& eng = state->engine;
    switch (e->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (!state->mouseDown) {
            state->mouseDown = true;
            state->mouseButton = e->mouse_button;
            eng.input().pointerDown(0, e->mouse_x, e->mouse_y, ote::PointerType::Mouse, mouseButton(e->mouse_button), e->modifiers);
        }
        state->mouseX = e->mouse_x;
        state->mouseY = e->mouse_y;
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (state->mouseDown && e->mouse_button == state->mouseButton) {
            state->mouseDown = false;
            eng.input().pointerUp(0, e->mouse_x, e->mouse_y);
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (state->mouseDown) eng.input().pointerMove(0, e->mouse_x, e->mouse_y, e->modifiers);
        state->mouseX = e->mouse_x;
        state->mouseY = e->mouse_y;
        break;
    case SAPP_EVENTTYPE_MOUSE_LEAVE:
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        eng.input().wheel(state->mouseX, state->mouseY, e->scroll_x, e->scroll_y, false, e->modifiers);
        break;
    case SAPP_EVENTTYPE_TOUCHES_BEGAN:
        for (int i = 0; i < e->num_touches; ++i)
            if (e->touches[i].changed)
                eng.input().pointerDown(touchId(e->touches[i]), e->touches[i].pos_x, e->touches[i].pos_y, touchType(e->touches[i]));
        break;
    case SAPP_EVENTTYPE_TOUCHES_MOVED:
        for (int i = 0; i < e->num_touches; ++i)
            if (e->touches[i].changed) eng.input().pointerMove(touchId(e->touches[i]), e->touches[i].pos_x, e->touches[i].pos_y);
        break;
    case SAPP_EVENTTYPE_TOUCHES_ENDED:
        for (int i = 0; i < e->num_touches; ++i)
            if (e->touches[i].changed) eng.input().pointerUp(touchId(e->touches[i]), e->touches[i].pos_x, e->touches[i].pos_y);
        break;
    case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
        eng.input().cancelAll();
        break;
    case SAPP_EVENTTYPE_UNFOCUSED:
        eng.input().cancelAll();
        for (int k = 0; k <= static_cast<int>(ote::Key::ResetTilt); ++k) eng.input().keyUp(static_cast<ote::Key>(k));
        state->mouseDown = false;
        break;
    case SAPP_EVENTTYPE_KEY_UP: {
        ote::Key k;
        if (mapKey(e->key_code, k)) eng.input().keyUp(k);
        break;
    }
    case SAPP_EVENTTYPE_KEY_DOWN: {
        ote::Key mapped;
        if (mapKey(e->key_code, mapped)) {
            if (!e->key_repeat) eng.input().keyDown(mapped, e->modifiers);
            break;
        }
        if (e->key_repeat) break;
        if (e->key_code >= SAPP_KEYCODE_1 && e->key_code <= SAPP_KEYCODE_4) {
            state->preset = e->key_code - SAPP_KEYCODE_1;
            eng.setLayers(preset(state->preset));
        } else if (e->key_code == SAPP_KEYCODE_P) {
            eng.setFirstPerson(!eng.firstPerson(), 2.0);
        } else if (e->key_code == SAPP_KEYCODE_M) {
            eng.set3D(!eng.is3D());
        } else if (e->key_code == SAPP_KEYCODE_X) {
            state->xray = state->xray < 0.99f ? 1.0f : 0.45f;
            eng.setTerrainTransparency(state->xray);
        } else if (e->key_code == SAPP_KEYCODE_H) {
            state->heat = !state->heat;
            eng.setElevationHeatmap(state->heat);
        } else if (e->key_code == SAPP_KEYCODE_F) {
            ote::View v;
            v.lat = 46.8529;
            v.lon = -121.7604;
            v.gsd = 12;
            v.tilt = 60;
            v.azimuth = 20;
            eng.flyTo(v, 4.0);
        } else if (e->key_code == SAPP_KEYCODE_G) {
            if (!state->grid) {
                state->grid = true;
                state->gridType = 0;
            } else if (state->gridType < 2) {
                ++state->gridType;
            } else {
                state->grid = false;
            }
            eng.setGridType(static_cast<ote::GridType>(state->gridType));
            eng.setGridEnabled(state->grid);
        } else if (e->key_code == SAPP_KEYCODE_K) {
            state->slope = !state->slope;
            eng.setTerrainSlope(state->slope);
        } else if (e->key_code == SAPP_KEYCODE_L) {
            double gsd = eng.view().gsd;
            double interval = 10;
            for (double c : {10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0})
                if (c >= gsd * 15) {
                    interval = c;
                    break;
                }
            eng.addContours(eng.viewRegion(), interval);
        } else if (e->key_code == SAPP_KEYCODE_B) {
            double lat, lon;
            if (eng.pick(state->mouseX, state->mouseY, lat, lon))
                for (const ote::Feature& f : ote::bullseye({lat, lon, 0}, eng.view().gsd * 250.0)) eng.addFeature(f);
        } else if (e->key_code == SAPP_KEYCODE_C) {
            double lat, lon;
            if (eng.pick(state->mouseX, state->mouseY, lat, lon)) {
                if (state->rbStarted)
                    for (const ote::Feature& f : ote::rangeBearingLine({state->rbLat, state->rbLon, 0}, {lat, lon, 0})) eng.addFeature(f);
                state->rbStarted = !state->rbStarted;
                state->rbLat = lat;
                state->rbLon = lon;
            }
        } else if (e->key_code == SAPP_KEYCODE_V) {
            double lat, lon;
            if (eng.pick(state->mouseX, state->mouseY, lat, lon)) {
                if (state->viewshed) eng.removeImageOverlay(state->viewshed);
                state->viewshed = eng.computeViewshed({lat, lon, 0}, 2.0, 5000.0);
            }
        } else if (e->key_code == SAPP_KEYCODE_Y) {
            state->terrain = !state->terrain;
            eng.setTerrainEnabled(state->terrain);
        } else if (e->key_code == SAPP_KEYCODE_U) {
            state->depth = !state->depth;
            eng.setDepthPerception(state->depth);
        } else if (e->key_code == SAPP_KEYCODE_O) {
            state->exaggeration = state->exaggeration >= 3.0 ? 1.0 : state->exaggeration + 0.5;
            eng.setTerrainExaggeration(state->exaggeration);
        } else if (e->key_code == SAPP_KEYCODE_R) {
            ote::View v = eng.view();
            v.azimuth = 0;
            v.tilt = 0;
            eng.flyTo(v, 0.4);
        }
        break;
    }
    default:
        break;
    }
}

}

void parseArgs(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--view") {
            ote::View& v = state->startView;
            std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf", &v.lat, &v.lon, &v.gsd, &v.tilt, &v.azimuth);
        } else if (a == "--offscreen") {
            state->offscreen = std::atoi(argv[++i]) != 0;
        } else if (a == "--shot") {
            state->shotPath = argv[++i];
        } else if (a == "--film") {
            state->filmDir = argv[++i];
            std::error_code ec;
            std::filesystem::create_directories(state->filmDir, ec);
        } else if (a == "--film-every") {
            state->filmEvery = std::max(0.02, std::atof(argv[++i]));
        } else if (a == "--shot-timeout") {
            state->shotTimeout = std::atof(argv[++i]);
        } else if (a == "--size") {
            std::sscanf(argv[++i], "%dx%d", &state->width, &state->height);
        } else if (a == "--cache") {
            state->cacheDir = argv[++i];
        } else if (a == "--offline") {
            state->offline = std::atoi(argv[++i]) != 0;
        } else if (a == "--mbtiles" || a == "--gpkg" || a == "--osmdroid") {
            ote::ImageryLayer l;
            l.type = a == "--mbtiles" ? ote::SourceType::MBTiles
                     : a == "--gpkg"  ? ote::SourceType::GeoPackage
                                      : ote::SourceType::Osmdroid;
            l.path = argv[++i];
            l.name = l.path;
            state->customLayers.push_back(l);
        } else if (a == "--gdal") {
            ote::ImageryLayer l;
            l.type = ote::SourceType::Gdal;
            l.path = argv[++i];
            l.name = l.path;
            state->customLayers.push_back(l);
        } else if (a == "--quantized-mesh") {
            state->quantizedMesh = argv[++i];
        } else if (a == "--gdal-dem") {
            state->gdalDem = argv[++i];
        } else if (a == "--mobac") {
            for (auto& l : ote::loadMobac(argv[++i])) state->customLayers.push_back(l);
        } else if (a == "--import") {
            state->imports.push_back(argv[++i]);
        } else if (a == "--cache-limit") {
            state->cacheLimit = std::atoi(argv[++i]);
        } else if (a == "--export") {
            ote::Region& r = state->exportRegion;
            char path[1024] = {0};
            state->hasExport = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%d,%d,%1023[^\n]", &r.west, &r.south, &r.east, &r.north,
                                           &state->exportMin, &state->exportMax, path) == 7;
            state->exportPath = path;
        } else if (a == "--download") {
            ote::Region& r = state->download;
            state->hasDownload = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%d,%d", &r.west, &r.south, &r.east, &r.north,
                                             &state->downloadMin, &state->downloadMax) == 6;
        } else if (a == "--hit") {
            state->hasHit = std::sscanf(argv[++i], "%lf,%lf", &state->hitX, &state->hitY) == 2;
        } else if (a == "--las") {
            state->pointClouds.push_back(argv[++i]);
        } else if (a == "--stereo") {
            state->stereo = std::max(0.0, std::atof(argv[++i]));
        } else if (a == "--sun") {
            std::string v = argv[++i];
            state->sunTime = v == "now" ? 0.0 : std::atof(v.c_str());
        } else if (a == "--tileset") {
            state->tilesets.push_back(argv[++i]);
        } else if (a == "--model") {
            ote::ModelPlacement p;
            char path[1024] = {0};
            if (std::sscanf(argv[++i], "%1023[^,],%lf,%lf,%lf,%lf", path, &p.position.lat, &p.position.lon, &p.heading, &p.scale) == 5)
                state->models.push_back({path, p});
        } else if (a == "--features") {
            state->featureFiles.push_back(argv[++i]);
        } else if (a == "--heatmap") {
            state->heat = std::atoi(argv[++i]) != 0;
        } else if (a == "--zoom-test") {
            ote::View& v = state->zoomTarget;
            state->zoomTest = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf,%lf", &v.lat, &v.lon, &v.gsd, &v.tilt, &v.azimuth,
                                          &state->zoomSeconds) >= 3;
        } else if (a == "--first-person") {
            state->shotFirstPerson = std::sscanf(argv[++i], "%lf,%lf,%lf", &state->fpHeading, &state->fpPitch, &state->fpHeight) >= 2;
        } else if (a == "--memory") {
            state->memory = std::atoi(argv[++i]);
        } else if (a == "--bench") {
            state->bench = std::atoi(argv[++i]);
        } else if (a == "--xray") {
            state->xray = static_cast<float>(std::atof(argv[++i]));
        } else if (a == "--grid") {
            state->grid = std::atoi(argv[++i]) != 0;
        } else if (a == "--grid-type") {
            state->gridType = std::clamp(std::atoi(argv[++i]), 0, 2);
        } else if (a == "--slope") {
            state->slope = std::atoi(argv[++i]) != 0;
        } else if (a == "--contours") {
            state->contourInterval = std::atof(argv[++i]);
        } else if (a == "--bullseye") {
            std::array<double, 3> b{};
            if (std::sscanf(argv[++i], "%lf,%lf,%lf", &b[0], &b[1], &b[2]) == 3) state->bullseyes.push_back(b);
        } else if (a == "--rings") {
            std::array<double, 4> r{};
            if (std::sscanf(argv[++i], "%lf,%lf,%lf,%lf", &r[0], &r[1], &r[2], &r[3]) == 4) state->rings.push_back(r);
        } else if (a == "--rb") {
            std::array<double, 4> r{};
            if (std::sscanf(argv[++i], "%lf,%lf,%lf,%lf", &r[0], &r[1], &r[2], &r[3]) == 4) state->rbLines.push_back(r);
        } else if (a == "--viewshed") {
            state->shotViewshed = std::atoi(argv[++i]) != 0;
        } else if (a == "--terrain") {
            state->terrain = std::atoi(argv[++i]) != 0;
        } else if (a == "--exaggeration") {
            state->exaggeration = std::atof(argv[++i]);
        } else if (a == "--elevation-dir") {
            state->elevationDir = argv[++i];
        } else if (a == "--script") {
            loadScript(argv[++i]);
        } else if (a == "--zoom-start") {
            state->zoomStartAfter = std::atof(argv[++i]);
        } else if (a == "--diag") {
            state->diag = std::atoi(argv[++i]) != 0;
        } else if (a == "--lod-blend") {
#if defined(_WIN32)
            _putenv_s("OTE_LOD_BLEND", argv[++i]);
#else
            setenv("OTE_LOD_BLEND", argv[++i], 1);
#endif
        } else if (a == "--margin") {
#if defined(_WIN32)
            _putenv_s("OTE_MARGIN", argv[++i]);
#else
            setenv("OTE_MARGIN", argv[++i], 1);
#endif
        } else if (a == "--holes") {
            state->holes = std::atoi(argv[++i]) != 0;
#if defined(_WIN32)
            if (state->holes) _putenv_s("OTE_HOLES", "1");
#else
            if (state->holes) setenv("OTE_HOLES", "1", 1);
#endif
        } else if (a == "--preset") {
            state->preset = std::atoi(argv[++i]);
        }
    }
}

void loadScript(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        ViewerState::ScriptStep st;
        char op[32] = {0};
        int n = std::sscanf(line, "%lf %31s %lf %lf %lf %lf %lf %lf", &st.at, op, &st.v[0], &st.v[1], &st.v[2], &st.v[3], &st.v[4], &st.v[5]);
        if (n >= 2) {
            st.op = op;
            state->script.push_back(st);
        }
    }
    std::fclose(f);
}

bool runScript() {
    ote::InputController& in = state->engine.input();
    double t = state->shotElapsed;
    if (state->dragging) {
        const auto& d = state->drag;
        double k = std::clamp((t - d.at) / std::max(d.v[4], 0.01), 0.0, 1.0);
        double x = d.v[0] + (d.v[2] - d.v[0]) * k, y = d.v[1] + (d.v[3] - d.v[1]) * k;
        in.pointerMove(77, x, y);
        if (k >= 1.0) {
            in.pointerUp(77, x, y);
            state->dragging = false;
        }
    }
    while (state->scriptNext < state->script.size() && state->script[state->scriptNext].at <= t && !state->dragging) {
        const auto& st = state->script[state->scriptNext++];
        if (const char* dp = std::getenv("OTE_DIAG"))
            if (FILE* df = std::fopen(dp, "a")) {
                ote::View cv = state->engine.view();
                std::fprintf(df, "input t=%.2f %s %.0f %.0f %.0f %.0f %.2f | view %.5f %.5f gsd %.2f tilt %.1f az %.1f | so far frames %lld incomplete %lld coarse %lld loads %lld\n", t,
                             st.op.c_str(), st.v[0], st.v[1], st.v[2], st.v[3], st.v[4], cv.lat, cv.lon, cv.gsd, cv.tilt, cv.azimuth, state->scriptFrames,
                             state->scriptIncomplete, state->scriptCoarse, state->scriptLoads);
                std::fclose(df);
            }
        if (st.op == "wheel") in.wheel(st.v[0], st.v[1], 0, st.v[2], false, 0);
        else if (st.op == "preset") {
            state->preset = static_cast<int>(st.v[0]);
            state->engine.setLayers(preset(state->preset));
        }
        else if (st.op == "drag" || st.op == "rdrag") {
            state->drag = st;
            state->drag.at = t;
            state->dragging = true;
            in.pointerDown(77, st.v[0], st.v[1], ote::PointerType::Mouse, st.op == "rdrag" ? ote::MouseButton::Right : ote::MouseButton::Left, 0);
        } else if (st.op == "view") {
            ote::View v;
            v.lat = st.v[0];
            v.lon = st.v[1];
            v.gsd = st.v[2];
            v.tilt = st.v[3];
            v.azimuth = st.v[4];
            state->engine.setView(v);
        }
    }
    return state->scriptNext < state->script.size() || state->dragging || in.active();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    state = new ViewerState();
    state->startView.lat = 30.0;
    state->startView.lon = -100.0;
    state->startView.gsd = 25000.0;
    parseArgs(argc, argv);
    sapp_desc d{};
    d.init_cb = init;
    d.frame_cb = frame;
    d.cleanup_cb = cleanup;
    d.event_cb = event;
    d.width = state->width;
    d.height = state->height;
    d.sample_count = state->shotPath.empty() ? 4 : 1;
    d.high_dpi = true;
    d.window_title = "OpenTAKEngine";
    d.logger.func = slog_func;
    return d;
}
