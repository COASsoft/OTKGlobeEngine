#if defined(__ANDROID__)
#include <android/configuration.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <pthread.h>
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

#include "otk/engine.hpp"
#include "shot.hpp"
#include "watchdog.hpp"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

void parseArgs(int argc, char** argv);
void loadScript(const char* path);
bool runScript();
#if defined(_WIN32)
void installPointerInput(const void* hwnd, otk::InputController* input);
#endif

namespace {

struct ViewerState {
    otk::Engine engine;
    float mouseX = 0, mouseY = 0;
    int preset = 0;
    otk::View startView;
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
    bool clearCache = false;
    int fps = 60;
    int idleFps = 20;
    bool offline = false;
    std::vector<otk::ImageryLayer> customLayers;
    bool hasDownload = false;
    otk::Region download;
    otk::Region labelsRegion;
    bool hasLabelsDownload = false;
    int downloadMin = 0, downloadMax = 0;
    int downloadId = 0;
    int labelsDownload = 0;
    std::vector<std::string> imports;
    int gridType = 0;
    bool slope = false;
    double contourInterval = 0;
    int controls = -1;
    // bool buildings = true;
    bool buildings = false;
    bool contours = false;
    std::vector<std::array<double, 3>> bullseyes;
    std::vector<std::array<double, 4>> rings;
    std::vector<std::array<double, 4>> rbLines;
    bool rbStarted = false;
    double rbLat = 0, rbLon = 0;
    bool hasExport = false;
    otk::Region exportRegion;
    int exportMin = 0, exportMax = 0;
    std::string exportPath;
    int cacheLimit = 0;
    std::vector<std::string> featureFiles;
    std::vector<std::string> openPaths;
    std::vector<otk::ImageryLayer> addedLayers;
    std::vector<otk::ElevationLayer> elevation;
    std::string status;
    double statusTime = 0;
    bool viewGiven = false;
    std::vector<std::string> tilesets;
    double stereo = -1.0;
    double sunTime = -1.0;
    std::vector<std::string> pointClouds;
    std::vector<std::pair<std::string, otk::ModelPlacement>> models;
    std::string selected;
    bool hasHit = false;
    double hitX = 0, hitY = 0;
    bool terrain = true;
    bool grid = false;
    bool labels = true;
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
    struct Gesture {
        double start = 0;
        double seconds = 0;
        int count = 0;
        double from[2][2] = {};
        double to[2][2] = {};
        double angle0 = 0, angle1 = 0, radius = 0;
        double cx = 0, cy = 0;
        bool turn = false;
        otk::PointerType type = otk::PointerType::Touch;
        otk::MouseButton button = otk::MouseButton::Left;
    };
    bool gesturing = false;
    Gesture gesture;
    std::string framesPath;
    FILE* framesFile = nullptr;
    double exitAfter = -1.0;
    double inputEnd = -1.0;
    double settledAt = -1.0;
    bool quitting = false;
    struct Totals {
        long long frames = 0, pops = 0, jank25 = 0, jank50 = 0, incomplete = 0, reveals = 0, revealFrames = 0;
        double seconds = 0, pop = 0, merge = 0, terrain = 0, blur = 0, blur2 = 0, depth = 0, fade = 0, worst = 0, reveal = 0;
    };
    Totals during, after;
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
    otk::View zoomTarget;
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

float viewerDpi() {
#if defined(__ANDROID__)
    static float scale = 0.0f;
    if (scale <= 0.0f) {
        scale = 1.0f;
        auto* activity = static_cast<const ANativeActivity*>(sapp_android_get_native_activity());
        if (activity && activity->assetManager) {
            AConfiguration* cfg = AConfiguration_new();
            AConfiguration_fromAssetManager(cfg, activity->assetManager);
            const int32_t density = AConfiguration_getDensity(cfg);
            AConfiguration_delete(cfg);
            if (density > 0 && density != ACONFIGURATION_DENSITY_NONE && density != ACONFIGURATION_DENSITY_ANY) scale = static_cast<float>(density) / 160.0f;
        }
    }
    return scale;
#else
    return sapp_dpi_scale();
#endif
}

void openPath(const std::string& path);

void fileLog(const char* tag, uint32_t level, uint32_t item, const char* msg, uint32_t line, const char* file, void*) {
    if (!logFile) return;
    std::fprintf(logFile, "[%s] level %u item %u line %u %s: %s\n", tag ? tag : "", level, item, line, file ? file : "",
                 msg ? msg : "");
    std::fflush(logFile);
}

std::vector<otk::ImageryLayer> preset(int i) {
    const std::vector<otk::Basemap> maps = otk::defaultBasemaps();
    return maps[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(maps.size()) - 1))].layers;
}

