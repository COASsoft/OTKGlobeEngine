#include "core/camera.hpp"
#include "core/geo.hpp"
#include "core/geoid.hpp"
#include "elevation/quantized_mesh.hpp"
#include "ote/features.hpp"
#include "ote/formats.hpp"
#include "ote/mgrs.hpp"
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
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
