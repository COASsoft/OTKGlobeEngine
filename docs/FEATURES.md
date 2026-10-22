# Feature reference

Everything the engine can do, grouped by area. For a quick overview see the [README](../README.md).

## Globe and camera
- TAK-style scene model: focus point, ground resolution, azimuth, tilt up to 84 degrees, terrain collision
- 2D/3D toggle, first-person mode (walk and look at eye height), animated fly-to
- Atmosphere and sky from ground level to space

## Imagery
- Online XYZ, TMS, quadkey, WMS 1.1.1/1.3.0 (EPSG:3857 and 4326), WMTS templates, MOBAC XML (including multi-layer and WMS sources); Cesium, MapLibre and Leaflet URL placeholders (`{reverseY}`, `{westDegrees}`, `{bbox-epsg-3857}`, `{quadkey}`, `{r}` and more); custom request headers for keys and referers
- Offline MBTiles, PMTiles v3, GeoPackage tiles (any tile matrix), osmdroid, RMaps/BigPlanet and OsmAnd SQLite, and GeoTIFF/NITF/MrSID/JPEG2000/GeoPDF/CADRG via the optional GDAL module
- SQLite tile cache with offline mode and fallback, region download
- Mapbox Vector Tiles (TileJSON or MBTiles) rendered on the GPU with place, peak and water labels
- `file://` URL templates for offline `{z}/{x}/{y}` tile folders
- Layer stacking with opacity, KML/KMZ ground overlays, arbitrary image overlays

## Terrain
- WGS84 quadtree, 33x33 post meshes with skirts, screen-space-error LOD, slope shading ("enhanced depth perception"), exaggeration, x-ray transparency
- Terrarium elevation tiles; Cesium terrain from `layer.json`, `{z}/{x}/{y}` templates or Cesium ion (quantized-mesh-1.0 and heightmap-1.0, tile availability, token refresh); DTED 0-3 and SRTM HGT folders, GDAL DEMs, EGM96 geoid (heights are HAE)
- Line of sight, viewshed, elevation profile, elevation heat map

## Vectors, labels and models
- Built-in world labels, styled like a satellite map with labels: country, state and province, and county lines, and the names of countries, states, provinces, counties, cities, towns and villages worldwide, placed by importance with decluttering. The same switch adds roads in satellite-map style (gold motorways and trunk roads, gray arterials and streets, dashed tunnels and paths), interstate, US, state and route shields, street names along the roads ("22nd Ave SE"), neighbourhood and suburb names, park, forest and reserve names, and lake, river and creek names. One switch (`Engine::setLabelsEnabled`, `otk_set_labels`), on by default. Borders and place names are inside the engine and work offline (about 9.5 MB, `-DOTK_LABELS=OFF` leaves them out); roads, street-level names and buildings come from OpenStreetMap vector tiles that are downloaded into the cache for the area you stop on when zoomed in (anywhere in the world; `Engine::setAutoDownloadLabels(false)` turns this off) or for a whole region (`Engine::downloadLabels`, `otk_download_labels`), and are then read from disk; `setLabelsSource` points them at an offline `.mbtiles`/`.pmtiles` file or another download server. The kernel's `otk::placesNear` looks up nearby towns from the built-in data.
- KML/KMZ (styles, style maps, icons, MultiGeometry, gx:Track, NetworkLink with view-based refresh, Model, extrude), GeoJSON (simplestyle), GPX, Shapefile (reprojected from its `.prj`: UTM, State Plane, Albers, Mercator), plus any OGR format via GDAL
- CoT with ATAK iconsets (`usericon`), range and bearing lines, dashed and dotted strokes
- 3D buildings built into the engine, currently off by default and left out of the layers panel (turn on with `Engine::setBuildingsEnabled(true)`, `otk_set_buildings` or viewer `--buildings 1`): OpenStreetMap footprints and heights from the same offline vector tiles as the roads, standing on the terrain, with roofs cut from the aerial imagery under them, gable and hip roofs on houses, and photographed facades (glass towers, office grids, brick, stone, painted siding and stucco with windows, metal sheds) chosen by height and size. They work anywhere OpenStreetMap has buildings, offline once an area is downloaded, with no map service or API key.
- 3D trees (switched off for now; the code is kept in `src/raster/canopy.cpp`, `buildCanopyMesh` and the building layer, and is turned on again in `GlobeRenderer::render`): tree heights from the Meta and WRI global canopy height map become individual trees coloured from the satellite imagery.
- Ground-clamped lines and polygons draped into the terrain texture, 3D lines, extruded walls and raised polygons, icons, SDF labels with decluttering, hit testing
- Zoom to content: `loadData` returns the features, overlays and models a file added with their extent, `featuresRegion`, `tilesetRegion` and `pointCloudRegion` give extents, and `viewForRegion` frames any region
- MGRS/UTM conversions and a draped MGRS grid
- glTF/GLB, OBJ/MTL and COLLADA models, OGC 3D Tiles 1.0/1.1 (tileset.json, .3tz, URLs, external and implicit tilesets, b3dm, i3dm, pnts, cmpt, glb/glTF; Draco, meshopt and KTX2 compression; GPU instancing), LAS point clouds (WGS84 and UTM natively, any CRS through the GDAL module)
- Photorealistic 3D Tiles from Google and Cesium ion (`ion://` URIs, token refresh, on-screen credits) or your own photogrammetry; metallic-roughness materials with normal, emissive and occlusion maps, unlit photogrammetry, lighting by the real sun for a given time
- VR and AR hosts: `frameXr` renders any number of OpenXR-style eye views with one tile update per frame, table-top scale, side-by-side stereo ([docs/XR.md](docs/XR.md))

