#define SOKOL_IMPL
#define SOKOL_NO_ENTRY_OVERRIDE
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include "ote/engine.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

struct ViewerState {
    ote::Engine engine;
    bool leftDown = false;
    bool rightDown = false;
    float mouseX = 0, mouseY = 0;
    int preset = 0;
    int touchCount = 0;
    float t0x = 0, t0y = 0, t1x = 0, t1y = 0;
    double titleTimer = 0;
};

ViewerState* state = nullptr;

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
    sg_setup(&desc);
    state->engine.init();
    state->engine.setViewport(sapp_width(), sapp_height(), sapp_dpi_scale());
    state->engine.setLayers(preset(0));
    ote::View v;
    v.lat = 30.0;
    v.lon = -100.0;
    v.gsd = 25000.0;
    state->engine.setView(v);
}

void frame() {
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
    sapp_desc d{};
    d.init_cb = init;
    d.frame_cb = frame;
    d.cleanup_cb = cleanup;
    d.event_cb = event;
    d.width = 1600;
    d.height = 900;
    d.sample_count = 4;
    d.high_dpi = true;
    d.window_title = "OpenTAKEngine";
    d.logger.func = slog_func;
    return d;
}
