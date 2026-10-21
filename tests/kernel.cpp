#include "otk/elevation.hpp"
#include "otk/feature_query.hpp"
#include "otk/formats.hpp"
#include "otk/importer.hpp"
#include "otk/layers.hpp"
#include "otk/mgrs.hpp"
#include "otk/navigation.hpp"
#include "otk/places.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::string data(const char* name) { return std::string(OTK_TEST_DATA) + "/" + name; }

}

int main() {
    using namespace otk;

    ImportResult geo = importData(data("olympia.geojson"));
    check(geo.ok && geo.data.features.size() == 4, "geojson import");
    ImportResult back = importBytes(exportBytes(geo.data.features, "kml"), "kml");
    check(back.ok && back.data.features.size() == 4, "kml round trip");
    check(importData(data("sample.cot")).ok && importData(data("shapes.shp")).ok && importData(data("track.gpx")).ok, "cot, shapefile and gpx import");

    const std::vector<Feature>& f = geo.data.features;
    FeatureQuery near;
    near.useRadius = true;
    near.center = {47.0359, -122.9049, 0};
    near.radiusMeters = 300;
    check(queryFeatures(f, near).size() == 2, "features within 300 m, including the polygon around the point");
    near.radiusMeters = 1500;
    check(queryFeatures(f, near).size() == 3, "features within 1500 m, including the nearby road");
    FeatureQuery box;
    box.useRegion = true;
    box.region = {-122.91, 46.96, -122.89, 46.98};
    std::vector<std::size_t> inBox = queryFeatures(f, box);
    check(inBox.size() == 1 && f[inBox[0]].name == "Olympia Airport", "features in a region");
    FeatureQuery named;
    named.nameContains = "CAPITOL";
    check(queryFeatures(f, named).size() == 2, "features by name");
    std::vector<Feature> tagged = f;
    tagged[1].attributes.push_back({"unit", "alpha"});
    tagged[2].attributes.push_back({"unit", "bravo"});
    FeatureQuery attr;
    attr.attributes = {{"unit", "alpha"}};
    std::vector<std::size_t> byAttr = queryFeatures(tagged, attr);
    check(byAttr.size() == 1 && tagged[byAttr[0]].name == "Olympia Airport", "features by attribute");
    FeatureQuery wrap;
    wrap.useRegion = true;
    wrap.region = {179.0, -10.0, -179.0, 10.0};
    Feature dateline;
    dateline.geometries.push_back({GeometryType::Point, {{0.0, -179.5, 0}}, {}});
    check(matchesQuery(dateline, wrap) && !matchesQuery(f[0], wrap), "regions across the antimeridian");

    ElevationService elevation(2);
    ElevationLayer dem;
    dem.type = ElevationType::PMTiles;
    dem.path = data("dem-1234-z10.pmtiles");
    elevation.setSources({dem});
    double msl = elevation.heightMsl(44.928, -111.0885);
    check(std::abs(msl - 1234.0) < 0.05, "elevation service height above mean sea level");
    LineOfSight los = elevation.lineOfSight({44.928, -111.0885, 0}, 2.0, {44.94, -111.07, 0}, 2.0);
    check(los.visible && los.distance > 1000.0, "line of sight over flat terrain");
    std::vector<ProfileSample> profile = elevation.profile({{44.928, -111.0885, 0}, {44.94, -111.07, 0}}, 16);
    bool flat = profile.size() == 16;
    for (const ProfileSample& s : profile) flat = flat && std::abs(s.elevation - geoidOffset(s.lat, s.lon) - 1234.0) < 0.05;
    check(flat && profile.back().distance > 1000.0, "elevation profile");

    check(planImport(data("olympia.geojson")).target == ImportTarget::Features, "import plan for vector data");
    ImportPlan pm = planImport(data("dem-1234-z10.pmtiles"));
    check(pm.target == ImportTarget::Elevation && pm.elevation.size() == 1 && pm.elevation[0].type == ElevationType::PMTiles,
          "import plan for a pmtiles dem");
    check(pm.hasRegion && pm.region.south < 44.928 && pm.region.north > 44.928 && pm.region.west < -111.0885 && pm.region.east > -111.0885,
          "pmtiles extent from the header");
    ImportPlan mobac = planImport(data("mobac_multilayer_wms.xml"));
    check(mobac.target == ImportTarget::Imagery && mobac.imagery.size() == 2, "import plan for a mobac source");
    check(planImport(data("pyramid.gltf")).target == ImportTarget::Model, "import plan for a model");
    std::error_code ec;
    auto tileset = std::filesystem::temp_directory_path() / "otk_kernel_tileset";
    std::filesystem::create_directories(tileset, ec);
    std::ofstream(tileset / "tileset.json") << R"({"asset":{"version":"1.0"},"geometricError":10,"root":{"geometricError":0}})";
    ImportPlan ts = planImport(tileset.string());
    check(ts.target == ImportTarget::Tileset && ts.resource.size() > 12 && ts.resource.substr(ts.resource.size() - 12) == "tileset.json",
          "import plan for a 3d tiles folder");
    std::filesystem::remove_all(tileset, ec);
    auto dir = std::filesystem::temp_directory_path() / "otk_kernel_terrain";
    std::filesystem::create_directories(dir, ec);
    std::ofstream(dir / "layer.json") << R"({"format":"quantized-mesh-1.0","tiles":["{z}/{x}/{y}.terrain"],"bounds":[-123,46,-122,47]})";
    ImportPlan terrain = planImport(dir.string());
    check(terrain.target == ImportTarget::Elevation && terrain.elevation.size() == 1 &&
              terrain.elevation[0].type == ElevationType::QuantizedMesh,
          "import plan for a cesium terrain folder");
    check(terrain.hasRegion && terrain.region.west == -123 && terrain.region.north == 47, "cesium terrain extent from layer.json");
    std::filesystem::remove_all(dir, ec);
    ImportPlan missing = planImport(data("does-not-exist.kml"));
    check(missing.target == ImportTarget::Unsupported && !missing.message.empty(), "import plan for a missing file");

    double lat = 0, lon = 0;
    check(fromMgrs(toMgrs(47.0359, -122.9049), lat, lon) && std::abs(lat - 47.0359) < 1e-4 && std::abs(lon + 122.9049) < 1e-4,
          "mgrs round trip");
    GeodesicLine line = geodesicInverse(47.0359, -122.9049, 46.9694, -122.9027);
    check(std::abs(line.distance - 7393.0) < 15.0, "geodesic distance");

    std::string legacy = R"(<event version="2.0" uid="u1" type="a-f-G" how="m-g" time="2026-01-01T00:00:00Z" start="2026-01-01T00:00:00Z" )"
                         R"(stale="2026-01-02T00:00:00Z"><point lat="47" lon="-122" hae="0" ce="10" le="10"/><detail>)"
                         R"(<__ote_attributes><a k="unit" v="alpha"/></__ote_attributes></detail></event>)";
    auto hasUnit = [](const ImportResult& r) {
        if (!r.ok || r.data.features.size() != 1) return false;
        for (const auto& a : r.data.features[0].attributes)
            if (a.first == "unit" && a.second == "alpha") return true;
        return false;
    };
    ImportResult old = importBytes(std::vector<std::uint8_t>(legacy.begin(), legacy.end()), "cot");
    check(hasUnit(old), "cot attributes written by 0.1.0");
    check(hasUnit(importBytes(exportBytes(old.data.features, "cot"), "cot")), "cot detail written back unchanged");
    std::vector<Feature> fresh = old.data.features;
    std::erase_if(fresh[0].attributes, [](const auto& kv) { return kv.first == "cot:detail"; });
    std::vector<std::uint8_t> rewritten = exportBytes(fresh, "cot");
    std::string text(rewritten.begin(), rewritten.end());
    check(hasUnit(importBytes(rewritten, "cot")) && text.find("__otk_attributes") != std::string::npos, "cot attributes round trip");

#if defined(OTK_LABELS)
    check(placesAvailable(), "built-in labels data");
    {
        auto named = [](const std::vector<Place>& list, const char* name) {
            for (const Place& p : list)
                if (p.name == name) return true;
            return false;
        };
        std::vector<Place> towns = placesNear(47.0379, -122.9007, 20000.0, 50);
        check(named(towns, "Olympia") && !towns.empty() && towns.front().distanceMeters < 5000.0, "built-in place names near Olympia");
        check(named(placesNear(47.4, -120.5, 400000.0, 10, {PlaceKind::State}), "WASHINGTON"), "built-in state label");
        check(named(placesNear(39.5, -98.0, 800000.0, 5, {PlaceKind::Country}), "United States"), "built-in country label");
        std::vector<Place> capitals = placesNear(38.9, -77.03, 30000.0, 20);
        bool capital = false;
        for (const Place& p : capitals) capital = capital || (p.capital && p.name == "Washington");
        check(capital, "built-in national capital");
    }
#endif

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
