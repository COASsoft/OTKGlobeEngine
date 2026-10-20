# OpenTAKEngine

A lightweight, cross-platform C++ map engine modeled on the TAK map engine: a WGS84 globe draped with 2D imagery, 3D terrain, vector features, labels, 3D models and 3D Tiles.

Targets Windows, macOS, Linux, Android, iOS and iPadOS from one codebase. Rendering goes through sokol_gfx (Direct3D 11, Metal, OpenGL 4.1, OpenGL ES 3) with shaders written once. CI builds all five targets on every push.

See [docs/PLAN.md](docs/PLAN.md) for the architecture and decisions, and [docs/EMBEDDING.md](docs/EMBEDDING.md) for using the engine from Android, iOS, macOS, Windows, C# and Qt. [docs/RENDERING.md](docs/RENDERING.md) covers continuous, zero-blank rendering and how it is measured; [docs/FORMATS.md](docs/FORMATS.md) is the data import/export compatibility matrix, and [docs/OFFLINE.md](docs/OFFLINE.md) covers caching and airgapped networks.

| Globe | Terrain | First person |
| --- | --- | --- |
| ![](screenshots/opentakengine/01_globe.png) | ![](screenshots/opentakengine/05_rainier_terrain.png) | ![](screenshots/opentakengine/15_first_person.png) |
| **Vector tiles** | **3D Tiles** | **Features and labels** |
| ![](screenshots/opentakengine/14_vector_tiles.png) | ![](screenshots/opentakengine/16_3d_tiles.png) | ![](screenshots/opentakengine/08_features_kml_terrain.png) |
| **MGRS grid** | **Viewshed** | **Elevation heat map** |
| ![](screenshots/opentakengine/09_mgrs_grid.png) | ![](screenshots/opentakengine/10_viewshed.png) | ![](screenshots/opentakengine/13_elevation_heatmap.png) |
| **2525 units, contours, bullseye, R&B** | **Terrain slope** | **Airgapped, empty cache** |
| ![](screenshots/opentakengine/19_mil_symbols_contours_bullseye.png) | ![](screenshots/opentakengine/20_terrain_slope.png) | ![](screenshots/opentakengine/21_airgapped_empty_cache.png) |

ATAK reference captures are in `screenshots/atak`.

## Features

**Globe and camera**
- TAK-style scene model: focus point, ground resolution, azimuth, tilt up to 84 degrees, terrain collision
- 2D/3D toggle, first-person mode (walk and look at eye height), animated fly-to
- Atmosphere and sky from ground level to space

**Imagery**
- Online XYZ, TMS, quadkey, WMS 1.1.1/1.3.0 (EPSG:3857 and 4326), WMTS templates, MOBAC XML (including multi-layer sources)
- Offline MBTiles, GeoPackage tiles (any tile matrix), osmdroid SQLite, and GeoTIFF/NITF/MrSID/JPEG2000/GeoPDF/CADRG via the optional GDAL module
- SQLite tile cache with offline mode and fallback, region download
- Mapbox Vector Tiles (TileJSON or MBTiles) rendered on the GPU with place, peak and water labels
- `file://` URL templates for offline `{z}/{x}/{y}` tile folders
- Layer stacking with opacity, KML/KMZ ground overlays, arbitrary image overlays

**Terrain**
- WGS84 quadtree, 33x33 post meshes with skirts, screen-space-error LOD, slope shading ("enhanced depth perception"), exaggeration, x-ray transparency
- Terrarium and quantized-mesh elevation tiles, DTED 0-3 and SRTM HGT folders, GDAL DEMs, EGM96 geoid (heights are HAE)
- Line of sight, viewshed, elevation profile, elevation heat map

**Vectors, labels and models**
- KML/KMZ (styles, style maps, icons, MultiGeometry, gx:Track), GeoJSON (simplestyle), GPX, Shapefile, plus any OGR format via GDAL
- Ground-clamped lines and polygons draped into the terrain texture, 3D lines, icons, SDF labels with decluttering, hit testing
- MGRS/UTM conversions and a draped MGRS grid
- glTF/GLB models, Cesium 3D Tiles (tileset.json, .3tz, URLs, b3dm/glb, external tilesets), LAS point clouds (WGS84 and UTM natively, any CRS through the GDAL module)

