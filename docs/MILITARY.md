# Military use

What the engine offers for tactical and mission-planning applications.

## In the engine

| Area | Capability | API |
| --- | --- | --- |
| Symbology | MIL-STD-2525C/D icons with frames, affiliation colours, echelon and modifiers; CoT `a-` types are symbolized automatically | `ote/milsym.hpp` |
| Coordinates | MGRS, UTM, UPS (polar MGRS), GARS, DD/DM/DMS, NATO mils; WMM2025 magnetic declination; grid convergence; true/magnetic/grid bearings | `ote/navigation.hpp`, `ote/mgrs.hpp` |
| Graphics | Range rings, bullseye, range and bearing lines, sectors, ellipses, geodesic circles; MGRS, lat/lon and GARS grids | `ote/navigation.hpp`, `Engine::setGridType` |
| Terrain analysis | Line of sight, viewshed, elevation profiles, contours, slope shading (helicopter landing zone screening) | `Engine::lineOfSight`, `computeViewshed`, `addContours`, `setTerrainSlope` |
| Illumination | Sun and moon azimuth/elevation, moon phase and illumination, BMNT/BMCT/EECT/EENT and astronomical twilight, moonrise/moonset, ground illuminance in lux for NVG planning | `ote/astro.hpp`, `ote_sun_moon_at`, `ote_sun_moon_times` |
| UAS full-motion video | MISB ST 0601 KLV from MPEG-TS or raw KLV: sensor track, frame-centre track, image footprints (from corner points or sensor pose and field of view), line of sight; checksum validated | `ote/klv.hpp`, `loadFeatures("flight.ts")` |
| Data | CoT, TAK data packages, KML/KMZ, GPX, GeoJSON, Shapefile, CSV, WKT, GML; DTED, SRTM, GeoTIFF, NITF, CADRG/CIB through GDAL | `docs/FORMATS.md` |
| Disconnected operations | Persistent cache with stale-while-revalidate, unreachable-host detection, region downloads, MBTiles/GeoPackage transfer, embedded world backdrop | `docs/OFFLINE.md` |
| 3D | Photogrammetry and LiDAR as 3D Tiles (Draco, KTX2, .3tz), LAS point clouds, glTF models with instancing | `docs/PHOTOREALISM.md` |
| VR / sand table | Head-mounted rendering for OpenXR hosts, table-top scale, side-by-side stereo | `docs/XR.md` |

Out of scope unless a licensed implementation is supplied: tactical data links (Link 16, VMF, JREAP) and other formats with controlled specifications.