void init() {
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    if (const char* lp = std::getenv("OTK_LOG")) {
        logFile = std::fopen(lp, "w");
        desc.logger.func = fileLog;
    }
    otk::configureGfx(desc);
    if (const char* wd = std::getenv("OTK_WATCHDOG")) watchdogStart(wd);
#if defined(__ANDROID__)
    {
        auto* activity = static_cast<const ANativeActivity*>(sapp_android_get_native_activity());
        otk::setAndroidJavaVm(activity->vm);
        if (state->cacheDir.empty() && activity->internalDataPath) state->cacheDir = activity->internalDataPath;
        if (activity->externalDataPath) {
            std::string path = std::string(activity->externalDataPath) + "/args.txt";
            if (FILE* f = std::fopen(path.c_str(), "r")) {
                std::vector<std::string> tokens{"otk_viewer"};
                char buf[1024];
                while (std::fscanf(f, "%1023s", buf) == 1) tokens.push_back(buf);
                std::fclose(f);
                std::vector<char*> args;
                for (auto& t : tokens) args.push_back(t.data());
                parseArgs(static_cast<int>(args.size()), args.data());
                __android_log_print(ANDROID_LOG_INFO, "OTKGlobeEngine", "args %zu from %s", tokens.size() - 1, path.c_str());
            }
        }
        pthread_setname_np(pthread_self(), "otk-app");
    }
#endif
    sg_setup(&desc);
    state->engine.init();
    state->engine.input().onTap([](double x, double y, int taps, otk::PointerType) {
        if (taps != 1) return;
        otk::Feature f;
        std::uint64_t id = state->engine.hitTest(x, y, 16.0 * viewerDpi());
        state->selected = id && state->engine.feature(id, f) ? (f.name.empty() ? "feature " + std::to_string(id) : f.name) : "";
    });
#if defined(_WIN32)
    installPointerInput(sapp_win32_get_hwnd(), &state->engine.input());
#endif
    state->engine.setViewport(sapp_width(), sapp_height(), viewerDpi());
    std::string cache = state->cacheDir;
    if (cache.empty()) cache = otk::defaultCacheDirectory();
    if (!cache.empty()) {
        std::error_code ec;
        if (state->clearCache) std::filesystem::remove_all(cache, ec);
        std::filesystem::create_directories(cache, ec);
        state->engine.setCacheDirectory(cache);
    }
    state->engine.setOffline(state->offline);
    if (state->memory > 0) state->engine.setMemoryBudget(state->memory);
    {
        std::vector<otk::ElevationLayer>& elev = state->elevation;
        if (!state->elevationDir.empty()) {
            otk::ElevationLayer d;
            d.type = otk::ElevationType::Directory;
            d.path = state->elevationDir;
            d.maxZoom = 14;
            elev.push_back(d);
        }
        if (!state->quantizedMesh.empty()) {
            otk::ElevationLayer q;
            q.type = otk::ElevationType::QuantizedMesh;
            q.url = state->quantizedMesh;
            q.maxZoom = 14;
            elev.push_back(q);
        }
        if (!state->gdalDem.empty()) {
            otk::ElevationLayer g;
            g.type = otk::ElevationType::Gdal;
            g.path = state->gdalDem;
            elev.push_back(g);
        }
        for (auto& e : otk::defaultElevation()) elev.push_back(e);
        state->engine.setElevation(elev);
        state->engine.setTerrainEnabled(state->terrain);
        state->engine.setDiagnostics(state->diag);
        state->engine.setTerrainExaggeration(state->exaggeration);
    }
    state->engine.setLayers(state->customLayers.empty() ? preset(state->preset) : state->customLayers);
    for (const auto& f : state->featureFiles) state->engine.loadFeatures(f);
    state->engine.setGridEnabled(state->grid);
    state->engine.setLabelsEnabled(state->labels);
    state->engine.setBuildingsEnabled(state->buildings);
    if (state->controls >= 0) state->engine.setControls(static_cast<std::uint32_t>(state->controls));
    else if (!state->shotPath.empty()) state->engine.setControls(0);
    if (state->contours) state->engine.setContours(true, state->contourInterval);
    state->engine.setElevationHeatmap(state->heat);
    state->engine.setTerrainTransparency(state->xray);

    for (const auto& [path, placement] : state->models) state->engine.addModel(path, placement);
    for (const auto& t : state->tilesets) state->engine.addTileset(t);
    if (state->sunTime >= 0) state->engine.setSunLighting(true, state->sunTime);
    for (const auto& p : state->pointClouds) state->engine.addPointCloud(p);
    if (state->cacheLimit > 0) state->engine.setCacheLimit(state->cacheLimit);
    state->engine.setGridType(static_cast<otk::GridType>(state->gridType));
    state->engine.setTerrainSlope(state->slope);
    for (const auto& b : state->bullseyes)
        for (const otk::Feature& f : otk::bullseye({b[0], b[1], 0}, b[2])) state->engine.addFeature(f);
    for (const auto& r : state->rings)
        for (const otk::Feature& f : otk::rangeRings({r[0], r[1], 0}, r[2], static_cast<int>(r[3]), 8)) state->engine.addFeature(f);
    for (const auto& r : state->rbLines)
        for (const otk::Feature& f : otk::rangeBearingLine({r[0], r[1], 0}, {r[2], r[3], 0})) state->engine.addFeature(f);
    for (const auto& path : state->imports) {
        std::string err;
        long long n = state->engine.importTiles(0, path, &err);
        std::printf("import %s: %lld tiles%s%s\n", path.c_str(), n, err.empty() ? "" : " ", err.c_str());
    }
    if (state->hasDownload)
        state->downloadId = state->engine.downloadRegion(0, state->download, state->downloadMin, state->downloadMax);
    if (state->hasLabelsDownload) state->labelsDownload = state->engine.downloadLabels(state->labelsRegion);
    state->engine.setView(state->startView);
    if (state->shotFirstPerson) {
        state->engine.setFirstPerson(true, state->fpHeight);
        state->engine.look(state->fpHeading, state->fpPitch);
    }
    for (const auto& p : state->openPaths) openPath(p);
    state->viewGiven = false;
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
    otk::View v = state->engine.view();
    if (const char* dp = std::getenv("OTK_DIAG"))
        if (FILE* df = std::fopen(dp, "a")) {
            otk::Stats hs = state->engine.stats();
            std::fprintf(df, "holes frame %d t=%.2f blocks %d at %d,%d tiles %d L%d cov %d ret %d inc %d hollow %d ph %d surf %d free %d alt %.1f | view %.5f %.5f gsd %.2f tilt %.1f az %.1f\n", state->frames, state->shotElapsed, blocks, hx, hy, hs.terrainTiles,
                         hs.maxLevel, hs.coverageOnlyTiles, hs.retainedTiles, hs.incompleteTiles, hs.hollowSkipped, hs.placeholders, hs.surfaces, hs.freeSurfaces, v.alt, v.lat, v.lon, v.gsd, v.tilt, v.azimuth);
            std::fclose(df);
        }
    if (blocks > state->holeMax) {
        state->holeMax = blocks;
        writePng((state->shotPath + ".holes.png").c_str(), state->width, state->height, px);
    }
}