## Military
- MIL-STD-2525C and 2525D point symbols drawn by the engine: affiliation frames and colours for space, air, ground, sea surface, subsurface and SOF; planned and assumed friend/suspect/pending dashed frames; echelon, headquarters staff, task force, feint/dummy and installation amplifiers; joker/faker/exercise markers; common unit, equipment, air, sea and subsurface icons. CoT `a-` types (and `Style::sidc`) become 2525 icons automatically ([sheet](screenshots/otkglobeengine/22_mil_std_2525_symbols.png)).
- WMM2025 magnetic declination and inclination (matches the NOAA test values), UTM grid convergence, bearings in true/magnetic/grid north, degrees or NATO mils
- Coordinates: decimal, DM, DMS, MGRS (including polar UPS), UTM/UPS and GARS formatting, and a parser that accepts all of them
- Vincenty geodesics (inverse, direct, densified paths, area), range rings, bullseye, range and bearing lines, sectors, ellipses and geodesic circles as features
- Topographic contours that follow the view: sienna minor and index lines draped on the terrain under the roads, elevations written along the index lines, an interval that steps with the zoom (100/500 m down to 10/50 m, or 400/2000 ft down to 40/200 ft) or a fixed one, redrawn for the visible area when the camera stops. One-off contours for a region as features, terrain slope shading, MGRS, lat/lon and GARS grids
- Sun and moon position, moon phase and illumination, BMNT/EENT and other twilight times, moonrise/moonset and ground illuminance in lux for night-vision planning
- UAS full-motion video metadata: MISB ST 0601 KLV from MPEG-TS or raw KLV becomes sensor and frame-centre tracks, image footprints and line of sight
- Tracks past their CoT stale time are drawn faded

## Loading behaviour
- Continuous level of detail: toward the edge of its detail range each surface takes on its parent's colours while keeping its own fine detail, so imagery levels coloured differently by the provider blend without tile edges, and refining or merging does not pop (`Engine::setLodMorph`, 0 to 1)
- Missing tiles are drawn from the nearest loaded ancestor, never blank; new and upgraded tiles cross-fade, and the most urgent decoded tiles reach the GPU first
- Parent, child and zoom-out prefetch inside a GPU memory budget (by default 35% of video memory on desktops, 16% of RAM on phones and tablets)
- Separate local (cache, files) and network loader pools
- Airgap-ready cache: expired tiles shown instantly and refreshed in the background, unreachable hosts fail fast and are re-probed, pinned region downloads that wait out outages, size limit, MBTiles/GeoPackage export and import into the cache, built-in world backdrop where nothing is cached ([docs/OFFLINE.md](docs/OFFLINE.md))

