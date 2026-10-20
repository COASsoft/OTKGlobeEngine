# Military use

What the engine offers for tactical and mission-planning applications today, and what would add the most value next.

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

## Worth adding next

Ranked by how often the capability is needed in the field against the effort it takes in this codebase.

| # | Capability | Why it matters | Effort |
| --- | --- | --- | --- |
| 1 | MIL-STD-2525 tactical graphics (phase lines, boundaries, axes of advance, obstacles, fire support areas, airspace control measures) | The common operational picture and every operations order use them; icons alone do not cover planning overlays. | Large |
| 2 | FMV video draped on terrain | Completes the UAS workflow: decode H.264/H.265 frames with the platform decoder and project them through the KLV footprint already computed. | Medium |
| 3 | Measuring and picking on 3D Tiles | Precise grid coordinates and heights from photogrammetry for fires and CAS, with an estimate of target location error. | Medium |
| 4 | Encrypted storage | Data at rest (tile cache, imported packages) encrypted with FIPS 140-3 validated crypto; most accreditations require it. | Medium |
| 5 | Security markings | Classification banners from MISB ST 0102 security metadata and package metadata; declutter rules that never hide markings. | Small |
| 6 | Night and NVG display modes | Red or NVG-compatible low-luminance palettes, display dimming, and a simulated NVG/thermal view for planning with the illumination data. | Small |
| 7 | 3D airspace volumes | Extruded polygons and corridors for ROZs, restricted areas and ACO/ATO measures, with altitude labels. | Medium |
| 8 | Terrain analysis against 3D Tiles and DSM | Line of sight, viewshed and HLZ obstruction checks against buildings and vegetation, not only bare earth. | Medium |
| 9 | MISB ST 0903 VMTI | Moving-target detections from the video metadata as tracks on the map. | Small |
| 10 | Live CoT transport | TLS client for TAK Server and mesh SA multicast (239.2.3.1:6969); CoT parsing is in place, the transport is application code today. | Medium |
| 11 | Embedded global elevation | A coarse public-domain DEM in the binary so relief, line of sight and profiles work offline from the first start. | Small |
| 12 | METOC overlays | GRIB2 wind, cloud and visibility layers for planning. | Medium |

Out of scope unless a licensed implementation is supplied: tactical data links (Link 16, VMF, JREAP) and other formats with controlled specifications.
