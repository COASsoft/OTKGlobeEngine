#ifndef OTK_H
#define OTK_H

#include <stdint.h>

#if defined(_WIN32) && defined(OTK_SHARED)
#if defined(OTK_BUILDING)
#define OTK_API __declspec(dllexport)
#else
#define OTK_API __declspec(dllimport)
#endif
#elif defined(OTK_SHARED)
#define OTK_API __attribute__((visibility("default")))
#else
#define OTK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct otk_engine otk_engine;

typedef enum otk_source {
    OTK_SOURCE_URL = 0,
    OTK_SOURCE_WMS = 1,
    OTK_SOURCE_WMTS = 2,
    OTK_SOURCE_MBTILES = 3,
    OTK_SOURCE_GEOPACKAGE = 4,
    OTK_SOURCE_OSMDROID = 5,
    OTK_SOURCE_GDAL = 6,
    OTK_SOURCE_MVT = 7
} otk_source;

typedef struct otk_view {
    double lat;
    double lon;
    double gsd;
    double azimuth;
    double tilt;
} otk_view;

OTK_API otk_engine* otk_create_gl(int gles, int sample_count);
OTK_API otk_engine* otk_create_d3d11(void* device, void* device_context, int sample_count);
OTK_API otk_engine* otk_create_metal(void* mtl_device, int sample_count);
OTK_API void otk_destroy(otk_engine* e);

OTK_API void otk_frame_gl(otk_engine* e, int width, int height, uint32_t framebuffer, float dpi_scale);
OTK_API void otk_frame_d3d11(otk_engine* e, int width, int height, void* render_view, void* resolve_view,
                             void* depth_stencil_view, float dpi_scale);
OTK_API void otk_frame_metal(otk_engine* e, int width, int height, void* current_drawable, void* depth_stencil_texture,
                             void* msaa_color_texture, float dpi_scale);
OTK_API int otk_needs_redraw(const otk_engine* e);

typedef struct otk_xr_view {
    double position[3];
    double orientation[4];
    double angle_left, angle_right, angle_up, angle_down;
    int x, y, width, height;
    void* render_view;
    void* resolve_view;
    void* depth_stencil_view;
    uint32_t gl_framebuffer;
} otk_xr_view;

OTK_API void otk_set_xr_anchor(otk_engine* e, double lat, double lon, double alt, double heading_deg, double scale);
OTK_API void otk_frame_xr(otk_engine* e, const otk_xr_view* views, int count, int target_width, int target_height, float dpi_scale);

OTK_API void otk_set_android_vm(void* java_vm);
OTK_API void otk_set_cache_directory(otk_engine* e, const char* dir);
OTK_API void otk_set_offline(otk_engine* e, int offline);
OTK_API void otk_set_memory_budget(otk_engine* e, int megabytes);
OTK_API void otk_set_cache_limit(otk_engine* e, int megabytes);
OTK_API int64_t otk_cache_bytes(const otk_engine* e);
OTK_API void otk_network_changed(otk_engine* e);
OTK_API void otk_set_offline_backdrop(otk_engine* e, int enabled);

typedef struct otk_download_status {
    int64_t total;
    int64_t done;
    int64_t failed;
    int64_t retrying;
    int finished;
    int cancelled;
} otk_download_status;

OTK_API int64_t otk_count_region_tiles(const otk_engine* e, int layer, double west, double south, double east, double north,
                                       int min_zoom, int max_zoom);
OTK_API int otk_download_region(otk_engine* e, int layer, double west, double south, double east, double north, int min_zoom,
                                int max_zoom);
OTK_API void otk_get_download_status(const otk_engine* e, int id, otk_download_status* out);
OTK_API void otk_cancel_download(otk_engine* e, int id);
OTK_API int64_t otk_export_tiles(otk_engine* e, int layer, double west, double south, double east, double north, int min_zoom,
                                 int max_zoom, const char* path);
OTK_API int64_t otk_import_tiles(otk_engine* e, int layer, const char* path);

OTK_API void otk_clear_layers(otk_engine* e);
OTK_API int otk_add_layer(otk_engine* e, otk_source type, const char* name, const char* url_or_path, int min_zoom,
                          int max_zoom, float opacity);
