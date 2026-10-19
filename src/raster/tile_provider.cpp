#include "raster/tile_provider.hpp"

#include "db/sqlite.hpp"
#include "gdal/gdal_sources.hpp"
#include "net/http.hpp"

#include <json.hpp>
#include "raster/tile_cache.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace ote {

namespace {

constexpr std::int64_t kMinExpirySeconds = 7 * 24 * 3600;

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

std::string quadkey(int z, int x, int y) {
    std::string q;
    for (int i = z; i > 0; --i) {
        int mask = 1 << (i - 1);
        char d = '0';
        if (x & mask) d += 1;
        if (y & mask) d += 2;
        q.push_back(d);
    }
    return q;
}

std::string fmt(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.10g", v);
    return b;
}

class HttpProvider : public TileProvider {
public:
    HttpProvider(const ImageryLayer& layer, TileCache* cache, std::atomic<bool>* offline)
        : layer_(layer), cache_(cache), offline_(offline) {
        matrix_ = layer.srid == 4326 && layer.level0Resolution > 0 ? TileMatrix::geographicCustom(layer.minZoom, layer.maxZoom, layer.tileSize, layer.level0Resolution)
                 : layer.srid == 4326 ? TileMatrix::geographic(layer.minZoom, layer.maxZoom, layer.tileSize)
                                     : TileMatrix::webMercator(layer.minZoom, layer.maxZoom, layer.tileSize);
        key_ = layer.url + "|" + layer.wmsLayers + "|" + layer.wmsStyles + "|" + std::to_string(layer.srid) + "|" +
               (layer.tms ? "tms" : "xyz");
        ua_ = layer.userAgent.empty() ? std::string("OpenTAKEngine/0.1") : layer.userAgent;
    }

    bool remote() const override { return true; }
    bool vector() const override { return layer_.type == SourceType::Mvt; }
    bool cached(int z, int x, int y) override { return cache_ && layer_.cache && cache_->has(key_, z, x, y); }

    void resolveTileJson() {
        std::lock_guard lock(resolveMutex_);
        if (resolved_) return;
        resolved_ = true;
        if (layer_.type != SourceType::Mvt || layer_.url.find("{z}") != std::string::npos) return;
        net::Response res = net::get(layer_.url, ua_);
        if (res.status != 200) return;
        nlohmann::json j = nlohmann::json::parse(res.body.begin(), res.body.end(), nullptr, false);
        if (j.is_discarded() || !j.contains("tiles") || !j["tiles"].is_array() || j["tiles"].empty()) return;
        layer_.url = j["tiles"][0].get<std::string>();
    }

    bool fetchLocal(int z, int x, int y, FetchResult& out) override {
        bool offline = offline_ && offline_->load();
        if (!cache_ || !cache_->isOpen() || !layer_.cache) {
            if (offline) out.status = -2;
            return offline;
        }
        std::int64_t expires = 0;
        std::vector<std::uint8_t> data;
        if (cache_->get(key_, z, x, y, data, expires) && (expires > TileCache::now() || offline)) {
            out.status = 200;
            out.data = std::move(data);
            out.fromCache = true;
            return true;
        }
        if (offline) {
            out.status = -2;
            return true;
        }
        return false;
    }

    FetchResult fetch(int z, int x, int y) override {
        FetchResult r;
        if (!resolved_) resolveTileJson();
        std::vector<std::uint8_t> data;
        std::int64_t expires = 0;
        bool useCache = cache_ && cache_->isOpen() && layer_.cache;
        bool have = useCache && cache_->get(key_, z, x, y, data, expires);
        bool offline = offline_ && offline_->load();
        if (have && (expires > TileCache::now() || offline)) {
            r.status = 200;
            r.data = std::move(data);
            r.fromCache = true;
            return r;
        }
        if (offline) {
            r.status = -2;
            return r;
        }
        std::string url = tileUrl(layer_, matrix_, z, x, y);
        net::Response res;
        if (url.rfind("file://", 0) == 0) {
            std::string path = url.substr(7);
            if (path.size() > 2 && path[0] == '/' && path[2] == ':') path = path.substr(1);
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f) {
                std::fseek(f, 0, SEEK_END);
                long n = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                res.body.resize(n > 0 ? static_cast<size_t>(n) : 0);
                res.status = std::fread(res.body.data(), 1, res.body.size(), f) == res.body.size() ? 200 : 500;
                std::fclose(f);
            } else {
                res.status = 404;
            }
            r.status = res.status;
            r.data = std::move(res.body);
            return r;
        }
        res = net::get(url, ua_);
        if (res.status == 200 && !res.body.empty()) {
            if (useCache) cache_->put(key_, z, x, y, res.body, TileCache::now() + kMinExpirySeconds);
            r.status = 200;
            r.data = std::move(res.body);
            return r;
        }
        if (have) {
            r.status = 200;
            r.data = std::move(data);
            r.fromCache = true;
            return r;
        }
        r.status = res.status;
        return r;
    }

private:
    ImageryLayer layer_;
    TileCache* cache_;
    std::atomic<bool>* offline_;
    std::string key_;
    std::string ua_;
    std::mutex resolveMutex_;
    std::atomic<bool> resolved_{false};
};

