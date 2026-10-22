#include "otk/otk.h"
#include "otk/astro.hpp"
#include "otk/milsym.hpp"
#include "otk/navigation.hpp"

#include "otk/engine.hpp"
#include "otk/formats.hpp"

#include <sokol_gfx.h>

#include <algorithm>
#include <cstring>
#include <vector>

struct otk_engine {
    otk::Engine engine;
    std::vector<otk::ImageryLayer> layers;
    std::vector<otk::ElevationLayer> elevation;
    sg_pixel_format color = SG_PIXELFORMAT_RGBA8;
    int samples = 1;
};

namespace {

otk_engine* create(const sg_environment& env) {
    sg_desc desc{};
    desc.environment = env;
    otk::configureGfx(desc);
    sg_setup(&desc);
    if (!sg_isvalid()) return nullptr;
    auto* e = new otk_engine();
    e->color = env.defaults.color_format;
    e->samples = env.defaults.sample_count;
    e->engine.init();
    e->engine.setLayers(otk::defaultLayers());
    e->layers = otk::defaultLayers();
    e->elevation = otk::defaultElevation();
    e->engine.setElevation(e->elevation);
    return e;
}

sg_swapchain swapchain(const otk_engine* e, int w, int h) {
    sg_swapchain sc{};
    sc.width = w;
    sc.height = h;
    sc.sample_count = e->samples;
    sc.color_format = e->color;
    sc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    return sc;
}

void frame(otk_engine* e, sg_swapchain sc, float dpi) {
    e->engine.setViewport(sc.width, sc.height, dpi);
    e->engine.frame(sc);
    sg_commit();
}

}