OTK_API int otk_add_wms_layer(otk_engine* e, const char* name, const char* url, const char* layers, int srid,
                              const char* version, float opacity);
OTK_API int otk_load_mobac(otk_engine* e, const char* xml_or_path);
OTK_API void otk_use_default_layers(otk_engine* e);

OTK_API void otk_set_terrain(otk_engine* e, int enabled);
OTK_API void otk_set_terrain_exaggeration(otk_engine* e, double factor);
OTK_API void otk_set_depth_perception(otk_engine* e, int enabled);
OTK_API void otk_set_terrain_transparency(otk_engine* e, float alpha);
OTK_API void otk_add_elevation_directory(otk_engine* e, const char* path);
OTK_API double otk_elevation(const otk_engine* e, double lat, double lon);

OTK_API void otk_set_view(otk_engine* e, const otk_view* v);
OTK_API void otk_get_view(const otk_engine* e, otk_view* out);
OTK_API int otk_pick(const otk_engine* e, double sx, double sy, double* lat, double* lon);
OTK_API void otk_pan(otk_engine* e, double x0, double y0, double x1, double y1);
OTK_API void otk_zoom_at(otk_engine* e, double sx, double sy, double factor);
OTK_API void otk_rotate(otk_engine* e, double degrees);
OTK_API void otk_tilt(otk_engine* e, double degrees);
OTK_API void otk_set_first_person(otk_engine* e, int enabled, double height_above_ground);
OTK_API void otk_walk(otk_engine* e, double forward_m, double right_m);
OTK_API void otk_look(otk_engine* e, double heading_deg, double pitch_deg);
OTK_API void otk_set_3d(otk_engine* e, int enabled);
OTK_API void otk_fly_to(otk_engine* e, const otk_view* v, double seconds);
OTK_API void otk_rotate_at(otk_engine* e, double sx, double sy, double degrees);
OTK_API void otk_set_heading_locked_pan(otk_engine* e, int enabled);

typedef enum otk_pointer_type { OTK_POINTER_MOUSE = 0, OTK_POINTER_TOUCH = 1, OTK_POINTER_PEN = 2 } otk_pointer_type;
typedef enum otk_mouse_button { OTK_BUTTON_NONE = 0, OTK_BUTTON_LEFT = 1, OTK_BUTTON_RIGHT = 2, OTK_BUTTON_MIDDLE = 3 } otk_mouse_button;
typedef enum otk_modifier { OTK_MOD_SHIFT = 1, OTK_MOD_CTRL = 2, OTK_MOD_ALT = 4, OTK_MOD_SUPER = 8 } otk_modifier;
typedef enum otk_key_code {
    OTK_KEY_LEFT = 0,
    OTK_KEY_RIGHT,
    OTK_KEY_UP,
    OTK_KEY_DOWN,
    OTK_KEY_ZOOM_IN,
    OTK_KEY_ZOOM_OUT,
    OTK_KEY_ROTATE_LEFT,
    OTK_KEY_ROTATE_RIGHT,
    OTK_KEY_TILT_UP,
    OTK_KEY_TILT_DOWN,
    OTK_KEY_RESET_NORTH,
    OTK_KEY_RESET_TILT
} otk_key_code;
typedef void (*otk_tap_callback)(void* user, double x, double y, int taps, int pointer_type);
typedef void (*otk_press_callback)(void* user, double x, double y, int pointer_type);

OTK_API void otk_pointer_down(otk_engine* e, int id, double x, double y, int pointer_type, int button, int modifiers);
OTK_API void otk_pointer_move(otk_engine* e, int id, double x, double y, int modifiers);
OTK_API void otk_pointer_up(otk_engine* e, int id, double x, double y);
OTK_API void otk_pointer_cancel(otk_engine* e, int id);
OTK_API void otk_pointer_cancel_all(otk_engine* e);
OTK_API void otk_wheel(otk_engine* e, double x, double y, double dx, double dy, int precise, int modifiers);
OTK_API void otk_magnify(otk_engine* e, double x, double y, double scale);
OTK_API void otk_rotate_gesture(otk_engine* e, double x, double y, double degrees);
OTK_API void otk_key(otk_engine* e, int key, int down, int modifiers);
OTK_API void otk_set_tap_callback(otk_engine* e, otk_tap_callback callback, void* user);
OTK_API void otk_set_long_press_callback(otk_engine* e, otk_press_callback callback, void* user);

