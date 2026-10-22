#include "otk/otk.h"

#include <jni.h>

#include <string>
#include <vector>

namespace {

std::string str(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

otk_engine* E(jlong h) { return reinterpret_cast<otk_engine*>(h); }

JavaVM* gVm = nullptr;

struct Listener {
    jobject target = nullptr;
    jmethodID tap = nullptr;
    jmethodID press = nullptr;
};

JNIEnv* env() {
    JNIEnv* e = nullptr;
    if (gVm && gVm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) return nullptr;
    return e;
}

void onTap(void* user, double x, double y, int taps, int type) {
    auto* l = static_cast<Listener*>(user);
    if (JNIEnv* e = env()) e->CallVoidMethod(l->target, l->tap, x, y, taps, type);
}

void onPress(void* user, double x, double y, int type) {
    auto* l = static_cast<Listener*>(user);
    if (JNIEnv* e = env()) e->CallVoidMethod(l->target, l->press, x, y, type);
}

}

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    gVm = vm;
    otk_set_android_vm(vm);
    return JNI_VERSION_1_6;
}

#define OTK_FN(name) Java_com_otkglobeengine_OTKGlobeEngine_##name

JNIEXPORT jlong JNICALL OTK_FN(nativeCreate)(JNIEnv*, jclass, jint samples) {
    return reinterpret_cast<jlong>(otk_create_gl(1, samples));
}

JNIEXPORT void JNICALL OTK_FN(nativeDestroy)(JNIEnv*, jclass, jlong h) { otk_destroy(E(h)); }

JNIEXPORT void JNICALL OTK_FN(nativeFrame)(JNIEnv*, jclass, jlong h, jint w, jint hgt, jfloat dpi) {
    otk_frame_gl(E(h), w, hgt, 0, dpi);
}

JNIEXPORT jboolean JNICALL OTK_FN(nativeNeedsRedraw)(JNIEnv*, jclass, jlong h) { return otk_needs_redraw(E(h)) != 0; }

JNIEXPORT void JNICALL OTK_FN(nativeSetCacheDirectory)(JNIEnv* env, jclass, jlong h, jstring dir) {
    otk_set_cache_directory(E(h), str(env, dir).c_str());
}

JNIEXPORT void JNICALL OTK_FN(nativeSetOffline)(JNIEnv*, jclass, jlong h, jboolean v) { otk_set_offline(E(h), v ? 1 : 0); }

JNIEXPORT void JNICALL OTK_FN(nativeSetMemoryBudget)(JNIEnv*, jclass, jlong h, jint mb) { otk_set_memory_budget(E(h), mb); }

JNIEXPORT void JNICALL OTK_FN(nativeSetCacheLimit)(JNIEnv*, jclass, jlong h, jint mb) { otk_set_cache_limit(E(h), mb); }

JNIEXPORT void JNICALL OTK_FN(nativeNetworkChanged)(JNIEnv*, jclass, jlong h) { otk_network_changed(E(h)); }

JNIEXPORT jint JNICALL OTK_FN(nativeDownloadRegion)(JNIEnv*, jclass, jlong h, jint layer, jdouble w, jdouble s, jdouble e, jdouble n,
                                                    jint minZ, jint maxZ) {
    return otk_download_region(E(h), layer, w, s, e, n, minZ, maxZ);
}

JNIEXPORT jlongArray JNICALL OTK_FN(nativeDownloadStatus)(JNIEnv* env, jclass, jlong h, jint id) {
    otk_download_status st{};
    otk_get_download_status(E(h), id, &st);
    jlong values[6] = {st.total, st.done, st.failed, st.retrying, st.finished, st.cancelled};
    jlongArray out = env->NewLongArray(6);
    env->SetLongArrayRegion(out, 0, 6, values);
    return out;
}

JNIEXPORT void JNICALL OTK_FN(nativeCancelDownload)(JNIEnv*, jclass, jlong h, jint id) { otk_cancel_download(E(h), id); }

JNIEXPORT jlong JNICALL OTK_FN(nativeExportTiles)(JNIEnv* env, jclass, jlong h, jint layer, jdouble w, jdouble s, jdouble e, jdouble n,
                                                  jint minZ, jint maxZ, jstring path) {
    return otk_export_tiles(E(h), layer, w, s, e, n, minZ, maxZ, str(env, path).c_str());
}

