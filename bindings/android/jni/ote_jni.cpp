#include "ote/ote.h"

#include <jni.h>

#include <string>

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
