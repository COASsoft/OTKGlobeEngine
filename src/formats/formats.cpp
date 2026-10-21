#include "ote/formats.hpp"

#include "formats/adapters.hpp"
#include "ote/klv.hpp"
#include "gdal/gdal_sources.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace ote {

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::filesystem::path u8(const std::string& s) { return std::filesystem::u8path(s); }

std::string extensionOf(const std::string& name) { return lowerCopy(u8(name).extension().string()); }

std::vector<FormatInfo> buildFormats() {
    bool gdal = gdalAvailable();
    auto f = [](std::string id, std::string name, std::vector<std::string> ext, DataKind kind, bool imp, bool exp, bool needsGdal, bool available,
                std::string loadWith, std::string notes) {
        FormatInfo i;
        i.id = std::move(id);
        i.name = std::move(name);
        i.extensions = std::move(ext);
        i.kind = kind;
        i.canImport = imp;
        i.canExport = exp;
        i.requiresGdal = needsGdal;
        i.available = available;
        i.loadWith = std::move(loadWith);
        i.notes = std::move(notes);
        return i;
    };
    using K = DataKind;
    return {
        f("kml", "KML 2.2", {".kml"}, K::Vector, true, true, false, true, "importData / loadFeatures",
          "Placemarks, MultiGeometry, Style/StyleMap, icons, ExtendedData, TimeStamp/TimeSpan, GroundOverlay"),
        f("kmz", "KMZ", {".kmz"}, K::Vector, true, true, false, true, "importData / loadFeatures", "Zipped KML with icons and overlays"),
        f("geojson", "GeoJSON (RFC 7946)", {".geojson", ".json"}, K::Vector, true, true, false, true, "importData / loadFeatures",
          "All properties kept; simplestyle-spec colours; id preserved"),
        f("gpx", "GPX 1.1", {".gpx"}, K::Vector, true, true, false, true, "importData / loadFeatures", "Waypoints, routes, tracks, times"),
        f("shp", "Esri Shapefile", {".shp"}, K::Vector, true, false, false, true, "importData / loadFeatures",
          "Points, lines, polygons with holes, all DBF fields; .prj ignored (WGS84 assumed); export via GDAL"),
        f("cot", "Cursor on Target (CoT) XML", {".cot", ".xml"}, K::Vector, true, true, false, true, "importData / loadFeatures",
          "Events, drawn shapes (links, rectangles, ellipses), callsign, remarks, colours, ce/le, how; raw <detail> kept for round-trips"),
        f("tak-package", "TAK data package", {".zip", ".dpk"}, K::Package, true, true, false, true, "importData / loadFeatures",
          "MANIFEST/manifest.xml contents; nested CoT, KML, KMZ, GeoJSON, GPX, CSV, GML, Shapefile; attachments listed as warnings"),
        f("csv", "CSV (lat/lon or WKT column)", {".csv", ".tsv"}, K::Vector, true, true, false, true, "importData / loadFeatures",
          "Delimiter detection, quoted fields, all columns kept as attributes"),
        f("wkt", "Well-Known Text", {".wkt"}, K::Vector, true, true, false, true, "importData / loadFeatures",
          "POINT, LINESTRING, POLYGON, MULTI*, GEOMETRYCOLLECTION, Z; one geometry per line"),
        f("gml", "GML 2/3", {".gml"}, K::Vector, true, true, false, true, "importData / loadFeatures",
          "Simple features in WGS84 with correct EPSG:4326 axis order; other CRSs need GDAL"),
        f("ogr", "OGR vector sources (GeoPackage, FileGDB, MapInfo, DXF, SpatiaLite, PostGIS, S-57, …)",
          {".gpkg", ".gdb", ".tab", ".mif", ".dxf", ".sqlite", ".000"}, K::Vector, true, false, true, gdal, "importData / loadFeatures",
          "Any OGR driver in the linked GDAL build, reprojected to WGS84"),
        f("mvt", "Mapbox Vector Tiles / TileJSON", {".pbf", ".mvt"}, K::Tiles, true, false, false, true, "ImageryLayer type Mvt",
          "Fills, lines and labels rendered into the surface"),
        f("xyz", "XYZ / TMS / quadkey tile URLs", {}, K::Tiles, true, false, false, true, "ImageryLayer type Url",
          "{z}{x}{y}{-y}{q}{s} templates, Web Mercator or geographic"),
        f("wms", "OGC WMS 1.1.1 / 1.3.0", {}, K::Tiles, true, false, false, true, "ImageryLayer type Wms", "EPSG:3857 and EPSG:4326"),
        f("wmts", "OGC WMTS", {}, K::Tiles, true, false, false, true, "ImageryLayer type Wmts", "REST and KVP templates"),
        f("mbtiles", "MBTiles", {".mbtiles"}, K::Tiles, true, true, false, true, "ImageryLayer type MBTiles / downloadRegion",
          "Raster tiles; offline downloads write to the tile cache"),
        f("gpkg-tiles", "GeoPackage tiles", {".gpkg"}, K::Tiles, true, false, false, true, "ImageryLayer type GeoPackage",
          "3857 and 4326 tile matrix sets"),
        f("osmdroid", "osmdroid / RMaps / OsmAnd SQLite", {".sqlite", ".sqlitedb"}, K::Tiles, true, false, false, true, "ImageryLayer type Osmdroid",
          "ATAK/osmdroid tile archives and MOBAC RMaps, BigPlanet and OsmAnd exports"),
        f("mobac", "MOBAC custom map XML", {".xml"}, K::Tiles, true, false, false, true, "loadMobac", "customMapSource and customMultiLayerMapSource"),
        f("geotiff", "GeoTIFF / Cloud Optimized GeoTIFF", {".tif", ".tiff"}, K::Raster, true, false, true, gdal, "ImageryLayer type Gdal",
          "Any CRS reprojected on the fly; COG over /vsicurl/"),
        f("gdal-raster", "Other GDAL rasters (JPEG2000, NITF, CADRG/CIB RPF, ECW, MrSID, …)", {".jp2", ".ntf", ".nitf", ".toc", ".ecw", ".sid"},
          K::Raster, true, false, true, gdal, "ImageryLayer type Gdal", "Availability depends on drivers compiled into GDAL; ECW/MrSID need licensed plugins"),
        f("dted", "DTED level 0/1/2", {".dt0", ".dt1", ".dt2"}, K::Elevation, true, false, false, true, "addElevationDirectory", "Native reader"),
        f("hgt", "SRTM HGT", {".hgt"}, K::Elevation, true, false, false, true, "addElevationDirectory", "1 and 3 arc-second"),
        f("dem", "GDAL DEMs (GeoTIFF, IMG, BAG, …)", {".tif", ".img", ".bag"}, K::Elevation, true, false, true, gdal, "ElevationLayer type Gdal", ""),
        f("quantized-mesh", "Cesium quantized-mesh", {".terrain"}, K::Elevation, true, false, false, true, "ElevationLayer type QuantizedMesh", ""),
        f("terrarium", "Terrarium RGB elevation tiles", {".png"}, K::Elevation, true, false, false, true, "ElevationLayer type Terrarium", "Default online terrain"),
        f("gltf", "glTF 2.0 / GLB", {".gltf", ".glb"}, K::Model, true, false, false, true, "addModel", "Metallic-roughness PBR, normal/emissive/occlusion maps, KHR_materials_unlit, KHR_texture_transform, Draco, meshopt, KTX2, EXT_mesh_gpu_instancing"),
        f("3dtiles", "OGC 3D Tiles 1.0/1.1", {".json", ".b3dm", ".i3dm", ".pnts", ".cmpt", ".glb", ".subtree", ".3tz"}, K::Model, true, false, false,
          true, "addTileset", "Explicit and implicit tilesets, b3dm, i3dm, pnts, cmpt, glTF content, Draco/meshopt/KTX2, .3tz; Google Photorealistic 3D Tiles and Cesium ion (ion://) with credits"),
        f("obj", "Wavefront OBJ", {".obj"}, K::Model, false, false, false, false, "", "Planned adapter"),
        f("collada", "COLLADA", {".dae"}, K::Model, false, false, false, false, "", "Planned adapter (KMZ models use COLLADA)"),
        f("fbx", "Autodesk FBX", {".fbx"}, K::Model, false, false, false, false, "", "Proprietary; convert to glTF"),
        f("las", "ASPRS LAS 1.0–1.4", {".las"}, K::PointCloud, true, false, false, true, "addPointCloud", "Instanced point rendering"),
        f("laz", "LAZ (compressed LAS)", {".laz"}, K::PointCloud, false, false, false, false, "", "Needs LASzip (LGPL) as an optional module"),
        f("klv", "MISB ST 0601 UAS metadata (KLV, MPEG-TS)", {".ts", ".m2ts", ".klv"}, K::Vector, true, false, false, true,
          "importData / loadFeatures", "Sensor and frame-centre tracks, footprints and line of sight from FMV video or raw KLV"),
        f("cot-stream", "CoT over TCP/UDP/TLS (TAK Server, mesh SA)", {}, K::Stream, false, false, false, false, "",
          "Planned network adapter; CoT messages already parse with the cot adapter"),
    };
}