JNIEXPORT jlong JNICALL OTK_FN(nativeImportTiles)(JNIEnv* env, jclass, jlong h, jint layer, jstring path) {
    return otk_import_tiles(E(h), layer, str(env, path).c_str());
}

JNIEXPORT jlong JNICALL OTK_FN(nativeAddSymbol)(JNIEnv* env, jclass, jlong h, jdouble lat, jdouble lon, jdouble alt, jstring code, jstring name) {
    return static_cast<jlong>(otk_add_symbol(E(h), lat, lon, alt, str(env, code).c_str(), str(env, name).c_str()));
}

JNIEXPORT jint JNICALL OTK_FN(nativeAddRangeRings)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble spacing, jint count, jint radials,
                                                   jint argb) {
    return otk_add_range_rings(E(h), lat, lon, spacing, count, radials, static_cast<uint32_t>(argb));
}

JNIEXPORT jint JNICALL OTK_FN(nativeAddBullseye)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble radius, jint rings, jdouble step,
                                                 jint reference, jint unit, jint argb) {
    return otk_add_bullseye(E(h), lat, lon, radius, rings, step, static_cast<otk_north_reference>(reference), static_cast<otk_angle_unit>(unit),
                            static_cast<uint32_t>(argb));
}

JNIEXPORT jint JNICALL OTK_FN(nativeAddRangeBearing)(JNIEnv*, jclass, jlong h, jdouble lat1, jdouble lon1, jdouble lat2, jdouble lon2,
                                                     jint reference, jint unit, jint distanceUnit, jint argb) {
    return otk_add_range_bearing(E(h), lat1, lon1, lat2, lon2, static_cast<otk_north_reference>(reference), static_cast<otk_angle_unit>(unit),
                                 static_cast<otk_distance_unit>(distanceUnit), static_cast<uint32_t>(argb));
}

JNIEXPORT jint JNICALL OTK_FN(nativeAddContoursInView)(JNIEnv*, jclass, jlong h, jdouble interval, jint majorEvery, jint argb) {
    return otk_add_contours_in_view(E(h), interval, majorEvery, static_cast<uint32_t>(argb));
}

JNIEXPORT void JNICALL OTK_FN(nativeSetTerrainSlope)(JNIEnv*, jclass, jlong h, jboolean enabled, jfloat opacity, jdouble maxSlope) {
    otk_set_terrain_slope(E(h), enabled ? 1 : 0, opacity, maxSlope);
}

JNIEXPORT void JNICALL OTK_FN(nativeSetGridType)(JNIEnv*, jclass, jlong h, jint type) { otk_set_grid_type(E(h), static_cast<otk_grid_type>(type)); }

JNIEXPORT jstring JNICALL OTK_FN(nativeFormatCoordinate)(JNIEnv* env, jclass, jdouble lat, jdouble lon, jint format, jint precision) {
    char buf[128];
    otk_format_coordinate(lat, lon, static_cast<otk_coordinate_format>(format), precision, buf, sizeof(buf));
    return env->NewStringUTF(buf);
}

JNIEXPORT jdoubleArray JNICALL OTK_FN(nativeParseCoordinate)(JNIEnv* env, jclass, jstring text) {
    double lat = 0, lon = 0;
    if (!otk_parse_coordinate(str(env, text).c_str(), &lat, &lon)) return nullptr;
    jdouble values[2] = {lat, lon};
    jdoubleArray out = env->NewDoubleArray(2);
    env->SetDoubleArrayRegion(out, 0, 2, values);
    return out;
}

JNIEXPORT jdoubleArray JNICALL OTK_FN(nativeSunMoon)(JNIEnv* env, jclass, jdouble t, jdouble lat, jdouble lon) {
    otk_sun_moon r{};
    otk_sun_moon_at(t, lat, lon, &r);
    const jdouble v[] = {r.sun_azimuth, r.sun_elevation, r.moon_azimuth, r.moon_elevation, r.moon_illumination, r.moon_phase_angle,
                         static_cast<jdouble>(r.moon_waxing), r.sun_lux, r.moon_lux, r.total_lux};
    jdoubleArray out = env->NewDoubleArray(10);
    env->SetDoubleArrayRegion(out, 0, 10, v);
    return out;
}