class SqliteProvider : public TileProvider {
protected:
    bool openDb(const std::string& path) { return db_.open(path, true); }
    FetchResult query(db::Statement& st, int a, int b, int c) {
        std::lock_guard lock(mutex_);
        FetchResult r;
        if (!st.valid()) return r;
        st.reset();
        st.bind(1, a);
        st.bind(2, b);
        st.bind(3, c);
        if (st.step()) {
            r.data = st.blob(0);
            r.status = r.data.empty() ? 404 : 200;
        } else {
            r.status = 404;
        }
        st.reset();
        return r;
    }
    std::mutex mutex_;
    db::Database db_;
};

class MBTilesProvider : public SqliteProvider {
public:
    explicit MBTilesProvider(const ImageryLayer& layer) {
        int minZ = layer.minZoom, maxZ = layer.maxZoom;
        if (openDb(layer.path)) {
            auto st = db_.prepare("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles");
            if (st.valid() && st.step() && !st.isNull(0)) {
                minZ = st.integer(0);
                maxZ = st.integer(1);
            }
            tile_ = db_.prepare("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?");
            auto fmt = db_.prepare("SELECT value FROM metadata WHERE name='format'");
            if (fmt.valid() && fmt.step()) vector_ = fmt.text(0) == "pbf" || fmt.text(0) == "mvt";
        }
        matrix_ = TileMatrix::webMercator(minZ, maxZ, layer.tileSize);
    }
    bool vector() const override { return vector_; }
    FetchResult fetch(int z, int x, int y) override { return query(tile_, z, x, (1 << z) - 1 - y); }

private:
    db::Statement tile_;
    bool vector_ = false;
};

class OsmdroidProvider : public SqliteProvider {
public:
    explicit OsmdroidProvider(const ImageryLayer& layer) {
        openDb(layer.path);
        if (db_.isOpen()) tile_ = db_.prepare("SELECT tile FROM tiles WHERE key=?");
        matrix_ = TileMatrix::webMercator(layer.minZoom, layer.maxZoom, layer.tileSize);
    }
    FetchResult fetch(int z, int x, int y) override {
        std::lock_guard lock(mutex_);
        FetchResult r;
        r.status = 404;
        if (!tile_.valid()) return r;
        std::int64_t key = (((static_cast<std::int64_t>(z) << z) + x) << z) + y;
        tile_.reset();
        tile_.bind(1, key);
        if (tile_.step()) {
            r.data = tile_.blob(0);
            r.status = r.data.empty() ? 404 : 200;
        }
        tile_.reset();
        return r;
    }

private:
    db::Statement tile_;
};

class GeoPackageProvider : public SqliteProvider {
public:
    explicit GeoPackageProvider(const ImageryLayer& layer) {
        if (!openDb(layer.path)) return;
        std::string table = layer.table;
        if (table.empty()) {
            auto st = db_.prepare("SELECT table_name FROM gpkg_contents WHERE data_type='tiles' ORDER BY table_name LIMIT 1");
            if (st.valid() && st.step()) table = st.text(0);
        }
        if (table.empty()) return;
        int srsId = 0;
        double minX = 0, maxY = 0;
        {
            auto st = db_.prepare("SELECT srs_id, min_x, max_y FROM gpkg_tile_matrix_set WHERE table_name=?");
            if (!st.valid()) return;
            st.bind(1, table);
            if (!st.step()) return;
            srsId = st.integer(0);
            minX = st.real(1);
            maxY = st.real(2);
        }
        int srid = srsId;
        {
            auto st = db_.prepare("SELECT organization, organization_coordsys_id FROM gpkg_spatial_ref_sys WHERE srs_id=?");
            if (st.valid()) {
                st.bind(1, srsId);
                if (st.step()) srid = st.integer(1);
            }
        }
        if (srid == 900913 || srid == 3395) srid = 3857;
        matrix_.srid = srid == 4326 ? 4326 : 3857;
        matrix_.originX = minX;
        matrix_.originY = maxY;
        auto st = db_.prepare("SELECT zoom_level, matrix_width, matrix_height, tile_width, tile_height, pixel_x_size, "
                              "pixel_y_size FROM gpkg_tile_matrix WHERE table_name=? ORDER BY zoom_level");
        if (!st.valid()) return;
        st.bind(1, table);
        matrix_.minZoom = 1 << 30;
        matrix_.maxZoom = -1;
        while (st.step()) {
            int z = st.integer(0);
            if (z < 0 || z > 30) continue;
            if (static_cast<int>(matrix_.levels.size()) <= z) matrix_.levels.resize(z + 1);
            TileLevel& l = matrix_.levels[z];
            l.valid = true;
            l.matrixWidth = st.integer(1);
            l.matrixHeight = st.integer(2);
            l.tileWidth = st.integer(3);
            l.tileHeight = st.integer(4);
            l.resolutionX = st.real(5);
            l.resolutionY = st.real(6);
            matrix_.minZoom = std::min(matrix_.minZoom, z);
            matrix_.maxZoom = std::max(matrix_.maxZoom, z);
        }
        std::string sql = "SELECT tile_data FROM \"" + table + "\" WHERE zoom_level=? AND tile_column=? AND tile_row=?";
        tile_ = db_.prepare(sql.c_str());
    }
    FetchResult fetch(int z, int x, int y) override { return query(tile_, z, x, y); }

private:
    db::Statement tile_;
};

}