std::vector<std::uint8_t> readFile(const std::string& path, std::uint64_t limit, std::string& error) {
    std::vector<std::uint8_t> out;
    std::error_code ec;
    auto size = std::filesystem::file_size(u8(path), ec);
    if (ec) {
        error = "cannot read " + path;
        return out;
    }
    if (size > limit) {
        error = "file is " + std::to_string(size) + " bytes, over the import limit";
        return out;
    }
    std::ifstream in(u8(path), std::ios::binary);
    out.resize(static_cast<size_t>(size));
    if (size && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size))) {
        error = "cannot read " + path;
        out.clear();
    }
    return out;
}

std::string xmlRoot(const std::vector<std::uint8_t>& b) {
    size_t i = 0;
    while (i < b.size() && i < 4096) {
        size_t lt = std::string::npos;
        for (size_t k = i; k < b.size() && k < 4096; ++k)
            if (b[k] == '<') {
                lt = k;
                break;
            }
        if (lt == std::string::npos || lt + 1 >= b.size()) return {};
        char c = static_cast<char>(b[lt + 1]);
        if (c == '?' || c == '!') {
            i = lt + 2;
            continue;
        }
        std::string name;
        for (size_t k = lt + 1; k < b.size() && name.size() < 64; ++k) {
            char ch = static_cast<char>(b[k]);
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '>' || ch == '/') break;
            name += ch;
        }
        size_t colon = name.find(':');
        return lowerCopy(colon == std::string::npos ? name : name.substr(colon + 1));
    }
    return {};
}

}