JNIEXPORT jdoubleArray JNICALL OTK_FN(nativeSunMoonTimes)(JNIEnv* env, jclass, jdouble t, jdouble lat, jdouble lon, jdouble hours) {
    otk_twilight r{};
    otk_sun_moon_times(t, lat, lon, hours, &r);
    const jdouble v[] = {r.astronomical_dawn, r.nautical_dawn, r.civil_dawn, r.sunrise, r.sunset,
                         r.civil_dusk, r.nautical_dusk, r.astronomical_dusk, r.moonrise, r.moonset};
    jdoubleArray out = env->NewDoubleArray(10);
    env->SetDoubleArrayRegion(out, 0, 10, v);
    return out;
}

JNIEXPORT jlong JNICALL OTK_FN(nativeAddTilesetEx)(JNIEnv* env, jclass, jlong h, jstring path, jdouble sse, jstring headers, jint lighting,
                                                    jboolean cacheOffline) {
    return static_cast<jlong>(otk_add_tileset_ex(E(h), str(env, path).c_str(), sse, str(env, headers).c_str(), lighting, cacheOffline ? 1 : 0));
}

JNIEXPORT jstring JNICALL OTK_FN(nativeCredits)(JNIEnv* env, jclass, jlong h) {
    int n = otk_credits(E(h), nullptr, 0);
    std::string text(static_cast<size_t>(n) + 1, '\0');
    otk_credits(E(h), text.data(), n + 1);
    text.resize(static_cast<size_t>(n));
    return env->NewStringUTF(text.c_str());
}

JNIEXPORT void JNICALL OTK_FN(nativeSetSunLighting)(JNIEnv*, jclass, jlong h, jboolean enabled, jdouble t) {
    otk_set_sun_lighting(E(h), enabled ? 1 : 0, t);
}

JNIEXPORT void JNICALL OTK_FN(nativeSetXrAnchor)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble alt, jdouble heading, jdouble scale) {
    otk_set_xr_anchor(E(h), lat, lon, alt, heading, scale);
}

JNIEXPORT void JNICALL OTK_FN(nativeFrameXr)(JNIEnv* env, jclass, jlong h, jdoubleArray poses, jintArray rects, jintArray framebuffers, jint tw,
                                             jint th, jfloat dpi) {
    jsize count = env->GetArrayLength(framebuffers);
    if (count <= 0 || env->GetArrayLength(poses) < count * 11 || env->GetArrayLength(rects) < count * 4) return;
    std::vector<jdouble> p(static_cast<size_t>(count) * 11);
    std::vector<jint> r(static_cast<size_t>(count) * 4), fb(static_cast<size_t>(count));
    env->GetDoubleArrayRegion(poses, 0, count * 11, p.data());
    env->GetIntArrayRegion(rects, 0, count * 4, r.data());
    env->GetIntArrayRegion(framebuffers, 0, count, fb.data());
    std::vector<otk_xr_view> views(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        otk_xr_view& v = views[static_cast<size_t>(i)];
        const jdouble* q = &p[static_cast<size_t>(i) * 11];
        for (int k = 0; k < 3; ++k) v.position[k] = q[k];
        for (int k = 0; k < 4; ++k) v.orientation[k] = q[3 + k];
        v.angle_left = q[7];
        v.angle_right = q[8];
        v.angle_up = q[9];
        v.angle_down = q[10];
        v.x = r[static_cast<size_t>(i) * 4];
        v.y = r[static_cast<size_t>(i) * 4 + 1];
        v.width = r[static_cast<size_t>(i) * 4 + 2];
        v.height = r[static_cast<size_t>(i) * 4 + 3];
        v.gl_framebuffer = static_cast<uint32_t>(fb[static_cast<size_t>(i)]);
    }
    otk_frame_xr(E(h), views.data(), count, tw, th, dpi);
}

JNIEXPORT jdouble JNICALL OTK_FN(nativeMagneticDeclination)(JNIEnv*, jclass, jdouble lat, jdouble lon, jdouble alt, jdouble year) {
    return otk_magnetic_declination(lat, lon, alt, year);
}

JNIEXPORT void JNICALL OTK_FN(nativeClearLayers)(JNIEnv*, jclass, jlong h) { otk_clear_layers(E(h)); }

JNIEXPORT jint JNICALL OTK_FN(nativeAddLayer)(JNIEnv* env, jclass, jlong h, jint type, jstring name, jstring url, jint minZ,
                                               jint maxZ, jfloat opacity) {
    return otk_add_layer(E(h), static_cast<otk_source>(type), str(env, name).c_str(), str(env, url).c_str(), minZ, maxZ, opacity);
}