OTK_API int otk_load_features(otk_engine* e, const char* path);
OTK_API int otk_export_features(otk_engine* e, const char* path, const char* format);
OTK_API int otk_detect_format(const char* path, char* out, int capacity);
OTK_API int otk_register_iconset(const char* path, char* uid_out, int capacity);
OTK_API int otk_formats_markdown(char* out, int capacity);
OTK_API uint64_t otk_add_point(otk_engine* e, double lat, double lon, double alt, const char* name, uint32_t argb);
OTK_API uint64_t otk_add_line(otk_engine* e, const double* lat_lon_pairs, int count, const char* name, uint32_t argb,
                              float width);
OTK_API int otk_remove_feature(otk_engine* e, uint64_t id);
OTK_API void otk_clear_features(otk_engine* e);
OTK_API uint64_t otk_hit_test(const otk_engine* e, double sx, double sy, double radius_px);
OTK_API uint64_t otk_add_model(otk_engine* e, const char* path, double lat, double lon, double heading, double scale);
OTK_API void otk_set_grid(otk_engine* e, int enabled);
OTK_API void otk_set_labels(otk_engine* e, int enabled);
OTK_API int otk_labels(const otk_engine* e);
OTK_API void otk_set_labels_source(otk_engine* e, const char* url_or_path);
OTK_API int otk_labels_layer(const otk_engine* e);
OTK_API int otk_download_labels(otk_engine* e, double west, double south, double east, double north, int max_zoom);
OTK_API uint64_t otk_add_tileset(otk_engine* e, const char* path_or_url, double max_sse);
OTK_API uint64_t otk_add_tileset_ex(otk_engine* e, const char* path_or_url, double max_sse, const char* headers, int lighting,
                                    int cache_offline);
OTK_API int otk_credits(const otk_engine* e, char* buffer, int size);
OTK_API void otk_set_sun_lighting(otk_engine* e, int enabled, double unix_seconds);
OTK_API int otk_remove_tileset(otk_engine* e, uint64_t id);
OTK_API uint64_t otk_add_point_cloud(otk_engine* e, const char* las_path, uint64_t max_points, float point_size_px);
OTK_API uint64_t otk_viewshed(otk_engine* e, double lat, double lon, double height, double radius);

OTK_API int otk_to_mgrs(double lat, double lon, int digits, char* out, int capacity);
OTK_API int otk_from_mgrs(const char* mgrs, double* lat, double* lon);

typedef enum otk_north_reference { OTK_NORTH_TRUE = 0, OTK_NORTH_MAGNETIC = 1, OTK_NORTH_GRID = 2 } otk_north_reference;
typedef enum otk_angle_unit { OTK_ANGLE_DEGREES = 0, OTK_ANGLE_MILS = 1 } otk_angle_unit;
typedef enum otk_distance_unit { OTK_DISTANCE_METRIC = 0, OTK_DISTANCE_IMPERIAL = 1, OTK_DISTANCE_NAUTICAL = 2 } otk_distance_unit;
typedef enum otk_coordinate_format {
    OTK_COORD_DECIMAL = 0,
    OTK_COORD_DM = 1,
    OTK_COORD_DMS = 2,
    OTK_COORD_MGRS = 3,
    OTK_COORD_UTM = 4,
    OTK_COORD_GARS = 5
} otk_coordinate_format;
typedef enum otk_grid_type { OTK_GRID_MGRS = 0, OTK_GRID_LATLON = 1, OTK_GRID_GARS = 2 } otk_grid_type;

OTK_API double otk_geodesic_inverse(double lat1, double lon1, double lat2, double lon2, double* initial_bearing, double* final_bearing);
OTK_API void otk_geodesic_direct(double lat, double lon, double bearing, double distance, double* out_lat, double* out_lon);
OTK_API double otk_magnetic_declination(double lat, double lon, double altitude_m, double decimal_year);