**Military**
- MIL-STD-2525C and 2525D point symbols drawn by the engine: affiliation frames and colours for space, air, ground, sea surface, subsurface and SOF; planned and assumed friend/suspect/pending dashed frames; echelon, headquarters staff, task force, feint/dummy and installation amplifiers; joker/faker/exercise markers; common unit, equipment, air, sea and subsurface icons. CoT `a-` types (and `Style::sidc`) become 2525 icons automatically ([sheet](screenshots/opentakengine/22_mil_std_2525_symbols.png)).
- WMM2025 magnetic declination and inclination (matches the NOAA test values), UTM grid convergence, bearings in true/magnetic/grid north, degrees or NATO mils
- Coordinates: decimal, DM, DMS, MGRS (including polar UPS), UTM/UPS and GARS formatting, and a parser that accepts all of them
- Vincenty geodesics (inverse, direct, densified paths, area), range rings, bullseye, range and bearing lines, sectors, ellipses and geodesic circles as features
- Contour lines from loaded elevation (MSL, major/minor with labels), terrain slope shading, MGRS, lat/lon and GARS grids

**Loading behaviour**
- Readiness-gated refinement: a parent tile stays on screen until its children are fully loaded, so zooming never shows tiles popping in
- Parent, child and zoom-out prefetch inside a GPU memory budget (512 MB desktop, 256 MB mobile by default), cross-fade between levels
- Separate local (cache, files) and network loader pools
- Airgap-ready cache: expired tiles shown instantly and refreshed in the background, unreachable hosts fail fast and are re-probed, pinned region downloads that wait out outages, size limit, MBTiles/GeoPackage export and import into the cache, built-in world backdrop where nothing is cached ([docs/OFFLINE.md](docs/OFFLINE.md))

**Size and speed**
- The whole engine is about 4 MB (Windows DLL; Android arm64 library 4 MB, 1.8 MB compressed; the Android viewer APK is 1.8 MB) including SQLite, image decoders, the label font and the geoid
- CPU time per frame on a 2015 desktop i5: 0.2 ms (globe), 0.5 ms (street), 0.9 ms (tilted terrain), 1.1 ms (first person)

**Bindings**
- C ABI `include/ote/ote.h` and `opentakengine_c` shared library
- Android AAR with `OpenTakView` (`scripts/android_aar.py`), Swift `OpenTAKView` and xcframework (`scripts/apple_xcframework.sh`), C# wrapper (`bindings/dotnet`)

## Build

Windows (Visual Studio 2022 or newer, CMake 3.24+, Ninja):

```
build.cmd
build\windows\apps\viewer\ote_viewer.exe
```

Linux: `cmake -S . -B build -G Ninja && cmake --build build` (libcurl, X11, Xi, Xcursor and OpenGL development packages; add `-DOTE_WITH_GDAL=ON` and libgdal-dev for the GDAL module).

macOS: `cmake -S . -B build -G Ninja && cmake --build build`. iOS: `cmake -S . -B build -G Xcode -DCMAKE_SYSTEM_NAME=iOS`.

Android: `python scripts/android_apk.py --install --run` builds, signs and installs a standalone viewer APK (needs the Android SDK and NDK).

OpenGL on Windows (to check the GL path used on Linux and Android): add `-DOTE_GFX_BACKEND=SOKOL_GLCORE -DOTE_SHDC_LANG=glsl410` to the CMake configure.

Tests: `ctest --test-dir build/windows` runs the unit tests (geodesy, MGRS, tile matrices, camera, geoid, file parsers, quantized mesh); CI runs them on Windows, Linux and macOS.

## Viewer