JNIEXPORT jint JNICALL OTK_FN(nativeLoadMobac)(JNIEnv* env, jclass, jlong h, jstring xml) {
    return otk_load_mobac(E(h), str(env, xml).c_str());
}

JNIEXPORT void JNICALL OTK_FN(nativeUseDefaultLayers)(JNIEnv*, jclass, jlong h) { otk_use_default_layers(E(h)); }

JNIEXPORT void JNICALL OTK_FN(nativeSetTerrain)(JNIEnv*, jclass, jlong h, jboolean v) { otk_set_terrain(E(h), v ? 1 : 0); }

JNIEXPORT void JNICALL OTK_FN(nativeAddElevationDirectory)(JNIEnv* env, jclass, jlong h, jstring dir) {
    otk_add_elevation_directory(E(h), str(env, dir).c_str());
}

JNIEXPORT jdouble JNICALL OTK_FN(nativeElevation)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon) {
    return otk_elevation(E(h), lat, lon);
}

JNIEXPORT void JNICALL OTK_FN(nativeSetView)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble gsd, jdouble az,
                                             jdouble tilt) {
    otk_view v{lat, lon, gsd, az, tilt};
    otk_set_view(E(h), &v);
}

JNIEXPORT void JNICALL OTK_FN(nativeFlyTo)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble gsd, jdouble az,
                                           jdouble tilt, jdouble seconds) {
    otk_view v{lat, lon, gsd, az, tilt};
    otk_fly_to(E(h), &v, seconds);
}

JNIEXPORT void JNICALL OTK_FN(nativeGetView)(JNIEnv* env, jclass, jlong h, jdoubleArray out) {
    otk_view v{};
    otk_get_view(E(h), &v);
    jdouble values[5] = {v.lat, v.lon, v.gsd, v.azimuth, v.tilt};
    env->SetDoubleArrayRegion(out, 0, 5, values);
}

