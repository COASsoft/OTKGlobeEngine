# OTKGlobeEngine

OTKGlobeEngine is a 3D map of the whole Earth that you can put inside your own app. It draws satellite imagery, terrain, roads and place names, and it keeps working offline.

It runs on Windows, macOS, Linux (including Raspberry Pi), Android, iOS and iPadOS, and in web browsers. It is free to use, including in paid and closed-source products (Apache 2.0 license).

Website and downloads: https://grangedevgroup-code.github.io/OTKGlobeEngine/

| Globe | Terrain | First person |
| --- | --- | --- |
| ![](screenshots/otkglobeengine/01_globe.png) | ![](screenshots/otkglobeengine/05_rainier_terrain.png) | ![](screenshots/otkglobeengine/15_first_person.png) |
| **Vector tiles** | **3D Tiles** | **Features and labels** |
| ![](screenshots/otkglobeengine/14_vector_tiles.png) | ![](screenshots/otkglobeengine/16_3d_tiles.png) | ![](screenshots/otkglobeengine/08_features_kml_terrain.png) |

## Try it

Download the viewer for your system from the [latest release](https://github.com/grangedevgroup-code/OTKGlobeEngine/releases/latest) and run it. You can drag a map file (KML, GeoJSON, GPX, MBTiles, and many more) onto the window to open it.

On Android, install `otkglobeengine-viewer-<version>-android-arm64.apk`.

## Install

There are two builds of the engine:

- **Native** (the main one): for apps on Windows, macOS, Linux, Android, iPhone and iPad. Fastest, with the full feature set, and keeps downloaded maps on disk for offline use.
- **Web**: the same engine compiled to WebAssembly for web pages. Use it only if your app runs in a browser.

### Native apps

Pick the line that matches what you are building. Everything except NuGet comes from the [latest release](https://github.com/grangedevgroup-code/OTKGlobeEngine/releases/latest).

| You are building | Get | Then |
| --- | --- | --- |
| A C or C++ app on Windows, Linux or macOS | `.zip`, `.tar.gz`, `.deb` or `.rpm` | `find_package(OTKGlobeEngine)` in CMake |
| A C# / .NET app | `dotnet add package OTKGlobeEngine` | `new OTKGlobeEngine.Engine(...)` |
| An Android app | `otkglobeengine-<version>.aar` | Add `OTKGlobeView` to a layout |
| An iPhone, iPad or Mac app | `OTKGlobeEngine-<version>.xcframework.zip` | Add `OTKGlobeView.swift` |
| An app on a Raspberry Pi | `sudo apt install ./otkglobeengine-<version>-raspberrypi-arm64.deb` | `find_package(OTKGlobeEngine)` |
| A C++ app built from source | `FetchContent_Declare(otk GIT_REPOSITORY https://github.com/grangedevgroup-code/OTKGlobeEngine.git GIT_TAG v<version>)` | `target_link_libraries(app PRIVATE otkglobeengine)` |

Every file in a release is listed in `SHA256SUMS.txt` so you can check your download.

### Web pages

```
npm install otkglobeengine
```

Your server must send two headers so the engine can use threads. The [web package README](bindings/web/README.md) shows them, along with a complete example. In the browser, roads and buildings that have loaded last until the page is closed; for maps that keep working offline across restarts, use a native build.

## First program

The engine never opens its own window. Your app makes the window and graphics device, creates the engine once, and asks it to draw each frame. In C (the same calls exist for C#, Swift, Kotlin and C++):

```c
#include <otk/otk.h>

otk_engine* map = otk_create_gl(0, 4);
otk_set_cache_directory(map, "map-cache");

otk_view olympia = {.lat = 47.04, .lon = -122.90, .gsd = 2.0, .azimuth = 0, .tilt = 45};
otk_fly_to(map, &olympia, 3.0);

while (app_running) {
    otk_frame_gl(map, width, height, 0, 1.0f);
}

otk_destroy(map);
```

The map comes with zoom, compass, 2D/3D and layer buttons in the bottom-right corner. Turn them off with `otk_set_controls(map, 0)` if you want your own.

[docs/EMBEDDING.md](docs/EMBEDDING.md) shows complete setups for Android, iOS, macOS, Windows (Direct3D), C# and Qt. For a web page, see the [web package README](bindings/web/README.md).

## What it can do

- **Maps:** satellite, topographic and street maps out of the box; add your own from almost any tile server or offline file.
- **Terrain:** real 3D mountains and valleys, with line of sight, viewshed and elevation profiles.
- **Labels and roads:** country, state, city and town names are built in. Roads and street names load by themselves anywhere in the world when you zoom in, and stay saved so that area works offline afterwards.
- **3D buildings:** built from OpenStreetMap, with roofs taken from the satellite image. Off for now; turn them on with `setBuildingsEnabled(true)`.
- **Your data:** KML/KMZ, GeoJSON, GPX, Shapefile, CoT, CSV, TAK data packages, 3D models (glTF, OBJ, COLLADA), 3D Tiles and LAS point clouds.
- **Offline:** everything you have viewed is cached, and whole regions can be downloaded ahead of time ([docs/OFFLINE.md](docs/OFFLINE.md)).
- **Military:** MIL-STD-2525 symbols, MGRS grids, range and bearing, contours, magnetic declination, sun and moon times ([docs/MILITARY.md](docs/MILITARY.md)).
- **Small and fast:** about 4 MB, and well under a millisecond of CPU time per frame on an old desktop.

The full list is in [docs/FEATURES.md](docs/FEATURES.md).

## Build from source

Windows (needs Visual Studio 2022, CMake and Ninja):

```
build.cmd
build\windows\apps\viewer\otk_viewer.exe
```

Linux and macOS:

```
cmake -S . -B build -G Ninja
cmake --build build
```

Android, Raspberry Pi, BSD, the viewer's options and running the tests are covered in [docs/VIEWER.md](docs/VIEWER.md).

## More documentation

- [docs/EMBEDDING.md](docs/EMBEDDING.md) — using the engine in your app
- [docs/FEATURES.md](docs/FEATURES.md) — everything it can do
- [docs/VIEWER.md](docs/VIEWER.md) — the viewer app and building from source
- [docs/FORMATS.md](docs/FORMATS.md) — which file formats can be opened and saved
- [docs/OFFLINE.md](docs/OFFLINE.md) — caching and networks without internet
- [docs/PHOTOREALISM.md](docs/PHOTOREALISM.md) — photorealistic 3D cities
- [docs/XR.md](docs/XR.md) — VR, AR and stereo
- [docs/MILITARY.md](docs/MILITARY.md) — military features

## License

Apache License 2.0 ([LICENSE](LICENSE)). You can use, change and sell it in any product, open or closed. Keep the license text with copies you share.

Bundled libraries and map data have their own licenses, listed in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md). Some map data (OpenStreetMap, the default imagery) asks you to show credit on screen; the built-in credits line does that for you.

To report a security problem, see [SECURITY.md](SECURITY.md).