const std::vector<FormatInfo>& formats() {
    static const std::vector<FormatInfo> list = buildFormats();
    return list;
}

const FormatInfo* findFormat(const std::string& id) {
    for (const FormatInfo& f : formats())
        if (f.id == id) return &f;
    return nullptr;
}

std::string detectFormat(const std::vector<std::uint8_t>& b, const std::string& nameHint) {
    std::string ext = extensionOf(nameHint);
    auto starts = [&](const char* magic) {
        size_t n = std::strlen(magic);
        return b.size() >= n && std::memcmp(b.data(), magic, n) == 0;
    };
    if (starts("PK\x03\x04")) {
        if (ext == ".kmz") return "kmz";
        if (ext == ".3tz") return "3dtiles";
        mz_zip_archive zip{};
        std::string kind = "tak-package";
        if (mz_zip_reader_init_mem(&zip, b.data(), b.size(), 0)) {
            bool manifest = false, kml = false;
            mz_uint count = std::min<mz_uint>(mz_zip_reader_get_num_files(&zip), 20000);
            for (mz_uint i = 0; i < count; ++i) {
                char name[512];
                mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
                std::string l = lowerCopy(name);
                manifest = manifest || l == "manifest/manifest.xml" || l == "manifest.xml";
                kml = kml || l == "doc.kml" || (l.size() > 4 && l.substr(l.size() - 4) == ".kml" && l.find('/') == std::string::npos);
            }
            mz_zip_reader_end(&zip);
            if (!manifest && kml) kind = "kmz";
        }
        return kind;
    }
    if (starts("SQLite format 3")) {
        if (ext == ".mbtiles") return "mbtiles";
        if (ext == ".gpkg") return "gpkg-tiles";
        if (ext == ".sqlite" || ext == ".sqlitedb") return "osmdroid";
        return "ogr";
    }
    if (starts("LASF")) return "las";
    if (starts("glTF")) return "gltf";
    if (starts("b3dm")) return "3dtiles";
    if (starts("II*") || starts("MM\x00*")) return "geotiff";
    if (b.size() >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0x27 && b[3] == 0x0A) return "shp";
    if (starts("UHL")) return "dted";
    if (starts("\x06\x0E\x2B\x34\x02\x0B\x01\x01\x0E\x01\x03\x01\x01")) return "klv";
    if ((ext == ".ts" || ext == ".m2ts") && b.size() >= 188 && b[0] == 0x47) return "klv";
    std::string root = xmlRoot(b);
    if (!root.empty()) {
        if (root == "kml") return "kml";
        if (root == "event" || root == "events") return "cot";
        if (root == "gpx") return "gpx";
        if (root == "featurecollection" || root == "featurecollectiontype" || ext == ".gml") return "gml";
        if (root == "missionpackagemanifest") return "tak-package";
        if (root == "custommapsource" || root == "custommultilayermapsource" || root == "customwmsmapsource") return "mobac";
    }
    size_t i = 0;
    while (i < b.size() && std::isspace(b[i])) ++i;
    if (i < b.size() && b[i] == '{') {
        std::string head(b.begin() + static_cast<std::ptrdiff_t>(i), b.begin() + static_cast<std::ptrdiff_t>(std::min(b.size(), i + 4096)));
        if (head.find("\"asset\"") != std::string::npos && head.find("\"root\"") != std::string::npos) return "3dtiles";
        if (head.find("\"tiles\"") != std::string::npos &&
            (head.find("quantized-mesh-1.0") != std::string::npos || head.find("heightmap-1.0") != std::string::npos))
            return "quantized-mesh";
        if (head.find("\"tiles\"") != std::string::npos && head.find("\"tilejson\"") != std::string::npos) return "mvt";
        return "geojson";
    }
    if (ext == ".wkt") return "wkt";
    if (ext == ".csv" || ext == ".tsv") return "csv";
    if (ext == ".cot") return "cot";
    if (ext == ".hgt") return "hgt";
    if (ext == ".dt0" || ext == ".dt1" || ext == ".dt2") return "dted";
    if (ext == ".terrain") return "quantized-mesh";
    if (ext == ".las") return "las";
    if (ext == ".gltf") return "gltf";
    if (ext == ".obj") return "obj";
    if (ext == ".dae") return "collada";
    if (ext == ".fbx") return "fbx";
    if (ext == ".laz") return "laz";
    if (ext == ".jp2" || ext == ".ntf" || ext == ".nitf" || ext == ".toc" || ext == ".ecw" || ext == ".sid") return "gdal-raster";
    std::string text(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(b.size(), 2048)));
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (const char* w : {"POINT", "LINESTRING", "POLYGON", "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON", "GEOMETRYCOLLECTION"})
        if (upper.compare(upper.find_first_not_of(" \t\r\n") == std::string::npos ? 0 : upper.find_first_not_of(" \t\r\n"), std::strlen(w), w) == 0)
            return "wkt";
    size_t nl = text.find('\n');
    std::string first = lowerCopy(text.substr(0, nl));
    if ((first.find("lat") != std::string::npos && first.find("lon") != std::string::npos) || first.find("wkt") != std::string::npos) return "csv";
    for (const FormatInfo& f : formats())
        if (f.kind == DataKind::Vector)
            for (const std::string& e : f.extensions)
                if (e == ext && f.id != "cot") return f.id;
    return {};
}

