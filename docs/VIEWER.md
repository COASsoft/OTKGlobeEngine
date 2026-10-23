# Viewer and building from source

## Viewer

```
otk_viewer [file or folder ...] [--view lat,lon,gsd,tilt,azimuth] [--preset 0..3] [--size WxH]
           [--mbtiles f] [--gpkg f] [--osmdroid f] [--gdal f] [--mobac xml]
           [--features file] [--model path,lat,lon,heading,scale] [--tileset dir|.3tz|url|ion://id?access_token=t] [--las file]
           [--stereo eye_separation_m (0 = auto)] [--sun unix_seconds|now]
           [--elevation-dir dir] [--gdal-dem f] [--quantized-mesh url] [--terrain 0|1] [--exaggeration x]
           [--cache dir] [--offline 1] [--cache-limit MB] [--import file] [--export w,s,e,n,minz,maxz,file]
           [--memory MB] [--labels 0|1] [--grid 1] [--grid-type 0|1|2] [--heatmap 1] [--slope 1] [--contours metres|0] [--download-labels w,s,e,n] [--controls mask]
           [--bullseye lat,lon,radius] [--rings lat,lon,spacing,count] [--rb lat1,lon1,lat2,lon2]
           [--first-person heading,pitch[,height]] [--download w,s,e,n,minz,maxz]
           [--shot out.png] [--shot-timeout s] [--script file] [--film dir] [--film-every s]
           [--zoom-test lat,lon,gsd,tilt,az,seconds] [--zoom-start s] [--diag 1]
           [--frames file.csv] [--exit-after s] [--clear-cache 1] [--env NAME=VALUE] [--fps n] [--idle-fps n]
```

- Drop files or folders on the window, or pass them as arguments, to open them: KML/KMZ (with overlays and models), GeoJSON, GPX, Shapefile, CoT, CSV, GML, TAK data packages, ATAK iconsets, MBTiles, PMTiles, GeoPackage, osmdroid/RMaps/OsmAnd archives, MOBAC sources, DTED/HGT files or folders, Cesium terrain folders, 3D Tiles, LAS, glTF/OBJ/COLLADA models (placed at the view centre) and, with GDAL, GeoTIFF imagery and DEMs. The view flies to what was loaded and the title bar reports what was added.
- Input is handled by the engine's shared controller (`engine.input()`, `otk_pointer_*` in the C ABI), so every host behaves the same:
  - Mouse: left drag pans (with inertia), wheel zooms at the cursor, right or middle drag (or Ctrl/Alt + left) rotates and tilts, double click zooms in; Shift + wheel tilts, Alt + wheel rotates.
  - Touch and pen: one finger pans with fling, pinch zooms about the fingers, twist rotates past a 10° threshold, two-finger vertical drag (or three fingers) tilts, double tap zooms in, double-tap-and-drag zooms continuously, two-finger tap zooms out, long press reports a callback. Windows touchscreens and pens use `WM_POINTER`.
  - Trackpads: pinch (magnify) zooms and rotate rotates on macOS and iPad; precise two-finger scroll pans.
  - Keys: arrows or `W`/`A`/`S`/`D` pan (walk in first person), `+`/`-` zoom, `Q`/`E` rotate, `PageUp`/`PageDown` tilt, `N` north up, `T` level the camera.
  - Panning keeps the current heading; only explicit rotation changes it. The poles are reachable at any zoom without the view spinning.
- Viewer keys: `1`-`4` imagery presets (satellite, Esri topographic, OpenStreetMap, vector tiles), `R` reset north and tilt, `I` labels (borders, place names and downloaded roads), `J` download roads and street names for the view, `Y` terrain, `O` exaggeration, `U` depth shading, `H` heat map, `G` grid (MGRS, lat/lon, GARS, off), `K` terrain slope, `L` contours on or off (auto interval; `--contours N` starts them with a fixed one), `B` bullseye at the cursor, `C` range and bearing (press at both ends), `V` viewshed at the cursor, `F` fly to Mount Rainier, `M` 2D/3D, `P` first person, `X` terrain x-ray.
- `--shot` renders offscreen until everything has loaded, writes the PNG plus a `.txt` with statistics, and exits. `--zoom-test` flies to a view and reports incomplete, coarse (two or more levels of fallback) and blank frames; `--zoom-start s` starts it after a fixed delay. `--script` plays timed input from a file, during a `--shot` or in the open window:
  - `seconds wheel x y notches`
  - `seconds drag x0 y0 x1 y1 duration` (mouse)
  - `seconds swipe x0 y0 x1 y1 duration` (one finger, flings on release)
  - `seconds pinch cx cy startDistance endDistance duration [angle]`
  - `seconds twist cx cy distance startAngle endAngle duration`
  - `seconds tilt cx cy dy duration` (two fingers)
  - `seconds view lat lon gsd tilt azimuth`
  - `seconds fly lat lon gsd tilt azimuth duration`
