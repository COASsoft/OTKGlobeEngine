# Embedding OpenTAKEngine

The engine never owns a window. A host app creates a GPU surface with its own toolkit, creates the engine once for that device, and calls a frame function whenever the surface needs drawing. `ote_needs_redraw` tells the host when tiles are still arriving, so a host can render on demand instead of continuously.

Packages:

- Android: `python scripts/android_aar.py` builds `build/opentakengine-<version>.aar` (arm64-v8a, armeabi-v7a, x86_64) containing `com.opentakengine.OpenTakView`, a ready-made `GLSurfaceView` with ATAK-style gestures, and `OpenTakEngine` with the full native API.
- iOS, iPadOS, macOS: `bash scripts/apple_xcframework.sh` (on a Mac; CI publishes it as an artifact) builds `OpenTAKEngine.xcframework` with the `OpenTAKEngineC` module. Add `bindings/apple/OpenTAKView.swift` for a ready-made `MTKView` with gestures.
- .NET: `bindings/dotnet/OpenTakEngine.csproj` wraps the C ABI (`Engine`, `Native`); ship `opentakengine_c.dll`/`.so`/`.dylib` next to the app.

Two APIs are available:

- C++: `include/ote/engine.hpp` (`ote::Engine`), linked against the `opentakengine` static library. The host calls `sg_setup` itself with `ote::configureGfx(desc)` applied, as `apps/viewer` does.
- C ABI: `include/ote/ote.h`, in the static library and in the `opentakengine_c` shared library. It sets up the GPU layer from native handles and is the entry point for Kotlin/JNI, Swift, C# and other languages.

The graphics backend is chosen at build time: Direct3D 11 on Windows, Metal on macOS and iOS, OpenGL 4.1 on Linux, OpenGL ES 3 on Android.

## Kernel only (no GPU)

Servers, converters and other headless programs link the `ote_kernel` target (add the repository with `add_subdirectory` or `FetchContent`, optionally with `-DOTE_ENGINE=OFF`). Everything in it works without a window or graphics driver:

```cpp
#include "ote/elevation.hpp"
#include "ote/feature_query.hpp"
#include "ote/formats.hpp"
#include "ote/importer.hpp"

ote::ImportPlan plan = ote::planImport("survey.kmz");
ote::ImportResult data = ote::importData(plan.path);

ote::FeatureQuery near;
near.useRadius = true;
near.center = {47.0359, -122.9049, 0};
near.radiusMeters = 500;
for (std::size_t i : ote::queryFeatures(data.data.features, near)) { /* ... */ }

ote::ElevationService elevation;
elevation.setCacheDirectory(ote::defaultCacheDirectory());
elevation.setSources(ote::defaultElevation());
double msl = elevation.heightMsl(46.8529, -121.7604);
ote::LineOfSight los = elevation.lineOfSight({46.85, -121.76, 0}, 2.0, {46.90, -121.70, 0}, 2.0);
```

## Android (GLSurfaceView, Kotlin + JNI)

```kotlin
override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
    engine = Native.create()
}
override fun onDrawFrame(gl: GL10?) {
    Native.frame(engine, width, height, density)
}
```

```cpp
JNIEXPORT jlong JNICALL Java_app_Native_create(JNIEnv* env, jclass) {
    JavaVM* vm;
    env->GetJavaVM(&vm);
    ote_set_android_vm(vm);
    ote_engine* e = ote_create_gl(1, 1);
    ote_set_cache_directory(e, "/data/data/app/files");
    return reinterpret_cast<jlong>(e);
}
JNIEXPORT void JNICALL Java_app_Native_frame(JNIEnv*, jclass, jlong e, jint w, jint h, jfloat dpi) {
    ote_frame_gl(reinterpret_cast<ote_engine*>(e), w, h, 0, dpi);
}
```

`ote_set_android_vm` is required: tiles are fetched through the platform `HttpURLConnection`, so no TLS library ships in the APK. `scripts/android_apk.py` builds a standalone NativeActivity viewer APK without Gradle.

## iOS, iPadOS and macOS (MTKView, Swift)

```swift
let engine = ote_create_metal(Unmanaged.passUnretained(view.device!).toOpaque(), 1)
view.depthStencilPixelFormat = .depth32Float_stencil8
view.colorPixelFormat = .bgra8Unorm

func draw(in view: MTKView) {
    guard let drawable = view.currentDrawable, let depth = view.depthStencilTexture else { return }
    ote_frame_metal(engine, Int32(view.drawableSize.width), Int32(view.drawableSize.height),
                    Unmanaged.passUnretained(drawable).toOpaque(),
                    Unmanaged.passUnretained(depth).toOpaque(), nil, Float(view.contentScaleFactor))
}
```

## Windows (Direct3D 11)

```cpp
ote_engine* e = ote_create_d3d11(device, context, 1);
ote_frame_d3d11(e, width, height, backbufferRtv, nullptr, depthDsv, dpiScale);
swapChain->Present(1, 0);
```

The back buffer must be `DXGI_FORMAT_B8G8R8A8_UNORM` and the depth view `DXGI_FORMAT_D24_UNORM_S8_UINT`.

## C# (.NET P/Invoke)

```csharp
[DllImport("opentakengine_c")] static extern IntPtr ote_create_d3d11(IntPtr device, IntPtr context, int samples);
[DllImport("opentakengine_c")] static extern void ote_frame_d3d11(IntPtr e, int w, int h, IntPtr rtv, IntPtr resolve, IntPtr dsv, float dpi);
[DllImport("opentakengine_c")] static extern int ote_load_features(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
```

## Qt (QOpenGLWidget on Linux)

```cpp
void initializeGL() override { engine = ote_create_gl(0, 1); }
void paintGL() override {
    ote_frame_gl(engine, width() * devicePixelRatio(), height() * devicePixelRatio(), defaultFramebufferObject(), devicePixelRatio());
    if (ote_needs_redraw(engine)) update();
}
```

## Common calls

```c
ote_view v = {47.04, -122.90, 20.0, 0.0, 45.0};
ote_set_view(e, &v);
ote_add_layer(e, OTE_SOURCE_MBTILES, "Offline", "/maps/area.mbtiles", 0, 18, 1.0f);
ote_load_mobac(e, "/maps/source.xml");
ote_add_elevation_directory(e, "/dted");
ote_load_features(e, "/data/overlay.kmz");
ote_open_result opened;
if (ote_open(e, "/data/anything.pmtiles", &opened) && opened.has_region) {
    ote_view_for_region(e, opened.west, opened.south, opened.east, opened.north, &v);
    ote_fly_to(e, &v, 2.0);
}
ote_set_grid(e, 1);
char mgrs[32];
ote_to_mgrs(47.04, -122.90, 5, mgrs, sizeof mgrs);
```
