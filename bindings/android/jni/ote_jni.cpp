#include "ote/ote.h"

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

ote_engine* E(jlong h) { return reinterpret_cast<ote_engine*>(h); }

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
    ote_set_android_vm(vm);
    return JNI_VERSION_1_6;
}

#define OTE_FN(name) Java_com_opentakengine_OpenTakEngine_##name

JNIEXPORT jlong JNICALL OTE_FN(nativeCreate)(JNIEnv*, jclass, jint samples) {
    return reinterpret_cast<jlong>(ote_create_gl(1, samples));
}

JNIEXPORT void JNICALL OTE_FN(nativeDestroy)(JNIEnv*, jclass, jlong h) { ote_destroy(E(h)); }

JNIEXPORT void JNICALL OTE_FN(nativeFrame)(JNIEnv*, jclass, jlong h, jint w, jint hgt, jfloat dpi) {
    ote_frame_gl(E(h), w, hgt, 0, dpi);
}

JNIEXPORT jboolean JNICALL OTE_FN(nativeNeedsRedraw)(JNIEnv*, jclass, jlong h) { return ote_needs_redraw(E(h)) != 0; }

JNIEXPORT void JNICALL OTE_FN(nativeSetCacheDirectory)(JNIEnv* env, jclass, jlong h, jstring dir) {
    ote_set_cache_directory(E(h), str(env, dir).c_str());
}

JNIEXPORT void JNICALL OTE_FN(nativeSetOffline)(JNIEnv*, jclass, jlong h, jboolean v) { ote_set_offline(E(h), v ? 1 : 0); }

JNIEXPORT void JNICALL OTE_FN(nativeSetMemoryBudget)(JNIEnv*, jclass, jlong h, jint mb) { ote_set_memory_budget(E(h), mb); }

JNIEXPORT void JNICALL OTE_FN(nativeSetCacheLimit)(JNIEnv*, jclass, jlong h, jint mb) { ote_set_cache_limit(E(h), mb); }

JNIEXPORT void JNICALL OTE_FN(nativeNetworkChanged)(JNIEnv*, jclass, jlong h) { ote_network_changed(E(h)); }

JNIEXPORT jint JNICALL OTE_FN(nativeDownloadRegion)(JNIEnv*, jclass, jlong h, jint layer, jdouble w, jdouble s, jdouble e, jdouble n,
                                                    jint minZ, jint maxZ) {
    return ote_download_region(E(h), layer, w, s, e, n, minZ, maxZ);
}

JNIEXPORT jlongArray JNICALL OTE_FN(nativeDownloadStatus)(JNIEnv* env, jclass, jlong h, jint id) {
    ote_download_status st{};
    ote_get_download_status(E(h), id, &st);
    jlong values[6] = {st.total, st.done, st.failed, st.retrying, st.finished, st.cancelled};
    jlongArray out = env->NewLongArray(6);
    env->SetLongArrayRegion(out, 0, 6, values);
    return out;
}

JNIEXPORT void JNICALL OTE_FN(nativeCancelDownload)(JNIEnv*, jclass, jlong h, jint id) { ote_cancel_download(E(h), id); }

JNIEXPORT jlong JNICALL OTE_FN(nativeExportTiles)(JNIEnv* env, jclass, jlong h, jint layer, jdouble w, jdouble s, jdouble e, jdouble n,
                                                  jint minZ, jint maxZ, jstring path) {
    return ote_export_tiles(E(h), layer, w, s, e, n, minZ, maxZ, str(env, path).c_str());
}

JNIEXPORT jlong JNICALL OTE_FN(nativeImportTiles)(JNIEnv* env, jclass, jlong h, jint layer, jstring path) {
    return ote_import_tiles(E(h), layer, str(env, path).c_str());
}

JNIEXPORT jlong JNICALL OTE_FN(nativeAddSymbol)(JNIEnv* env, jclass, jlong h, jdouble lat, jdouble lon, jdouble alt, jstring code, jstring name) {
    return static_cast<jlong>(ote_add_symbol(E(h), lat, lon, alt, str(env, code).c_str(), str(env, name).c_str()));
}