## Size and speed
- The whole engine is about 4 MB (Windows DLL; Android arm64 library 4 MB, 1.8 MB compressed; the Android viewer APK is 1.8 MB) including SQLite, image decoders, the label font and the geoid; the built-in world labels add 9.5 MB (`-DOTK_LABELS=OFF` leaves them out)
- CPU time per frame on a 2015 desktop i5: 0.2 ms (globe), 0.5 ms (street), 0.9 ms (tilted terrain), 1.1 ms (first person)

## Bindings
- C ABI `include/otk/otk.h` and `otkglobeengine_c` shared library
- Android AAR with `OTKGlobeView` (`scripts/android_aar.py`), Swift `OTKGlobeView` and xcframework (`scripts/apple_xcframework.sh`), C# wrapper (`bindings/dotnet`)

## Kernel and engine libraries

The project builds two libraries, matching the split between the TAK kernel and the TAK map engine:

- **Kernel** (`otk_kernel`): GPU-free geospatial and data services that servers, tools and headless hosts can use without a display. Format detection, import and export (KML/KMZ, GeoJSON, GPX, Shapefile, CoT, CSV, WKT, GML, TAK data packages, ATAK iconsets, KLV); import planning (`planImport` decides how any file or folder should be loaded); feature queries by region, radius, CoT type, name, attributes and time (`FeatureQuery`); elevation queries, line of sight and profiles from any elevation source (`ElevationService`); MGRS, UTM, UPS, GARS and coordinate parsing; Vincenty geodesics and range-and-bearing shapes; WMM magnetic model; EGM96 geoid; sun and moon; MIL-STD-2525 symbol images; tile cache, offline mode and network access; tile archives (MBTiles, PMTiles, GeoPackage, osmdroid/RMaps/OsmAnd); OBJ and COLLADA to glTF conversion.
- **Engine** (`otkglobeengine`): the renderer on top of the kernel: globe, imagery, terrain, features, labels, models, 3D Tiles, point clouds, grids, input, plus `Engine::open` to load anything `planImport` understands and zoom-to-content helpers.

`-DOTK_ENGINE=OFF` builds only the kernel and its tests, with no graphics dependencies (Linux needs only libcurl).

## Built-in controls

The engine draws a small, plain set of controls in the bottom-right corner: zoom in and out, a compass that points north and turns the map back to north when pressed, a 2D/3D button, and a layers button. The layers button opens the map list (Satellite, Topographic, Streets, Vector), switches for labels, contours and 3D terrain, and a button that downloads roads for the view. Along the bottom edge are a scale bar (press it for feet and miles) and the map credits (press them for the full list). Mouse, touch and pen all work, and the controls take a press before the map does.

- Turn them off: `engine.setControls(0)` (`otk_set_controls(e, 0)`, .NET `Controls = 0`, Swift `controls = 0`, Android `setControls(0)`, viewer `--controls 0`).
- Keep only some: pass a mask of `ControlZoom`, `ControlCompass`, `ControlTilt`, `ControlLayers`, `ControlScale` and `ControlAttribution` (`OTK_CONTROL_*` in C); `ControlAll` is the default.
- Leave them out of the build: `-DOTK_CONTROLS=OFF`. The calls stay and do nothing.
- Keep them clear of system bars or your own UI: `setControlsPadding(left, top, right, bottom)` in density-independent pixels (`otk_set_controls_padding`). Android's `OTKGlobeView` applies the window insets (navigation bar, taskbar) by itself until you set a padding.
- Change the map list: `setBasemaps({{"Name", layers}, ...})`; `setBasemap(i)` and `basemap()` pick and read the current one (`otk_set_basemap`, `otk_basemap`, `otk_basemap_count`, `otk_basemap_name`). `otk::defaultBasemaps()` is the built-in list.
- The code is `src/ui/controls.cpp` and its shader `src/ui/controls.glsl`. It uses only the public `Engine` API (`view`, `flyTo`, `input().zoomAnimated`, `setBasemap`, `setLabelsEnabled`, `setContours`, `setTerrainEnabled`, `downloadLabels`, `credits`), so an app can turn it off and build its own controls from the same calls.
- With the credits turned off, show `Engine::credits()` somewhere else: the default imagery, OpenStreetMap and the CC BY label data ask for attribution. Imagery layers carry it in `ImageryLayer::attribution`.
