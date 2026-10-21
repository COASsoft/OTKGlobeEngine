#include "otk/importer.hpp"

#include "db/sqlite.hpp"
#include "gdal/gdal_sources.hpp"
#include "otk/formats.hpp"
#include "raster/pmtiles.hpp"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>

namespace otk {

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

bool hasWord(const std::string& text, std::initializer_list<const char*> words) {
    std::string l = lowered(text);
    std::string token;
    auto match = [&] {
        for (const char* w : words)
            if (token == w) return true;
        return false;
    };
    for (char c : l) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            token.push_back(c);
            continue;
        }
        if (match()) return true;
        token.clear();
    }
    return match();
}

bool pmtilesElevation(const std::string& path, const std::string& name, HeightEncoding& encoding) {
    encoding = HeightEncoding::Terrarium;
    std::string meta;
    if (auto archive = PmTilesArchive::open(path)) {
        if (archive->vector()) return false;
        meta = archive->metadata();
    }
    if (containsAny(meta, {"terrain-rgb", "terrainrgb"})) {
        encoding = HeightEncoding::MapboxRgb;
        return true;
    }
    return containsAny(meta, {"terrarium"}) || hasWord(meta, {"elevation", "dem", "dtm", "heightmap"}) ||
           hasWord(name, {"dem", "dtm", "dsm", "srtm", "elevation", "elev", "heightmap", "terrarium", "heights"});
}

bool usefulRegion(const Region& r) {
    return std::isfinite(r.west) && std::isfinite(r.south) && std::isfinite(r.east) && std::isfinite(r.north) && r.north > r.south &&
           r.north - r.south < 170.0 && r.south >= -90.0 && r.north <= 90.0 && r.west >= -180.0 && r.east <= 180.0;
}

bool mbtilesRegion(const std::string& path, Region& out) {
    db::Database database;
    if (!database.open(path, true)) return false;
    db::Statement st = database.prepare("SELECT value FROM metadata WHERE name = 'bounds'");
    if (!st.valid() || !st.step()) return false;
    return std::sscanf(st.text(0).c_str(), "%lf,%lf,%lf,%lf", &out.west, &out.south, &out.east, &out.north) == 4;
}

bool geopackageRegion(const std::string& path, Region& out) {
    db::Database database;
    if (!database.open(path, true)) return false;
    db::Statement st = database.prepare("SELECT min_x, min_y, max_x, max_y, srs_id FROM gpkg_contents WHERE data_type = 'tiles' AND min_x IS NOT NULL");
    if (!st.valid() || !st.step()) return false;
    double x0 = st.real(0), y0 = st.real(1), x1 = st.real(2), y1 = st.real(3);
    const int srs = static_cast<int>(st.real(4));
    if (srs == 4326) {
        out = {x0, y0, x1, y1};
        return true;
    }
    if (srs != 3857 && srs != 900913) return false;
    constexpr double r = 6378137.0, deg = 180.0 / 3.14159265358979323846;
    auto lat = [&](double y) { return (2.0 * std::atan(std::exp(y / r)) - 3.14159265358979323846 / 2.0) * deg; };
    out = {x0 / r * deg, lat(y0), x1 / r * deg, lat(y1)};
    return true;
}

bool layerJsonRegion(const std::filesystem::path& file, Region& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.contains("bounds") || !j["bounds"].is_array() || j["bounds"].size() != 4) return false;
    for (const auto& v : j["bounds"])
        if (!v.is_number()) return false;
    out = {j["bounds"][0].get<double>(), j["bounds"][1].get<double>(), j["bounds"][2].get<double>(), j["bounds"][3].get<double>()};
    return true;
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
    Region r;
    bool found = false;
    if (id == "pmtiles") {
        if (auto archive = PmTilesArchive::open(path)) {
            const auto& h = archive->header();
            r = {h.west, h.south, h.east, h.north};
            found = true;
        }
    } else if (id == "mbtiles") {
        found = mbtilesRegion(path, r);
    } else if (id == "gpkg-tiles") {
        found = geopackageRegion(path, r);
    } else if (id == "quantized-mesh") {
        found = layerJsonRegion(folder ? p / "layer.json" : p, r);
    }
    if (found && usefulRegion(r)) {
        plan.region = r;
        plan.hasRegion = true;
    }
    return plan;
}

}
