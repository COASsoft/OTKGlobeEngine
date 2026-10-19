# Data interoperability

OpenTAKEngine reads and writes data through a registry of format adapters (`ote/formats.hpp`). Each adapter declares what it is, which extensions it uses, whether it imports and exports, and whether it needs the optional GDAL module. Imported content becomes engine `Feature`s, independent of rendering, so it can be stored, queried, transformed, rendered and exported without re-parsing.

```cpp
ote::ImportResult in = ote::importData("mission.zip");          // format detected from content
ote::ExportResult out = ote::exportFeatures(in.data.features, "mission.kml");
for (const auto& note : out.lossy) puts(note.c_str());           // explicit list of what the target cannot hold
```

`Engine::loadFeatures` uses the same path, so every vector format below loads with one call, and `Engine::exportFeatures` writes any of the exporters. The C ABI exposes `ote_export_features`, `ote_detect_format` and `ote_formats_markdown`.

## Detection

Formats are detected from content first and extension second: zip archives are told apart by their entries (TAK `MANIFEST/manifest.xml`, KMZ `doc.kml`), XML by its root element (`kml`, `event`/`events`, `gpx`, GML feature collections, MOBAC sources), JSON by its members (GeoJSON, 3D Tiles `tileset.json`, TileJSON), and binaries by magic numbers (SQLite, LAS, glTF, b3dm, TIFF, Shapefile, DTED). CSV and WKT are recognised from their first lines.

## Metadata preservation

* Every feature carries a stable `uid`, a CoT `type`, ordered string `attributes`, and `time`, `start` and `stale` (UTC seconds).
* KML: Placemark `id`, `ExtendedData` (`Data` and `SchemaData`), `TimeStamp`, `TimeSpan`, styles, altitude mode.
* GeoJSON: `id`, every property (non-string values kept as JSON text), simplestyle colours.
* GPX: name, description, type, symbol, comment, waypoint time, track start and end.
* Shapefile: every DBF field.
* CoT: uid, type, how, time/start/stale, ce/le, callsign, remarks, colours, drawn shapes, and the raw `<detail>` block, which is written back unchanged so CoT round-trips are lossless. Non-CoT attributes ride in a private `<__ote_attributes>` element that TAK clients ignore.
* CSV: all columns that are not coordinates, name, description, uid, type or time become attributes.
* GML: feature `gml:id`, simple property elements, WGS84 geometry with the correct EPSG:4326 axis order.

Round trips through GeoJSON, KML, GML, CSV, CoT and TAK data packages are covered by `ote_unit` and keep uid, type, attributes, times and polygon holes. Exports report every loss in `ExportResult::lossy` (for example GPX has no polygons or attributes, WKT keeps geometry only).

## Untrusted input

* Files over 1 GiB, archives over 2 GiB uncompressed or 20,000 entries, and archive nesting deeper than three levels are refused (`ImportLimits`).
* Archive entries with absolute paths, drive letters or `..` are skipped.
* XML is parsed without DTD processing, so external entities are never fetched.
* Feature counts are capped at five million per import.

## Compatibility matrix

"Needs GDAL" formats are available when the engine is built with `-DOTE_WITH_GDAL=ON` (the Linux CI build does); availability then depends on the drivers compiled into that GDAL. At runtime `formats()` and `formatsMarkdown()` report the formats present in the running build.