std::string detectFormat(const std::string& path) {
    std::string ext = extensionOf(path);
    std::error_code ec;
    if (std::filesystem::is_directory(u8(path), ec)) {
        if (ext == ".gdb") return "ogr";
        return "dted";
    }
    std::ifstream in(u8(path), std::ios::binary);
    std::vector<std::uint8_t> head(65536);
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    if (ext == ".zip" || ext == ".dpk" || ext == ".kmz") {
        std::string err;
        std::vector<std::uint8_t> all = readFile(path, 64ull << 20, err);
        if (!all.empty()) return detectFormat(all, path);
    }
    return detectFormat(head, path);
}

ImportResult importBytes(const std::vector<std::uint8_t>& bytes, const std::string& formatId, const ImportLimits& limits) {
    ImportResult res;
    res.format = formatId.empty() ? detectFormat(bytes) : formatId;
    if (bytes.size() > limits.maxFileBytes) {
        res.error = "input over the import size limit";
        return res;
    }
    const std::string& f = res.format;
    if (f == "kml" || f == "kmz" || f == "gpx" || f == "geojson") {
        res.data = parseBytes(bytes, f == "geojson" ? ".geojson" : "." + f);
    } else if (f == "klv") {
        res.data.features = uasFeatures(demuxUasMetadata(bytes.data(), bytes.size()));
        if (res.data.features.empty()) res.warnings.push_back("no MISB ST 0601 packets found");
    } else if (f == "cot") {
        res.data.features = adapters::parseCot(bytes, res.warnings);
    } else if (f == "csv") {
        res.data.features = adapters::parseCsv(bytes, res.warnings);
    } else if (f == "wkt") {
        res.data.features = adapters::parseWktText(bytes, res.warnings);
    } else if (f == "gml") {
        res.data.features = adapters::parseGml(bytes, res.warnings);
    } else if (f == "tak-package") {
        return adapters::importPackage(bytes, limits, 0);
    } else {
        const FormatInfo* info = findFormat(f);
        res.error = info ? info->name + " is not a feature format; load it with " + (info->loadWith.empty() ? "a future adapter" : info->loadWith)
                         : "unrecognised data";
        return res;
    }
    if (res.data.features.size() > limits.maxFeatures) {
        res.data.features.resize(static_cast<size_t>(limits.maxFeatures));
        res.warnings.push_back("feature limit reached");
    }
    res.ok = true;
    return res;
}

