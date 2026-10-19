# OpenTAKEngine

A lightweight, cross-platform C++ map engine modeled on the TAK map engine: a WGS84 globe draped with 2D imagery, 3D terrain, vector features, labels, 3D models and 3D Tiles.

Targets Windows, macOS, Linux, Android, iOS and iPadOS from one codebase. Rendering goes through sokol_gfx (Direct3D 11, Metal, OpenGL 4.1, OpenGL ES 3) with shaders written once. CI builds all five targets on every push.

See [docs/PLAN.md](docs/PLAN.md) for the architecture and decisions, and [docs/EMBEDDING.md](docs/EMBEDDING.md) for using the engine from Android, iOS, macOS, Windows, C# and Qt.

| Globe | Terrain | First person |
| --- | --- | --- |
| ![](screenshots/opentakengine/01_globe.png) | ![](screenshots/opentakengine/05_rainier_terrain.png) | ![](screenshots/opentakengine/15_first_person.png) |
| **Vector tiles** | **3D Tiles** | **Features and labels** |
| ![](screenshots/opentakengine/14_vector_tiles.png) | ![](screenshots/opentakengine/16_3d_tiles.png) | ![](screenshots/opentakengine/08_features_kml_terrain.png) |
| **MGRS grid** | **Viewshed** | **Elevation heat map** |
| ![](screenshots/opentakengine/09_mgrs_grid.png) | ![](screenshots/opentakengine/10_viewshed.png) | ![](screenshots/opentakengine/13_elevation_heatmap.png) |

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
- WGS84 quadtree, 33x33 post meshes with skirts, screen-space-error LOD, slope shading ("enhanced depth perception"), exaggeration
- Terrarium and quantized-mesh elevation tiles, DTED 0-3 and SRTM HGT folders, GDAL DEMs, EGM96 geoid (heights are HAE)
- Line of sight, viewshed, elevation profile, elevation heat map

**Vectors, labels and models**
- KML/KMZ (styles, style maps, icons, MultiGeometry, gx:Track), GeoJSON (simplestyle), GPX, Shapefile, plus any OGR format via GDAL
- Ground-clamped lines and polygons draped into the terrain texture, 3D lines, icons, SDF labels with decluttering, hit testing
- MGRS/UTM conversions and a draped MGRS grid
- glTF/GLB models, Cesium 3D Tiles (tileset.json, .3tz, URLs, b3dm/glb, external tilesets), LAS point clouds (WGS84 and UTM natively, any CRS through the GDAL module)

**Loading behaviour**
- Readiness-gated refinement: a parent tile stays on screen until its children are fully loaded, so zooming never shows tiles popping in
- Parent, child and zoom-out prefetch inside a GPU memory budget (512 MB desktop, 256 MB mobile by default), cross-fade between levels
- Separate local (cache, files) and network loader pools

## Build

Windows (Visual Studio 2022 or newer, CMake 3.24+, Ninja):

```
build.cmd
build\windows\apps\viewer\ote_viewer.exe
```

Linux: `cmake -S . -B build -G Ninja && cmake --build build` (libcurl, X11, Xi, Xcursor and OpenGL development packages; add `-DOTE_WITH_GDAL=ON` and libgdal-dev for the GDAL module).

macOS: `cmake -S . -B build -G Ninja && cmake --build build`. iOS: `cmake -S . -B build -G Xcode -DCMAKE_SYSTEM_NAME=iOS`.

Android: `python scripts/android_apk.py --install --run` builds, signs and installs a standalone viewer APK (needs the Android SDK and NDK).

## Viewer

```
ote_viewer [--view lat,lon,gsd,tilt,azimuth] [--preset 0..3] [--size WxH]
           [--mbtiles f] [--gpkg f] [--osmdroid f] [--gdal f] [--mobac xml]
           [--features file] [--model path,lat,lon,heading,scale] [--tileset dir|.3tz|url] [--las file]
           [--elevation-dir dir] [--gdal-dem f] [--quantized-mesh url] [--terrain 0|1] [--exaggeration x]
           [--cache dir] [--offline 1] [--memory MB] [--grid 1] [--heatmap 1]
           [--first-person heading,pitch[,height]] [--download w,s,e,n,minz,maxz]
           [--shot out.png] [--shot-timeout s] [--zoom-test lat,lon,gsd,tilt,az,seconds]
```

- Mouse: left drag pans, wheel zooms, right drag rotates and tilts. Touch: one finger pans, pinch zooms, twist rotates, two-finger vertical drag tilts.
- Keys: `1`-`4` imagery presets (satellite, satellite + USGS topo, OpenStreetMap, vector tiles), `R` reset north, `T` terrain, `E` exaggeration, `D` depth shading, `H` heat map, `G` MGRS grid, `V` viewshed at the cursor, `F` fly to Mount Rainier, `M` 2D/3D, `P` first person (`W`/`A`/`S`/`D` to walk).
- `--shot` renders offscreen until everything has loaded, writes the PNG plus a `.txt` with statistics, and exits. `--zoom-test` flies to a view and reports how many frames showed incompletely loaded tiles.
- Environment: `OTE_TRACE=file` per-frame trace, `OTE_WATCHDOG=file` main-thread stack dump on a 3 s stall, `OTE_LOG=file` sokol validation messages.

## License

OpenTAKEngine is written independently. The upstream TAK engine (GPLv3) was studied for behaviour and architecture only; no upstream code is included. Noto Sans is under the SIL Open Font License (`assets/fonts/OFL.txt`).