JNIEXPORT jint JNICALL OTE_FN(nativeAddRangeRings)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble spacing, jint count, jint radials,
                                                   jint argb) {
    return ote_add_range_rings(E(h), lat, lon, spacing, count, radials, static_cast<uint32_t>(argb));
}

JNIEXPORT jint JNICALL OTE_FN(nativeAddBullseye)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble radius, jint rings, jdouble step,
                                                 jint reference, jint unit, jint argb) {
    return ote_add_bullseye(E(h), lat, lon, radius, rings, step, static_cast<ote_north_reference>(reference), static_cast<ote_angle_unit>(unit),
                            static_cast<uint32_t>(argb));
}

JNIEXPORT jint JNICALL OTE_FN(nativeAddRangeBearing)(JNIEnv*, jclass, jlong h, jdouble lat1, jdouble lon1, jdouble lat2, jdouble lon2,
                                                     jint reference, jint unit, jint distanceUnit, jint argb) {
    return ote_add_range_bearing(E(h), lat1, lon1, lat2, lon2, static_cast<ote_north_reference>(reference), static_cast<ote_angle_unit>(unit),
                                 static_cast<ote_distance_unit>(distanceUnit), static_cast<uint32_t>(argb));
}

JNIEXPORT jint JNICALL OTE_FN(nativeAddContoursInView)(JNIEnv*, jclass, jlong h, jdouble interval, jint majorEvery, jint argb) {
    return ote_add_contours_in_view(E(h), interval, majorEvery, static_cast<uint32_t>(argb));
}

JNIEXPORT void JNICALL OTE_FN(nativeSetTerrainSlope)(JNIEnv*, jclass, jlong h, jboolean enabled, jfloat opacity, jdouble maxSlope) {
    ote_set_terrain_slope(E(h), enabled ? 1 : 0, opacity, maxSlope);
}

JNIEXPORT void JNICALL OTE_FN(nativeSetGridType)(JNIEnv*, jclass, jlong h, jint type) { ote_set_grid_type(E(h), static_cast<ote_grid_type>(type)); }

JNIEXPORT jstring JNICALL OTE_FN(nativeFormatCoordinate)(JNIEnv* env, jclass, jdouble lat, jdouble lon, jint format, jint precision) {
    char buf[128];
    ote_format_coordinate(lat, lon, static_cast<ote_coordinate_format>(format), precision, buf, sizeof(buf));
    return env->NewStringUTF(buf);
}

JNIEXPORT jdoubleArray JNICALL OTE_FN(nativeParseCoordinate)(JNIEnv* env, jclass, jstring text) {
    double lat = 0, lon = 0;
    if (!ote_parse_coordinate(str(env, text).c_str(), &lat, &lon)) return nullptr;
    jdouble values[2] = {lat, lon};
    jdoubleArray out = env->NewDoubleArray(2);
    env->SetDoubleArrayRegion(out, 0, 2, values);
    return out;
}

JNIEXPORT jdoubleArray JNICALL OTE_FN(nativeSunMoon)(JNIEnv* env, jclass, jdouble t, jdouble lat, jdouble lon) {
    ote_sun_moon r{};
    ote_sun_moon_at(t, lat, lon, &r);
    const jdouble v[] = {r.sun_azimuth, r.sun_elevation, r.moon_azimuth, r.moon_elevation, r.moon_illumination, r.moon_phase_angle,
                         static_cast<jdouble>(r.moon_waxing), r.sun_lux, r.moon_lux, r.total_lux};
    jdoubleArray out = env->NewDoubleArray(10);
    env->SetDoubleArrayRegion(out, 0, 10, v);
    return out;
}