ImportResult importData(const std::string& path, const std::string& formatId, const ImportLimits& limits) {
    ImportResult res;
    std::string f = formatId.empty() ? detectFormat(path) : formatId;
    res.format = f;
    if (f == "shp" || f == "ogr" || (f.empty() && gdalAvailable())) {
        res.data = parseFile(path);
        res.ok = !res.data.features.empty();
        if (!res.ok) res.error = f == "ogr" && !gdalAvailable() ? "this build has no GDAL" : "no features read";
        if (res.format.empty()) res.format = "ogr";
        return res;
    }
    if (f == "klv") {
        res.data.features = uasFeatures(readUasMetadata(path));
        res.ok = !res.data.features.empty();
        if (!res.ok) res.error = "no MISB ST 0601 packets found";
        return res;
    }
    std::string err;
    std::vector<std::uint8_t> bytes = readFile(path, limits.maxFileBytes, err);
    if (!err.empty()) {
        res.error = err;
        return res;
    }
    if (f == "kml") {
        res.data = parseFile(path);
        res.ok = true;
        return res;
    }
    return importBytes(bytes, f, limits);
}

std::vector<std::uint8_t> exportBytes(const std::vector<Feature>& features, const std::string& formatId, ExportResult* result) {
    ExportResult local;
    ExportResult& r = result ? *result : local;
    r.format = formatId;
    std::string text;
    if (formatId == "geojson") text = adapters::writeGeoJson(features, r);
    else if (formatId == "kml") text = adapters::writeKml(features, r);
    else if (formatId == "gpx") text = adapters::writeGpx(features, r);
    else if (formatId == "cot") text = adapters::writeCot(features, r);
    else if (formatId == "csv") text = adapters::writeCsv(features, r);
    else if (formatId == "wkt") text = adapters::writeWktText(features, r);
    else if (formatId == "gml") text = adapters::writeGml(features, r);
    else if (formatId == "tak-package") {
        std::vector<std::uint8_t> zip = adapters::writePackage(features, r);
        r.ok = !zip.empty();
        return zip;
    } else if (formatId == "kmz") {
        std::string kml = adapters::writeKml(features, r);
        mz_zip_archive zip{};
        std::vector<std::uint8_t> out;
        if (mz_zip_writer_init_heap(&zip, 0, 1 << 16)) {
            mz_zip_writer_add_mem(&zip, "doc.kml", kml.data(), kml.size(), MZ_DEFAULT_COMPRESSION);
            void* buf = nullptr;
            size_t size = 0;
            if (mz_zip_writer_finalize_heap_archive(&zip, &buf, &size) && buf) {
                out.assign(static_cast<std::uint8_t*>(buf), static_cast<std::uint8_t*>(buf) + size);
                mz_free(buf);
            }
            mz_zip_writer_end(&zip);
        }
        r.ok = !out.empty();
        return out;
    } else {
        r.error = "no exporter for '" + formatId + "'";
        return {};
    }
    r.ok = true;
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

ExportResult exportFeatures(const std::vector<Feature>& features, const std::string& path, const std::string& formatId) {
    ExportResult r;
    std::string f = formatId;
    if (f.empty()) {
        std::string ext = extensionOf(path);
        if (ext == ".geojson" || ext == ".json") f = "geojson";
        else if (ext == ".kml") f = "kml";
        else if (ext == ".kmz") f = "kmz";
        else if (ext == ".gpx") f = "gpx";
        else if (ext == ".cot" || ext == ".xml") f = "cot";
        else if (ext == ".csv") f = "csv";
        else if (ext == ".wkt") f = "wkt";
        else if (ext == ".gml") f = "gml";
        else if (ext == ".zip" || ext == ".dpk") f = "tak-package";
    }
    std::vector<std::uint8_t> bytes = exportBytes(features, f, &r);
    if (!r.ok) {
        if (r.error.empty()) r.error = "export failed";
        return r;
    }
    std::ofstream out(u8(path), std::ios::binary);
    if (!out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        r.ok = false;
        r.error = "cannot write " + path;
    }
    return r;
}

std::string formatsMarkdown() {
    auto kind = [](DataKind k) {
        switch (k) {
        case DataKind::Vector:
            return "Vector";
        case DataKind::Raster:
            return "Raster";
        case DataKind::Elevation:
            return "Elevation";
        case DataKind::Model:
            return "3D model";
        case DataKind::PointCloud:
            return "Point cloud";
        case DataKind::Tiles:
            return "Tiles / service";
        case DataKind::Package:
            return "Package";
        case DataKind::Stream:
            return "Stream";
        }
        return "";
    };
    std::string out = "| Format | Id | Kind | Import | Export | Needs GDAL | In this build | Load with | Notes |\n| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const FormatInfo& f : formats()) {
        std::string ext;
        for (const auto& e : f.extensions) ext += (ext.empty() ? "" : " ") + e;
        out += "| " + f.name + (ext.empty() ? "" : " (" + ext + ")") + " | `" + f.id + "` | " + kind(f.kind) + " | " +
               (f.canImport ? "yes" : "no") + " | " + (f.canExport ? "yes" : "no") + " | " + (f.requiresGdal ? "yes" : "no") + " | " +
               (f.available ? "yes" : "no") + " | " + (f.loadWith.empty() ? "-" : f.loadWith) + " | " + f.notes + " |\n";
    }
    return out;
}

}
