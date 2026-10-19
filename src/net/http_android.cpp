#include "net/http.hpp"

#include <jni.h>

#include <atomic>

namespace ote::net {

namespace {

std::atomic<JavaVM*> javaVm{nullptr};

struct Env {
    JNIEnv* env = nullptr;
    bool attached = false;
    Env() {
        JavaVM* vm = javaVm.load();
        if (!vm) return;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
            else env = nullptr;
        }
    }
    ~Env() {
        if (attached) javaVm.load()->DetachCurrentThread();
    }
};

bool check(JNIEnv* e) {
    if (e->ExceptionCheck()) {
        e->ExceptionClear();
        return false;
    }
    return true;
}

}

void setJavaVm(void* vm) { javaVm = static_cast<JavaVM*>(vm); }

Response get(const std::string& url, const std::string& userAgent) {
    Response res;
    thread_local Env holder;
    JNIEnv* e = holder.env;
    if (!e) {
        res.error = "no JavaVM";
        return res;
    }
    if (e->PushLocalFrame(32) != 0) return res;
    jclass urlClass = e->FindClass("java/net/URL");
    jclass connClass = e->FindClass("java/net/HttpURLConnection");
    jclass streamClass = e->FindClass("java/io/InputStream");
    jstring jurl = e->NewStringUTF(url.c_str());
    jobject u = e->NewObject(urlClass, e->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V"), jurl);
    jobject conn = check(e) && u ? e->CallObjectMethod(u, e->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;")) : nullptr;
    if (!check(e) || !conn) {
        res.error = "open failed";
        e->PopLocalFrame(nullptr);
        return res;
    }
    e->CallVoidMethod(conn, e->GetMethodID(connClass, "setConnectTimeout", "(I)V"), 10000);
    e->CallVoidMethod(conn, e->GetMethodID(connClass, "setReadTimeout", "(I)V"), 20000);
    e->CallVoidMethod(conn, e->GetMethodID(connClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"),
                      e->NewStringUTF("User-Agent"), e->NewStringUTF(userAgent.c_str()));
    jint code = e->CallIntMethod(conn, e->GetMethodID(connClass, "getResponseCode", "()I"));
    if (!check(e)) {
        res.error = "request failed";
        e->PopLocalFrame(nullptr);
        return res;
    }
    res.status = code;
    if (code >= 200 && code < 300) {
        jobject in = e->CallObjectMethod(conn, e->GetMethodID(connClass, "getInputStream", "()Ljava/io/InputStream;"));
        if (check(e) && in) {
            jbyteArray buf = e->NewByteArray(65536);
            jmethodID read = e->GetMethodID(streamClass, "read", "([B)I");
            for (;;) {
                jint n = e->CallIntMethod(in, read, buf);
                if (!check(e) || n < 0) break;
                size_t off = res.body.size();
                res.body.resize(off + static_cast<size_t>(n));
                e->GetByteArrayRegion(buf, 0, n, reinterpret_cast<jbyte*>(res.body.data() + off));
            }
            e->CallVoidMethod(in, e->GetMethodID(streamClass, "close", "()V"));
            check(e);
        }
    }
    e->CallVoidMethod(conn, e->GetMethodID(connClass, "disconnect", "()V"));
    check(e);
    e->PopLocalFrame(nullptr);
    return res;
}

}