JNIEXPORT jdoubleArray JNICALL OTE_FN(nativeSunMoonTimes)(JNIEnv* env, jclass, jdouble t, jdouble lat, jdouble lon, jdouble hours) {
    ote_twilight r{};
    ote_sun_moon_times(t, lat, lon, hours, &r);
    const jdouble v[] = {r.astronomical_dawn, r.nautical_dawn, r.civil_dawn, r.sunrise, r.sunset,
                         r.civil_dusk, r.nautical_dusk, r.astronomical_dusk, r.moonrise, r.moonset};
    jdoubleArray out = env->NewDoubleArray(10);
    env->SetDoubleArrayRegion(out, 0, 10, v);
    return out;
}

JNIEXPORT jlong JNICALL OTE_FN(nativeAddTilesetEx)(JNIEnv* env, jclass, jlong h, jstring path, jdouble sse, jstring headers, jint lighting,
                                                    jboolean cacheOffline) {
    return static_cast<jlong>(ote_add_tileset_ex(E(h), str(env, path).c_str(), sse, str(env, headers).c_str(), lighting, cacheOffline ? 1 : 0));
}

JNIEXPORT jstring JNICALL OTE_FN(nativeCredits)(JNIEnv* env, jclass, jlong h) {
    int n = ote_credits(E(h), nullptr, 0);
    std::string text(static_cast<size_t>(n) + 1, '\0');
    ote_credits(E(h), text.data(), n + 1);
    text.resize(static_cast<size_t>(n));
    return env->NewStringUTF(text.c_str());
}

JNIEXPORT void JNICALL OTE_FN(nativeSetSunLighting)(JNIEnv*, jclass, jlong h, jboolean enabled, jdouble t) {
    ote_set_sun_lighting(E(h), enabled ? 1 : 0, t);
}

JNIEXPORT void JNICALL OTE_FN(nativeSetXrAnchor)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble alt, jdouble heading, jdouble scale) {
    ote_set_xr_anchor(E(h), lat, lon, alt, heading, scale);
}

JNIEXPORT void JNICALL OTE_FN(nativeFrameXr)(JNIEnv* env, jclass, jlong h, jdoubleArray poses, jintArray rects, jintArray framebuffers, jint tw,
                                             jint th, jfloat dpi) {
    jsize count = env->GetArrayLength(framebuffers);
    if (count <= 0 || env->GetArrayLength(poses) < count * 11 || env->GetArrayLength(rects) < count * 4) return;
    std::vector<jdouble> p(static_cast<size_t>(count) * 11);
    std::vector<jint> r(static_cast<size_t>(count) * 4), fb(static_cast<size_t>(count));
    env->GetDoubleArrayRegion(poses, 0, count * 11, p.data());
    env->GetIntArrayRegion(rects, 0, count * 4, r.data());
    env->GetIntArrayRegion(framebuffers, 0, count, fb.data());
    std::vector<ote_xr_view> views(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        ote_xr_view& v = views[static_cast<size_t>(i)];
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
    ote_frame_xr(E(h), views.data(), count, tw, th, dpi);
}

JNIEXPORT jdouble JNICALL OTE_FN(nativeMagneticDeclination)(JNIEnv*, jclass, jdouble lat, jdouble lon, jdouble alt, jdouble year) {
    return ote_magnetic_declination(lat, lon, alt, year);
}

JNIEXPORT void JNICALL OTE_FN(nativeClearLayers)(JNIEnv*, jclass, jlong h) { ote_clear_layers(E(h)); }

JNIEXPORT jint JNICALL OTE_FN(nativeAddLayer)(JNIEnv* env, jclass, jlong h, jint type, jstring name, jstring url, jint minZ,
                                               jint maxZ, jfloat opacity) {
    return ote_add_layer(E(h), static_cast<ote_source>(type), str(env, name).c_str(), str(env, url).c_str(), minZ, maxZ, opacity);
}

JNIEXPORT jint JNICALL OTE_FN(nativeLoadMobac)(JNIEnv* env, jclass, jlong h, jstring xml) {
    return ote_load_mobac(E(h), str(env, xml).c_str());
}

JNIEXPORT void JNICALL OTE_FN(nativeUseDefaultLayers)(JNIEnv*, jclass, jlong h) { ote_use_default_layers(E(h)); }

JNIEXPORT void JNICALL OTE_FN(nativeSetTerrain)(JNIEnv*, jclass, jlong h, jboolean v) { ote_set_terrain(E(h), v ? 1 : 0); }

JNIEXPORT void JNICALL OTE_FN(nativeAddElevationDirectory)(JNIEnv* env, jclass, jlong h, jstring dir) {
    ote_add_elevation_directory(E(h), str(env, dir).c_str());
}

JNIEXPORT jdouble JNICALL OTE_FN(nativeElevation)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon) {
    return ote_elevation(E(h), lat, lon);
}

