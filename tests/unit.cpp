#include "core/camera.hpp"
#include "core/geo.hpp"
#include "core/geoid.hpp"
#include "elevation/quantized_mesh.hpp"
#include "ote/features.hpp"
#include "ote/formats.hpp"
#include "ote/mgrs.hpp"
#include "ote/milsym.hpp"
#include "ote/navigation.hpp"
#include "net/http.hpp"
#include "ote/engine.hpp"
#include "raster/tile_cache.hpp"
#include "raster/tile_loader.hpp"
#include "raster/tile_matrix.hpp"
#include "raster/tile_package.hpp"
#include "raster/tile_provider.hpp"
#include "raster/vector_tile.hpp"
#include "formats/adapters.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    using adapters::parseWkt;
    using adapters::writeWkt;

    for (auto [lat, lon, h] : {std::tuple{0.0, 0.0, 0.0}, {46.8529, -121.7604, 4392.0}, {-33.9, 151.2, -20.0}, {89.9, 45.0, 100.0}}) {
        GeoPoint g = ecefToGeodetic(geodeticToEcef(lat, lon, h));
        bool ok = std::abs(g.lat - lat) < 1e-9 && std::abs(g.lon - lon) < 1e-9 && std::abs(g.alt - h) < 1e-4;
        check(ok, ("ecef roundtrip " + std::to_string(lat)).c_str());
    }

    check(toMgrs(46.8529, -121.7604, 5) == "10T ES 94499 89563", "mgrs rainier");
    for (auto [lat, lon] : {std::pair{46.8529, -121.7604}, {-33.8688, 151.2093}, {60.0, 5.5}, {0.5, 0.5}, {-77.8, 166.7}}) {
        double rl, ro;
        bool ok = fromMgrs(toMgrs(lat, lon, 5), rl, ro) && length(geodeticToEcef(rl, ro, 0) - geodeticToEcef(lat, lon, 0)) < 1.5;
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

    {
        double t = parseIsoTime("2026-10-19T12:34:56.789Z");
        check(formatIsoTime(t) == "2026-10-19T12:34:56.789Z", "iso time round trip");
        check(std::abs(parseIsoTime("2026-10-19T14:34:56.789+02:00") - t) < 1e-6, "iso time offset");
        check(std::abs(parseIsoTime("1969-12-31T23:59:59Z") + 1.0) < 1e-9, "iso time before epoch");
    }
    {
        ImportResult cot = importData(data("sample.cot"));
        bool ok = cot.ok && cot.format == "cot" && cot.data.features.size() == 2;
        if (ok) {
            const Feature& a = cot.data.features[0];
            const Feature& b = cot.data.features[1];
            ok = a.uid == "ANDROID-123" && a.type == "a-f-G-U-C" && a.name == "Rainier Team" && a.description == "Summit check-in" &&
                 a.geometries[0].type == GeometryType::Point && std::abs(a.geometries[0].points[0].alt - 4392.0) < 1e-6 &&
                 a.altitudeMode == AltitudeMode::Absolute && a.style.iconColor == 0xFFFF0000u &&
                 formatIsoTime(a.stale) == "2026-10-19T12:05:00.000Z" && b.geometries[0].type == GeometryType::Polygon &&
                 b.geometries[0].points.size() == 4 && b.style.strokeColor == 0xFF0000FFu;
        }
        check(ok, "cot import (marker and drawn polygon)");
        ExportResult er;
        std::vector<std::uint8_t> out = exportBytes(cot.data.features, "cot", &er);
        ImportResult back = importBytes(out, "cot");
        bool same = back.ok && back.data.features.size() == 2 && back.data.features[0].uid == "ANDROID-123" &&
                    back.data.features[0].type == "a-f-G-U-C" && back.data.features[1].geometries[0].points.size() == 4 &&
                    std::abs(back.data.features[0].time - cot.data.features[0].time) < 1e-3;
        check(same, "cot round trip");
        ExportResult pr;
        std::vector<std::uint8_t> pkg = exportBytes(cot.data.features, "tak-package", &pr);
        check(detectFormat(pkg, "export.zip") == "tak-package", "data package detection");
        ImportResult unpacked = importBytes(pkg, "tak-package");
        bool pkgOk = unpacked.ok && unpacked.data.features.size() == 2;
        if (pkgOk) {
            pkgOk = (unpacked.data.features[0].uid == "ANDROID-123" || unpacked.data.features[1].uid == "ANDROID-123");
        }
        check(pkgOk, "data package round trip");
    }
    {
        Feature f;
        f.uid = "uid-7";
        f.type = "a-h-G";
        f.name = "Hostile";
        f.description = "Seen <near> \"ridge\" & road";
        f.time = parseIsoTime("2026-01-02T03:04:05Z");
        f.start = f.time;
        f.stale = f.time + 600;
        f.altitudeMode = AltitudeMode::Absolute;
        setAttribute(f, "speed", "12.5");
        setAttribute(f, "unit", "A1");
        f.geometries.push_back({GeometryType::Polygon, {{46, -122, 10}, {46, -121, 10}, {47, -121, 10}, {47, -122, 10}}, {{{46.4, -121.6, 10}, {46.4, -121.4, 10}, {46.6, -121.4, 10}}}});
        std::vector<Feature> one{f};
        for (const char* fmt : {"geojson", "kml", "gml", "csv"}) {
            ExportResult r;
            std::vector<std::uint8_t> bytes = exportBytes(one, fmt, &r);
            ImportResult in = importBytes(bytes, detectFormat(bytes, std::string("x.") + fmt));
            bool ok = in.ok && in.data.features.size() == 1;
            if (ok) {
                const Feature& g = in.data.features[0];
                const std::string* unit = findAttribute(g, "unit");
                ok = g.uid == "uid-7" && g.type == "a-h-G" && g.name == "Hostile" && g.description == f.description && unit && *unit == "A1" &&
                     g.geometries.size() == 1 && g.geometries[0].type == GeometryType::Polygon && g.geometries[0].points.size() == 4 &&
                     g.geometries[0].holes.size() == 1 && std::abs(g.time - f.time) < 1e-3;
            }
            std::string what = std::string(fmt) + " round trip keeps uid, type, attributes, time, holes";
            check(ok, what.c_str());
        }
        ExportResult gr;
        std::vector<std::uint8_t> gpx = exportBytes(one, "gpx", &gr);
        check(!gr.lossy.empty() && importBytes(gpx, "gpx").data.features.size() == 1, "gpx export reports lossy conversion");
    }
    {
        ImportResult csv = importData(data("points.csv"));
        bool ok = csv.ok && csv.data.features.size() == 2 && csv.data.features[0].name == "Camp, Muir" &&
                  std::abs(csv.data.features[0].geometries[0].points[0].alt - 3100) < 1e-9;
        const std::string* unit = ok ? findAttribute(csv.data.features[0], "unit") : nullptr;
        check(ok && unit && *unit == "Alpha" && csv.data.features[0].description == "says \"hi\"", "csv import with quoted fields and attributes");
        std::vector<Geometry> g;
        bool parsed = parseWkt("MULTIPOLYGON (((0 0, 10 0, 10 10, 0 10, 0 0), (2 2, 4 2, 4 4, 2 2)), ((20 20, 30 20, 30 30, 20 20)))", g);
        check(parsed && g.size() == 2 && g[0].holes.size() == 1 && g[1].points.size() == 3, "wkt multipolygon");
        std::vector<Geometry> back;
        check(parseWkt(writeWkt(g), back) && back.size() == 2 && back[0].holes.size() == 1, "wkt round trip");
        std::vector<Geometry> empty;
        check(parseWkt("POINT EMPTY", empty) && empty.empty(), "wkt empty");
    }
    {
        ImportResult gml = importData(data("sample.gml"));
        bool ok = gml.ok && gml.format == "gml" && gml.data.features.size() == 2;
        if (ok) {
            const Coord& c = gml.data.features[0].geometries[0].points[0];
            const std::string* h = findAttribute(gml.data.features[0], "height");
            ok = std::abs(c.lat - 46.8529) < 1e-9 && std::abs(c.lon + 121.7604) < 1e-9 && h && *h == "4392" &&
                 gml.data.features[1].geometries[0].holes.size() == 1;
        }
        check(ok, "gml import with EPSG:4326 axis order and holes");
    }
    {
        std::string md = formatsMarkdown();
        if (const char* out = std::getenv("OTE_FORMATS_MD")) std::ofstream(out, std::ios::binary) << md;
        check(md.find("`tak-package`") != std::string::npos && md.find("`cot`") != std::string::npos, "compatibility matrix");
        std::string evil = "PK";
        ImportLimits tight;
        tight.maxArchiveEntries = 1;
        Feature a;
        a.name = "a";
        a.geometries.push_back({GeometryType::Point, {{1, 2, 0}}, {}});
        std::vector<Feature> two{a, a};
        two[1].uid = "b";
        std::vector<std::uint8_t> pkg = exportBytes(two, "tak-package");
        ImportResult limited = importBytes(pkg, "tak-package", tight);
        check(!limited.ok && !limited.error.empty(), "archive entry limit enforced");
    }
    {
        const std::string host = "http://unit-unreachable.invalid";
        std::uint64_t epoch = net::reachabilityEpoch();
        for (int i = 0; i < 3; ++i) net::report(host + "/1/0/0.png", 0);
        bool down = !net::hostAvailable(host + "/2/0/0.png") && net::hostsDown() == 1 && !net::admit(host + "/2/1/1.png");
        net::report(host + "/1/0/0.png", 200);
        check(down && net::hostAvailable(host + "/x") && net::hostsDown() == 0 && net::reachabilityEpoch() == epoch + 1,
              "unreachable host fails fast and recovers");
        check(net::hostKey("HTTPS://Tiles.Example.com:8443/a/b?c=d") == "https://tiles.example.com:8443", "host key");
    }
    check(stripCredentials("https://t.example/{z}/{x}/{y}.png?key=abc&style=dark&access_token=x") ==
              "https://t.example/{z}/{x}/{y}.png?style=dark" &&
              stripCredentials("https://t.example/{z}?apikey=1") == "https://t.example/{z}",
          "cache key ignores credentials");
    {
        auto layers = loadMobac("<customMapSource><name>n</name><url>http://a/{$z}/{$x}/{$y}</url><tileUpdate>0</tileUpdate></customMapSource>");
        auto hourly = loadMobac("<customMapSource><name>n</name><url>http://a/{$z}/{$x}/{$y}</url><tileUpdate>3600000</tileUpdate></customMapSource>");
        check(layers.size() == 1 && layers[0].expirySeconds < 0 && hourly.size() == 1 && hourly[0].expirySeconds == 3600,
              "mobac tileUpdate");
    }
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "ote_unit_cache";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        {
            TileCache cache;
            bool opened = cache.open((dir / "tiles.sqlite").string());
            std::vector<std::uint8_t> blob(64 * 1024, 7);
            cache.put("a", 3, 1, 1, blob, TileCache::now() + 100, true);
            for (int i = 0; i < 64; ++i) cache.put("a", 10, i, 0, blob, TileCache::now() + 100);
            cache.setLimit(1 << 20);
            std::vector<std::uint8_t> got;
            std::int64_t exp = 0;
            int left = 0;
            for (int i = 0; i < 64; ++i) left += cache.has("a", 10, i, 0) ? 1 : 0;
            check(opened && cache.get("a", 3, 1, 1, got, exp) && got.size() == blob.size() && left < 64 && cache.bytes() <= (1 << 20) + 128 * 1024,
                  "cache limit evicts oldest, keeps pinned");
            cache.put("a", 3, 1, 1, std::vector<std::uint8_t>(10, 1), TileCache::now() + 100, false);
            cache.setLimit(1);
            check(cache.get("a", 3, 1, 1, got, exp) && got.size() == 10, "refresh keeps pin");
            cache.setLimit(0);
        }
        {
            std::ofstream(dir / "garbage.sqlite", std::ios::binary) << std::string(4096, 'x');
            TileCache cache;
            bool ok = cache.open((dir / "garbage.sqlite").string());
            cache.put("b", 0, 0, 0, {1, 2, 3}, TileCache::now() + 100);
            check(ok && cache.has("b", 0, 0, 0), "corrupt cache file replaced");
        }
        {
            TileCache cache;
            cache.open((dir / "stale.sqlite").string());
            std::atomic<bool> offline{false};
            ImageryLayer layer;
            layer.url = "http://unit-stale.invalid/{z}/{x}/{y}.png";
            auto provider = makeProvider(layer, &cache, &offline);
            std::string key = layerCacheKey(layer);
            cache.put(key, 4, 2, 3, {9, 9, 9}, TileCache::now() - 10);
            FetchResult r;
            bool local = provider->fetchLocal(4, 2, 3, r);
            check(local && r.status == 200 && r.stale && r.data.size() == 3, "expired tile served immediately");
            for (int i = 0; i < 3; ++i) net::report("http://unit-stale.invalid/0/0/0.png", 0);
            FetchResult miss;
            bool fast = provider->fetchLocal(5, 0, 0, miss);
            check(fast && miss.status == -3 && !provider->available(5, 0, 0), "uncached tile fails fast while host is down");
            offline = true;
            FetchResult off;
            check(provider->fetchLocal(5, 1, 1, off) && off.status == -2, "offline miss");
            net::resetReachability();
        }
        {
            TileCache cache;
            cache.open((dir / "export.sqlite").string());
            TileLoader loader(1, 1);
            loader.setCache(&cache);
            ImageryLayer layer;
            layer.name = "Unit";
            layer.url = "http://unit-export.invalid/{z}/{x}/{y}.png";
            loader.setLayers({layer});
            std::string key = layerCacheKey(layer);
            std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 1, 2, 3, 4};
            int stored = 0;
            for (int z = 0; z <= 3; ++z)
                for (int n = 0; n < (1 << (2 * z)); ++n, ++stored) cache.put(key, z, n % (1 << z), n / (1 << z), png, TileCache::now() + 100);
            std::string err;
            std::int64_t mb = loader.exportTiles(0, {-180, -85, 180, 85}, 0, 3, (dir / "out.mbtiles").string(), err);
            std::int64_t gp = loader.exportTiles(0, {0, 0, 180, 85}, 0, 3, (dir / "out.gpkg").string(), err);
            TileCache other;
            other.open((dir / "import.sqlite").string());
            TileLoader target(1, 1);
            target.setCache(&other);
            target.setLayers({layer});
            std::int64_t in = target.importTiles(0, (dir / "out.mbtiles").string(), err);
            std::vector<std::uint8_t> got;
            std::int64_t exp = 0;
            bool roundtrip = other.get(key, 3, 5, 2, got, exp) && got == png;
            TileMatrix gm;
            std::int64_t gcount = 0;
            readTilePackage((dir / "out.gpkg").string(), gm, [&](int, int, int, const std::vector<std::uint8_t>&) {
                ++gcount;
                return true;
            }, err);
            check(mb == stored && in == stored && roundtrip && gp > 0 && gcount == gp && sameTiling(gm, TileMatrix::webMercator(0, 3)),
                  "export to MBTiles/GeoPackage and import into the cache");
        }
        fs::remove_all(dir, ec);
    }
    {
        auto dms = [](double d, double m, double s) { return (d < 0 ? -1 : 1) * (std::abs(d) + m / 60.0 + s / 3600.0); };
        double la1 = dms(-37, 57, 3.72030), lo1 = dms(144, 25, 29.52440), la2 = dms(-37, 39, 10.15610), lo2 = dms(143, 55, 35.38390);
        GeodesicLine g = geodesicInverse(la1, lo1, la2, lo2);
        check(std::abs(g.distance - 54972.271) < 0.01 && std::abs(g.initialBearing - dms(306, 52, 5.37)) < 1e-4 &&
                  std::abs(g.finalBearing - dms(307, 10, 25.07)) < 1e-4,
              "vincenty inverse (Flinders Peak - Buninyong)");
        double fb = 0;
        Coord d = geodesicDirect({la1, lo1, 0}, g.initialBearing, g.distance, &fb);
        check(std::abs(d.lat - la2) < 1e-8 && std::abs(d.lon - lo2) < 1e-8 && std::abs(fb - g.finalBearing) < 1e-6, "vincenty direct");
        GeodesicLine anti = geodesicInverse(0.0, 0.0, 0.5, 179.7);
        check(anti.distance > 19900000 && anti.distance < 20100000, "near-antipodal fallback");
        std::vector<Coord> square{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
        double area = geodesicArea(square);
        check(std::abs(area - 1.2308e10) / 1.2308e10 < 0.01, "geodesic area of a 1 degree cell");
    }
    {
        struct T {
            double year, km, lat, lon, decl, incl;
        };
        const T tests[] = {{2025.0, 28, 89, -121, -99.77, 88.47}, {2025.0, 65, 43, 93, 0.50, 64.10},  {2025.0, 51, -33, 109, -5.49, -67.50},
                           {2025.5, 69, 38, -144, 12.93, 56.97}, {2025.5, 50, -70, -133, 57.21, -71.94}, {2026.0, 46, -24, -122, 14.01, -34.17}};
        bool ok = true;
        for (const T& t : tests) {
            MagneticField m = magneticField(t.lat, t.lon, t.km * 1000.0, t.year);
            if (std::abs(m.declination - t.decl) > 0.011 || std::abs(m.inclination - t.incl) > 0.011) {
                ok = false;
                std::printf("     wmm %.1f %g %g: D %.3f (want %.2f) I %.3f (want %.2f)\n", t.year, t.lat, t.lon, m.declination, t.decl,
                            m.inclination, t.incl);
            }
        }
        check(ok, "WMM2025 declination and inclination match NOAA test values");
        check(std::abs(gridConvergence(47.0, -123.0)) < 1e-9 && std::abs(gridConvergence(47.0, -122.9) - 0.1 * std::sin(47.0 * 3.14159265358979 / 180)) < 1e-4,
              "grid convergence");
        double t = 100.0;
        bool refs = std::abs(bearingToTrue(bearingFromTrue(t, 47, -122.9, NorthReference::Magnetic), 47, -122.9, NorthReference::Magnetic) - t) < 1e-9 &&
                    std::abs(bearingFromTrue(t, 47, -122.9, NorthReference::Magnetic) - (t - magneticDeclination(47, -122.9))) < 1e-9;
        check(refs, "true/magnetic/grid bearing conversion");
        check(formatBearing(90, AngleUnit::Mils, NorthReference::Magnetic) == "1600 mils M" && std::abs(degreesToMils(180) - 3200) < 1e-9 &&
                  formatBearing(5.4) == "005\xC2\xB0T",
              "bearing formatting in degrees and NATO mils");
        check(formatDistance(850) == "850 m" && formatDistance(12345) == "12.3 km" && formatDistance(1852, DistanceUnit::Nautical) == "1.00 NM",
              "distance formatting");
    }
    {
        check(toMgrs(90.0, 0.0, 5) == "Z AH 00000 00000" && toMgrs(-90.0, 0.0, 5) == "B AN 00000 00000", "polar MGRS at the poles");
        bool ok = true;
        for (auto [la, lo] : {std::pair{86.0, 45.0}, {88.5, -120.0}, {-85.0, -120.0}, {-81.0, 10.0}, {84.5, 179.0}}) {
            double rl = 0, ro = 0;
            std::string m = toMgrs(la, lo, 5);
            ok = ok && !m.empty() && fromMgrs(m, rl, ro) && length(geodeticToEcef(rl, ro, 0) - geodeticToEcef(la, lo, 0)) < 2.0;
            if (!ok) std::printf("     polar %s -> %f %f\n", m.c_str(), rl, ro);
        }
        check(ok, "polar MGRS (UPS) roundtrip");
        check(toGars(0.0, 0.0) == "361HN37" && toGars(-90.0, -180.0, 1) == "001AA" && toGars(89.99, 179.99, 1) == "720QZ", "GARS cells");
        bool garsOk = true;
        for (auto [la, lo] : {std::pair{47.04, -122.9}, {-33.87, 151.21}, {0.01, 0.01}, {64.2, -21.9}}) {
            double rl = 0, ro = 0;
            garsOk = garsOk && fromGars(toGars(la, lo), rl, ro) && std::abs(rl - la) <= 0.25 / 6 + 1e-9 && std::abs(ro - lo) <= 0.25 / 6 + 1e-9;
        }
        check(garsOk, "GARS roundtrip");
        struct P {
            const char* text;
            double lat, lon;
        };
        const P parses[] = {{"47.04, -122.9", 47.04, -122.9},
                            {"-33.87 151.21", -33.87, 151.21},
                            {"33.87 -151.21", 33.87, -151.21},
                            {"47.04N 122.9W", 47.04, -122.9},
                            {"N 47\xC2\xB0 02.4000' W 122\xC2\xB0 54.0000'", 47.04, -122.9},
                            {"47 02 24 N 122 54 00 W", 47.04, -122.9},
                            {"S33 52 12 E151 12 36", -33.87, 151.21},
                            {"10T ES 94499 89563", 46.8529, -121.7604},
                            {"361HN37", 0.0417, 0.0417}};
        bool parseOk = true;
        for (const P& p : parses) {
            double la = 0, lo = 0;
            bool ok = parseCoordinate(p.text, la, lo) && std::abs(la - p.lat) < 1e-3 && std::abs(lo - p.lon) < 1e-3;
            if (!ok) std::printf("     parse '%s' -> %f %f\n", p.text, la, lo);
            parseOk = parseOk && ok;
        }
        check(parseOk, "coordinate parsing (DD, DM, DMS, MGRS, GARS)");
        bool fmtOk = true;
        for (auto [pla, plo] : {std::pair{46.8529, -121.7604}, {87.5, 33.3}})
            for (CoordinateFormat cf : {CoordinateFormat::Decimal, CoordinateFormat::DegreesMinutes, CoordinateFormat::DegreesMinutesSeconds,
                                        CoordinateFormat::Mgrs, CoordinateFormat::Utm}) {
                double la = 0, lo = 0;
                std::string s = formatCoordinate(pla, plo, cf);
                bool ok = parseCoordinate(s, la, lo) && std::abs(la - pla) < 2e-5 && std::abs(lo - plo) < 2e-5 * (pla > 80 ? 20 : 1);
                if (!ok) std::printf("     format/parse '%s' -> %f %f\n", s.c_str(), la, lo);
                fmtOk = fmtOk && ok;
            }
        check(fmtOk, "coordinate formats roundtrip");
    }
    {
        std::vector<Feature> be = bullseye({47.0, -122.9, 0}, 10000, 3, 30);
        std::vector<Feature> rr = rangeRings({47.0, -122.9, 0}, 1000, 4, 8);
        std::vector<Feature> rb = rangeBearingLine({47.0, -122.9, 0}, {47.1, -122.8, 0}, NorthReference::True);
        GeodesicLine g = geodesicInverse(47.0, -122.9, 47.1, -122.8);
        const std::string* dist = rb.empty() ? nullptr : findAttribute(rb[0], "distance_m");
        Feature circle = geodesicCircle({10, 20, 0}, 5000);
        double worst = 0;
        for (const Coord& c : circle.geometries[0].points) worst = std::max(worst, std::abs(geodesicInverse(10, 20, c.lat, c.lon).distance - 5000));
        check(be.size() == 31 && rr.size() == 16 && rb.size() == 2 && dist && std::abs(std::stod(*dist) - g.distance) < 1e-3 && worst < 1e-3,
              "bullseye, range rings, range and bearing line, geodesic circle");
    }
    {
        check(sidcFromCotType("a-f-G-U-C-I") == "SFGPUCI--------" && sidcFromCotType("a-h-A-M-F") == "SHAPMF---------" &&
                  sidcFromCotType("b-m-p-s-p-i").empty(),
              "CoT type to 2525C SIDC");
        check(cotTypeFromSidc("SFGPUCI----D---") == "a-f-G-U-C-I" && cotTypeFromSidc("10031000001211000000") == "a-f-G-U-C-I", "SIDC to CoT type");
        SymbolCode d = parseSidc("10061000161211000000");
        check(d.valid && d.affiliation == Affiliation::Hostile && d.echelon == 'F' && d.function == "UCI---", "2525D numeric SIDC");
        const char* sheet[] = {"SFGPUCI----D---", "SHGPUCA----E---", "SNGPUCR--------", "SUGPUCF--------", "SFGAUCE---F----", "SFAPMF---------",
                               "SHAPMH---------", "SFSPCLDD-------", "SHUPS----------", "SFGPUCIZ--H----", "SFGPUSM----A---", "SAGPUCD--------",
                               "SFGPUCFM-AF----", "SHGPUCAA--B----", "SFGPEVAT-------", "SFGPUUS----G---", "SPGPU----------", "SFGPIZ---------",
                               "SFFPA----------", "SJGPUCI----C---", "SFGPUCV--------", "10031000141211000000", "SSGPUCI--------", "SFGPUST----I---"};
        int n = static_cast<int>(sizeof(sheet) / sizeof(sheet[0]));
        bool drawn = true;
        const int cell = 96, cols = 6;
        std::vector<std::uint8_t> rgb(static_cast<size_t>(cols * cell) * ((n + cols - 1) / cols * cell) * 3, 255);
        for (int i = 0; i < n; ++i) {
            Image img = renderSymbolImage(sheet[i], cell);
            int opaque = 0;
            for (size_t p = 3; p < img.rgba.size(); p += 4) opaque += img.rgba[p] > 128 ? 1 : 0;
            drawn = drawn && img.width == cell && opaque > cell * cell / 20;
            for (int y = 0; y < cell; ++y)
                for (int x = 0; x < cell; ++x) {
                    const std::uint8_t* s = &img.rgba[(static_cast<size_t>(y) * cell + x) * 4];
                    std::uint8_t* t = &rgb[((static_cast<size_t>(i / cols * cell + y)) * cols * cell + i % cols * cell + x) * 3];
                    for (int k = 0; k < 3; ++k) t[k] = static_cast<std::uint8_t>((s[k] * s[3] + 255 * (255 - s[3])) / 255);
                }
        }
        if (const char* out = std::getenv("OTE_MILSYM_SHEET")) {
            std::ofstream ppm(out, std::ios::binary);
            ppm << "P6\n" << cols * cell << " " << (n + cols - 1) / cols * cell << "\n255\n";
            ppm.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        }
        check(drawn, "2525 symbols render");
        Feature unit;
        unit.type = "a-f-G-U-C-I";
        unit.geometries.push_back({GeometryType::Point, {{47, -122.9, 0}}, {}});
        applySymbol(unit);
        check(unit.style.sidc == "SFGPUCI--------" && unit.style.iconImage && unit.style.iconColor == 0xFFFFFFFF, "CoT units get 2525 icons");
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
