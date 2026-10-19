#include "core/camera.hpp"
#include "core/geo.hpp"
#include "core/geoid.hpp"
#include "elevation/quantized_mesh.hpp"
#include "ote/features.hpp"
#include "ote/mgrs.hpp"
#include "raster/tile_loader.hpp"
#include "raster/tile_matrix.hpp"
#include "raster/vector_tile.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    } else {
        std::printf("ok   %s\n", what);
    }
}

std::string data(const char* name) { return std::string(OTE_TEST_DATA) + "/" + name; }

}

int main() {
    using namespace ote;

    for (auto [lat, lon, h] : {std::tuple{0.0, 0.0, 0.0}, {46.8529, -121.7604, 4392.0}, {-33.9, 151.2, -20.0}, {89.9, 45.0, 100.0}}) {
        GeoPoint g = ecefToGeodetic(geodeticToEcef(lat, lon, h));
        bool ok = std::abs(g.lat - lat) < 1e-9 && std::abs(g.lon - lon) < 1e-9 && std::abs(g.alt - h) < 1e-4;
        check(ok, ("ecef roundtrip " + std::to_string(lat)).c_str());
    }

    check(toMgrs(46.8529, -121.7604, 5) == "10T ES 94499 89563", "mgrs rainier");
    for (auto [lat, lon] : {std::pair{46.8529, -121.7604}, {-33.8688, 151.2093}, {60.0, 5.5}, {0.5, 0.5}, {-77.8, 166.7}}) {
        double rl, ro;
        bool ok = fromMgrs(toMgrs(lat, lon, 5), rl, ro) && std::abs(rl - lat) < 2e-5 && std::abs(ro - lon) < 2e-5;
        check(ok, ("mgrs roundtrip " + std::to_string(lat)).c_str());
    }
    check(utmZone(60.0, 5.5) == 32, "utm norway exception");
    check(utmBand(-80.0) == 'C' && utmBand(83.9) == 'X', "utm bands");

    TileMatrix wm = TileMatrix::webMercator(0, 19);
    double minX, minY, maxX, maxY;
    wm.tileBounds(1, 1, 0, minX, minY, maxX, maxY);
    check(std::abs(minX) < 1e-6 && std::abs(maxY - 20037508.342789244) < 1e-3, "web mercator tile bounds");
    auto ranges = regionRanges(wm, {-122.95, 47.0, -122.85, 47.08}, 10, 15);
    std::int64_t tiles = 0;
    for (auto& r : ranges) tiles += r.count();
    check(ranges.size() == 6 && tiles == 172, "region tile count");
    TileMatrix geo = TileMatrix::geographic(0, 10);
    check(geo.levels[0].matrixWidth == 2 && geo.levels[0].matrixHeight == 1, "geographic matrix level 0");

    Camera cam;
    cam.width = 1280;
    cam.height = 720;
    cam.focus = {47.0, -122.0, 0.0};
    cam.gsd = 10.0;
    cam.tilt = 45.0;
    cam.azimuth = 30.0;
    cam.update();
    Vec3 hit;
    bool picked = cam.screenToEcef(640, 360, hit);
    GeoPoint g = ecefToGeodetic(hit);
    check(picked && std::abs(g.lat - 47.0) < 1e-6 && std::abs(g.lon + 122.0) < 1e-6, "camera center pick");
    double sx, sy;
    check(cam.ecefToScreen(cam.focusEcef(), sx, sy) && std::abs(sx - 640) < 1e-3 && std::abs(sy - 360) < 1e-3, "camera project focus");
    cam.tilt = 89.0;
    cam.update();
    check(cam.tilt <= Camera::kMaxTilt, "camera tilt clamp");

    check(std::abs(geoidHeight(0, 0) - 17.2) < 0.5, "geoid at 0,0");
    check(std::abs(geoidHeight(47.0, -122.0) + 21.0) < 2.0, "geoid at olympia");

    std::vector<std::uint8_t> raw{1, 2, 3, 4};
    check(gunzip(raw) == raw, "gunzip passthrough");

    auto kml = parseFeatures(data("styles.kml"));
    check(kml.size() == 3 && kml[0].name == "Rainier Summit" && kml[0].style.iconColor == 0xFFFF0000, "kml styles");
    auto json = parseFeatures(data("olympia.geojson"));
    check(json.size() == 4 && json[2].geometries[0].type == GeometryType::Polygon, "geojson");
    auto gpx = parseFeatures(data("track.gpx"));
    check(gpx.size() == 3, "gpx");
    auto shp = parseFeatures(data("shapes.shp"));
    check(shp.size() == 2 && shp[0].name == "Shape Point", "shapefile with dbf");
    auto kmz = parseFile(data("overlay.kmz"));
    check(kmz.overlays.size() == 1 && kmz.overlays[0].width == 64 && kmz.features.size() == 1, "kmz ground overlay");
    auto mobac = loadMobac(data("mobac_multilayer_wms.xml"));
    check(mobac.size() == 2 && mobac[1].type == SourceType::Wms && mobac[1].srid == 4326, "mobac multilayer");

    std::ifstream f(data("qm_z12_x1319_y3117.terrain"), std::ios::binary);
    std::vector<std::uint8_t> qm((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    double span = 180.0 / 4096;
    double west = -180 + 1319 * span, south = -90 + 3117 * span;
    HeightGrid grid;
    bool decoded = decodeQuantizedMesh(gunzip(qm), west, south, west + span, south + span, 64, grid);
    double expect = 1500 + 300 * std::cos((47.0 - 47) * 20) * std::cos((-122.0 + 122) * 20);
    check(decoded && std::abs(grid.sample(-122.0, 47.0) - expect) < 2.0, "quantized mesh decode");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