typedef struct otk_sun_moon {
    double sun_azimuth, sun_elevation;
    double moon_azimuth, moon_elevation, moon_illumination, moon_phase_angle;
    int moon_waxing;
    double sun_lux, moon_lux, total_lux;
} otk_sun_moon;

typedef struct otk_twilight {
    double astronomical_dawn, nautical_dawn, civil_dawn, sunrise, sunset, civil_dusk, nautical_dusk, astronomical_dusk, moonrise, moonset;
} otk_twilight;

OTK_API void otk_sun_moon_at(double unix_seconds, double lat, double lon, otk_sun_moon* out);
OTK_API void otk_sun_moon_times(double start_unix_seconds, double lat, double lon, double hours, otk_twilight* out);
OTK_API double otk_grid_convergence(double lat, double lon);
OTK_API double otk_bearing_from_true(double true_bearing, double lat, double lon, otk_north_reference reference);
OTK_API int otk_format_coordinate(double lat, double lon, otk_coordinate_format format, int precision, char* out, int capacity);
OTK_API int otk_parse_coordinate(const char* text, double* lat, double* lon);

OTK_API uint64_t otk_add_symbol(otk_engine* e, double lat, double lon, double alt, const char* sidc_or_cot_type, const char* name);
OTK_API int otk_render_symbol(const char* sidc, int size_px, uint8_t* rgba_out, int capacity);
OTK_API int otk_add_range_rings(otk_engine* e, double lat, double lon, double spacing_m, int count, int radials, uint32_t argb);
OTK_API int otk_add_bullseye(otk_engine* e, double lat, double lon, double radius_m, int rings, double bearing_step,
                             otk_north_reference reference, otk_angle_unit unit, uint32_t argb);
OTK_API int otk_add_range_bearing(otk_engine* e, double lat1, double lon1, double lat2, double lon2, otk_north_reference reference,
                                  otk_angle_unit unit, otk_distance_unit distance_unit, uint32_t argb);
OTK_API int otk_add_contours(otk_engine* e, double west, double south, double east, double north, double interval, int major_every,
                             uint32_t argb);
OTK_API int otk_add_contours_in_view(otk_engine* e, double interval, int major_every, uint32_t argb);
OTK_API void otk_set_contours(otk_engine* e, int enabled, double interval, otk_distance_unit unit);
OTK_API int otk_contours(const otk_engine* e);
OTK_API void otk_set_terrain_slope(otk_engine* e, int enabled, float opacity, double max_slope_degrees);
OTK_API void otk_set_grid_type(otk_engine* e, otk_grid_type type);
OTK_API void otk_view_region(const otk_engine* e, double* west, double* south, double* east, double* north);
OTK_API void otk_view_for_region(const otk_engine* e, double west, double south, double east, double north, otk_view* out);

typedef enum otk_import_target {
    OTK_IMPORT_FEATURES = 0,
    OTK_IMPORT_IMAGERY = 1,
    OTK_IMPORT_ELEVATION = 2,
    OTK_IMPORT_TILESET = 3,
    OTK_IMPORT_POINT_CLOUD = 4,
    OTK_IMPORT_MODEL = 5,
    OTK_IMPORT_ICONSET = 6,
    OTK_IMPORT_UNSUPPORTED = 7
} otk_import_target;

typedef struct otk_open_result {
    int ok;
    otk_import_target target;
    int items;
    int has_region;
    double west;
    double south;
    double east;
    double north;
    uint64_t id;
} otk_open_result;

OTK_API int otk_open(otk_engine* e, const char* path, otk_open_result* out);
OTK_API int otk_load_features_region(otk_engine* e, const char* path, double* west, double* south, double* east, double* north);
OTK_API int otk_tileset_region(const otk_engine* e, uint64_t id, double* west, double* south, double* east, double* north);
OTK_API int otk_point_cloud_region(const otk_engine* e, uint64_t id, double* west, double* south, double* east, double* north);

#ifdef __cplusplus
}
#endif

#endif