- `--film` saves half-size frames every `--film-every` seconds with a `film.csv` of pending loads.
- `--frames file.csv` writes one row per frame: timing, view, tiles, loads, and the share of the screen that switched without a fade, merged without a fade, faded, or showed imagery one or more levels coarser than wanted. It also writes `file.csv.txt` with totals for the input and settle periods: frames actually drawn (`drawn_frames`), how often new detail appeared (`reveal_frames`, `tile_reveals`), seconds until loading finished (`settled_after_input`), data downloaded (`net_mb`, `net_requests`), and tiles loaded and dropped unseen (`imagery_loads`, `imagery_wasted`).
- `--exit-after s` quits that many seconds after the script's input ends.
- `--clear-cache 1` empties the cache folder first.
- `--idle-fps n`: when nothing changes, the viewer draws about `n` frames per second (0 turns this off). On Android and Windows the default is 5, and input or arriving tiles wake it at once. Elsewhere the default is 20, after half a second without changes.
- `--fps n` asks an Android screen for `n` frames per second (60 by default, 0 leaves the system's choice). This keeps 90 and 120 Hz phones from drawing the map more often than needed.
- `--env NAME=VALUE` sets one of the `OTK_` settings below. This is the way to set them on Android.
- `--diag 1` logs subtree collapses and deep-fallback tiles.
- On Android the viewer also reads arguments from `Android/data/com.otkglobeengine.viewer/files/args.txt` and logs statistics under the logcat tag `OTKGlobeEngine`.
- Environment: `OTK_LOD_MORPH=0..1` level-of-detail colour blend (1 by default, 0 off; always off on software rasterizers such as llvmpipe), `OTK_TRACE=file` per-frame trace, `OTK_WATCHDOG=file` main-thread stack dump on a 3 s stall, `OTK_LOG=file` sokol validation messages, `OTK_PROFILE=file` per-section CPU times (wall and thread time for the main pass), `OTK_MERGE_FADE=seconds` how long zoom-out merges fade (0 off), `OTK_REVEAL_HOLD=seconds` how long ready detail may wait for slower tiles so an area sharpens at once (0.6 by default, 0 shows each tile as soon as it is ready), `OTK_REVEAL_GAP=seconds` shortest time between two reveals (0.3), `OTK_MOTION_HOLD=seconds` how long new detail may wait while the map moves (2), `OTK_BUSY_LOG=1` logs why the engine keeps drawing, `OTK_VISUAL_LOG=file` one line per untidy tile change, `OTK_PERF=file` frame and cache statistics, `OTK_NET_DELAY_MS` / `OTK_NET_JITTER_MS` / `OTK_NET_FAIL_RATE` / `OTK_NET_TIMEOUT_RATE` / `OTK_NET_CORRUPT_RATE` / `OTK_NET_404_RATE` / `OTK_NET_UNREACHABLE` network fault injection.

## Building from source

Windows (Visual Studio 2022 or newer, CMake 3.24+, Ninja):

```
build.cmd
build\windows\apps\viewer\otk_viewer.exe
```

Linux: `cmake -S . -B build -G Ninja && cmake --build build` (libcurl, X11, Xi, Xcursor and OpenGL development packages; add `-DOTK_WITH_GDAL=ON` and libgdal-dev for the GDAL module).

Raspberry Pi 4 and 5 (64-bit Raspberry Pi OS Bookworm or newer): their GPUs run OpenGL ES 3.1, so build with `-DOTK_GLES=ON` (`sudo apt install cmake ninja-build libcurl4-openssl-dev libx11-dev libxi-dev libxcursor-dev libegl-dev libgles-dev`). Releases include a ready-made `raspberrypi-arm64` package: `sudo apt install ./otkglobeengine-VERSION-raspberrypi-arm64.deb`.

FreeBSD, OpenBSD and NetBSD: `sh scripts/bsd_build.sh` installs the build dependencies (cmake, ninja, curl, X11 and Mesa), builds and runs the tests; `OTK_PACKAGE=1` also makes the packages. sokol-shdc has no BSD binaries, so these hosts use the GLSL 4.1 shader headers checked in under `src/render/shaders/generated` (`cmake --build build --target otk_pregenerate_shaders` refreshes them; CI fails when they are stale).

macOS: `cmake -S . -B build -G Ninja && cmake --build build`. iOS: `cmake -S . -B build -G Xcode -DCMAKE_SYSTEM_NAME=iOS`.

Web (WebAssembly npm package): `python scripts/web_build.py` builds `bindings/web/dist` and installs the Emscripten SDK into `~/emsdk` if it is missing; `--pack` also makes the `.tgz`. `node bindings/web/example/serve.mjs` serves a test page at http://localhost:8080/example/.

Android: `python scripts/android_apk.py --install --run` builds, signs and installs a standalone viewer APK (needs the Android SDK and NDK).

OpenGL on Windows (to check the GL path used on Linux and Android): add `-DOTK_GFX_BACKEND=SOKOL_GLCORE -DOTK_SHDC_LANG=glsl410` to the CMake configure.

Kernel only (headless servers and tools): `cmake -S . -B build -G Ninja -DOTK_ENGINE=OFF && cmake --build build`.

Tests: `ctest --test-dir build/windows` runs the kernel tests (`otk_kernel_test`, linked against the kernel alone) and the engine unit tests (geodesy, MGRS, tile matrices, camera, geoid, file parsers, quantized mesh); CI runs them on Windows, Linux, macOS, FreeBSD, OpenBSD and NetBSD, and builds the kernel on its own without graphics packages.

Packages: `cpack --config build/CPackConfig.cmake` makes the platform packages.