void showStatus(const std::string& s);

void writeSummary() {
    if (state->framesPath.empty()) return;
    if (state->framesFile) {
        std::fclose(state->framesFile);
        state->framesFile = nullptr;
    }
    std::string text;
    char line[512];
    for (int pass = 0; pass < 2; ++pass) {
        const ViewerState::Totals& T = pass == 0 ? state->during : state->after;
        std::snprintf(line, sizeof(line),
                      "%s frames=%lld seconds=%.2f pop_screens=%.4f pops=%lld merge_screens=%.4f terrain_pop_screens=%.4f blur_seconds=%.4f "
                      "blur2_seconds=%.4f depth_seconds=%.4f fade_seconds=%.4f incomplete_frames=%lld jank25=%lld jank50=%lld worst_ms=%.1f reveal_frames=%lld tile_reveals=%lld reveal_screens=%.4f\n",
                      pass == 0 ? "input" : "settle", T.frames, T.seconds, T.pop, T.pops, T.merge, T.terrain, T.blur, T.blur2, T.depth, T.fade,
                      T.incomplete, T.jank25, T.jank50, T.worst, T.revealFrames, T.reveals, T.reveal);
        text += line;
    }
    const otk::Stats run = state->engine.stats();
    std::snprintf(line, sizeof(line), "settled_after_input=%.2f fade_misses=%lld imagery_loads=%lld imagery_wasted=%lld net_mb=%.2f net_requests=%lld\n",
                  state->settledAt >= 0 && state->inputEnd >= 0 ? state->settledAt - state->inputEnd : -1.0, run.fadeMisses, run.imageryLoads,
                  run.imageryWasted, static_cast<double>(run.networkBytes) / 1048576.0, run.networkRequests);
    text += line;
    if (FILE* f = std::fopen((state->framesPath + ".txt").c_str(), "w")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "OTKGlobeEngine", text.c_str());
#else
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);
#endif
}

void logFrame(double dt) {
    if (!state->framesFile) return;
    const otk::Stats s = state->engine.stats();
    const otk::View v = state->engine.view();
    const bool busy = state->engine.needsRedraw();
    const bool input = state->scriptNext < state->script.size() || state->gesturing || state->engine.input().active() || state->engine.animating();
    const double t = state->shotElapsed;
    std::fprintf(state->framesFile,
                 "%.4f,%.2f,%.2f,%.2f,%.6f,%.6f,%.4f,%.1f,%.1f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.5f,%d,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.4f,%d,%d\n", t,
                 dt * 1000.0, s.cpuMs, s.cpuThreadMs, v.lat, v.lon, v.gsd, v.tilt, v.azimuth, s.terrainTiles, s.maxLevel, s.incompleteTiles, s.coarseTiles,
                 s.maxFallbackDepth, s.blankTiles, s.surfaceRenders, s.uploads, s.terrainBuilds, s.pendingRequests, s.popArea, s.pops, s.mergeArea,
                 s.terrainPopArea, s.blurArea, s.blurArea2, s.blurDepth, s.fadeArea, s.coverArea, busy ? 1 : 0, input ? 1 : 0);
    if (state->scriptNext == 0) return;
    if (input) {
        state->inputEnd = -1.0;
        state->settledAt = -1.0;
    } else if (state->inputEnd < 0) {
        state->inputEnd = t;
    }
    ViewerState::Totals& T = input ? state->during : state->after;
    ++T.frames;
    T.seconds += dt;
    T.pop += s.popArea;
    T.pops += s.pops;
    T.merge += s.mergeArea;
    T.terrain += s.terrainPopArea;
    T.blur += s.blurArea * dt;
    T.blur2 += s.blurArea2 * dt;
    T.depth += s.blurDepth * dt;
    T.fade += s.fadeArea * dt;
    T.reveals += s.reveals;
    T.reveal += s.revealArea;
    if (s.revealArea > 0.001) ++T.revealFrames;
    if (s.incompleteTiles > 0) ++T.incomplete;
    if (busy || input) {
        if (dt > 0.025) ++T.jank25;
        if (dt > 0.050) ++T.jank50;
        T.worst = std::max(T.worst, dt * 1000.0);
    }
    if (!input && !busy && state->settledAt < 0) state->settledAt = t;
    if (busy && !input) state->settledAt = -1.0;
    if (!input && state->exitAfter >= 0 && !state->quitting && t - state->inputEnd >= state->exitAfter) {
        state->quitting = true;
        writeSummary();
        sapp_quit();
    }
}

