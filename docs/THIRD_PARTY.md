# Third-party components

OpenTAKEngine's own code is under the Apache License 2.0 ([LICENSE](../LICENSE)): use it for anything, change it and sell it, in open or closed products, without having to share your changes. Copies you pass on keep the license text, and files you changed say so.

The libraries and data built into the engine keep their own licenses. Most are public domain or zlib and ask for nothing. The MIT, ISC, BSD and Apache ones ask that their license text ships with copies of the software. Putting this file and the linked license texts in a product's "licenses" screen or folder covers that. Nothing here requires naming OpenTAKEngine.

| Component | Used for | License | In a binary you ship |
| --- | --- | --- | --- |
| [sokol](https://github.com/floooh/sokol) | Graphics, app shell of the demo viewer | zlib | Nothing required |
| [stb](https://github.com/nothings/stb) | PNG/JPEG decoding, font rasterising | Public domain (or MIT) | Nothing required |
| [SQLite](https://sqlite.org) | Tile cache, MBTiles, GeoPackage | Public domain | Nothing required |
| [pugixml](https://github.com/zeux/pugixml) | KML, GPX, GML, CoT, MOBAC | MIT | License text |
| [nlohmann/json](https://github.com/nlohmann/json) | GeoJSON, 3D Tiles, TileJSON | MIT | License text |
| [miniz](https://github.com/richgel999/miniz) | KMZ, data packages, .3tz | MIT | License text |
| [earcut.hpp](https://github.com/mapbox/earcut.hpp) | Polygon triangulation | ISC | License text |
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF parsing | MIT | License text |
| [meshoptimizer](https://github.com/zeux/meshoptimizer) decoder | `EXT_meshopt_compression` | MIT | License text; leave out with `-DOTE_WITH_MESHOPT=OFF` |
| [Draco](https://github.com/google/draco) decoder | `KHR_draco_mesh_compression`, Draco point clouds | Apache-2.0 | License text; leave out with `-DOTE_WITH_DRACO=OFF` |
| [Basis Universal](https://github.com/BinomialLLC/basis_universal) transcoder and its Zstandard decoder | KTX2 / `KHR_texture_basisu` | Apache-2.0, Zstandard BSD-3-Clause | License texts; leave out with `-DOTE_WITH_KTX2=OFF` |
| Noto Sans Bold (subset) | Labels | SIL Open Font License 1.1 ([OFL.txt](../assets/fonts/OFL.txt)) | OFL text with the font |
| NASA Blue Marble | Offline backdrop | Public domain | Nothing required; leave out with `-DOTE_EMBED_BACKDROP=OFF` |
| EGM96 geoid grid (NGA) | Height conversion | Public domain | Nothing required |
| WMM2025 coefficients (NOAA/NCEI) | Magnetic declination | Public domain | Nothing required |
| [GDAL](https://gdal.org) | Optional raster/vector module, off by default | MIT/X11 | License text when `-DOTE_WITH_GDAL=ON` |
| libcurl (Linux only) | HTTP | curl (MIT-style) | License text |

Build tools that do not end up in the binary (CMake, Ninja, sokol-shdc) are not listed.

The online map services that the demo presets point at (Esri World Imagery, OpenStreetMap, USGS, NASA GIBS, AWS Terrain Tiles) have their own terms of use, for example OpenStreetMap's attribution requirement. Those apply to using the services, not to the engine's code; an airgapped deployment with its own data is not affected.
