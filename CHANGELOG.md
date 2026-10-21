# Changelog

## Unreleased

### Labels
- Built-in world labels drawn like a satellite map with labels: solid country borders, dotted state and province lines, dashed county lines draped on the terrain, and the names of countries, states, provinces, counties, cities, towns and villages worldwide with city dots, chosen by importance and spacing and decluttered against each other. Shown by default; one switch turns all of it on or off (`Engine::setLabelsEnabled`, `otk_set_labels`, Android `OTKGlobeView.setLabels`, .NET `Labels`, Swift `labels`, viewer key `I` and `--labels 0|1`).
- Works offline: the boundaries (geoBoundaries), place names (GeoNames) and country labels (Natural Earth) are embedded in the kernel, about 9.5 MB; `-DOTK_LABELS=OFF` builds without them. `scripts/make_labels.py` rebuilds the data from the sources.
- Kernel: `otk::placesNear` returns nearby towns, states or countries from the same data.

## 0.1.1

### Renamed
- The project is now OTKGlobeEngine (formerly OpenTAKEngine). The repositories are github.com/grangedevgroup-code/OTKGlobeEngine and gitlab.com/grangedevgroup/OTKGlobeEngine, and the website is https://grangedevgroup-code.github.io/OTKGlobeEngine/.
- The code prefix follows the name: namespace `otk`, C ABI `otk_*` in `otk/otk.h`, headers under `include/otk`, CMake options and environment variables `OTK_*`, libraries `otkglobeengine` and `otkglobeengine_c`, CMake package `OTKGlobeEngine`, pkg-config `otkglobeengine`, Android package `com.otkglobeengine`, .NET package and namespace `OTKGlobeEngine`, Swift `OTKGlobeView` with module `OTKGlobeEngineC`.
- An existing tile cache moves to the new cache directory on first use. CoT and GML files written by 0.1.0 still import with their attributes.

### Kernel and engine
- The GPU-free kernel (`otk_kernel`) is a separate library: formats, import planning (`planImport`), feature queries (`FeatureQuery`), elevation queries, line of sight and profiles (`ElevationService`), coordinates, geodesy, magnetic model, geoid, sun and moon, 2525 symbol images, tile archives, cache and network. `-DOTK_ENGINE=OFF` builds the kernel alone with no graphics dependencies.
- `Engine::open` loads any file or folder `planImport` understands. `loadData` returns the features, overlays and models a file added with their extent, and `featuresRegion`, `tilesetRegion`, `pointCloudRegion` and `viewForRegion` zoom to content (C ABI `otk_open`, `otk_view_for_region`, `otk_load_features_region`, `otk_tileset_region`, `otk_point_cloud_region`).
- `Engine::queryFeatures` and `elevationLayers`.

### Compatibility
- Request headers on imagery and elevation layers; Cesium, MapLibre and Leaflet URL placeholders (`{reverseY}`, `{westDegrees}`, `{bbox-epsg-3857}`, `{quadkey}`, `{r}` and more).
- Cesium terrain from `layer.json`, Cesium ion and local terrain folders; quantized-mesh and heightmap tiles, tile availability and extents.
- RMaps/BigPlanet and OsmAnd SQLite archives; PMTiles detected by content.
- KML NetworkLink with view-based refresh and HTTP, Model (COLLADA, OBJ, glTF), extrude, remote icons.
- OBJ/MTL and COLLADA models.
- ATAK iconsets and `usericon`, range-and-bearing lines, dashed and dotted strokes.
- Shapefiles reprojected from their `.prj` (UTM and other Transverse Mercator, State Plane, Albers, Mercator, Web Mercator).
- `.gltf` files are no longer detected as GeoJSON.

### Viewer
- Files and folders open when dropped on the window or passed as arguments, and the view flies to them.

### Platforms and packages
- Raspberry Pi 4 and 5: OpenGL ES 3 build (`-DOTK_GLES=ON`) and `raspberrypi-arm64` packages.

### Website
- Charcoal theme; the documentation, changelog, security policy and license open on the website.

## 0.1.0

First release.

### Engine
- WGS84 globe with imagery tile pyramids (XYZ, TMS, WMS, WMTS, MBTiles, GeoPackage, osmdroid, MOBAC, GDAL), vector tiles, terrain from Terrarium, quantized-mesh, DTED, SRTM and GDAL DEMs with an EGM96 geoid, 2D and 3D modes, first person, terrain exaggeration and x-ray.
- Continuous, zero-blank rendering with screen-space-error LOD, prefetching, retained subtrees and per-frame work budgets; GPU memory budget from VRAM.
- Features from KML/KMZ, GeoJSON, GPX, Shapefile, CSV, WKT, GML, CoT and TAK data packages, with exporters; labels with decluttering, hit testing.
- glTF models with metallic-roughness materials, normal, emissive and occlusion maps, unlit materials, GPU instancing and sun lighting by time.
- OGC 3D Tiles 1.0/1.1: explicit and implicit tilesets, b3dm, i3dm, pnts, cmpt and glTF content, Draco, meshopt and KTX2 compression, .3tz archives, Google Photorealistic 3D Tiles and Cesium ion with credits; LAS point clouds.
- VR and AR hosts: multi-view rendering for OpenXR eye poses with one tile update per frame, table-top scale, side-by-side stereo.
- Offline and airgapped operation: persistent cache with stale-while-revalidate, unreachable-host detection, pinned region downloads, MBTiles/GeoPackage export and import, embedded world backdrop.

### Military
- MIL-STD-2525C/D symbols, CoT symbolization, MGRS/UTM/UPS/GARS/DMS coordinates, WMM2025 declination, NATO mils, grids, range rings, bullseye, range and bearing.
- Line of sight, viewshed, profiles, contours and slope shading.
- Sun and moon position, moon phase, twilight times (BMNT/EENT), moonrise/moonset and ground illuminance for night-vision planning.
- MISB ST 0601 KLV from MPEG-TS: sensor and frame-centre tracks, footprints and line of sight.

### Platforms and packages
- Windows, Linux, macOS, iOS, Android, FreeBSD, OpenBSD and NetBSD.
- C ABI (`ote.h`) as a shared library with CMake package config and pkg-config; .NET (NuGet), Android (AAR), Apple (XCFramework) and Swift bindings.
- Apache License 2.0.