| Format | Id | Kind | Import | Export | Needs GDAL | Load with | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| KML 2.2 (.kml) | `kml` | Vector | yes | yes | no | importData / loadFeatures | Placemarks, MultiGeometry, Style/StyleMap, icons, ExtendedData, TimeStamp/TimeSpan, GroundOverlay |
| KMZ (.kmz) | `kmz` | Vector | yes | yes | no | importData / loadFeatures | Zipped KML with icons and overlays |
| GeoJSON (RFC 7946) (.geojson .json) | `geojson` | Vector | yes | yes | no | importData / loadFeatures | All properties kept; simplestyle-spec colours; id preserved |
| GPX 1.1 (.gpx) | `gpx` | Vector | yes | yes | no | importData / loadFeatures | Waypoints, routes, tracks, times |
| Esri Shapefile (.shp) | `shp` | Vector | yes | no | no | importData / loadFeatures | Points, lines, polygons with holes, all DBF fields; .prj ignored (WGS84 assumed); export via GDAL |
| Cursor on Target (CoT) XML (.cot .xml) | `cot` | Vector | yes | yes | no | importData / loadFeatures | Events, drawn shapes (links, rectangles, ellipses), callsign, remarks, colours, ce/le, how; raw <detail> kept for round-trips |
| TAK data package (.zip .dpk) | `tak-package` | Package | yes | yes | no | importData / loadFeatures | MANIFEST/manifest.xml contents; nested CoT, KML, KMZ, GeoJSON, GPX, CSV, GML, Shapefile; attachments listed as warnings |
| CSV (lat/lon or WKT column) (.csv .tsv) | `csv` | Vector | yes | yes | no | importData / loadFeatures | Delimiter detection, quoted fields, all columns kept as attributes |
| Well-Known Text (.wkt) | `wkt` | Vector | yes | yes | no | importData / loadFeatures | POINT, LINESTRING, POLYGON, MULTI*, GEOMETRYCOLLECTION, Z; one geometry per line |
| GML 2/3 (.gml) | `gml` | Vector | yes | yes | no | importData / loadFeatures | Simple features in WGS84 with correct EPSG:4326 axis order; other CRSs need GDAL |
| OGR vector sources (GeoPackage, FileGDB, MapInfo, DXF, SpatiaLite, PostGIS, S-57, …) (.gpkg .gdb .tab .mif .dxf .sqlite .000) | `ogr` | Vector | yes | no | yes | importData / loadFeatures | Any OGR driver in the linked GDAL build, reprojected to WGS84 |
| Mapbox Vector Tiles / TileJSON (.pbf .mvt) | `mvt` | Tiles / service | yes | no | no | ImageryLayer type Mvt | Fills, lines and labels rendered into the surface |
| XYZ / TMS / quadkey tile URLs | `xyz` | Tiles / service | yes | no | no | ImageryLayer type Url | {z}{x}{y}{-y}{q}{s} templates, Web Mercator or geographic |
| OGC WMS 1.1.1 / 1.3.0 | `wms` | Tiles / service | yes | no | no | ImageryLayer type Wms | EPSG:3857 and EPSG:4326 |
| OGC WMTS | `wmts` | Tiles / service | yes | no | no | ImageryLayer type Wmts | REST and KVP templates |
| MBTiles (.mbtiles) | `mbtiles` | Tiles / service | yes | yes | no | ImageryLayer type MBTiles / downloadRegion | Raster tiles; offline downloads write to the tile cache |
| GeoPackage tiles (.gpkg) | `gpkg-tiles` | Tiles / service | yes | no | no | ImageryLayer type GeoPackage | 3857 and 4326 tile matrix sets |
| osmdroid SQLite (.sqlite) | `osmdroid` | Tiles / service | yes | no | no | ImageryLayer type Osmdroid | ATAK/osmdroid tile archives |
| MOBAC custom map XML (.xml) | `mobac` | Tiles / service | yes | no | no | loadMobac | customMapSource and customMultiLayerMapSource |
| GeoTIFF / Cloud Optimized GeoTIFF (.tif .tiff) | `geotiff` | Raster | yes | no | yes | ImageryLayer type Gdal | Any CRS reprojected on the fly; COG over /vsicurl/ |
| Other GDAL rasters (JPEG2000, NITF, CADRG/CIB RPF, ECW, MrSID, …) (.jp2 .ntf .nitf .toc .ecw .sid) | `gdal-raster` | Raster | yes | no | yes | ImageryLayer type Gdal | Availability depends on drivers compiled into GDAL; ECW/MrSID need licensed plugins |
| DTED level 0/1/2 (.dt0 .dt1 .dt2) | `dted` | Elevation | yes | no | no | addElevationDirectory | Native reader |
| SRTM HGT (.hgt) | `hgt` | Elevation | yes | no | no | addElevationDirectory | 1 and 3 arc-second |
| GDAL DEMs (GeoTIFF, IMG, BAG, …) (.tif .img .bag) | `dem` | Elevation | yes | no | yes | ElevationLayer type Gdal |  |
| Cesium quantized-mesh (.terrain) | `quantized-mesh` | Elevation | yes | no | no | ElevationLayer type QuantizedMesh |  |
| Terrarium RGB elevation tiles (.png) | `terrarium` | Elevation | yes | no | no | ElevationLayer type Terrarium | Default online terrain |
| glTF 2.0 / GLB (.gltf .glb) | `gltf` | 3D model | yes | no | no | addModel | PBR base colour, textures, node transforms |
| OGC 3D Tiles 1.0/1.1 (.json .b3dm .glb .3tz) | `3dtiles` | 3D model | yes | no | no | addTileset | tileset.json, b3dm, glb content, external tilesets, .3tz archives |
| Wavefront OBJ (.obj) | `obj` | 3D model | no | no | no | - | Planned adapter |
| COLLADA (.dae) | `collada` | 3D model | no | no | no | - | Planned adapter (KMZ models use COLLADA) |
| Autodesk FBX (.fbx) | `fbx` | 3D model | no | no | no | - | Proprietary; convert to glTF |
| ASPRS LAS 1.0–1.4 (.las) | `las` | Point cloud | yes | no | no | addPointCloud | Instanced point rendering |
| LAZ (compressed LAS) (.laz) | `laz` | Point cloud | no | no | no | - | Needs LASzip (LGPL) as an optional module |
| CoT over TCP/UDP/TLS (TAK Server, mesh SA) | `cot-stream` | Stream | no | no | no | - | Planned network adapter; CoT messages already parse with the cot adapter |

## Military and government formats

* NITF, CADRG/CIB (RPF `A.TOC`), DTED, ADRG and S-57 load through GDAL's drivers; DTED and SRTM HGT also have native readers.
* MIL-STD-2525 / APP-6 symbology is rendering, not a data format; CoT `type` strings are preserved so a symbology module can draw them.
* VMAP/VPF has no GDAL driver; it needs OGDI and is not supported.
* Formats without public specifications or with export-controlled specifications are out of scope unless a licensed implementation is supplied.

## Licensing

GDAL (MIT/X11), pugixml (MIT), nlohmann/json (MIT), miniz (MIT), stb (public domain/MIT) and SQLite (public domain) are linked. LAZ would need LASzip (LGPL) as an optional module; ECW and MrSID need their vendors' SDKs through GDAL. No ATAK (GPLv3) code is used.

## Planned adapters

OBJ and COLLADA models, LAZ point clouds, and a CoT network adapter (TCP/UDP/TLS to TAK Server and mesh SA). CoT messages from any transport already parse with the `cot` adapter.