#if defined(__ANDROID__)
void androidInsets() {
    static std::chrono::steady_clock::time_point last;
    const auto now = std::chrono::steady_clock::now();
    if (last.time_since_epoch().count() != 0 && now - last < std::chrono::seconds(1)) return;
    last = now;
    auto* activity = static_cast<const ANativeActivity*>(sapp_android_get_native_activity());
    if (!activity || !activity->vm || !activity->clazz) return;
    JavaVM* vm = activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    auto call = [&](jobject obj, const char* name, const char* sig) -> jobject {
        if (!obj) return nullptr;
        jclass cls = env->GetObjectClass(obj);
        jmethodID m = env->GetMethodID(cls, name, sig);
        jobject out = nullptr;
        if (m && !env->ExceptionCheck()) out = env->CallObjectMethod(obj, m);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            out = nullptr;
        }
        env->DeleteLocalRef(cls);
        return out;
    };
    jobject window = call(activity->clazz, "getWindow", "()Landroid/view/Window;");
    jobject decor = call(window, "getDecorView", "()Landroid/view/View;");
    jobject insets = call(decor, "getRootWindowInsets", "()Landroid/view/WindowInsets;");
    int inset[4] = {0, 0, 0, 0};
    bool ok = insets != nullptr;
    auto ints = [&](jobject obj, const char* const* names, int* out) {
        if (!obj) return false;
        jclass cls = env->GetObjectClass(obj);
        bool good = true;
        for (int i = 0; i < 4 && good; ++i) {
            jmethodID m = env->GetMethodID(cls, names[i], "()I");
            if (!m || env->ExceptionCheck()) {
                env->ExceptionClear();
                good = false;
                break;
            }
            out[i] = std::max(out[i], static_cast<int>(env->CallIntMethod(obj, m)));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                good = false;
            }
        }
        env->DeleteLocalRef(cls);
        return good;
    };
    if (insets) {
        const char* bars[4] = {"getSystemWindowInsetLeft", "getSystemWindowInsetTop", "getSystemWindowInsetRight", "getSystemWindowInsetBottom"};
        ok = ints(insets, bars, inset);
        jobject cutout = call(insets, "getDisplayCutout", "()Landroid/view/DisplayCutout;");
        const char* safe[4] = {"getSafeInsetLeft", "getSafeInsetTop", "getSafeInsetRight", "getSafeInsetBottom"};
        if (cutout) {
            ints(cutout, safe, inset);
            env->DeleteLocalRef(cutout);
        }
        jclass cls = env->GetObjectClass(insets);
        jmethodID corner = env->GetMethodID(cls, "getRoundedCorner", "(I)Landroid/view/RoundedCorner;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            corner = nullptr;
        }
        for (int position = 0; corner && position < 4; ++position) {
            jobject rc = env->CallObjectMethod(insets, corner, position);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                rc = nullptr;
            }
            if (!rc) continue;
            jclass rcc = env->GetObjectClass(rc);
            jmethodID radius = env->GetMethodID(rcc, "getRadius", "()I");
            int r = 0;
            if (radius && !env->ExceptionCheck()) r = env->CallIntMethod(rc, radius);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                r = 0;
            }
            const int edge = static_cast<int>(std::ceil(r * 0.3));
            const bool left = position == 0 || position == 3, top = position == 0 || position == 1;
            inset[left ? 0 : 2] = std::max(inset[left ? 0 : 2], edge);
            inset[top ? 1 : 3] = std::max(inset[top ? 1 : 3], edge);
            env->DeleteLocalRef(rcc);
            env->DeleteLocalRef(rc);
        }
        env->DeleteLocalRef(cls);
    }
    for (jobject o : {insets, decor, window})
        if (o) env->DeleteLocalRef(o);
    if (attached) vm->DetachCurrentThread();
    if (!ok) return;
    const double d = std::max(static_cast<double>(viewerDpi()), 0.5);
    state->engine.setControlsPadding(inset[0] / d, inset[1] / d, inset[2] / d, inset[3] / d);
}

void androidFrameRate() {
    static ANativeWindow* last = nullptr;
    ANativeWindow* window = _sapp.android.current.window;
    if (!window || window == last || state->fps <= 0) return;
    last = window;
    using SetFrameRate = int32_t (*)(ANativeWindow*, float, int8_t);
    static void* lib = dlopen("libandroid.so", RTLD_NOW);
    static auto fn = lib ? reinterpret_cast<SetFrameRate>(dlsym(lib, "ANativeWindow_setFrameRate")) : nullptr;
    if (!fn) {
        __android_log_print(ANDROID_LOG_INFO, "OTKGlobeEngine", "setFrameRate unavailable");
        return;
    }
    const int32_t rc = fn(window, static_cast<float>(state->fps), 0);
    __android_log_print(ANDROID_LOG_INFO, "OTKGlobeEngine", "setFrameRate %d -> %d", state->fps, rc);
}
#endif