std::string tileUrl(const ImageryLayer& layer, const TileMatrix& m, int z, int x, int y) {
    std::string url = layer.url;
    std::string server = layer.servers.empty() ? std::string() : layer.servers[(x + y) % layer.servers.size()];
    if (layer.type == SourceType::Wms) {
        double minX, minY, maxX, maxY;
        m.tileBounds(z, x, y, minX, minY, maxX, maxY);
        bool v13 = layer.wmsVersion == "1.3.0";
        std::string bbox = (v13 && m.srid == 4326)
                               ? fmt(minY) + "," + fmt(minX) + "," + fmt(maxY) + "," + fmt(maxX)
                               : fmt(minX) + "," + fmt(minY) + "," + fmt(maxX) + "," + fmt(maxY);
        url += url.find('?') == std::string::npos ? "?" : (url.back() == '?' || url.back() == '&' ? "" : "&");
        url += "SERVICE=WMS&REQUEST=GetMap&VERSION=" + layer.wmsVersion + "&LAYERS=" + layer.wmsLayers +
               "&STYLES=" + layer.wmsStyles + "&FORMAT=" + layer.wmsFormat + "&TRANSPARENT=TRUE&WIDTH=" +
               std::to_string(layer.tileSize) + "&HEIGHT=" + std::to_string(layer.tileSize) + (v13 ? "&CRS=" : "&SRS=") +
               "EPSG:" + std::to_string(m.srid == 4326 ? 4326 : 3857) + "&BBOX=" + bbox;
        if (!layer.wmsExtra.empty()) url += (layer.wmsExtra[0] == '&' ? "" : "&") + layer.wmsExtra;
        replaceAll(url, "{s}", server);
        replaceAll(url, "{$serverpart}", server);
        return url;
    }
    int yy = layer.tms ? (1 << z) - 1 - y : y;
    for (const char* k : {"{$z}", "{z}", "{TileMatrix}"}) replaceAll(url, k, std::to_string(z));
    for (const char* k : {"{$x}", "{x}", "{TileCol}"}) replaceAll(url, k, std::to_string(x));
    for (const char* k : {"{$y}", "{y}", "{TileRow}"}) replaceAll(url, k, std::to_string(yy));
    replaceAll(url, "{-y}", std::to_string((1 << z) - 1 - y));
    for (const char* k : {"{$q}", "{q}"}) replaceAll(url, k, quadkey(z, x, y));
    for (const char* k : {"{$serverpart}", "{s}"}) replaceAll(url, k, server);
    return url;
}

namespace {
class NullProvider : public TileProvider {
public:
    NullProvider() { matrix_ = TileMatrix::webMercator(0, 0, 256); }
    FetchResult fetch(int, int, int) override {
        FetchResult r;
        r.status = 404;
        return r;
    }
};
}

std::shared_ptr<TileProvider> makeProvider(const ImageryLayer& layer, TileCache* cache, std::atomic<bool>* offline) {
    switch (layer.type) {
    case SourceType::Gdal: {
        auto p = makeGdalRaster(layer);
        if (p) return p;
        return std::make_shared<NullProvider>();
    }
    case SourceType::MBTiles:
        return std::make_shared<MBTilesProvider>(layer);
    case SourceType::GeoPackage:
        return std::make_shared<GeoPackageProvider>(layer);
    case SourceType::Osmdroid:
        return std::make_shared<OsmdroidProvider>(layer);
    default:
        return std::make_shared<HttpProvider>(layer, cache, offline);
    }
}

}
