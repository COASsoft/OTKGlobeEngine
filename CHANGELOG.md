# Changelog

## Unreleased

### Labels
- Built-in world labels drawn like a satellite map with labels: solid country borders, dashed state, province and county lines with a dark edge, draped on the terrain at a constant on-screen width, and the names of countries, states, provinces, counties, cities, towns and villages worldwide with city dots, chosen by importance and spacing and decluttered against each other. Shown by default; one switch turns all of it on or off (`Engine::setLabelsEnabled`, `otk_set_labels`, Android `OTKGlobeView.setLabels`, .NET `Labels`, Swift `labels`, viewer key `I` and `--labels 0|1`).
- Works offline: the boundaries (geoBoundaries), place names (GeoNames) and country labels (Natural Earth) are embedded in the kernel, about 9.5 MB; `-DOTK_LABELS=OFF` builds without them. `scripts/make_labels.py` rebuilds the data from the sources.
- Kernel: `otk::placesNear` returns nearby towns, states or countries from the same data.
- Roads on the same switch, styled like a satellite map: gold motorways and trunk roads, gray primary to residential streets with dark edges, dashed tunnels and paths, widths that grow with the zoom; interstate, US route, state route and other road shields; street names drawn along the road and shortened the usual way ("22nd Ave SE", "Capitol Way S"); neighbourhood, suburb and borough names; park, forest and reserve names in green; lake, bay, sea, river and creek names in blue.
- Text is rendered at its exact pixel size with a crisp outline, outlines drawn under every letter of a name instead of over the previous letter, and labels fade in and out instead of blinking.
- Roads and street-level names come from OpenStreetMap vector tiles (OpenMapTiles schema, OpenFreeMap by default) that are downloaded for an area and then read from disk; nothing streams while browsing. `Engine::downloadLabels(region, maxZoom)` (`otk_download_labels`, Android `OTKGlobeView.downloadLabels`, .NET `DownloadLabels`, Swift `downloadLabels`, viewer key `J` and `--download-labels`) stores them pinned in the tile cache, within OpenFreeMap's terms (3000 tiles, 50 MB, 4 requests at a time). `labelsLayer()` gives the layer for `exportTiles`/`importTiles`, and `Engine::setLabelsSource` (`otk_set_labels_source`) points roads at an offline `.mbtiles`/`.pmtiles` file or another download server; an empty source keeps only the built-in borders and places. Missing road tiles never hold back imagery.
- Layers: `ImageryLayer::stream = false` makes any layer read only from the cache, local files and downloaded areas; `ImageryLayer::maxRequests` limits parallel requests to its server, downloads included.

### 3D buildings
- Buildings in 3D, built into the engine and on by default: `Engine::setBuildingsEnabled` (`otk_set_buildings`, Android `OTKGlobeView.setBuildings`, .NET `Buildings`, Swift `buildings`, the "3D buildings" switch in the layers panel, viewer `--buildings 0|1`).
- Footprints and heights come from the OpenStreetMap `building` layer of the road and label tiles, so they work offline wherever roads were downloaded or a local `.mbtiles`/`.pmtiles` is set, worldwide, with no map service or API key.
- Roofs show the real aerial imagery: each area's roofs are baked from the base imagery at up to zoom 18 and re-baked as sharper tiles arrive. Houses get gable or hip roofs; taller buildings flat ones.
- Walls use photographed facades built into the engine (glass curtain walls, office grids, brick and stone with windows for towers and mid-rises; painted siding, stucco, concrete and metal with windows for houses, shops and sheds), picked from the height and footprint, tinted with OpenStreetMap `building:colour` where present, lit by the sun or a fixed light.
- Buildings stand on the terrain, grow in when they appear and fade out with distance and altitude.

### Web
- The engine now also builds for web pages (WebAssembly and WebGL 2) as the `otkglobeengine` npm package: `OTKGlobe.create(canvas)` gives the same globe, imagery, terrain, roads, labels, 3D buildings and controls as the native builds, with mouse, touch and keyboard input, `open()` for dropped or fetched files, and TypeScript types. `python scripts/web_build.py` builds it (it installs Emscripten if needed); pages must be served cross-origin isolated. Downloaded roads stay in memory for the session.
- Releases publish the npm package and the NuGet package when `NPM_TOKEN` and `NUGET_API_KEY` repository secrets are set.

### Loading
- Roads, street names and 3D buildings now appear by themselves anywhere in the world: when the camera stops while zoomed in close (12 m per pixel or closer), the engine downloads that area (up to about 3 km around the centre) into the cache, pinned like a manual download, so it keeps working offline afterwards. Areas already fetched are not asked for again. `Engine::setAutoDownloadLabels(false)` (`otk_set_auto_download_labels`) turns it off for strictly offline use.
- Phones and tablets get a larger tile memory budget (16% of RAM, was 10%), and prefetching stops when the budget is full instead of evicting tiles the view still needs. A tilted city view on a 4 GB tablet kept reloading tiles and never finished loading roads and buildings.
- Road names and styling no longer blink while moving a tilted view: labels come from the most detailed road tiles loaded for each area, stay in place for a moment while newer tiles arrive, and keep their fade when a label moves to a different tile.

### Controls
- Built-in map controls in the bottom-right corner, plain and on by default: zoom in and out, a compass that shows and resets north, a 2D/3D button, a layers button with the map list and switches for labels, contours and 3D terrain plus a roads download, a scale bar (metric or imperial) and the map credits. They work with mouse, touch and pen and take a press before the map does.
- `Engine::setControls(mask)` turns them off or keeps only some (`ControlZoom`, `ControlCompass`, `ControlTilt`, `ControlLayers`, `ControlScale`, `ControlAttribution`; `otk_set_controls`, .NET `Controls`, Swift `controls`, Android `setControls`, viewer `--controls`). `-DOTK_CONTROLS=OFF` leaves them out of the build. The code lives in `src/ui`.
- `Engine::setControlsPadding` moves the controls clear of system bars or app UI (`otk_set_controls_padding`); Android's `OTKGlobeView` and the Android viewer follow the window insets, so the controls stay above a tablet taskbar.
- Map list: `Engine::setBasemaps`, `basemaps`, `setBasemap`, `basemap` and `otk::defaultBasemaps()` (`otk_set_basemap`, `otk_basemap`, `otk_basemap_count`, `otk_basemap_name`); the viewer's `1`-`4` keys use the same list.
- `ImageryLayer::attribution`, listed first by `credits()`; the default imagery and maps fill it in. New getters `terrainEnabled`, `contourInterval` and `contourUnit`.

### Contours
- Contour mode like a topo map: `Engine::setContours(enabled, interval, unit)` (`otk_set_contours`, Android `OTKGlobeView.setContours`, .NET `SetContours`, Swift `setContours`, viewer key `L` and `--contours`). Minor and index lines in sienna, drawn on the terrain under roads and labels at a constant width, with the index elevations written along the lines in black with a white outline.
- The interval follows the zoom (100/500 m from about 150 m per pixel, then 50/250, 20/100 and 10/50 m; 400/2000 ft to 40/200 ft in feet) or stays at a fixed value. Lines are regenerated in the background for the visible area once the camera stops and replace the previous set, from elevation at the detail the view needs, so turning them on again or panning never stacks copies. Sea level is skipped.
- `contourLines` and `addContours` trace in one pass and simplify the lines.

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
