#include "ote/ote.h"

#include "ote/engine.hpp"

#include <sokol_gfx.h>

#include <algorithm>
#include <cstring>

struct ote_engine {
    ote::Engine engine;
    std::vector<ote::ImageryLayer> layers;
    std::vector<ote::ElevationLayer> elevation;
    sg_pixel_format color = SG_PIXELFORMAT_RGBA8;
    int samples = 1;
};

namespace {

ote_engine* create(const sg_environment& env) {
    sg_desc desc{};
    desc.environment = env;
    ote::configureGfx(desc);
    sg_setup(&desc);
    if (!sg_isvalid()) return nullptr;
    auto* e = new ote_engine();
    e->color = env.defaults.color_format;
    e->samples = env.defaults.sample_count;
    e->engine.init();
    e->engine.setLayers(ote::defaultLayers());
    e->layers = ote::defaultLayers();
    e->elevation = ote::defaultElevation();
    e->engine.setElevation(e->elevation);
    return e;
}

sg_swapchain swapchain(const ote_engine* e, int w, int h) {
    sg_swapchain sc{};
    sc.width = w;
    sc.height = h;
    sc.sample_count = e->samples;
    sc.color_format = e->color;
    sc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    return sc;
}

void frame(ote_engine* e, sg_swapchain sc, float dpi) {
    e->engine.setViewport(sc.width, sc.height, dpi);
    e->engine.frame(sc);
    sg_commit();
}

}

