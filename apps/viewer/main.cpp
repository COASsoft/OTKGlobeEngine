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
#include "shot.hpp"
#include "watchdog.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

struct ViewerState {
    ote::Engine engine;
    bool leftDown = false;
    bool rightDown = false;
    float mouseX = 0, mouseY = 0;
    int preset = 0;
    int touchCount = 0;
    ote::View startView;
    bool offscreen = false;
    std::string shotPath;
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
    bool terrain = true;
    bool depth = true;
    double exaggeration = 1.0;
    std::string elevationDir;
    int width = 1600;
    int height = 900;
    float t0x = 0, t0y = 0, t1x = 0, t1y = 0;
    double titleTimer = 0;
};

ViewerState* state = nullptr;
FILE* logFile = nullptr;

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
    } else if (i == 2) {
        ote::ImageryLayer osm;
        osm.name = "OpenStreetMap";
        osm.url = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
        osm.maxZoom = 19;
        layers = {osm};
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
    state->engine.setViewport(sapp_width(), sapp_height(), sapp_dpi_scale());
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
    {
        std::vector<ote::ElevationLayer> elev;
        if (!state->elevationDir.empty()) {
            ote::ElevationLayer d;
            d.type = ote::ElevationType::Directory;
            d.path = state->elevationDir;
            d.maxZoom = 14;
            elev.push_back(d);
        }
        for (auto& e : ote::defaultElevation()) elev.push_back(e);
        state->engine.setElevation(elev);
        state->engine.setTerrainEnabled(state->terrain);
        state->engine.setTerrainExaggeration(state->exaggeration);
    }
    state->engine.setLayers(state->customLayers.empty() ? preset(state->preset) : state->customLayers);
    if (state->hasDownload)
        state->downloadId = state->engine.downloadRegion(0, state->download, state->downloadMin, state->downloadMax);
    state->engine.setView(state->startView);
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

void frame() {
    watchdogBeat();
    if (!state->shotPath.empty()) {
        state->engine.setViewport(state->width, state->height, 1.0f);
        sg_attachments att{};
        att.colors[0] = state->shotColorView;
        att.depth_stencil = state->shotDepthView;
        state->engine.frameOffscreen(att, state->width, state->height);
        sg_commit();
        ++state->frames;
        state->shotElapsed += sapp_frame_duration();
        state->settledFrames = state->engine.needsRedraw() ? 0 : state->settledFrames + 1;
        bool downloading = state->downloadId && !state->engine.downloadStatus(state->downloadId).finished;
        bool settled = state->shotElapsed > 1.0 && state->settledFrames > 10 && !downloading;
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
                std::fprintf(f, "%s frames=%d seconds=%.1f settled=%d tiles=%d L%d imagery=%d pending=%d\n",
                             ok ? "saved" : "failed", state->frames, state->shotElapsed, settled ? 1 : 0,
                             s.terrainTiles, s.maxLevel, s.imageryTextures, s.pendingRequests);
                if (state->downloadId) {
                    ote::DownloadStatus d = state->engine.downloadStatus(state->downloadId);
                    std::fprintf(f, "download total=%lld done=%lld failed=%lld finished=%d\n",
                                 static_cast<long long>(d.total), static_cast<long long>(d.done),
                                 static_cast<long long>(d.failed), d.finished ? 1 : 0);
                }
                std::fclose(f);
            }
            sapp_quit();
        }
        return;
    }
    state->engine.setViewport(sapp_width(), sapp_height(), sapp_dpi_scale());
    state->engine.frame(sglue_swapchain());
    sg_commit();
    state->titleTimer += sapp_frame_duration();
    if (state->titleTimer > 0.5) {
        state->titleTimer = 0;
        ote::Stats s = state->engine.stats();
        ote::View v = state->engine.view();
        char title[256];
        std::snprintf(title, sizeof(title),
                      "OpenTAKEngine  %.5f, %.5f  gsd %.2f m/px  tilt %.0f  az %.0f  |  tiles %d  L%d  imagery %d  pending %d",
                      v.lat, v.lon, v.gsd, v.tilt, v.azimuth, s.terrainTiles, s.maxLevel, s.imageryTextures,
                      s.pendingRequests);
        sapp_set_window_title(title);
    }
}

void cleanup() {
    state->engine.shutdown();
    sg_shutdown();
    delete state;
    state = nullptr;
}