extern "C" {

otk_engine* otk_create_gl(int gles, int sample_count) {
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

otk_engine* otk_create_d3d11(void* device, void* device_context, int sample_count) {
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

otk_engine* otk_create_metal(void* mtl_device, int sample_count) {
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

void otk_destroy(otk_engine* e) {
    if (!e) return;
    e->engine.shutdown();
    delete e;
    if (sg_isvalid()) sg_shutdown();
}

void otk_frame_gl(otk_engine* e, int width, int height, uint32_t framebuffer, float dpi_scale) {
    sg_swapchain sc = swapchain(e, width, height);
    sc.gl.framebuffer = framebuffer;
    frame(e, sc, dpi_scale);
}

void otk_frame_d3d11(otk_engine* e, int width, int height, void* render_view, void* resolve_view, void* depth_stencil_view,
                     float dpi_scale) {
    sg_swapchain sc = swapchain(e, width, height);
    sc.d3d11.render_view = render_view;
    sc.d3d11.resolve_view = resolve_view;
    sc.d3d11.depth_stencil_view = depth_stencil_view;
    frame(e, sc, dpi_scale);
}

void otk_frame_metal(otk_engine* e, int width, int height, void* current_drawable, void* depth_stencil_texture,
                     void* msaa_color_texture, float dpi_scale) {
    sg_swapchain sc = swapchain(e, width, height);
    sc.metal.current_drawable = current_drawable;
    sc.metal.depth_stencil_texture = depth_stencil_texture;
    sc.metal.msaa_color_texture = msaa_color_texture;
    frame(e, sc, dpi_scale);
}

int otk_needs_redraw(const otk_engine* e) { return e->engine.needsRedraw() ? 1 : 0; }

void otk_set_xr_anchor(otk_engine* e, double lat, double lon, double alt, double heading_deg, double scale) {
    otk::XrAnchor a;
    a.lat = lat;
    a.lon = lon;
    a.alt = alt;
    a.heading = heading_deg;
    a.scale = scale;
    e->engine.setXrAnchor(a);
}

void otk_frame_xr(otk_engine* e, const otk_xr_view* views, int count, int target_width, int target_height, float dpi_scale) {
    if (!views || count <= 0) return;
    std::vector<otk::XrView> xr(static_cast<size_t>(count));
    std::vector<sg_swapchain> targets(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const otk_xr_view& v = views[i];
        otk::XrView& x = xr[i];
        for (int k = 0; k < 3; ++k) x.position[k] = v.position[k];
        for (int k = 0; k < 4; ++k) x.orientation[k] = v.orientation[k];
        x.angleLeft = v.angle_left;
        x.angleRight = v.angle_right;
        x.angleUp = v.angle_up;
        x.angleDown = v.angle_down;
        x.x = v.x;
        x.y = v.y;
        x.width = v.width;
        x.height = v.height;
        sg_swapchain sc = swapchain(e, target_width, target_height);
        sc.d3d11.render_view = v.render_view;
        sc.d3d11.resolve_view = v.resolve_view;
        sc.d3d11.depth_stencil_view = v.depth_stencil_view;
        sc.gl.framebuffer = v.gl_framebuffer;
        targets[i] = sc;
    }
    e->engine.setViewport(views[0].width > 0 ? views[0].width : target_width, views[0].height > 0 ? views[0].height : target_height, dpi_scale);
    e->engine.frameXr(xr.data(), targets.data(), count);
    sg_commit();
}

void otk_set_android_vm(void* java_vm) { otk::setAndroidJavaVm(java_vm); }

void otk_set_cache_directory(otk_engine* e, const char* dir) { e->engine.setCacheDirectory(dir ? dir : ""); }

void otk_set_offline(otk_engine* e, int offline) { e->engine.setOffline(offline != 0); }

void otk_set_memory_budget(otk_engine* e, int megabytes) { e->engine.setMemoryBudget(megabytes); }

void otk_set_cache_limit(otk_engine* e, int megabytes) { e->engine.setCacheLimit(megabytes); }

int64_t otk_cache_bytes(const otk_engine* e) { return e->engine.cacheBytes(); }

void otk_network_changed(otk_engine* e) { e->engine.networkChanged(); }

void otk_set_offline_backdrop(otk_engine* e, int enabled) { e->engine.setOfflineBackdrop(enabled != 0); }

int64_t otk_count_region_tiles(const otk_engine* e, int layer, double west, double south, double east, double north, int min_zoom,
                               int max_zoom) {
    return e->engine.countRegionTiles(layer, {west, south, east, north}, min_zoom, max_zoom);
}

int otk_download_region(otk_engine* e, int layer, double west, double south, double east, double north, int min_zoom, int max_zoom) {
    return e->engine.downloadRegion(layer, {west, south, east, north}, min_zoom, max_zoom);
}

void otk_get_download_status(const otk_engine* e, int id, otk_download_status* out) {
    if (!out) return;
    otk::DownloadStatus s = e->engine.downloadStatus(id);
    out->total = s.total;
    out->done = s.done;
    out->failed = s.failed;
    out->retrying = s.retrying;
    out->finished = s.finished ? 1 : 0;
    out->cancelled = s.cancelled ? 1 : 0;
}

void otk_cancel_download(otk_engine* e, int id) { e->engine.cancelDownload(id); }

int64_t otk_export_tiles(otk_engine* e, int layer, double west, double south, double east, double north, int min_zoom, int max_zoom,
                         const char* path) {
    return path ? e->engine.exportTiles(layer, {west, south, east, north}, min_zoom, max_zoom, path) : -1;
}

int64_t otk_import_tiles(otk_engine* e, int layer, const char* path) { return path ? e->engine.importTiles(layer, path) : -1; }

void otk_clear_layers(otk_engine* e) {
    e->layers.clear();
    e->engine.setLayers(e->layers);
}

int otk_add_layer(otk_engine* e, otk_source type, const char* name, const char* url_or_path, int min_zoom, int max_zoom,
                  float opacity) {
    otk::ImageryLayer l;
    l.type = static_cast<otk::SourceType>(type);
    l.name = name ? name : "";
    if (type == OTK_SOURCE_URL || type == OTK_SOURCE_WMTS || type == OTK_SOURCE_MVT) l.url = url_or_path ? url_or_path : "";
    else l.path = url_or_path ? url_or_path : "";
    l.minZoom = min_zoom;
    l.maxZoom = max_zoom;
    l.opacity = opacity;
    e->layers.push_back(l);
    e->engine.setLayers(e->layers);
    return static_cast<int>(e->layers.size()) - 1;
}

int otk_add_wms_layer(otk_engine* e, const char* name, const char* url, const char* layers, int srid, const char* version,
                      float opacity) {
    otk::ImageryLayer l;
    l.type = otk::SourceType::Wms;
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

int otk_load_mobac(otk_engine* e, const char* xml_or_path) {
    auto loaded = otk::loadMobac(xml_or_path ? xml_or_path : "");
    for (auto& l : loaded) e->layers.push_back(l);
    e->engine.setLayers(e->layers);
    return static_cast<int>(loaded.size());
}

void otk_use_default_layers(otk_engine* e) {
    e->layers = otk::defaultLayers();
    e->engine.setLayers(e->layers);
}

void otk_set_terrain(otk_engine* e, int enabled) { e->engine.setTerrainEnabled(enabled != 0); }

void otk_set_terrain_exaggeration(otk_engine* e, double factor) { e->engine.setTerrainExaggeration(factor); }

void otk_set_depth_perception(otk_engine* e, int enabled) { e->engine.setDepthPerception(enabled != 0); }

void otk_set_terrain_transparency(otk_engine* e, float alpha) { e->engine.setTerrainTransparency(alpha); }

void otk_add_elevation_directory(otk_engine* e, const char* path) {
    otk::ElevationLayer d;
    d.type = otk::ElevationType::Directory;
    d.path = path ? path : "";
    d.maxZoom = 14;
    e->elevation.insert(e->elevation.begin(), d);
    e->engine.setElevation(e->elevation);
}

double otk_elevation(const otk_engine* e, double lat, double lon) { return e->engine.elevationAt(lat, lon); }

void otk_set_view(otk_engine* e, const otk_view* v) {
    otk::View view = e->engine.view();
    view.lat = v->lat;
    view.lon = v->lon;
    view.gsd = v->gsd;
    view.azimuth = v->azimuth;
    view.tilt = v->tilt;
    e->engine.setView(view);
}

void otk_get_view(const otk_engine* e, otk_view* out) {
    otk::View v = e->engine.view();
    out->lat = v.lat;
    out->lon = v.lon;
    out->gsd = v.gsd;
    out->azimuth = v.azimuth;
    out->tilt = v.tilt;
}

int otk_pick(const otk_engine* e, double sx, double sy, double* lat, double* lon) {
    return e->engine.pick(sx, sy, *lat, *lon) ? 1 : 0;
}

void otk_pan(otk_engine* e, double x0, double y0, double x1, double y1) { e->engine.pan(x0, y0, x1, y1); }

void otk_zoom_at(otk_engine* e, double sx, double sy, double factor) { e->engine.zoomAt(sx, sy, factor); }

void otk_rotate(otk_engine* e, double degrees) { e->engine.rotate(degrees); }

void otk_rotate_at(otk_engine* e, double sx, double sy, double degrees) { e->engine.rotateAt(sx, sy, degrees); }

void otk_set_heading_locked_pan(otk_engine* e, int enabled) { e->engine.setHeadingLockedPan(enabled != 0); }

void otk_pointer_down(otk_engine* e, int id, double x, double y, int pointer_type, int button, int modifiers) {
    e->engine.input().pointerDown(id, x, y, static_cast<otk::PointerType>(std::clamp(pointer_type, 0, 2)),
                                  static_cast<otk::MouseButton>(std::clamp(button, 0, 3)), modifiers);
}

void otk_pointer_move(otk_engine* e, int id, double x, double y, int modifiers) { e->engine.input().pointerMove(id, x, y, modifiers); }

void otk_pointer_up(otk_engine* e, int id, double x, double y) { e->engine.input().pointerUp(id, x, y); }

void otk_pointer_cancel(otk_engine* e, int id) { e->engine.input().pointerCancel(id); }

void otk_pointer_cancel_all(otk_engine* e) { e->engine.input().cancelAll(); }

void otk_wheel(otk_engine* e, double x, double y, double dx, double dy, int precise, int modifiers) {
    e->engine.input().wheel(x, y, dx, dy, precise != 0, modifiers);
}

void otk_magnify(otk_engine* e, double x, double y, double scale) { e->engine.input().magnify(x, y, scale); }

void otk_rotate_gesture(otk_engine* e, double x, double y, double degrees) { e->engine.input().rotateGesture(x, y, degrees); }

void otk_key(otk_engine* e, int key, int down, int modifiers) {
    if (key < OTK_KEY_LEFT || key > OTK_KEY_RESET_TILT) return;
    if (down) e->engine.input().keyDown(static_cast<otk::Key>(key), modifiers);
    else e->engine.input().keyUp(static_cast<otk::Key>(key));
}

void otk_set_tap_callback(otk_engine* e, otk_tap_callback callback, void* user) {
    if (!callback) {
        e->engine.input().onTap(nullptr);
        return;
    }
    e->engine.input().onTap([callback, user](double x, double y, int taps, otk::PointerType type) {
        callback(user, x, y, taps, static_cast<int>(type));
    });
}

void otk_set_long_press_callback(otk_engine* e, otk_press_callback callback, void* user) {
    if (!callback) {
        e->engine.input().onLongPress(nullptr);
        return;
    }
    e->engine.input().onLongPress([callback, user](double x, double y, otk::PointerType type) { callback(user, x, y, static_cast<int>(type)); });
}

void otk_tilt(otk_engine* e, double degrees) { e->engine.tiltBy(degrees); }

void otk_set_first_person(otk_engine* e, int enabled, double height) { e->engine.setFirstPerson(enabled != 0, height); }

void otk_walk(otk_engine* e, double forward_m, double right_m) { e->engine.walk(forward_m, right_m); }

void otk_look(otk_engine* e, double heading_deg, double pitch_deg) { e->engine.look(heading_deg, pitch_deg); }

void otk_set_3d(otk_engine* e, int enabled) { e->engine.set3D(enabled != 0); }

void otk_fly_to(otk_engine* e, const otk_view* v, double seconds) {
    otk::View view = e->engine.view();
    view.lat = v->lat;
    view.lon = v->lon;
    view.gsd = v->gsd;
    view.azimuth = v->azimuth;
    view.tilt = v->tilt;
    e->engine.flyTo(view, seconds);
}

int otk_load_features(otk_engine* e, const char* path) {
    return static_cast<int>(e->engine.loadFeatures(path ? path : "").size());
}

int otk_export_features(otk_engine* e, const char* path, const char* format) {
    otk::ExportResult r = e->engine.exportFeatures({}, path ? path : "", format ? format : "");
    return r.ok ? 1 : 0;
}

namespace {
int copyOut(const std::string& s, char* out, int capacity) {
    if (out && capacity > 0) {
        size_t n = std::min(s.size(), static_cast<size_t>(capacity - 1));
        std::memcpy(out, s.data(), n);
        out[n] = 0;
    }
    return static_cast<int>(s.size());
}
}

int otk_detect_format(const char* path, char* out, int capacity) { return copyOut(otk::detectFormat(std::string(path ? path : "")), out, capacity); }

int otk_formats_markdown(char* out, int capacity) { return copyOut(otk::formatsMarkdown(), out, capacity); }

int otk_register_iconset(const char* path, char* uid_out, int capacity) {
    return copyOut(otk::registerIconset(std::string(path ? path : "")), uid_out, capacity);
}

uint64_t otk_add_point(otk_engine* e, double lat, double lon, double alt, const char* name, uint32_t argb) {
    otk::Feature f;
    f.name = name ? name : "";
    f.style.iconColor = argb;
    f.geometries.push_back({otk::GeometryType::Point, {{lat, lon, alt}}, {}});
    return e->engine.addFeature(f);
}

uint64_t otk_add_line(otk_engine* e, const double* pts, int count, const char* name, uint32_t argb, float width) {
    otk::Feature f;
    f.name = name ? name : "";
    f.style.strokeColor = argb;
    f.style.strokeWidth = width;
    otk::Geometry g;
    g.type = otk::GeometryType::LineString;
    for (int i = 0; i < count; ++i) g.points.push_back({pts[i * 2], pts[i * 2 + 1], 0});
    f.geometries.push_back(g);
    return e->engine.addFeature(f);
}

int otk_remove_feature(otk_engine* e, uint64_t id) { return e->engine.removeFeature(id) ? 1 : 0; }

void otk_clear_features(otk_engine* e) { e->engine.clearFeatures(); }

uint64_t otk_hit_test(const otk_engine* e, double sx, double sy, double radius_px) {
    return e->engine.hitTest(sx, sy, radius_px);
}

uint64_t otk_add_model(otk_engine* e, const char* path, double lat, double lon, double heading, double scale) {
    otk::ModelPlacement p;
    p.position = {lat, lon, 0};
    p.heading = heading;
    p.scale = scale;
    return e->engine.addModel(path ? path : "", p);
}

void otk_set_grid(otk_engine* e, int enabled) { e->engine.setGridEnabled(enabled != 0); }

void otk_set_labels(otk_engine* e, int enabled) { e->engine.setLabelsEnabled(enabled != 0); }

int otk_labels(const otk_engine* e) { return e->engine.labelsEnabled() ? 1 : 0; }

void otk_set_labels_source(otk_engine* e, const char* url_or_path) { e->engine.setLabelsSource(url_or_path ? url_or_path : ""); }

int otk_labels_layer(const otk_engine* e) { return e->engine.labelsLayer(); }

void otk_set_buildings(otk_engine* e, int enabled) { e->engine.setBuildingsEnabled(enabled != 0); }

int otk_buildings(const otk_engine* e) { return e->engine.buildingsEnabled() ? 1 : 0; }

void otk_set_controls(otk_engine* e, uint32_t controls) { e->engine.setControls(controls); }

uint32_t otk_controls(const otk_engine* e) { return e->engine.controls(); }

void otk_set_controls_padding(otk_engine* e, double left, double top, double right, double bottom) {
    e->engine.setControlsPadding(left, top, right, bottom);
}

int otk_basemap_count(const otk_engine* e) { return static_cast<int>(e->engine.basemaps().size()); }

int otk_basemap_name(const otk_engine* e, int index, char* out, int capacity) {
    const std::vector<otk::Basemap> maps = e->engine.basemaps();
    if (index < 0 || index >= static_cast<int>(maps.size())) return copyOut(std::string(), out, capacity);
    return copyOut(maps[static_cast<size_t>(index)].name, out, capacity);
}

void otk_set_basemap(otk_engine* e, int index) { e->engine.setBasemap(index); }

int otk_basemap(const otk_engine* e) { return e->engine.basemap(); }

void otk_set_auto_download_labels(otk_engine* e, int enabled) { e->engine.setAutoDownloadLabels(enabled != 0); }

int otk_auto_download_labels(const otk_engine* e) { return e->engine.autoDownloadLabels() ? 1 : 0; }

int otk_download_labels(otk_engine* e, double west, double south, double east, double north, int max_zoom) {
    return e->engine.downloadLabels({west, south, east, north}, max_zoom > 0 ? max_zoom : 14);
}

uint64_t otk_add_tileset(otk_engine* e, const char* path, double max_sse) {
    return e->engine.addTileset(path ? path : "", max_sse > 0 ? max_sse : 16.0);
}

int otk_remove_tileset(otk_engine* e, uint64_t id) { return e->engine.removeTileset(id) ? 1 : 0; }

uint64_t otk_add_tileset_ex(otk_engine* e, const char* path, double max_sse, const char* headers, int lighting, int cache_offline) {
    otk::TilesetOptions options;
    options.maxScreenSpaceError = max_sse > 0 ? max_sse : 16.0;
    options.lighting = lighting == 1 ? otk::TilesetLighting::Lit : lighting == 2 ? otk::TilesetLighting::Unlit : otk::TilesetLighting::Auto;
    options.cacheOffline = cache_offline != 0;
    std::string all = headers ? headers : "";
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find('\n', pos);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(pos, end - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
            options.headers.emplace_back(line.substr(0, colon), value);
        }
        pos = end + 1;
    }
    return e->engine.addTileset(path ? path : "", options);
}

int otk_credits(const otk_engine* e, char* buffer, int size) {
    std::string all;
    for (const std::string& c : e->engine.credits()) all += (all.empty() ? "" : "\n") + c;
    if (buffer && size > 0) {
        size_t n = std::min(all.size(), static_cast<size_t>(size - 1));
        std::memcpy(buffer, all.data(), n);
        buffer[n] = 0;
    }
    return static_cast<int>(all.size());
}

void otk_set_sun_lighting(otk_engine* e, int enabled, double unix_seconds) { e->engine.setSunLighting(enabled != 0, unix_seconds); }

uint64_t otk_add_point_cloud(otk_engine* e, const char* path, uint64_t max_points, float size) {
    return e->engine.addPointCloud(path ? path : "", max_points ? static_cast<std::size_t>(max_points) : 4000000, size > 0 ? size : 2.0f);
}

uint64_t otk_viewshed(otk_engine* e, double lat, double lon, double height, double radius) {
    return e->engine.computeViewshed({lat, lon, 0}, height, radius);
}

namespace {

int addAll(otk_engine* e, const std::vector<otk::Feature>& features) {
    for (const otk::Feature& f : features) e->engine.addFeature(f);
    return static_cast<int>(features.size());
}

otk::ShapeStyle shapeStyle(uint32_t argb) {
    otk::ShapeStyle s;
    if (argb) s.stroke = argb;
    return s;
}

}

double otk_geodesic_inverse(double lat1, double lon1, double lat2, double lon2, double* initial_bearing, double* final_bearing) {
    otk::GeodesicLine g = otk::geodesicInverse(lat1, lon1, lat2, lon2);
    if (initial_bearing) *initial_bearing = g.initialBearing;
    if (final_bearing) *final_bearing = g.finalBearing;
    return g.distance;
}

void otk_geodesic_direct(double lat, double lon, double bearing, double distance, double* out_lat, double* out_lon) {
    otk::Coord c = otk::geodesicDirect({lat, lon, 0}, bearing, distance);
    if (out_lat) *out_lat = c.lat;
    if (out_lon) *out_lon = c.lon;
}

void otk_sun_moon_at(double unix_seconds, double lat, double lon, otk_sun_moon* out) {
    if (!out) return;
    otk::CelestialPosition s = otk::sunPosition(unix_seconds, lat, lon);
    otk::MoonInfo m = otk::moonPosition(unix_seconds, lat, lon);
    otk::Illuminance l = otk::groundIlluminance(unix_seconds, lat, lon);
    out->sun_azimuth = s.azimuth;
    out->sun_elevation = s.apparentElevation;
    out->moon_azimuth = m.position.azimuth;
    out->moon_elevation = m.position.apparentElevation;
    out->moon_illumination = m.illuminatedFraction;
    out->moon_phase_angle = m.phaseAngle;
    out->moon_waxing = m.waxing ? 1 : 0;
    out->sun_lux = l.sunLux;
    out->moon_lux = l.moonLux;
    out->total_lux = l.totalLux;
}

void otk_sun_moon_times(double start_unix_seconds, double lat, double lon, double hours, otk_twilight* out) {
    if (!out) return;
    otk::SunMoonTimes t = otk::sunMoonTimes(start_unix_seconds, lat, lon, hours > 0 ? hours : 24.0);
    out->astronomical_dawn = t.astronomicalDawn;
    out->nautical_dawn = t.nauticalDawn;
    out->civil_dawn = t.civilDawn;
    out->sunrise = t.sunrise;
    out->sunset = t.sunset;
    out->civil_dusk = t.civilDusk;
    out->nautical_dusk = t.nauticalDusk;
    out->astronomical_dusk = t.astronomicalDusk;
    out->moonrise = t.moonrise;
    out->moonset = t.moonset;
}

double otk_magnetic_declination(double lat, double lon, double altitude_m, double decimal_year) {
    return otk::magneticDeclination(lat, lon, altitude_m, decimal_year);
}

double otk_grid_convergence(double lat, double lon) { return otk::gridConvergence(lat, lon); }

double otk_bearing_from_true(double true_bearing, double lat, double lon, otk_north_reference reference) {
    return otk::bearingFromTrue(true_bearing, lat, lon, static_cast<otk::NorthReference>(reference));
}

int otk_format_coordinate(double lat, double lon, otk_coordinate_format format, int precision, char* out, int capacity) {
    return copyOut(otk::formatCoordinate(lat, lon, static_cast<otk::CoordinateFormat>(format), precision), out, capacity);
}

int otk_parse_coordinate(const char* text, double* lat, double* lon) {
    double la = 0, lo = 0;
    if (!text || !otk::parseCoordinate(text, la, lo)) return 0;
    if (lat) *lat = la;
    if (lon) *lon = lo;
    return 1;
}

uint64_t otk_add_symbol(otk_engine* e, double lat, double lon, double alt, const char* code, const char* name) {
    otk::Feature f;
    std::string c = code ? code : "";
    if (c.rfind("a-", 0) == 0) f.type = c;
    else f.style.sidc = c;
    f.name = name ? name : "";
    f.altitudeMode = alt != 0 ? otk::AltitudeMode::Absolute : otk::AltitudeMode::ClampToGround;
    f.geometries.push_back({otk::GeometryType::Point, {{lat, lon, alt}}, {}});
    return e->engine.addFeature(f);
}

int otk_render_symbol(const char* sidc, int size_px, uint8_t* rgba_out, int capacity) {
    int needed = size_px * size_px * 4;
    if (!sidc || size_px <= 0) return 0;
    if (!rgba_out || capacity < needed) return needed;
    otk::Image img = otk::renderSymbolImage(sidc, size_px);
    if (img.width != size_px) return 0;
    std::memcpy(rgba_out, img.rgba.data(), static_cast<size_t>(needed));
    return needed;
}

int otk_add_range_rings(otk_engine* e, double lat, double lon, double spacing_m, int count, int radials, uint32_t argb) {
    return addAll(e, otk::rangeRings({lat, lon, 0}, spacing_m, count, radials, shapeStyle(argb)));
}

int otk_add_bullseye(otk_engine* e, double lat, double lon, double radius_m, int rings, double bearing_step, otk_north_reference reference,
                     otk_angle_unit unit, uint32_t argb) {
    return addAll(e, otk::bullseye({lat, lon, 0}, radius_m, rings, bearing_step, static_cast<otk::NorthReference>(reference),
                                   static_cast<otk::AngleUnit>(unit), shapeStyle(argb)));
}

int otk_add_range_bearing(otk_engine* e, double lat1, double lon1, double lat2, double lon2, otk_north_reference reference, otk_angle_unit unit,
                          otk_distance_unit distance_unit, uint32_t argb) {
    return addAll(e, otk::rangeBearingLine({lat1, lon1, 0}, {lat2, lon2, 0}, static_cast<otk::NorthReference>(reference),
                                           static_cast<otk::AngleUnit>(unit), static_cast<otk::DistanceUnit>(distance_unit), shapeStyle(argb)));
}

int otk_add_contours(otk_engine* e, double west, double south, double east, double north, double interval, int major_every, uint32_t argb) {
    return static_cast<int>(e->engine.addContours({west, south, east, north}, interval, major_every, 256, argb ? argb : 0xFFB0703Au).size());
}

int otk_add_contours_in_view(otk_engine* e, double interval, int major_every, uint32_t argb) {
    return static_cast<int>(e->engine.addContours(e->engine.viewRegion(), interval, major_every, 256, argb ? argb : 0xFFB0703Au).size());
}

void otk_set_contours(otk_engine* e, int enabled, double interval, otk_distance_unit unit) {
    e->engine.setContours(enabled != 0, interval, static_cast<otk::DistanceUnit>(unit));
}

int otk_contours(const otk_engine* e) { return e->engine.contoursEnabled() ? 1 : 0; }

void otk_set_terrain_slope(otk_engine* e, int enabled, float opacity, double max_slope_degrees) {
    e->engine.setTerrainSlope(enabled != 0, opacity > 0 ? opacity : 0.6f, max_slope_degrees > 0 ? max_slope_degrees : 45.0);
}

void otk_set_grid_type(otk_engine* e, otk_grid_type type) { e->engine.setGridType(static_cast<otk::GridType>(type)); }

void otk_view_region(const otk_engine* e, double* west, double* south, double* east, double* north) {
    otk::Region r = e->engine.viewRegion();
    if (west) *west = r.west;
    if (south) *south = r.south;
    if (east) *east = r.east;
    if (north) *north = r.north;
}

namespace {

int writeRegion(bool ok, const otk::Region& r, double* west, double* south, double* east, double* north) {
    if (!ok) return 0;
    if (west) *west = r.west;
    if (south) *south = r.south;
    if (east) *east = r.east;
    if (north) *north = r.north;
    return 1;
}

}

void otk_view_for_region(const otk_engine* e, double west, double south, double east, double north, otk_view* out) {
    if (!out) return;
    otk::View v = e->engine.viewForRegion({west, south, east, north});
    out->lat = v.lat;
    out->lon = v.lon;
    out->gsd = v.gsd;
    out->azimuth = v.azimuth;
    out->tilt = v.tilt;
}

int otk_open(otk_engine* e, const char* path, otk_open_result* out) {
    otk::OpenResult r = e->engine.open(path ? path : "");
    if (out) {
        *out = {};
        out->ok = r.ok ? 1 : 0;
        out->target = static_cast<otk_import_target>(static_cast<int>(r.plan.target));
        out->items = r.plan.target == otk::ImportTarget::Features
                         ? static_cast<int>(r.data.features.size() + r.data.overlays.size() + r.data.models.size())
                     : r.plan.target == otk::ImportTarget::Imagery   ? static_cast<int>(r.plan.imagery.size())
                     : r.plan.target == otk::ImportTarget::Elevation ? static_cast<int>(r.plan.elevation.size())
                                                                     : (r.ok ? 1 : 0);
        out->has_region = r.hasRegion ? 1 : 0;
        out->west = r.region.west;
        out->south = r.region.south;
        out->east = r.region.east;
        out->north = r.region.north;
        out->id = r.tileset ? r.tileset : r.pointCloud ? r.pointCloud : r.model;
    }
    return r.ok ? 1 : 0;
}

int otk_load_features_region(otk_engine* e, const char* path, double* west, double* south, double* east, double* north) {
    otk::LoadedData d = e->engine.loadData(path ? path : "");
    writeRegion(d.hasRegion, d.region, west, south, east, north);
    return static_cast<int>(d.features.size() + d.overlays.size() + d.models.size());
}

int otk_tileset_region(const otk_engine* e, uint64_t id, double* west, double* south, double* east, double* north) {
    otk::Region r;
    return writeRegion(e->engine.tilesetRegion(id, r), r, west, south, east, north);
}

int otk_point_cloud_region(const otk_engine* e, uint64_t id, double* west, double* south, double* east, double* north) {
    otk::Region r;
    return writeRegion(e->engine.pointCloudRegion(id, r), r, west, south, east, north);
}

int otk_to_mgrs(double lat, double lon, int digits, char* out, int capacity) {
    std::string s = otk::toMgrs(lat, lon, digits);
    if (!out || capacity <= 0) return static_cast<int>(s.size());
    std::strncpy(out, s.c_str(), static_cast<size_t>(capacity - 1));
    out[capacity - 1] = 0;
    return static_cast<int>(s.size());
}

int otk_from_mgrs(const char* mgrs, double* lat, double* lon) {
    return mgrs && otk::fromMgrs(mgrs, *lat, *lon) ? 1 : 0;
}

}
