#ifndef OTE_H
#define OTE_H

#include <stdint.h>

#if defined(_WIN32) && defined(OTE_SHARED)
#if defined(OTE_BUILDING)
#define OTE_API __declspec(dllexport)
#else
#define OTE_API __declspec(dllimport)
#endif
#elif defined(OTE_SHARED)
#define OTE_API __attribute__((visibility("default")))
#else
#define OTE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ote_engine ote_engine;

typedef enum ote_source {
    OTE_SOURCE_URL = 0,
    OTE_SOURCE_WMS = 1,
    OTE_SOURCE_WMTS = 2,
    OTE_SOURCE_MBTILES = 3,
    OTE_SOURCE_GEOPACKAGE = 4,
    OTE_SOURCE_OSMDROID = 5,
    OTE_SOURCE_GDAL = 6,
    OTE_SOURCE_MVT = 7
} ote_source;

typedef struct ote_view {
    double lat;
    double lon;
    double gsd;
    double azimuth;
    double tilt;
} ote_view;

OTE_API ote_engine* ote_create_gl(int gles, int sample_count);
OTE_API ote_engine* ote_create_d3d11(void* device, void* device_context, int sample_count);
OTE_API ote_engine* ote_create_metal(void* mtl_device, int sample_count);
OTE_API void ote_destroy(ote_engine* e);

OTE_API void ote_frame_gl(ote_engine* e, int width, int height, uint32_t framebuffer, float dpi_scale);
OTE_API void ote_frame_d3d11(ote_engine* e, int width, int height, void* render_view, void* resolve_view,
                             void* depth_stencil_view, float dpi_scale);
OTE_API void ote_frame_metal(ote_engine* e, int width, int height, void* current_drawable, void* depth_stencil_texture,
                             void* msaa_color_texture, float dpi_scale);
OTE_API int ote_needs_redraw(const ote_engine* e);

OTE_API void ote_set_android_vm(void* java_vm);
OTE_API void ote_set_cache_directory(ote_engine* e, const char* dir);
OTE_API void ote_set_offline(ote_engine* e, int offline);

OTE_API void ote_clear_layers(ote_engine* e);
OTE_API int ote_add_layer(ote_engine* e, ote_source type, const char* name, const char* url_or_path, int min_zoom,
                          int max_zoom, float opacity);
OTE_API int ote_add_wms_layer(ote_engine* e, const char* name, const char* url, const char* layers, int srid,
                              const char* version, float opacity);
OTE_API int ote_load_mobac(ote_engine* e, const char* xml_or_path);
OTE_API void ote_use_default_layers(ote_engine* e);

OTE_API void ote_set_terrain(ote_engine* e, int enabled);
OTE_API void ote_set_terrain_exaggeration(ote_engine* e, double factor);
OTE_API void ote_set_depth_perception(ote_engine* e, int enabled);
OTE_API void ote_add_elevation_directory(ote_engine* e, const char* path);
OTE_API double ote_elevation(const ote_engine* e, double lat, double lon);

OTE_API void ote_set_view(ote_engine* e, const ote_view* v);
OTE_API void ote_get_view(const ote_engine* e, ote_view* out);
OTE_API int ote_pick(const ote_engine* e, double sx, double sy, double* lat, double* lon);
OTE_API void ote_pan(ote_engine* e, double x0, double y0, double x1, double y1);
OTE_API void ote_zoom_at(ote_engine* e, double sx, double sy, double factor);
OTE_API void ote_rotate(ote_engine* e, double degrees);
OTE_API void ote_tilt(ote_engine* e, double degrees);
OTE_API void ote_set_first_person(ote_engine* e, int enabled, double height_above_ground);
OTE_API void ote_walk(ote_engine* e, double forward_m, double right_m);
OTE_API void ote_look(ote_engine* e, double heading_deg, double pitch_deg);
OTE_API void ote_set_3d(ote_engine* e, int enabled);
OTE_API void ote_fly_to(ote_engine* e, const ote_view* v, double seconds);

OTE_API int ote_load_features(ote_engine* e, const char* path);
OTE_API uint64_t ote_add_point(ote_engine* e, double lat, double lon, double alt, const char* name, uint32_t argb);
OTE_API uint64_t ote_add_line(ote_engine* e, const double* lat_lon_pairs, int count, const char* name, uint32_t argb,
                              float width);
OTE_API int ote_remove_feature(ote_engine* e, uint64_t id);
OTE_API void ote_clear_features(ote_engine* e);
OTE_API uint64_t ote_hit_test(const ote_engine* e, double sx, double sy, double radius_px);
OTE_API uint64_t ote_add_model(ote_engine* e, const char* path, double lat, double lon, double heading, double scale);
OTE_API void ote_set_grid(ote_engine* e, int enabled);
OTE_API uint64_t ote_viewshed(ote_engine* e, double lat, double lon, double height, double radius);

OTE_API int ote_to_mgrs(double lat, double lon, int digits, char* out, int capacity);
OTE_API int ote_from_mgrs(const char* mgrs, double* lat, double* lon);

#ifdef __cplusplus
}
#endif

#endif
