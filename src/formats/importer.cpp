#include "ote/importer.hpp"

#include "gdal/gdal_sources.hpp"
#include "ote/formats.hpp"
#include "raster/pmtiles.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>

namespace ote {

namespace {

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool containsAny(const std::string& text, std::initializer_list<const char*> words) {
    std::string l = lowered(text);
    for (const char* w : words)
        if (l.find(w) != std::string::npos) return true;
    return false;
}

std::string utf8(const std::filesystem::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool pmtilesElevation(const std::string& path, const std::string& name, HeightEncoding& encoding) {
    encoding = HeightEncoding::Terrarium;
    std::string meta;
    if (auto archive = PmTilesArchive::open(path)) {
        if (archive->vector()) return false;
        meta = lowered(archive->metadata());
    }
    if (containsAny(meta, {"terrain-rgb", "terrainrgb", "mapbox"}) && containsAny(meta, {"terrain", "elevation", "dem"})) {
        encoding = HeightEncoding::MapboxRgb;
        return true;
    }
    return containsAny(meta, {"terrarium", "elevation", "\"dem\"", "terrain"}) ||
           containsAny(name, {"dem", "dtm", "dsm", "terrain", "elev", "height", "srtm"});
}

}

ImportPlan planImport(const std::string& path) {
    ImportPlan plan;
    plan.path = path;
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(path);
    plan.name = utf8(p.filename());
    if (plan.name.empty()) plan.name = utf8(p.parent_path().filename());
    const bool folder = std::filesystem::is_directory(p, ec);
    if (folder) {
        if (std::filesystem::exists(p / "layer.json", ec)) plan.format = "quantized-mesh";
        else if (std::filesystem::exists(p / "tileset.json", ec)) plan.format = "3dtiles";
        else plan.format = "dted";
    } else if (!std::filesystem::exists(p, ec)) {
        plan.message = "not found";
        return plan;
    } else {
        plan.format = detectFormat(path);
    }
    const std::string& id = plan.format;
    const FormatInfo* info = findFormat(id);
    auto imagery = [&](SourceType type) {
        ImageryLayer l;
        l.type = type;
        l.path = path;
        l.name = plan.name;
        plan.imagery.push_back(l);
        plan.target = ImportTarget::Imagery;
    };
    auto elevation = [&](ElevationType type, const std::string& source) {
        ElevationLayer e;
        e.type = type;
        e.path = source;
        e.name = plan.name;
        plan.elevation.push_back(e);
        plan.target = ImportTarget::Elevation;
        return &plan.elevation.back();
    };
    if (id == "iconset") {
        plan.target = ImportTarget::Iconset;
        plan.resource = path;
    } else if (id == "mbtiles") {
        imagery(SourceType::MBTiles);
    } else if (id == "gpkg-tiles") {
        imagery(SourceType::GeoPackage);
    } else if (id == "osmdroid") {
        imagery(SourceType::Osmdroid);
    } else if (id == "pmtiles") {
        HeightEncoding encoding;
        if (pmtilesElevation(path, plan.name, encoding)) elevation(ElevationType::PMTiles, path)->encoding = encoding;
        else imagery(SourceType::PMTiles);
    } else if (id == "mobac") {
        plan.imagery = loadMobac(path);
        plan.target = plan.imagery.empty() ? ImportTarget::Unsupported : ImportTarget::Imagery;
        if (plan.imagery.empty()) plan.message = "no map sources found";
    } else if (id == "geotiff" || id == "dem") {
        if (!gdalAvailable()) plan.message = "needs the GDAL module";
        else if (gdalIsElevation(path)) elevation(ElevationType::Gdal, path);
        else imagery(SourceType::Gdal);
    } else if (id == "ogr") {
        if (!gdalAvailable()) plan.message = "needs the GDAL module";
        else plan.target = ImportTarget::Features;
    } else if (id == "dted" || id == "hgt") {
        elevation(ElevationType::Directory, folder ? path : utf8(p.parent_path()))->maxZoom = 14;
    } else if (id == "quantized-mesh") {
        elevation(ElevationType::QuantizedMesh, path)->maxZoom = 16;
    } else if (id == "3dtiles") {
        plan.target = ImportTarget::Tileset;
        plan.resource = folder ? utf8(p / "tileset.json") : path;
    } else if (id == "las") {
        plan.target = ImportTarget::PointCloud;
        plan.resource = path;
    } else if (id == "gltf" || id == "obj" || id == "collada") {
        plan.target = ImportTarget::Model;
        plan.resource = path;
    } else if (info && (info->kind == DataKind::Vector || id == "tak-package")) {
        plan.target = ImportTarget::Features;
    } else {
        plan.message = info ? info->name + " is not supported for import" : "unknown format";
    }
    return plan;
}

}