JNIEXPORT void JNICALL OTE_FN(nativeSetView)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble gsd, jdouble az,
                                             jdouble tilt) {
    ote_view v{lat, lon, gsd, az, tilt};
    ote_set_view(E(h), &v);
}

JNIEXPORT void JNICALL OTE_FN(nativeFlyTo)(JNIEnv*, jclass, jlong h, jdouble lat, jdouble lon, jdouble gsd, jdouble az,
                                           jdouble tilt, jdouble seconds) {
    ote_view v{lat, lon, gsd, az, tilt};
    ote_fly_to(E(h), &v, seconds);
}

JNIEXPORT void JNICALL OTE_FN(nativeGetView)(JNIEnv* env, jclass, jlong h, jdoubleArray out) {
    ote_view v{};
    ote_get_view(E(h), &v);
    jdouble values[5] = {v.lat, v.lon, v.gsd, v.azimuth, v.tilt};
    env->SetDoubleArrayRegion(out, 0, 5, values);
}

JNIEXPORT jboolean JNICALL OTE_FN(nativePick)(JNIEnv* env, jclass, jlong h, jdouble x, jdouble y, jdoubleArray out) {
    double lat = 0, lon = 0;
    if (!ote_pick(E(h), x, y, &lat, &lon)) return JNI_FALSE;
    jdouble values[2] = {lat, lon};
    env->SetDoubleArrayRegion(out, 0, 2, values);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL OTE_FN(nativePan)(JNIEnv*, jclass, jlong h, jdouble x0, jdouble y0, jdouble x1, jdouble y1) {
    ote_pan(E(h), x0, y0, x1, y1);
}

JNIEXPORT void JNICALL OTE_FN(nativeZoomAt)(JNIEnv*, jclass, jlong h, jdouble x, jdouble y, jdouble f) { ote_zoom_at(E(h), x, y, f); }

JNIEXPORT void JNICALL OTE_FN(nativeRotate)(JNIEnv*, jclass, jlong h, jdouble d) { ote_rotate(E(h), d); }

JNIEXPORT void JNICALL OTE_FN(nativeTilt)(JNIEnv*, jclass, jlong h, jdouble d) { ote_tilt(E(h), d); }

JNIEXPORT void JNICALL OTE_FN(nativeSetFirstPerson)(JNIEnv*, jclass, jlong h, jboolean v, jdouble height) {
    ote_set_first_person(E(h), v ? 1 : 0, height);
}

JNIEXPORT void JNICALL OTE_FN(nativeSet3D)(JNIEnv*, jclass, jlong h, jboolean v) { ote_set_3d(E(h), v ? 1 : 0); }

JNIEXPORT jint JNICALL OTE_FN(nativeLoadFeatures)(JNIEnv* env, jclass, jlong h, jstring path) {
    return ote_load_features(E(h), str(env, path).c_str());
}

JNIEXPORT jlong JNICALL OTE_FN(nativeAddPoint)(JNIEnv* env, jclass, jlong h, jdouble lat, jdouble lon, jdouble alt, jstring name,
                                               jint argb) {
    return static_cast<jlong>(ote_add_point(E(h), lat, lon, alt, str(env, name).c_str(), static_cast<uint32_t>(argb)));
}

JNIEXPORT jboolean JNICALL OTE_FN(nativeRemoveFeature)(JNIEnv*, jclass, jlong h, jlong id) {
    return ote_remove_feature(E(h), static_cast<uint64_t>(id)) != 0;
}

JNIEXPORT jlong JNICALL OTE_FN(nativeHitTest)(JNIEnv*, jclass, jlong h, jdouble x, jdouble y, jdouble r) {
    return static_cast<jlong>(ote_hit_test(E(h), x, y, r));
}

JNIEXPORT jlong JNICALL OTE_FN(nativeAddTileset)(JNIEnv* env, jclass, jlong h, jstring path, jdouble sse) {
    return static_cast<jlong>(ote_add_tileset(E(h), str(env, path).c_str(), sse));
}

JNIEXPORT void JNICALL OTE_FN(nativeSetGrid)(JNIEnv*, jclass, jlong h, jboolean v) { ote_set_grid(E(h), v ? 1 : 0); }

JNIEXPORT jstring JNICALL OTE_FN(nativeToMgrs)(JNIEnv* env, jclass, jdouble lat, jdouble lon, jint digits) {
    char buf[64];
    ote_to_mgrs(lat, lon, digits, buf, sizeof buf);
    return env->NewStringUTF(buf);
}

JNIEXPORT void JNICALL OTE_FN(nativePointerDown)(JNIEnv*, jclass, jlong h, jint id, jdouble x, jdouble y, jint type, jint button, jint mods) {
    ote_pointer_down(E(h), id, x, y, type, button, mods);
}

JNIEXPORT void JNICALL OTE_FN(nativePointerMove)(JNIEnv*, jclass, jlong h, jint id, jdouble x, jdouble y, jint mods) {
    ote_pointer_move(E(h), id, x, y, mods);
}

JNIEXPORT void JNICALL OTE_FN(nativePointerUp)(JNIEnv*, jclass, jlong h, jint id, jdouble x, jdouble y) { ote_pointer_up(E(h), id, x, y); }

JNIEXPORT void JNICALL OTE_FN(nativePointerCancelAll)(JNIEnv*, jclass, jlong h) { ote_pointer_cancel_all(E(h)); }

JNIEXPORT void JNICALL OTE_FN(nativeWheel)(JNIEnv*, jclass, jlong h, jdouble x, jdouble y, jdouble dx, jdouble dy, jboolean precise, jint mods) {
    ote_wheel(E(h), x, y, dx, dy, precise ? 1 : 0, mods);
}

JNIEXPORT void JNICALL OTE_FN(nativeKey)(JNIEnv*, jclass, jlong h, jint key, jboolean down, jint mods) { ote_key(E(h), key, down ? 1 : 0, mods); }

JNIEXPORT void JNICALL OTE_FN(nativeSetHeadingLockedPan)(JNIEnv*, jclass, jlong h, jboolean v) { ote_set_heading_locked_pan(E(h), v ? 1 : 0); }

JNIEXPORT jlong JNICALL OTE_FN(nativeSetTapListener)(JNIEnv* env, jclass, jlong h, jobject listener, jlong previous) {
    if (previous) {
        auto* old = reinterpret_cast<Listener*>(previous);
        env->DeleteGlobalRef(old->target);
        delete old;
    }
    if (!listener) {
        ote_set_tap_callback(E(h), nullptr, nullptr);
        ote_set_long_press_callback(E(h), nullptr, nullptr);
        return 0;
    }
    auto* l = new Listener();
    l->target = env->NewGlobalRef(listener);
    jclass cls = env->GetObjectClass(listener);
    l->tap = env->GetMethodID(cls, "onTap", "(DDII)V");
    l->press = env->GetMethodID(cls, "onLongPress", "(DDI)V");
    ote_set_tap_callback(E(h), onTap, l);
    ote_set_long_press_callback(E(h), onPress, l);
    return reinterpret_cast<jlong>(l);
}

}