void frame() {
#if defined(__ANDROID__)
    androidInsets();
    androidFrameRate();
#endif
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
            otk::View v = state->engine.view();
            state->viewshed = state->engine.computeViewshed({v.lat, v.lon, 0}, 2.0, 5000.0);
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
        logFrame(sapp_frame_duration());
        if (scripting && state->scriptNext > 0) {
            otk::Stats ss = state->engine.stats();
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
        bool downloading = (state->downloadId && !state->engine.downloadStatus(state->downloadId).finished) ||
                           (state->labelsDownload && !state->engine.downloadStatus(state->labelsDownload).finished) || zooming;
        bool settled = state->shotElapsed > 1.0 && state->settledFrames > 10 && !downloading &&
                       (!state->shotViewshed || state->viewshed);
        if (settled || state->shotElapsed >= state->shotTimeout) {
            bool ok = saveImagePng(state->shotColor, state->width, state->height,
#if defined(SOKOL_D3D11)
                                   sg_d3d11_device(), sg_d3d11_device_context(),
#else
                                   nullptr, nullptr,
#endif
                                   state->shotPath.c_str());
            otk::Stats s = state->engine.stats();
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
                otk::View fv = state->engine.view();
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
                    otk::Feature hf;
                    std::uint64_t hid = state->engine.hitTest(state->hitX, state->hitY, 16.0);
                    std::fprintf(f, "hit %llu %s", static_cast<unsigned long long>(hid),
                                 hid && state->engine.feature(hid, hf) ? hf.name.c_str() : "-");
                    std::fputc(10, f);
                }
                if (state->downloadId) {
                    otk::DownloadStatus d = state->engine.downloadStatus(state->downloadId);
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
                    otk::Stats net = state->engine.stats();
                    std::fprintf(f, "network unreachable_hosts=%d stale_tiles=%lld cache_mb=%.1f\n", net.unreachableHosts, net.staleTiles,
                                 static_cast<double>(state->engine.cacheBytes()) / 1048576.0);
                }
                std::fclose(f);
            }
            if (!state->quitting) writeSummary();
            state->quitting = true;
            sapp_quit();
        }
        return;
    }
    if (!state->script.empty() || state->framesFile) {
        state->shotElapsed += sapp_frame_duration();
        runScript();
    }
    state->engine.setViewport(sapp_width(), sapp_height(), viewerDpi());
    if (state->stereo >= 0) state->engine.frameStereo(sglue_swapchain(), state->stereo);
    else state->engine.frame(sglue_swapchain());
    sg_commit();
    logFrame(sapp_frame_duration());
    {
        static double idleFor = 0.0;
        idleFor = state->engine.needsRedraw() ? 0.0 : idleFor + sapp_frame_duration();
        if (state->idleFps > 0 && idleFor > 0.5) {
            const double pause = 1.0 / state->idleFps - 1.0 / 60.0;
            if (pause > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(pause));
        }
    }
    {
        otk::Stats ps = state->engine.stats();
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
            otk::View pv = state->engine.view();
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
            __android_log_write(ANDROID_LOG_INFO, "OTKGlobeEngine", line);
#else
            if (const char* pf = std::getenv("OTK_PERF")) {
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
    if (state->labelsDownload) {
        const otk::DownloadStatus d = state->engine.downloadStatus(state->labelsDownload);
        char msg[160];
        if (d.finished) {
            std::snprintf(msg, sizeof(msg), "Roads downloaded: %lld of %lld tiles%s", static_cast<long long>(d.done), static_cast<long long>(d.total),
                          d.failed ? ", some failed" : "");
            state->labelsDownload = 0;
            showStatus(msg);
        } else if (state->titleTimer > 0.45) {
            std::snprintf(msg, sizeof(msg), "Downloading roads %lld / %lld", static_cast<long long>(d.done + d.failed), static_cast<long long>(d.total));
            state->status = msg;
            state->statusTime = 8.0;
        }
    }
    state->titleTimer += sapp_frame_duration();
    state->statusTime = std::max(0.0, state->statusTime - sapp_frame_duration());
    if (state->titleTimer > 0.5) {
        state->titleTimer = 0;
        otk::Stats s = state->engine.stats();
        otk::View v = state->engine.view();
        char title[256];
        std::snprintf(title, sizeof(title),
                      "OTKGlobeEngine  %.5f, %.5f  gsd %.2f m/px  tilt %.0f  az %.0f  |  tiles %d  L%d  imagery %d  pending %d  %s",
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

otk::MouseButton mouseButton(sapp_mousebutton b) {
    switch (b) {
    case SAPP_MOUSEBUTTON_LEFT:
        return otk::MouseButton::Left;
    case SAPP_MOUSEBUTTON_RIGHT:
        return otk::MouseButton::Right;
    case SAPP_MOUSEBUTTON_MIDDLE:
        return otk::MouseButton::Middle;
    default:
        return otk::MouseButton::NoButton;
    }
}

int touchId(const sapp_touchpoint& t) { return static_cast<int>(t.identifier & 0x3fffffff) + 1; }

otk::PointerType touchType(const sapp_touchpoint& t) {
    return t.android_tooltype == SAPP_ANDROIDTOOLTYPE_STYLUS ? otk::PointerType::Pen : otk::PointerType::Touch;
}

bool mapKey(sapp_keycode k, otk::Key& out) {
    switch (k) {
    case SAPP_KEYCODE_LEFT:
    case SAPP_KEYCODE_A:
        out = otk::Key::Left;
        return true;
    case SAPP_KEYCODE_RIGHT:
    case SAPP_KEYCODE_D:
        out = otk::Key::Right;
        return true;
    case SAPP_KEYCODE_UP:
    case SAPP_KEYCODE_W:
        out = otk::Key::Up;
        return true;
    case SAPP_KEYCODE_DOWN:
    case SAPP_KEYCODE_S:
        out = otk::Key::Down;
        return true;
    case SAPP_KEYCODE_EQUAL:
    case SAPP_KEYCODE_KP_ADD:
        out = otk::Key::ZoomIn;
        return true;
    case SAPP_KEYCODE_MINUS:
    case SAPP_KEYCODE_KP_SUBTRACT:
        out = otk::Key::ZoomOut;
        return true;
    case SAPP_KEYCODE_Q:
        out = otk::Key::RotateLeft;
        return true;
    case SAPP_KEYCODE_E:
        out = otk::Key::RotateRight;
        return true;
    case SAPP_KEYCODE_PAGE_UP:
        out = otk::Key::TiltUp;
        return true;
    case SAPP_KEYCODE_PAGE_DOWN:
        out = otk::Key::TiltDown;
        return true;
    case SAPP_KEYCODE_N:
        out = otk::Key::ResetNorth;
        return true;
    case SAPP_KEYCODE_T:
        out = otk::Key::ResetTilt;
        return true;
    default:
        return false;
    }
}

std::vector<otk::ImageryLayer> withAdded(std::vector<otk::ImageryLayer> layers) {
    layers.insert(layers.end(), state->addedLayers.begin(), state->addedLayers.end());
    return layers;
}

void showStatus(const std::string& s) {
    state->status = s;
    state->statusTime = 8.0;
    state->titleTimer = 1.0;
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

void zoomTo(otk::Region region, double tilt, double padMeters = 0.0) {
    if (state->viewGiven) return;
    const double dLat = padMeters / 110574.0;
    const double dLon = padMeters / (111320.0 * std::max(std::cos(0.5 * (region.south + region.north) * 3.14159265358979323846 / 180.0), 0.01));
    region = {region.west - dLon, region.south - dLat, region.east + dLon, region.north + dLat};
    otk::View v = state->engine.viewForRegion(region);
    v.tilt = tilt;
    if (state->shotPath.empty()) state->engine.flyTo(v, 2.5);
    else state->engine.setView(v);
}

void openPath(const std::string& path) {
    otk::OpenResult r = state->engine.open(path);
    const otk::ImportPlan& plan = r.plan;
    if (!r.ok) {
        showStatus("Could not open " + plan.name + (plan.message.empty() ? "" : ": " + plan.message));
        return;
    }
    std::string what;
    switch (plan.target) {
    case otk::ImportTarget::Features:
        what = std::to_string(r.data.features.size()) + " features";
        if (!r.data.overlays.empty()) what += ", " + std::to_string(r.data.overlays.size()) + " overlays";
        if (!r.data.models.empty()) what += ", " + std::to_string(r.data.models.size()) + " models";
        what += " from " + plan.name;
        break;
    case otk::ImportTarget::Imagery:
        state->addedLayers.insert(state->addedLayers.end(), plan.imagery.begin(), plan.imagery.end());
        what = std::to_string(plan.imagery.size()) + " map layer(s) from " + plan.name;
        break;
    case otk::ImportTarget::Elevation:
        what = "Elevation from " + plan.name;
        break;
    case otk::ImportTarget::Tileset:
        what = "3D Tiles " + plan.name;
        break;
    case otk::ImportTarget::PointCloud:
        what = "Point cloud " + plan.name;
        break;
    case otk::ImportTarget::Model:
        what = "Model " + plan.name + " placed at the view centre";
        break;
    case otk::ImportTarget::Iconset:
        what = "Iconset " + r.iconset + " registered";
        break;
    case otk::ImportTarget::Unsupported:
        break;
    }
    if (r.hasRegion) {
        const bool models = plan.target == otk::ImportTarget::Model || !r.data.models.empty();
        const bool solid = models || plan.target == otk::ImportTarget::Tileset || plan.target == otk::ImportTarget::PointCloud;
        zoomTo(r.region, models ? 60.0 : solid ? 45.0 : 0.0, models ? 250.0 : 0.0);
    }
    showStatus(what);
}

void event(const sapp_event* e) {
    otk::Engine& eng = state->engine;
    switch (e->type) {
    case SAPP_EVENTTYPE_FILES_DROPPED:
        for (int i = 0; i < sapp_get_num_dropped_files(); ++i) openPath(sapp_get_dropped_file_path(i));
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (!state->mouseDown) {
            state->mouseDown = true;
            state->mouseButton = e->mouse_button;
            eng.input().pointerDown(0, e->mouse_x, e->mouse_y, otk::PointerType::Mouse, mouseButton(e->mouse_button), e->modifiers);
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
        eng.input().pointerMove(0, e->mouse_x, e->mouse_y, e->modifiers);
        state->mouseX = e->mouse_x;
        state->mouseY = e->mouse_y;
        break;
    case SAPP_EVENTTYPE_MOUSE_LEAVE:
        if (!state->mouseDown) eng.input().pointerMove(0, -1e6, -1e6, 0);
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
        for (int k = 0; k <= static_cast<int>(otk::Key::ResetTilt); ++k) eng.input().keyUp(static_cast<otk::Key>(k));
        state->mouseDown = false;
        break;
    case SAPP_EVENTTYPE_KEY_UP: {
        otk::Key k;
        if (mapKey(e->key_code, k)) eng.input().keyUp(k);
        break;
    }
    case SAPP_EVENTTYPE_KEY_DOWN: {
        otk::Key mapped;
        if (mapKey(e->key_code, mapped)) {
            if (!e->key_repeat) eng.input().keyDown(mapped, e->modifiers);
            break;
        }
        if (e->key_repeat) break;
        if (e->key_code >= SAPP_KEYCODE_1 && e->key_code <= SAPP_KEYCODE_4) {
            state->preset = e->key_code - SAPP_KEYCODE_1;
            eng.setLayers(withAdded(preset(state->preset)));
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
            otk::View v;
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
            eng.setGridType(static_cast<otk::GridType>(state->gridType));
            eng.setGridEnabled(state->grid);
        } else if (e->key_code == SAPP_KEYCODE_I) {
            state->labels = !state->labels;
            eng.setLabelsEnabled(state->labels);
        } else if (e->key_code == SAPP_KEYCODE_K) {
            state->slope = !state->slope;
            eng.setTerrainSlope(state->slope);
        } else if (e->key_code == SAPP_KEYCODE_J) {
            if (!eng.labelsEnabled()) {
                showStatus("Labels are off, press I first");
            } else if (int id = eng.downloadLabels(eng.viewRegion())) {
                state->labelsDownload = id;
                showStatus("Downloading roads and street names for this view");
            } else {
                showStatus("Roads come from a local file, nothing to download");
            }
        } else if (e->key_code == SAPP_KEYCODE_L) {
            eng.setContours(!eng.contoursEnabled(), state->contourInterval);
        } else if (e->key_code == SAPP_KEYCODE_B) {
            double lat, lon;
            if (eng.pick(state->mouseX, state->mouseY, lat, lon))
                for (const otk::Feature& f : otk::bullseye({lat, lon, 0}, eng.view().gsd * 250.0)) eng.addFeature(f);
        } else if (e->key_code == SAPP_KEYCODE_C) {
            double lat, lon;
            if (eng.pick(state->mouseX, state->mouseY, lat, lon)) {
                if (state->rbStarted)
                    for (const otk::Feature& f : otk::rangeBearingLine({state->rbLat, state->rbLon, 0}, {lat, lon, 0})) eng.addFeature(f);
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
            otk::View v = eng.view();
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
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (!a.empty() && a[0] != '-') {
            state->openPaths.push_back(a);
            continue;
        }
        if (i + 1 >= argc) break;
        if (a == "--view") {
            otk::View& v = state->startView;
            std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf", &v.lat, &v.lon, &v.gsd, &v.tilt, &v.azimuth);
            state->viewGiven = true;
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
        } else if (a == "--clear-cache") {
            state->clearCache = std::atoi(argv[++i]) != 0;
        } else if (a == "--fps") {
            state->fps = std::max(0, std::atoi(argv[++i]));
        } else if (a == "--idle-fps") {
            state->idleFps = std::max(0, std::atoi(argv[++i]));
        } else if (a == "--offline") {
            state->offline = std::atoi(argv[++i]) != 0;
        } else if (a == "--mbtiles" || a == "--gpkg" || a == "--osmdroid") {
            otk::ImageryLayer l;
            l.type = a == "--mbtiles" ? otk::SourceType::MBTiles
                     : a == "--gpkg"  ? otk::SourceType::GeoPackage
                                      : otk::SourceType::Osmdroid;
            l.path = argv[++i];
            l.name = l.path;
            state->customLayers.push_back(l);
        } else if (a == "--gdal") {
            otk::ImageryLayer l;
            l.type = otk::SourceType::Gdal;
            l.path = argv[++i];
            l.name = l.path;
            state->customLayers.push_back(l);
        } else if (a == "--quantized-mesh") {
            state->quantizedMesh = argv[++i];
        } else if (a == "--gdal-dem") {
            state->gdalDem = argv[++i];
        } else if (a == "--mobac") {
            for (auto& l : otk::loadMobac(argv[++i])) state->customLayers.push_back(l);
        } else if (a == "--import") {
            state->imports.push_back(argv[++i]);
        } else if (a == "--cache-limit") {
            state->cacheLimit = std::atoi(argv[++i]);
        } else if (a == "--export") {
            otk::Region& r = state->exportRegion;
            char path[1024] = {0};
            state->hasExport = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%d,%d,%1023[^\n]", &r.west, &r.south, &r.east, &r.north,
                                           &state->exportMin, &state->exportMax, path) == 7;
            state->exportPath = path;
        } else if (a == "--download") {
            otk::Region& r = state->download;
            state->hasDownload = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%d,%d", &r.west, &r.south, &r.east, &r.north,
                                             &state->downloadMin, &state->downloadMax) == 6;
        } else if (a == "--download-labels") {
            otk::Region& r = state->labelsRegion;
            state->hasLabelsDownload = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf", &r.west, &r.south, &r.east, &r.north) == 4;
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
            otk::ModelPlacement p;
            char path[1024] = {0};
            if (std::sscanf(argv[++i], "%1023[^,],%lf,%lf,%lf,%lf", path, &p.position.lat, &p.position.lon, &p.heading, &p.scale) == 5)
                state->models.push_back({path, p});
        } else if (a == "--features") {
            state->featureFiles.push_back(argv[++i]);
        } else if (a == "--heatmap") {
            state->heat = std::atoi(argv[++i]) != 0;
        } else if (a == "--zoom-test") {
            otk::View& v = state->zoomTarget;
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
        } else if (a == "--labels") {
            state->labels = std::atoi(argv[++i]) != 0;
        } else if (a == "--grid-type") {
            state->gridType = std::clamp(std::atoi(argv[++i]), 0, 2);
        } else if (a == "--slope") {
            state->slope = std::atoi(argv[++i]) != 0;
        } else if (a == "--buildings") {
            state->buildings = std::atoi(argv[++i]) != 0;
        } else if (a == "--controls") {
            state->controls = std::atoi(argv[++i]);
        } else if (a == "--contours") {
            state->contours = true;
            state->contourInterval = std::max(0.0, std::atof(argv[++i]));
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
        } else if (a == "--frames") {
            state->framesPath = argv[++i];
            if (state->framesFile) std::fclose(state->framesFile);
            state->framesFile = std::fopen(state->framesPath.c_str(), "w");
            if (state->framesFile)
                std::fputs("t,dt_ms,cpu_ms,thread_ms,lat,lon,gsd,tilt,az,tiles,max_level,incomplete,coarse,depth,blank,renders,uploads,builds,pending,"
                           "pop,pops,merge,terrain_pop,blur,blur2,blur_depth,fade,cover,busy,input\n",
                           state->framesFile);
#if defined(_WIN32)
            _putenv_s("OTK_VISUAL", "1");
#else
            setenv("OTK_VISUAL", "1", 1);
#endif
        } else if (a == "--exit-after") {
            state->exitAfter = std::atof(argv[++i]);
        } else if (a == "--env") {
            std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq != std::string::npos && eq > 0) {
#if defined(_WIN32)
                _putenv_s(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str());
#else
                setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
#endif
            }
        } else if (a == "--diag") {
            state->diag = std::atoi(argv[++i]) != 0;
        } else if (a == "--lod-blend") {
#if defined(_WIN32)
            _putenv_s("OTK_LOD_BLEND", argv[++i]);
#else
            setenv("OTK_LOD_BLEND", argv[++i], 1);
#endif
        } else if (a == "--margin") {
#if defined(_WIN32)
            _putenv_s("OTK_MARGIN", argv[++i]);
#else
            setenv("OTK_MARGIN", argv[++i], 1);
#endif
        } else if (a == "--holes") {
            state->holes = std::atoi(argv[++i]) != 0;
#if defined(_WIN32)
            if (state->holes) _putenv_s("OTK_HOLES", "1");
#else
            if (state->holes) setenv("OTK_HOLES", "1", 1);
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

void gesturePoint(const ViewerState::Gesture& g, int p, double k, double& x, double& y) {
    if (g.turn) {
        const double a = (g.angle0 + (g.angle1 - g.angle0) * k) * 3.14159265358979323846 / 180.0 + (p == 0 ? 0.0 : 3.14159265358979323846);
        x = g.cx + std::cos(a) * g.radius;
        y = g.cy + std::sin(a) * g.radius;
        return;
    }
    x = g.from[p][0] + (g.to[p][0] - g.from[p][0]) * k;
    y = g.from[p][1] + (g.to[p][1] - g.from[p][1]) * k;
}

void beginGesture(ViewerState::Gesture g, double t) {
    otk::InputController& in = state->engine.input();
    g.start = t;
    state->gesture = g;
    state->gesturing = true;
    for (int p = 0; p < g.count; ++p) {
        double x, y;
        gesturePoint(g, p, 0.0, x, y);
        in.pointerDown(101 + p, x, y, g.type, g.button, 0);
    }
}

bool runScript() {
    otk::InputController& in = state->engine.input();
    double t = state->shotElapsed;
    if (state->gesturing) {
        const auto& g = state->gesture;
        double k = std::clamp((t - g.start) / std::max(g.seconds, 0.01), 0.0, 1.0);
        for (int p = 0; p < g.count; ++p) {
            double x, y;
            gesturePoint(g, p, k, x, y);
            in.pointerMove(101 + p, x, y);
        }
        if (k >= 1.0) {
            for (int p = 0; p < g.count; ++p) {
                double x, y;
                gesturePoint(g, p, 1.0, x, y);
                in.pointerUp(101 + p, x, y);
            }
            state->gesturing = false;
        }
    }
    while (state->scriptNext < state->script.size() && state->script[state->scriptNext].at <= t && !state->gesturing) {
        const auto& st = state->script[state->scriptNext++];
        if (const char* dp = std::getenv("OTK_DIAG"))
            if (FILE* df = std::fopen(dp, "a")) {
                otk::View cv = state->engine.view();
                std::fprintf(df, "input t=%.2f %s %.0f %.0f %.0f %.0f %.2f | view %.5f %.5f gsd %.2f tilt %.1f az %.1f | so far frames %lld incomplete %lld coarse %lld loads %lld\n", t,
                             st.op.c_str(), st.v[0], st.v[1], st.v[2], st.v[3], st.v[4], cv.lat, cv.lon, cv.gsd, cv.tilt, cv.azimuth, state->scriptFrames,
                             state->scriptIncomplete, state->scriptCoarse, state->scriptLoads);
                std::fclose(df);
            }
        if (st.op == "wheel") in.wheel(st.v[0], st.v[1], 0, st.v[2], false, 0);
        else if (st.op == "tap") {
            in.pointerDown(78, st.v[0], st.v[1], otk::PointerType::Mouse, otk::MouseButton::Left, 0);
            in.pointerUp(78, st.v[0], st.v[1]);
        } else if (st.op == "hover") {
            in.pointerMove(79, st.v[0], st.v[1]);
        }
        else if (st.op == "preset") {
            state->preset = static_cast<int>(st.v[0]);
            state->engine.setLayers(withAdded(preset(state->preset)));
        }
        else if (st.op == "drag" || st.op == "rdrag" || st.op == "swipe") {
            ViewerState::Gesture g;
            g.count = 1;
            g.from[0][0] = st.v[0];
            g.from[0][1] = st.v[1];
            g.to[0][0] = st.v[2];
            g.to[0][1] = st.v[3];
            g.seconds = st.v[4];
            g.type = st.op == "swipe" ? otk::PointerType::Touch : otk::PointerType::Mouse;
            g.button = st.op == "rdrag" ? otk::MouseButton::Right : otk::MouseButton::Left;
            beginGesture(g, t);
        } else if (st.op == "pinch") {
            ViewerState::Gesture g;
            g.count = 2;
            const double a = st.v[5] * 3.14159265358979323846 / 180.0;
            for (int p = 0; p < 2; ++p) {
                const double s = p == 0 ? -0.5 : 0.5;
                g.from[p][0] = st.v[0] + std::cos(a) * st.v[2] * s;
                g.from[p][1] = st.v[1] + std::sin(a) * st.v[2] * s;
                g.to[p][0] = st.v[0] + std::cos(a) * st.v[3] * s;
                g.to[p][1] = st.v[1] + std::sin(a) * st.v[3] * s;
            }
            g.seconds = st.v[4];
            beginGesture(g, t);
        } else if (st.op == "twist") {
            ViewerState::Gesture g;
            g.count = 2;
            g.turn = true;
            g.cx = st.v[0];
            g.cy = st.v[1];
            g.radius = st.v[2] * 0.5;
            g.angle0 = st.v[3];
            g.angle1 = st.v[4];
            g.seconds = st.v[5];
            beginGesture(g, t);
        } else if (st.op == "tilt") {
            ViewerState::Gesture g;
            g.count = 2;
            for (int p = 0; p < 2; ++p) {
                g.from[p][0] = st.v[0] + (p == 0 ? -120.0 : 120.0);
                g.from[p][1] = st.v[1];
                g.to[p][0] = g.from[p][0];
                g.to[p][1] = st.v[1] + st.v[2];
            }
            g.seconds = st.v[3];
            beginGesture(g, t);
        } else if (st.op == "fly") {
            otk::View v;
            v.lat = st.v[0];
            v.lon = st.v[1];
            v.gsd = st.v[2];
            v.tilt = st.v[3];
            v.azimuth = st.v[4];
            state->engine.flyTo(v, st.v[5] > 0 ? st.v[5] : 3.0);
        } else if (st.op == "view") {
            otk::View v;
            v.lat = st.v[0];
            v.lon = st.v[1];
            v.gsd = st.v[2];
            v.tilt = st.v[3];
            v.azimuth = st.v[4];
            state->engine.setView(v);
        }
    }
    return state->scriptNext < state->script.size() || state->gesturing || in.active() || state->engine.animating();
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
    d.enable_dragndrop = true;
    d.max_dropped_files = 32;
    d.max_dropped_file_path_length = 4096;
    d.window_title = "OTKGlobeEngine";
    d.logger.func = slog_func;
    return d;
}