```
ote_viewer [--view lat,lon,gsd,tilt,azimuth] [--preset 0..3] [--size WxH]
           [--mbtiles f] [--gpkg f] [--osmdroid f] [--gdal f] [--mobac xml]
           [--features file] [--model path,lat,lon,heading,scale] [--tileset dir|.3tz|url] [--las file]
           [--elevation-dir dir] [--gdal-dem f] [--quantized-mesh url] [--terrain 0|1] [--exaggeration x]
           [--cache dir] [--offline 1] [--cache-limit MB] [--import file] [--export w,s,e,n,minz,maxz,file]
           [--memory MB] [--grid 1] [--grid-type 0|1|2] [--heatmap 1] [--slope 1] [--contours interval]
           [--bullseye lat,lon,radius] [--rings lat,lon,spacing,count] [--rb lat1,lon1,lat2,lon2]
           [--first-person heading,pitch[,height]] [--download w,s,e,n,minz,maxz]
           [--shot out.png] [--shot-timeout s] [--zoom-test lat,lon,gsd,tilt,az,seconds] [--zoom-start s] [--diag 1]
```

- Input is handled by the engine's shared controller (`engine.input()`, `ote_pointer_*` in the C ABI), so every host behaves the same:
  - Mouse: left drag pans (with inertia), wheel zooms at the cursor, right or middle drag (or Ctrl/Alt + left) rotates and tilts, double click zooms in; Shift + wheel tilts, Alt + wheel rotates.
  - Touch and pen: one finger pans with fling, pinch zooms about the fingers, twist rotates past a 10° threshold, two-finger vertical drag (or three fingers) tilts, double tap zooms in, double-tap-and-drag zooms continuously, two-finger tap zooms out, long press reports a callback. Windows touchscreens and pens use `WM_POINTER`.
  - Trackpads: pinch (magnify) zooms and rotate rotates on macOS and iPad; precise two-finger scroll pans.
  - Keys: arrows or `W`/`A`/`S`/`D` pan (walk in first person), `+`/`-` zoom, `Q`/`E` rotate, `PageUp`/`PageDown` tilt, `N` north up, `T` level the camera.
  - Panning keeps the current heading; only explicit rotation changes it. The poles are reachable at any zoom without the view spinning.
- Viewer keys: `1`-`4` imagery presets (satellite, satellite + USGS topo, OpenStreetMap, vector tiles), `R` reset north and tilt, `Y` terrain, `O` exaggeration, `U` depth shading, `H` heat map, `G` grid (MGRS, lat/lon, GARS, off), `K` terrain slope, `L` contours for the view, `B` bullseye at the cursor, `C` range and bearing (press at both ends), `V` viewshed at the cursor, `F` fly to Mount Rainier, `M` 2D/3D, `P` first person, `X` terrain x-ray.
- `--shot` renders offscreen until everything has loaded, writes the PNG plus a `.txt` with statistics, and exits. `--zoom-test` flies to a view and reports incomplete, coarse (two or more levels of fallback) and blank frames; `--zoom-start s` starts it after a fixed delay. `--diag 1` logs subtree collapses and deep-fallback tiles.
- On Android the viewer also reads arguments from `Android/data/com.opentakengine.viewer/files/args.txt` and logs statistics under the logcat tag `OpenTAKEngine`.
- Environment: `OTE_TRACE=file` per-frame trace, `OTE_WATCHDOG=file` main-thread stack dump on a 3 s stall, `OTE_LOG=file` sokol validation messages, `OTE_PROFILE=file` per-section CPU times, `OTE_PERF=file` frame and cache statistics, `OTE_NET_DELAY_MS` / `OTE_NET_JITTER_MS` / `OTE_NET_FAIL_RATE` / `OTE_NET_TIMEOUT_RATE` / `OTE_NET_CORRUPT_RATE` / `OTE_NET_404_RATE` / `OTE_NET_UNREACHABLE` network fault injection.

## License

OpenTAKEngine is written independently. The upstream TAK engine (GPLv3) was studied for behaviour and architecture only; no upstream code is included. Noto Sans is under the SIL Open Font License (`assets/fonts/OFL.txt`).
