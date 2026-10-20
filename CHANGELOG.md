# Changelog

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