JNIEXPORT jboolean JNICALL OTK_FN(nativePick)(JNIEnv* env, jclass, jlong h, jdouble x, jdouble y, jdoubleArray out) {
    double lat = 0, lon = 0;
    if (!otk_pick(E(h), x, y, &lat, &lon)) return JNI_FALSE;
    jdouble values[2] = {lat, lon};
    env->SetDoubleArrayRegion(out, 0, 2, values);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL OTK_FN(nativePan)(JNIEnv*, jclass, jlong h, jdouble x0, jdouble y0, jdouble x1, jdouble y1) {
    otk_pan(E(h), x0, y0, x1, y1);
}

JNIEXPORT void JNICALL OTK_FN(nativeZoomAt)(JNIEnv*, jclass, jlong h, jdouble x, jdouble y, jdouble f) { otk_zoom_at(E(h), x, y, f); }

JNIEXPORT void JNICALL OTK_FN(nativeRotate)(JNIEnv*, jclass, jlong h, jdouble d) { otk_rotate(E(h), d); }

JNIEXPORT void JNICALL OTK_FN(nativeTilt)(JNIEnv*, jclass, jlong h, jdouble d) { otk_tilt(E(h), d); }

JNIEXPORT void JNICALL OTK_FN(nativeSetFirstPerson)(JNIEnv*, jclass, jlong h, jboolean v, jdouble height) {
    otk_set_first_person(E(h), v ? 1 : 0, height);
}

JNIEXPORT void JNICALL OTK_FN(nativeSet3D)(JNIEnv*, jclass, jlong h, jboolean v) { otk_set_3d(E(h), v ? 1 : 0); }

JNIEXPORT jint JNICALL OTK_FN(nativeLoadFeatures)(JNIEnv* env, jclass, jlong h, jstring path) {
    return otk_load_features(E(h), str(env, path).c_str());
}

JNIEXPORT jlong JNICALL OTK_FN(nativeAddPoint)(JNIEnv* env, jclass, jlong h, jdouble lat, jdouble lon, jdouble alt, jstring name,
                                               jint argb) {
    return static_cast<jlong>(otk_add_point(E(h), lat, lon, alt, str(env, name).c_str(), static_cast<uint32_t>(argb)));
}

JNIEXPORT jboolean JNICALL OTK_FN(nativeRemoveFeature)(JNIEnv*, jclass, jlong h, jlong id) {
    return otk_remove_feature(E(h), static_cast<uint64_t>(id)) != 0;
}

JNIEXPORT jlong JNICALL OTK_FN(nativeHitTest)(JNIEnv*, jclass, jlong h, jdouble x, jdouble y, jdouble r) {
    return static_cast<jlong>(otk_hit_test(E(h), x, y, r));
}

JNIEXPORT jlong JNICALL OTK_FN(nativeAddTileset)(JNIEnv* env, jclass, jlong h, jstring path, jdouble sse) {
    return static_cast<jlong>(otk_add_tileset(E(h), str(env, path).c_str(), sse));
}

JNIEXPORT void JNICALL OTK_FN(nativeSetGrid)(JNIEnv*, jclass, jlong h, jboolean v) { otk_set_grid(E(h), v ? 1 : 0); }

JNIEXPORT void JNICALL OTK_FN(nativeSetLabels)(JNIEnv*, jclass, jlong h, jboolean v) { otk_set_labels(E(h), v ? 1 : 0); }

JNIEXPORT void JNICALL OTK_FN(nativeSetContours)(JNIEnv*, jclass, jlong h, jboolean v, jdouble interval, jint unit) {
    otk_set_contours(E(h), v ? 1 : 0, interval, static_cast<otk_distance_unit>(unit));
}

JNIEXPORT void JNICALL OTK_FN(nativeSetLabelsSource)(JNIEnv* env, jclass, jlong h, jstring source) {
    otk_set_labels_source(E(h), str(env, source).c_str());
}

JNIEXPORT jstring JNICALL OTK_FN(nativeToMgrs)(JNIEnv* env, jclass, jdouble lat, jdouble lon, jint digits) {
    char buf[64];
    otk_to_mgrs(lat, lon, digits, buf, sizeof buf);
    return env->NewStringUTF(buf);
}

JNIEXPORT void JNICALL OTK_FN(nativePointerDown)(JNIEnv*, jclass, jlong h, jint id, jdouble x, jdouble y, jint type, jint button, jint mods) {
    otk_pointer_down(E(h), id, x, y, type, button, mods);
}

JNIEXPORT void JNICALL OTK_FN(nativePointerMove)(JNIEnv*, jclass, jlong h, jint id, jdouble x, jdouble y, jint mods) {
    otk_pointer_move(E(h), id, x, y, mods);
}

JNIEXPORT void JNICALL OTK_FN(nativePointerUp)(JNIEnv*, jclass, jlong h, jint id, jdouble x, jdouble y) { otk_pointer_up(E(h), id, x, y); }

JNIEXPORT void JNICALL OTK_FN(nativePointerCancelAll)(JNIEnv*, jclass, jlong h) { otk_pointer_cancel_all(E(h)); }

JNIEXPORT void JNICALL OTK_FN(nativeWheel)(JNIEnv*, jclass, jlong h, jdouble x, jdouble y, jdouble dx, jdouble dy, jboolean precise, jint mods) {
    otk_wheel(E(h), x, y, dx, dy, precise ? 1 : 0, mods);
}

JNIEXPORT void JNICALL OTK_FN(nativeKey)(JNIEnv*, jclass, jlong h, jint key, jboolean down, jint mods) { otk_key(E(h), key, down ? 1 : 0, mods); }

JNIEXPORT void JNICALL OTK_FN(nativeSetHeadingLockedPan)(JNIEnv*, jclass, jlong h, jboolean v) { otk_set_heading_locked_pan(E(h), v ? 1 : 0); }

JNIEXPORT jlong JNICALL OTK_FN(nativeSetTapListener)(JNIEnv* env, jclass, jlong h, jobject listener, jlong previous) {
    if (previous) {
        auto* old = reinterpret_cast<Listener*>(previous);
        env->DeleteGlobalRef(old->target);
        delete old;
    }
    if (!listener) {
        otk_set_tap_callback(E(h), nullptr, nullptr);
        otk_set_long_press_callback(E(h), nullptr, nullptr);
        return 0;
    }
    auto* l = new Listener();
    l->target = env->NewGlobalRef(listener);
    jclass cls = env->GetObjectClass(listener);
    l->tap = env->GetMethodID(cls, "onTap", "(DDII)V");
    l->press = env->GetMethodID(cls, "onLongPress", "(DDI)V");
    otk_set_tap_callback(E(h), onTap, l);
    otk_set_long_press_callback(E(h), onPress, l);
    return reinterpret_cast<jlong>(l);
}

}