void event(const sapp_event* e) {
    ote::Engine& eng = state->engine;
    switch (e->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) state->leftDown = true;
        if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) state->rightDown = true;
        state->mouseX = e->mouse_x;
        state->mouseY = e->mouse_y;
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) state->leftDown = false;
        if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) state->rightDown = false;
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (state->leftDown) eng.pan(state->mouseX, state->mouseY, e->mouse_x, e->mouse_y);
        if (state->rightDown) {
            eng.rotate((e->mouse_x - state->mouseX) * 0.25);
            eng.tiltBy((state->mouseY - e->mouse_y) * 0.2);
        }
        state->mouseX = e->mouse_x;
        state->mouseY = e->mouse_y;
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        eng.zoomAt(state->mouseX, state->mouseY, std::pow(0.8, e->scroll_y));
        break;
    case SAPP_EVENTTYPE_TOUCHES_BEGAN:
    case SAPP_EVENTTYPE_TOUCHES_MOVED: {
        int n = e->num_touches;
        if (n == 1 && state->touchCount == 1 && e->type == SAPP_EVENTTYPE_TOUCHES_MOVED) {
            eng.pan(state->t0x, state->t0y, e->touches[0].pos_x, e->touches[0].pos_y);
        } else if (n >= 2 && state->touchCount >= 2 && e->type == SAPP_EVENTTYPE_TOUCHES_MOVED) {
            float ax = e->touches[0].pos_x, ay = e->touches[0].pos_y, bx = e->touches[1].pos_x, by = e->touches[1].pos_y;
            double d0 = std::hypot(state->t1x - state->t0x, state->t1y - state->t0y);
            double d1 = std::hypot(bx - ax, by - ay);
            double a0 = std::atan2(state->t1y - state->t0y, state->t1x - state->t0x);
            double a1 = std::atan2(by - ay, bx - ax);
            double dy0 = ay - state->t0y, dy1 = by - state->t1y;
            if (dy0 * dy1 > 0 && std::abs(d1 - d0) < 4.0 && std::abs(dy0) > std::abs(ax - state->t0x)) {
                eng.tiltBy(-(dy0 + dy1) * 0.5 * 0.15);
            } else {
                if (d0 > 1.0 && d1 > 1.0) eng.zoomAt((ax + bx) * 0.5, (ay + by) * 0.5, d0 / d1);
                eng.rotate((a1 - a0) * 57.29577951308232);
            }
        }
        state->touchCount = n;
        if (n >= 1) {
            state->t0x = e->touches[0].pos_x;
            state->t0y = e->touches[0].pos_y;
        }
        if (n >= 2) {
            state->t1x = e->touches[1].pos_x;
            state->t1y = e->touches[1].pos_y;
        }
        break;
    }
    case SAPP_EVENTTYPE_TOUCHES_ENDED:
    case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
        state->touchCount = 0;
        break;
    case SAPP_EVENTTYPE_KEY_DOWN:
        if (e->key_code >= SAPP_KEYCODE_1 && e->key_code <= SAPP_KEYCODE_3) {
            state->preset = e->key_code - SAPP_KEYCODE_1;
            eng.setLayers(preset(state->preset));
        } else if (e->key_code == SAPP_KEYCODE_T) {
            state->terrain = !state->terrain;
            eng.setTerrainEnabled(state->terrain);
        } else if (e->key_code == SAPP_KEYCODE_D) {
            state->depth = !state->depth;
            eng.setDepthPerception(state->depth);
        } else if (e->key_code == SAPP_KEYCODE_E) {
            state->exaggeration = state->exaggeration >= 3.0 ? 1.0 : state->exaggeration + 0.5;
            eng.setTerrainExaggeration(state->exaggeration);
        } else if (e->key_code == SAPP_KEYCODE_R) {
            ote::View v = eng.view();
            v.azimuth = 0;
            v.tilt = 0;
            eng.setView(v);
        } else if (e->key_code == SAPP_KEYCODE_UP) {
            eng.tiltBy(5);
        } else if (e->key_code == SAPP_KEYCODE_DOWN) {
            eng.tiltBy(-5);
        } else if (e->key_code == SAPP_KEYCODE_LEFT) {
            eng.rotate(-10);
        } else if (e->key_code == SAPP_KEYCODE_RIGHT) {
            eng.rotate(10);
        } else if (e->key_code == SAPP_KEYCODE_EQUAL || e->key_code == SAPP_KEYCODE_KP_ADD) {
            eng.zoomAt(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f, 0.5);
        } else if (e->key_code == SAPP_KEYCODE_MINUS || e->key_code == SAPP_KEYCODE_KP_SUBTRACT) {
            eng.zoomAt(sapp_widthf() * 0.5f, sapp_heightf() * 0.5f, 2.0);
        }
        break;
    default:
        break;
    }
}

}

sapp_desc sokol_main(int argc, char* argv[]) {
    state = new ViewerState();
    state->startView.lat = 30.0;
    state->startView.lon = -100.0;
    state->startView.gsd = 25000.0;
    for (int i = 1; i + 1 < argc; ++i) {
        std::string a = argv[i];
        if (a == "--view") {
            ote::View& v = state->startView;
            std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf", &v.lat, &v.lon, &v.gsd, &v.tilt, &v.azimuth);
        } else if (a == "--offscreen") {
            state->offscreen = std::atoi(argv[++i]) != 0;
        } else if (a == "--shot") {
            state->shotPath = argv[++i];
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
        } else if (a == "--mobac") {
            for (auto& l : ote::loadMobac(argv[++i])) state->customLayers.push_back(l);
        } else if (a == "--download") {
            ote::Region& r = state->download;
            state->hasDownload = std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%d,%d", &r.west, &r.south, &r.east, &r.north,
                                             &state->downloadMin, &state->downloadMax) == 6;
        } else if (a == "--terrain") {
            state->terrain = std::atoi(argv[++i]) != 0;
        } else if (a == "--exaggeration") {
            state->exaggeration = std::atof(argv[++i]);
        } else if (a == "--elevation-dir") {
            state->elevationDir = argv[++i];
        } else if (a == "--preset") {
            state->preset = std::atoi(argv[++i]);
        }
    }
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