extern "C" {

ote_engine* ote_create_gl(int gles, int sample_count) {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    (void)gles;
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    env.defaults.sample_count = sample_count > 0 ? sample_count : 1;
    return create(env);
#else
    (void)gles;
    (void)sample_count;
    return nullptr;
#endif
}

ote_engine* ote_create_d3d11(void* device, void* device_context, int sample_count) {
#if defined(SOKOL_D3D11)
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    env.defaults.sample_count = sample_count > 0 ? sample_count : 1;
    env.d3d11.device = device;
    env.d3d11.device_context = device_context;
    return create(env);
#else
    (void)device;
    (void)device_context;
    (void)sample_count;
    return nullptr;
#endif
}

ote_engine* ote_create_metal(void* mtl_device, int sample_count) {
#if defined(SOKOL_METAL)
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    env.defaults.sample_count = sample_count > 0 ? sample_count : 1;
    env.metal.device = mtl_device;
    return create(env);
#else
    (void)mtl_device;
    (void)sample_count;
    return nullptr;
#endif
}

void ote_destroy(ote_engine* e) {
    if (!e) return;
    e->engine.shutdown();
    delete e;
    if (sg_isvalid()) sg_shutdown();
}

void ote_frame_gl(ote_engine* e, int width, int height, uint32_t framebuffer, float dpi_scale) {
    sg_swapchain sc = swapchain(e, width, height);
    sc.gl.framebuffer = framebuffer;
    frame(e, sc, dpi_scale);
}

void ote_frame_d3d11(ote_engine* e, int width, int height, void* render_view, void* resolve_view, void* depth_stencil_view,
                     float dpi_scale) {
    sg_swapchain sc = swapchain(e, width, height);
    sc.d3d11.render_view = render_view;
    sc.d3d11.resolve_view = resolve_view;
    sc.d3d11.depth_stencil_view = depth_stencil_view;
    frame(e, sc, dpi_scale);
}

void ote_frame_metal(ote_engine* e, int width, int height, void* current_drawable, void* depth_stencil_texture,
                     void* msaa_color_texture, float dpi_scale) {
    sg_swapchain sc = swapchain(e, width, height);
    sc.metal.current_drawable = current_drawable;
    sc.metal.depth_stencil_texture = depth_stencil_texture;
    sc.metal.msaa_color_texture = msaa_color_texture;
    frame(e, sc, dpi_scale);
}

int ote_needs_redraw(const ote_engine* e) { return e->engine.needsRedraw() ? 1 : 0; }

void ote_set_android_vm(void* java_vm) { ote::setAndroidJavaVm(java_vm); }

void ote_set_cache_directory(ote_engine* e, const char* dir) { e->engine.setCacheDirectory(dir ? dir : ""); }

void ote_set_offline(ote_engine* e, int offline) { e->engine.setOffline(offline != 0); }

void ote_set_memory_budget(ote_engine* e, int megabytes) { e->engine.setMemoryBudget(megabytes); }

void ote_clear_layers(ote_engine* e) {
    e->layers.clear();
    e->engine.setLayers(e->layers);
}

int ote_add_layer(ote_engine* e, ote_source type, const char* name, const char* url_or_path, int min_zoom, int max_zoom,
                  float opacity) {
    ote::ImageryLayer l;
    l.type = static_cast<ote::SourceType>(type);
    l.name = name ? name : "";
    if (type == OTE_SOURCE_URL || type == OTE_SOURCE_WMTS || type == OTE_SOURCE_MVT) l.url = url_or_path ? url_or_path : "";
    else l.path = url_or_path ? url_or_path : "";
    l.minZoom = min_zoom;
    l.maxZoom = max_zoom;
    l.opacity = opacity;
    e->layers.push_back(l);
    e->engine.setLayers(e->layers);
    return static_cast<int>(e->layers.size()) - 1;
}

int ote_add_wms_layer(ote_engine* e, const char* name, const char* url, const char* layers, int srid, const char* version,
                      float opacity) {
    ote::ImageryLayer l;
    l.type = ote::SourceType::Wms;
    l.name = name ? name : "";
    l.url = url ? url : "";
    l.wmsLayers = layers ? layers : "";
    l.srid = srid == 4326 ? 4326 : 3857;
    if (version) l.wmsVersion = version;
    l.opacity = opacity;
    e->layers.push_back(l);
    e->engine.setLayers(e->layers);
    return static_cast<int>(e->layers.size()) - 1;
}

int ote_load_mobac(ote_engine* e, const char* xml_or_path) {
    auto loaded = ote::loadMobac(xml_or_path ? xml_or_path : "");
    for (auto& l : loaded) e->layers.push_back(l);
    e->engine.setLayers(e->layers);
    return static_cast<int>(loaded.size());
}

void ote_use_default_layers(ote_engine* e) {
    e->layers = ote::defaultLayers();
    e->engine.setLayers(e->layers);
}

void ote_set_terrain(ote_engine* e, int enabled) { e->engine.setTerrainEnabled(enabled != 0); }

void ote_set_terrain_exaggeration(ote_engine* e, double factor) { e->engine.setTerrainExaggeration(factor); }

void ote_set_depth_perception(ote_engine* e, int enabled) { e->engine.setDepthPerception(enabled != 0); }

void ote_set_terrain_transparency(ote_engine* e, float alpha) { e->engine.setTerrainTransparency(alpha); }

void ote_add_elevation_directory(ote_engine* e, const char* path) {
    ote::ElevationLayer d;
    d.type = ote::ElevationType::Directory;
    d.path = path ? path : "";
    d.maxZoom = 14;
    e->elevation.insert(e->elevation.begin(), d);
    e->engine.setElevation(e->elevation);
}

double ote_elevation(const ote_engine* e, double lat, double lon) { return e->engine.elevationAt(lat, lon); }

void ote_set_view(ote_engine* e, const ote_view* v) {
    ote::View view = e->engine.view();
    view.lat = v->lat;
    view.lon = v->lon;
    view.gsd = v->gsd;
    view.azimuth = v->azimuth;
    view.tilt = v->tilt;
    e->engine.setView(view);
}

void ote_get_view(const ote_engine* e, ote_view* out) {
    ote::View v = e->engine.view();
    out->lat = v.lat;
    out->lon = v.lon;
    out->gsd = v.gsd;
    out->azimuth = v.azimuth;
    out->tilt = v.tilt;
}

int ote_pick(const ote_engine* e, double sx, double sy, double* lat, double* lon) {
    return e->engine.pick(sx, sy, *lat, *lon) ? 1 : 0;
}

void ote_pan(ote_engine* e, double x0, double y0, double x1, double y1) { e->engine.pan(x0, y0, x1, y1); }

void ote_zoom_at(ote_engine* e, double sx, double sy, double factor) { e->engine.zoomAt(sx, sy, factor); }

void ote_rotate(ote_engine* e, double degrees) { e->engine.rotate(degrees); }

void ote_rotate_at(ote_engine* e, double sx, double sy, double degrees) { e->engine.rotateAt(sx, sy, degrees); }

void ote_set_heading_locked_pan(ote_engine* e, int enabled) { e->engine.setHeadingLockedPan(enabled != 0); }

void ote_pointer_down(ote_engine* e, int id, double x, double y, int pointer_type, int button, int modifiers) {
    e->engine.input().pointerDown(id, x, y, static_cast<ote::PointerType>(std::clamp(pointer_type, 0, 2)),
                                  static_cast<ote::MouseButton>(std::clamp(button, 0, 3)), modifiers);
}

void ote_pointer_move(ote_engine* e, int id, double x, double y, int modifiers) { e->engine.input().pointerMove(id, x, y, modifiers); }

void ote_pointer_up(ote_engine* e, int id, double x, double y) { e->engine.input().pointerUp(id, x, y); }

void ote_pointer_cancel(ote_engine* e, int id) { e->engine.input().pointerCancel(id); }

void ote_pointer_cancel_all(ote_engine* e) { e->engine.input().cancelAll(); }

void ote_wheel(ote_engine* e, double x, double y, double dx, double dy, int precise, int modifiers) {
    e->engine.input().wheel(x, y, dx, dy, precise != 0, modifiers);
}

void ote_magnify(ote_engine* e, double x, double y, double scale) { e->engine.input().magnify(x, y, scale); }

void ote_rotate_gesture(ote_engine* e, double x, double y, double degrees) { e->engine.input().rotateGesture(x, y, degrees); }

void ote_key(ote_engine* e, int key, int down, int modifiers) {
    if (key < OTE_KEY_LEFT || key > OTE_KEY_RESET_TILT) return;
    if (down) e->engine.input().keyDown(static_cast<ote::Key>(key), modifiers);
    else e->engine.input().keyUp(static_cast<ote::Key>(key));
}

void ote_set_tap_callback(ote_engine* e, ote_tap_callback callback, void* user) {
    if (!callback) {
        e->engine.input().onTap(nullptr);
        return;
    }
    e->engine.input().onTap([callback, user](double x, double y, int taps, ote::PointerType type) {
        callback(user, x, y, taps, static_cast<int>(type));
    });
}

void ote_set_long_press_callback(ote_engine* e, ote_press_callback callback, void* user) {
    if (!callback) {
        e->engine.input().onLongPress(nullptr);
        return;
    }
    e->engine.input().onLongPress([callback, user](double x, double y, ote::PointerType type) { callback(user, x, y, static_cast<int>(type)); });
}

void ote_tilt(ote_engine* e, double degrees) { e->engine.tiltBy(degrees); }

void ote_set_first_person(ote_engine* e, int enabled, double height) { e->engine.setFirstPerson(enabled != 0, height); }

void ote_walk(ote_engine* e, double forward_m, double right_m) { e->engine.walk(forward_m, right_m); }

void ote_look(ote_engine* e, double heading_deg, double pitch_deg) { e->engine.look(heading_deg, pitch_deg); }

void ote_set_3d(ote_engine* e, int enabled) { e->engine.set3D(enabled != 0); }

void ote_fly_to(ote_engine* e, const ote_view* v, double seconds) {
    ote::View view = e->engine.view();
    view.lat = v->lat;
    view.lon = v->lon;
    view.gsd = v->gsd;
    view.azimuth = v->azimuth;
    view.tilt = v->tilt;
    e->engine.flyTo(view, seconds);
}

int ote_load_features(ote_engine* e, const char* path) {
    return static_cast<int>(e->engine.loadFeatures(path ? path : "").size());
}

uint64_t ote_add_point(ote_engine* e, double lat, double lon, double alt, const char* name, uint32_t argb) {
    ote::Feature f;
    f.name = name ? name : "";
    f.style.iconColor = argb;
    f.geometries.push_back({ote::GeometryType::Point, {{lat, lon, alt}}, {}});
    return e->engine.addFeature(f);
}

uint64_t ote_add_line(ote_engine* e, const double* pts, int count, const char* name, uint32_t argb, float width) {
    ote::Feature f;
    f.name = name ? name : "";
    f.style.strokeColor = argb;
    f.style.strokeWidth = width;
    ote::Geometry g;
    g.type = ote::GeometryType::LineString;
    for (int i = 0; i < count; ++i) g.points.push_back({pts[i * 2], pts[i * 2 + 1], 0});
    f.geometries.push_back(g);
    return e->engine.addFeature(f);
}

int ote_remove_feature(ote_engine* e, uint64_t id) { return e->engine.removeFeature(id) ? 1 : 0; }

void ote_clear_features(ote_engine* e) { e->engine.clearFeatures(); }

uint64_t ote_hit_test(const ote_engine* e, double sx, double sy, double radius_px) {
    return e->engine.hitTest(sx, sy, radius_px);
}

uint64_t ote_add_model(ote_engine* e, const char* path, double lat, double lon, double heading, double scale) {
    ote::ModelPlacement p;
    p.position = {lat, lon, 0};
    p.heading = heading;
    p.scale = scale;
    return e->engine.addModel(path ? path : "", p);
}

void ote_set_grid(ote_engine* e, int enabled) { e->engine.setGridEnabled(enabled != 0); }

uint64_t ote_add_tileset(ote_engine* e, const char* path, double max_sse) {
    return e->engine.addTileset(path ? path : "", max_sse > 0 ? max_sse : 16.0);
}

int ote_remove_tileset(ote_engine* e, uint64_t id) { return e->engine.removeTileset(id) ? 1 : 0; }

uint64_t ote_add_point_cloud(ote_engine* e, const char* path, uint64_t max_points, float size) {
    return e->engine.addPointCloud(path ? path : "", max_points ? static_cast<std::size_t>(max_points) : 4000000, size > 0 ? size : 2.0f);
}

uint64_t ote_viewshed(ote_engine* e, double lat, double lon, double height, double radius) {
    return e->engine.computeViewshed({lat, lon, 0}, height, radius);
}

int ote_to_mgrs(double lat, double lon, int digits, char* out, int capacity) {
    std::string s = ote::toMgrs(lat, lon, digits);
    if (!out || capacity <= 0) return static_cast<int>(s.size());
    std::strncpy(out, s.c_str(), static_cast<size_t>(capacity - 1));
    out[capacity - 1] = 0;
    return static_cast<int>(s.size());
}

int ote_from_mgrs(const char* mgrs, double* lat, double* lon) {
    return mgrs && ote::fromMgrs(mgrs, *lat, *lon) ? 1 : 0;
}

}
