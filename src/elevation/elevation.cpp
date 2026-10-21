#include "elevation/elevation.hpp"

#include "core/geoid.hpp"
#include "core/math.hpp"
#include "elevation/quantized_mesh.hpp"
#include "gdal/gdal_sources.hpp"
#include "raster/vector_tile.hpp"
#include "raster/tile_provider.hpp"

#include "ote/image.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <list>
#include <map>

namespace ote {

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr size_t kGridCapacity = 192;

void applyGeoid(HeightGrid& g, int srid) {
    for (int r = 0; r < g.height; ++r) {
        double y = g.maxY - (r + 0.5) * (g.maxY - g.minY) / g.height;
        double lat = unprojectY(srid, y);
        for (int c = 0; c < g.width; ++c) {
            float& v = g.values[static_cast<size_t>(r) * g.width + c];
            if (std::isnan(v)) continue;
            double lon = unprojectX(srid, g.minX + (c + 0.5) * (g.maxX - g.minX) / g.width);
            v += static_cast<float>(geoidHeight(lat, lon));
        }
    }
}

std::string splitQuery(const std::string& url, std::string& query) {
    size_t q = url.find('?');
    if (q == std::string::npos) {
        query.clear();
        return url;
    }
    query = url.substr(q + 1);
    return url.substr(0, q);
}

std::string queryValue(const std::string& query, const std::string& name) {
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        std::string part = query.substr(pos, amp - pos);
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == name) return part.substr(eq + 1);
        pos = amp + 1;
    }
    return {};
}

bool hasHeader(const std::vector<std::pair<std::string, std::string>>& headers, const char* name) {
    for (const auto& [n, v] : headers) {
        std::string a = n;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (a == name) return true;
    }
    return false;
}

bool heightmapSized(size_t n) { return n == 8451 || n == 8452 || n == 8451 + 65536; }

class QuantizedMeshProvider : public ElevationProvider {
public:
    QuantizedMeshProvider(const ElevationLayer& layer, TileCache* cache, std::atomic<bool>* offline)
        : layer_(layer), cache_(cache), offline_(offline) {
        matrix_ = TileMatrix::geographic(0, layer.maxZoom, 64);
    }

    int fetch(int z, int x, int y, HeightGrid& out) override {
        int failure = 503;
        std::shared_ptr<TileProvider> inner = provider(false, failure);
        if (!inner) return failure;
        if (!availableTile(z, x, y)) return 404;
        FetchResult r = inner->fetch(z, x, y);
        if ((r.status == 401 || r.status == 403) && ion_) {
            inner = provider(true, failure);
            if (!inner) return failure;
            r = inner->fetch(z, x, y);
        }
        if (r.status != 200 || r.data.empty()) return r.status;
        double minX, minY, maxX, maxY;
        matrix_.tileBounds(z, x, y, minX, minY, maxX, maxY);
        std::vector<std::uint8_t> raw = gunzip(r.data);
        bool heightmap = formatKnown_ ? heightmap_ : heightmapSized(raw.size());
        bool ok = heightmap ? decodeHeightmapTerrain(raw, minX, minY, maxX, maxY, out)
                            : decodeQuantizedMesh(raw, minX, minY, maxX, maxY, 64, out);
        return ok ? 200 : 404;
    }

private:
    bool availableTile(int z, int x, int y) {
        std::lock_guard lock(mutex_);
        if (available_.empty()) return true;
        if (z >= static_cast<int>(available_.size())) return metadataExt_;
        int row = tms_ ? (1 << z) - 1 - y : y;
        for (const auto& r : available_[static_cast<size_t>(z)])
            if (x >= r[0] && x <= r[2] && row >= r[1] && row <= r[3]) return true;
        return false;
    }

    bool load(const std::string& url, const std::string& cacheKey, const net::Headers& headers, nlohmann::json& out, int& failure) {
        std::vector<std::uint8_t> body;
        bool offline = offline_ && offline_->load();
        bool useCache = cache_ && cache_->isOpen() && layer_.cache;
        if (!offline) {
            net::Response r = net::get(url, "OpenTAKEngine/0.1", headers);
            if (r.status == 200 && !r.body.empty()) {
                body = std::move(r.body);
                if (useCache) cache_->put(cacheKey, -1, 0, 0, body, std::int64_t(1) << 50, true);
            } else if (r.status == 404 || r.status == 401 || r.status == 403) {
                failure = 404;
            }
        }
        std::int64_t expires = 0;
        if (body.empty() && useCache) cache_->get(cacheKey, -1, 0, 0, body, expires);
        if (body.empty()) {
            if (offline) failure = -2;
            return false;
        }
        out = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
        return !out.is_discarded() && out.is_object();
    }

    std::shared_ptr<TileProvider> provider(bool refresh, int& failure) {
        std::lock_guard lock(mutex_);
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (inner_ && !(refresh && now - resolvedAt_ > 30.0)) return refresh ? nullptr : inner_;
        if (!inner_ && now < retryAt_) return nullptr;
        retryAt_ = now + 10.0;
        net::Headers headers(layer_.headers.begin(), layer_.headers.end());
        std::string url = layer_.url;
        std::string query;
        std::string base;
        std::string templ;
        bool ion = false;
        if (url.rfind("ion://", 0) == 0) {
            std::string params;
            std::string id = splitQuery(url.substr(6), params);
            std::string server = queryValue(params, "server");
            if (server.empty()) server = "https://api.cesium.com";
            nlohmann::json ep;
            std::string endpoint = server + "/v1/assets/" + id + "/endpoint?access_token=" + queryValue(params, "access_token");
            if (!load(endpoint, "ion-endpoint|" + server + "|" + id, {}, ep, failure)) return nullptr;
            if (!ep.contains("url") || !ep["url"].is_string()) {
                failure = 404;
                return nullptr;
            }
            base = ep["url"].get<std::string>();
            base = splitQuery(base, query);
            if (base.empty() || base.back() != '/') base += '/';
            if (ep.contains("accessToken") && ep["accessToken"].is_string())
                headers.emplace_back("Authorization", "Bearer " + ep["accessToken"].get<std::string>());
            ion = true;
        } else if (url.find("{z}") != std::string::npos) {
            templ = url;
        } else {
            std::string path = splitQuery(url, query);
            if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0) {
                base = path.substr(0, path.rfind('/') + 1);
            } else {
                base = path;
                if (base.empty() || base.back() != '/') base += '/';
            }
        }
        bool tms = true;
        bool heightmap = false;
        bool formatKnown = false;
        bool metadata = false;
        std::vector<std::vector<std::array<int, 4>>> available;
        if (templ.empty()) {
            std::string layerUrl = base + "layer.json" + (query.empty() ? "" : "?" + query);
            nlohmann::json lj;
            net::Headers jsonHeaders = headers;
            if (!hasHeader(jsonHeaders, "accept")) jsonHeaders.emplace_back("Accept", "application/json,*/*;q=0.01");
            if (!load(layerUrl, "qm-layer|" + base, jsonHeaders, lj, failure)) return nullptr;
            std::string format = lj.value("format", std::string("quantized-mesh-1.0"));
            heightmap = format.rfind("heightmap", 0) == 0;
            formatKnown = true;
            if (lj.value("projection", std::string("EPSG:4326")) != "EPSG:4326") {
                failure = 404;
                return nullptr;
            }
            tms = lj.value("scheme", std::string("tms")) != "slippyMap";
            if (lj.contains("extensions") && lj["extensions"].is_array())
                for (const auto& e : lj["extensions"])
                    if (e.is_string() && e.get<std::string>() == "metadata") metadata = true;
            if (lj.contains("available") && lj["available"].is_array())
                for (const auto& level : lj["available"]) {
                    available.emplace_back();
                    if (!level.is_array()) continue;
                    for (const auto& r : level)
                        if (r.is_object())
                            available.back().push_back({r.value("startX", 0), r.value("startY", 0), r.value("endX", -1), r.value("endY", -1)});
                }
            if (!lj.contains("tiles") || !lj["tiles"].is_array() || lj["tiles"].empty() || !lj["tiles"][0].is_string()) {
                failure = 404;
                return nullptr;
            }
            templ = lj["tiles"][0].get<std::string>();
            std::string version = lj.value("version", std::string("1.0.0"));
            for (size_t p; (p = templ.find("{version}")) != std::string::npos;) templ.replace(p, 9, version);
            if (templ.find("://") == std::string::npos) {
                if (!templ.empty() && templ[0] == '/') {
                    size_t scheme = base.find("://");
                    size_t host = scheme == std::string::npos ? std::string::npos : base.find('/', scheme + 3);
                    templ = (host == std::string::npos ? base : base.substr(0, host)) + templ;
                } else {
                    if (templ.rfind("./", 0) == 0) templ = templ.substr(2);
                    templ = base + templ;
                }
            }
            if (!query.empty() && !ion) templ += (templ.find('?') == std::string::npos ? "?" : "&") + query;
        }
        if (!hasHeader(headers, "accept"))
            headers.emplace_back("Accept", heightmap ? "application/octet-stream,*/*;q=0.01"
                                                     : "application/vnd.quantized-mesh,application/octet-stream;q=0.9,*/*;q=0.01");
        ImageryLayer l;
        l.name = layer_.name;
        l.url = templ;
        l.maxZoom = layer_.maxZoom;
        l.cache = layer_.cache;
        l.srid = 4326;
        l.tms = tms;
        l.headers.assign(headers.begin(), headers.end());
        inner_ = makeProvider(l, cache_, offline_);
        tms_ = tms;
        heightmap_ = heightmap;
        formatKnown_ = formatKnown;
        metadataExt_ = metadata;
        available_ = std::move(available);
        ion_ = ion;
        resolvedAt_ = now;
        return inner_;
    }

    ElevationLayer layer_;
    TileCache* cache_;
    std::atomic<bool>* offline_;
    std::mutex mutex_;
    std::shared_ptr<TileProvider> inner_;
    std::vector<std::vector<std::array<int, 4>>> available_;
    bool tms_ = true;
    bool heightmap_ = false;
    bool formatKnown_ = false;
    bool metadataExt_ = false;
    std::atomic<bool> ion_{false};
    double resolvedAt_ = 0;
    double retryAt_ = 0;
};

class TerrariumProvider : public ElevationProvider {
public:
    TerrariumProvider(const ElevationLayer& layer, TileCache* cache, std::atomic<bool>* offline) {
        ImageryLayer l;
        l.name = layer.name;
        l.url = layer.url;
        l.path = layer.path;
        l.minZoom = layer.minZoom;
        l.maxZoom = layer.maxZoom;
        l.tileSize = layer.tileSize;
        l.cache = layer.cache;
        if (layer.type == ElevationType::PMTiles) l.type = SourceType::PMTiles;
        if (layer.type == ElevationType::MBTiles) l.type = SourceType::MBTiles;
        inner_ = makeProvider(l, cache, offline);
        bathymetry_ = layer.bathymetry;
        mapbox_ = layer.encoding == HeightEncoding::MapboxRgb;
        matrix_ = layer.type == ElevationType::Terrarium ? TileMatrix::webMercator(layer.minZoom, layer.maxZoom, layer.tileSize) : inner_->matrix();
        if (layer.type == ElevationType::MBTiles) matrix_ = TileMatrix::webMercator(inner_->matrix().minZoom, inner_->matrix().maxZoom, layer.tileSize);
    }

    int fetch(int z, int x, int y, HeightGrid& out) override {
        FetchResult r = inner_->fetch(z, x, y);
        if (r.status != 200 || r.data.empty()) return r.status;
        Image img;
        if (!decodeImage(r.data.data(), r.data.size(), img)) return 404;
        out.width = img.width;
        out.height = img.height;
        out.values.resize(static_cast<size_t>(img.width) * img.height);
        for (size_t i = 0; i < out.values.size(); ++i) {
            const unsigned char* p = img.rgba.data() + i * 4;
            if (p[3] == 0) {
                out.values[i] = kNaN;
                continue;
            }
            float v = mapbox_ ? static_cast<float>(-10000.0 + (p[0] * 65536.0 + p[1] * 256.0 + p[2]) * 0.1)
                              : static_cast<float>(p[0] * 256.0 + p[1] + p[2] / 256.0 - 32768.0);
            out.values[i] = bathymetry_ ? v : std::max(v, 0.0f);
        }
        matrix_.tileBounds(z, x, y, out.minX, out.minY, out.maxX, out.maxY);
        applyGeoid(out, 3857);
        return 200;
    }

private:
    std::shared_ptr<TileProvider> inner_;
    bool bathymetry_ = false;
    bool mapbox_ = false;
};

struct Cell {
    int cols = 0;
    int rows = 0;
    bool southUp = true;
    bool columnMajor = true;
    std::vector<std::int16_t> data;

    float at(int c, int r) const {
        std::int16_t v = columnMajor ? data[static_cast<size_t>(c) * rows + r] : data[static_cast<size_t>(r) * cols + c];
        if (v <= -32767) return kNaN;
        return static_cast<float>(v);
    }

    float sample(double fx, double fy) const {
        double x = fx * (cols - 1);
        double y = (southUp ? fy : 1.0 - fy) * (rows - 1);
        int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, cols - 1), y0 = std::clamp(static_cast<int>(std::floor(y)), 0, rows - 1);
        int x1 = std::min(x0 + 1, cols - 1), y1 = std::min(y0 + 1, rows - 1);
        double tx = x - x0, ty = y - y0;
        float a = at(x0, y0), b = at(x1, y0), c = at(x0, y1), d = at(x1, y1);
        if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d)) {
            float best = kNaN;
            for (float v : {a, b, c, d})
                if (!std::isnan(v)) best = v;
            return best;
        }
        return static_cast<float>((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty);
    }
};

std::vector<std::uint8_t> readFile(const std::filesystem::path& p) {
    std::vector<std::uint8_t> out;
    FILE* f = nullptr;
#if defined(_WIN32)
    f = _wfopen(p.wstring().c_str(), L"rb");
#else
    f = std::fopen(p.string().c_str(), "rb");
#endif
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

int asciiInt(const std::uint8_t* p, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i)
        if (p[i] >= '0' && p[i] <= '9') v = v * 10 + (p[i] - '0');
    return v;
}

bool loadDted(const std::vector<std::uint8_t>& b, Cell& cell) {
    if (b.size() < 3428 || b[0] != 'U' || b[1] != 'H' || b[2] != 'L') return false;
    int lonCount = asciiInt(&b[47], 4), latCount = asciiInt(&b[51], 4);
    if (lonCount < 2 || latCount < 2) return false;
    size_t record = 12 + 2 * static_cast<size_t>(latCount);
    if (b.size() < 3428 + record * lonCount) return false;
    cell.cols = lonCount;
    cell.rows = latCount;
    cell.southUp = true;
    cell.columnMajor = true;
    cell.data.resize(static_cast<size_t>(lonCount) * latCount);
    for (int c = 0; c < lonCount; ++c) {
        const std::uint8_t* rec = &b[3428 + record * c + 8];
        for (int r = 0; r < latCount; ++r) {
            std::uint16_t raw = static_cast<std::uint16_t>((rec[2 * r] << 8) | rec[2 * r + 1]);
            int v = (raw & 0x8000) ? -static_cast<int>(raw & 0x7FFF) : static_cast<int>(raw);
            cell.data[static_cast<size_t>(c) * latCount + r] = static_cast<std::int16_t>(v == -32767 ? -32767 : v);
        }
    }
    return true;
}

bool loadHgt(const std::vector<std::uint8_t>& b, Cell& cell) {
    size_t n = static_cast<size_t>(std::llround(std::sqrt(static_cast<double>(b.size() / 2))));
    if (n < 2 || n * n * 2 != b.size()) return false;
    cell.cols = cell.rows = static_cast<int>(n);
    cell.southUp = false;
    cell.columnMajor = false;
    cell.data.resize(n * n);
    for (size_t i = 0; i < n * n; ++i) {
        std::int16_t v = static_cast<std::int16_t>((b[2 * i] << 8) | b[2 * i + 1]);
        cell.data[i] = v == -32768 ? static_cast<std::int16_t>(-32767) : v;
    }
    return true;
}

class DirectoryProvider : public ElevationProvider {
public:
    explicit DirectoryProvider(const ElevationLayer& layer) {
        matrix_ = TileMatrix::geographic(6, std::max(layer.maxZoom, 6), 64);
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(layer.path, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            index(it->path());
        }
    }

    int fetch(int z, int x, int y, HeightGrid& out) override {
        const int n = 64;
        matrix_.tileBounds(z, x, y, out.minX, out.minY, out.maxX, out.maxY);
        out.width = out.height = n;
        out.values.assign(static_cast<size_t>(n) * n, kNaN);
        bool any = false;
        for (int r = 0; r < n; ++r) {
            double lat = out.maxY - (r + 0.5) * (out.maxY - out.minY) / n;
            for (int c = 0; c < n; ++c) {
                double lon = out.minX + (c + 0.5) * (out.maxX - out.minX) / n;
                float v = sample(lat, lon);
                if (!std::isnan(v)) {
                    out.values[static_cast<size_t>(r) * n + c] = v;
                    any = true;
                }
            }
        }
        if (any) applyGeoid(out, 4326);
        return any ? 200 : 404;
    }

private:
    struct Entry {
        std::filesystem::path path;
        int rank = 0;
        bool hgt = false;
    };

    static long long key(int lat, int lon) { return static_cast<long long>(lat + 90) * 1000 + (lon + 180); }

    void index(const std::filesystem::path& p) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string stem = p.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        int lat = 0, lon = 0, rank = 0;
        bool hgt = false;
        if (ext == ".hgt") {
            char ns, ew;
            if (std::sscanf(stem.c_str(), "%c%2d%c%3d", &ns, &lat, &ew, &lon) != 4) return;
            if (ns == 's') lat = -lat;
            if (ew == 'w') lon = -lon;
            std::error_code ec;
            rank = std::filesystem::file_size(p, ec) > 3000000 ? 3 : 2;
            hgt = true;
        } else if (ext == ".dt0" || ext == ".dt1" || ext == ".dt2" || ext == ".dt3") {
            char ns;
            if (std::sscanf(stem.c_str(), "%c%d", &ns, &lat) != 2) return;
            if (ns == 's') lat = -lat;
            std::string dir = p.parent_path().filename().string();
            std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            char ew;
            if (std::sscanf(dir.c_str(), "%c%d", &ew, &lon) != 2) return;
            if (ew == 'w') lon = -lon;
            rank = ext[3] - '0' + 1;
        } else {
            return;
        }
        Entry& e = files_[key(lat, lon)];
        if (rank > e.rank) e = {p, rank, hgt};
    }

    float sample(double lat, double lon) {
        int la = static_cast<int>(std::floor(lat)), lo = static_cast<int>(std::floor(lon));
        long long k = key(la, lo);
        std::shared_ptr<Cell> cell;
        {
            std::lock_guard lock(mutex_);
            auto f = files_.find(k);
            if (f == files_.end()) return kNaN;
            auto c = cells_.find(k);
            if (c != cells_.end()) {
                lru_.remove(k);
                lru_.push_front(k);
                cell = c->second;
            }
        }
        if (!cell) {
            Entry e;
            {
                std::lock_guard lock(mutex_);
                e = files_[k];
            }
            auto loaded = std::make_shared<Cell>();
            std::vector<std::uint8_t> bytes = readFile(e.path);
            if (!(e.hgt ? loadHgt(bytes, *loaded) : loadDted(bytes, *loaded))) return kNaN;
            std::lock_guard lock(mutex_);
            cells_[k] = loaded;
            lru_.push_front(k);
            while (lru_.size() > 24) {
                cells_.erase(lru_.back());
                lru_.pop_back();
            }
            cell = loaded;
        }
        return cell->sample(lon - lo, lat - la);
    }

    std::mutex mutex_;
    std::map<long long, Entry> files_;
    std::map<long long, std::shared_ptr<Cell>> cells_;
    std::list<long long> lru_;
};

}

float HeightGrid::sample(double x, double y) const {
    if (width == 0 || height == 0) return kNaN;
    double fx = (x - minX) / (maxX - minX) * width - 0.5;
    double fy = (maxY - y) / (maxY - minY) * height - 0.5;
    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, width - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, height - 1);
    int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    double tx = std::clamp(fx - x0, 0.0, 1.0), ty = std::clamp(fy - y0, 0.0, 1.0);
    float a = values[static_cast<size_t>(y0) * width + x0], b = values[static_cast<size_t>(y0) * width + x1];
    float c = values[static_cast<size_t>(y1) * width + x0], d = values[static_cast<size_t>(y1) * width + x1];
    if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d)) {
        float best = kNaN;
        for (float v : {a, b, c, d})
            if (!std::isnan(v)) best = v;
        return best;
    }
    return static_cast<float>((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty);
}

ElevationManager::ElevationManager(int threads) {
    for (int i = 0; i < threads; ++i) workers_.emplace_back([this] { run(); });
}

ElevationManager::~ElevationManager() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
}

void ElevationManager::setSources(const std::vector<ElevationLayer>& layers, TileCache* cache, std::atomic<bool>* offline) {
    std::vector<std::shared_ptr<ElevationProvider>> providers;
    for (const auto& l : layers) {
        if (l.type == ElevationType::Directory) {
            providers.push_back(std::make_shared<DirectoryProvider>(l));
        } else if (l.type == ElevationType::QuantizedMesh) {
            providers.push_back(std::make_shared<QuantizedMeshProvider>(l, cache, offline));
        } else if (l.type == ElevationType::Gdal) {
            if (auto p = makeGdalElevation(l)) providers.push_back(p);
        } else {
            providers.push_back(std::make_shared<TerrariumProvider>(l, cache, offline));
        }
    }
    std::vector<double> finest;
    for (const auto& pr : providers) {
        const TileMatrix& m = pr->matrix();
        double f = 1e12;
        if (!m.levels.empty() && m.maxZoom >= 0 && m.maxZoom < static_cast<int>(m.levels.size()) && m.unitsPerDegree() > 0)
            f = m.levels[m.maxZoom].resolutionX / m.unitsPerDegree() * 111319.49;
        finest.push_back(f);
    }
    std::lock_guard lock(mutex_);
    layers_ = layers;
    providers_ = std::move(providers);
    finest_ = std::move(finest);
    queue_.clear();
    done_.clear();
    doneKeys_.clear();
    grids_.clear();
    used_.clear();
    failed_.clear();
    retry_.clear();
    ++generation_;
    ++version_;
}

const HeightGrid* ElevationManager::find(int source, int z, int x, int y) const {
    auto it = grids_.find({source, z, x, y});
    return it == grids_.end() ? nullptr : it->second.get();
}

const HeightGrid* ElevationManager::best(int source, int z, double px, double py, bool& exact) const {
    const TileMatrix& m = providers_[source]->matrix();
    exact = true;
    for (int zz = z; zz >= 0; --zz) {
        if (!m.hasLevel(zz)) continue;
        int tx = static_cast<int>(std::floor((px - m.originX) / m.spanX(zz)));
        int ty = static_cast<int>(std::floor((m.originY - py) / m.spanY(zz)));
        if (const HeightGrid* g = find(source, zz, tx, ty)) return g;
        exact = false;
    }
    return nullptr;
}

double ElevationManager::finestMeters(int source) const {
    return source >= 0 && source < static_cast<int>(finest_.size()) ? finest_[source] : 1e12;
}

bool ElevationManager::yields(int source, double targetMeters) const {
    double mine = finestMeters(source);
    if (!(targetMeters < mine * 0.999)) return false;
    for (int t = source + 1; t < static_cast<int>(finest_.size()); ++t)
        if (finest_[t] < mine * 0.999) return true;
    return false;
}

bool ElevationManager::gridFor(double west, double south, double east, double north, int posts, double priority,
                               std::vector<float>& heights, bool sampleHeights) {
    heights.assign(sampleHeights ? static_cast<size_t>(posts) * posts : 0, kNaN);
    bool complete = true;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    ++tick_;
    const double targetMeters = (east - west) / std::max(posts - 1, 1) * 111319.49 * std::cos(0.5 * (south + north) * kDegToRad);
    std::vector<int> order;
    std::vector<int> deferred;
    for (int s = 0; s < static_cast<int>(providers_.size()); ++s) (yields(s, targetMeters) ? deferred : order).push_back(s);
    order.insert(order.end(), deferred.begin(), deferred.end());
    for (int s : order) {
        const TileMatrix& m = providers_[s]->matrix();
        double maxLat = maxLatitude(m.srid);
        double so = std::max(south, -maxLat), no = std::min(north, maxLat);
        if (so >= no) {
            if (north > maxLat) {
                so = maxLat - 1e-6;
                no = maxLat;
            } else {
                so = -maxLat;
                no = -maxLat + 1e-6;
            }
        }
        double target = (east - west) / (posts - 1) * m.unitsPerDegree();
        int z = m.bestLevel(target);
        if (z < 0 || m.levels[z].resolutionX < target * 0.125) continue;
        for (const TileRange& r : regionRanges(m, {west, so, east, no}, z, z))
            for (int ty = r.y0; ty <= r.y1; ++ty)
                for (int tx = r.x0; tx <= r.x1; ++tx) {
                    Key k{s, z, tx, ty};
                    if (grids_.count(k)) {
                        used_[k] = tick_;
                        continue;
                    }
                    if (failed_.count(k)) continue;
                    complete = false;
                    auto rt = retry_.find(k);
                    if (rt != retry_.end() && rt->second.first > now) continue;
                    requests_.push_back({k, priority});
                }
        if (!sampleHeights) continue;
        const HeightGrid* last = nullptr;
        bool lastExact = false;
        for (int j = 0; j < posts; ++j) {
            double lat = south + (north - south) * j / (posts - 1);
            double py = projectY(m.srid, std::clamp(lat, -maxLat * 0.9999999, maxLat * 0.9999999));
            for (int i = 0; i < posts; ++i) {
                float& h = heights[static_cast<size_t>(j) * posts + i];
                if (!std::isnan(h)) continue;
                double lon = west + (east - west) * i / (posts - 1);
                double px = projectX(m.srid, lon);
                if (!(last && lastExact && px >= last->minX && px <= last->maxX && py >= last->minY &&
                      py <= last->maxY)) {
                    last = best(s, z, px, py, lastExact);
                }
                if (last) h = last->sample(px, py);
            }
        }
    }
    return complete;
}

double ElevationManager::sample(double lat, double lon) const {
    for (int s = 0; s < static_cast<int>(providers_.size()); ++s) {
        const TileMatrix& m = providers_[s]->matrix();
        double ml = maxLatitude(m.srid) * 0.9999999;
        double px = projectX(m.srid, lon), py = projectY(m.srid, std::clamp(lat, -ml, ml));
        bool exact = false;
        if (const HeightGrid* g = best(s, m.maxZoom, px, py, exact)) {
            float v = g->sample(px, py);
            if (!std::isnan(v)) return v;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void ElevationManager::submit() {
    std::sort(requests_.begin(), requests_.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::lock_guard lock(mutex_);
    queue_.clear();
    std::unordered_set<Key, KeyHash> seen;
    for (auto it = requests_.rbegin(); it != requests_.rend(); ++it)
        if (!inflight_.count(it->first) && !doneKeys_.count(it->first) && seen.insert(it->first).second) queue_.push_back(it->first);
    std::reverse(queue_.begin(), queue_.end());
    if (!queue_.empty()) cv_.notify_all();
}

bool ElevationManager::processResults(size_t max) {
    std::vector<Result> results;
    {
        std::lock_guard lock(mutex_);
        while (!done_.empty() && results.size() < max) {
            doneKeys_.erase(done_.front().key);
            if (done_.front().generation == generation_) results.push_back(std::move(done_.front()));
            done_.pop_front();
        }
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    for (auto& r : results) {
        if (r.status == 200) {
            grids_[r.key] = std::make_shared<HeightGrid>(std::move(r.grid));
            retry_.erase(r.key);
        } else if (r.status == 404 || r.status == 204 || r.status == 400 || r.status == -2 || r.status == -3) {
            failed_.insert(r.key);
            retry_.erase(r.key);
        } else {
            if (retry_.size() > 20000) retry_.clear();
            auto& rt = retry_[r.key];
            rt.second = std::min(rt.second + 1, 6);
            rt.first = now + std::min(30.0, 0.5 * static_cast<double>(1 << rt.second));
        }
        used_[r.key] = tick_;
    }
    if (grids_.size() > kGridCapacity) {
        std::vector<std::pair<std::uint64_t, Key>> c;
        for (auto& [k, g] : grids_) {
            auto u = used_.find(k);
            std::uint64_t t = u == used_.end() ? 0 : u->second;
            if (k.z > 3 && t + 2 < tick_) c.push_back({t, k});
        }
        std::sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < c.size() && grids_.size() > kGridCapacity; ++i) {
            grids_.erase(c[i].second);
            used_.erase(c[i].second);
        }
    }
    if (!results.empty()) ++version_;
    return !results.empty();
}

void ElevationManager::clearFailures() {
    failed_.clear();
    retry_.clear();
    ++version_;
}

int ElevationManager::pending() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size() + inflight_.size());
}

void ElevationManager::run() {
    for (;;) {
        Key k{};
        std::shared_ptr<ElevationProvider> provider;
        int gen = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            k = queue_.back();
            queue_.pop_back();
            if (k.source < 0 || k.source >= static_cast<int>(providers_.size())) continue;
            provider = providers_[k.source];
            inflight_.insert(k);
            gen = generation_;
        }
        Result r;
        r.key = k;
        r.generation = gen;
        r.status = provider->fetch(k.z, k.x, k.y, r.grid);
        std::lock_guard lock(mutex_);
        inflight_.erase(k);
        if (doneKeys_.insert(k).second) done_.push_back(std::move(r));
    }
}

std::shared_ptr<const HeightGrid> ElevationManager::queryTile(const std::shared_ptr<ElevationProvider>& provider, int source, int z, int x, int y,
                                                              int& status) const {
    Key k{source, z, x, y};
    {
        std::lock_guard lock(queryMutex_);
        auto it = queryTiles_.find(k);
        if (it != queryTiles_.end()) {
            it->second.second = ++queryTick_;
            status = 200;
            return it->second.first;
        }
        auto miss = queryMissing_.find(k);
        if (miss != queryMissing_.end()) {
            status = miss->second;
            return nullptr;
        }
    }
    auto grid = std::make_shared<HeightGrid>();
    status = provider->fetch(z, x, y, *grid);
    std::lock_guard lock(queryMutex_);
    if (status != 200) {
        if (status == 404 || status == 204 || status == 400) {
            if (queryMissing_.size() > 20000) queryMissing_.clear();
            queryMissing_[k] = status;
        }
        return nullptr;
    }
    queryTiles_[k] = {grid, ++queryTick_};
    if (queryTiles_.size() > 256) {
        auto oldest = queryTiles_.begin();
        for (auto it = queryTiles_.begin(); it != queryTiles_.end(); ++it)
            if (it->second.second < oldest->second.second) oldest = it;
        queryTiles_.erase(oldest);
    }
    return grid;
}

std::vector<double> ElevationManager::query(const std::vector<std::pair<double, double>>& latLon, double resolutionMeters,
                                            std::vector<double>* resolutions) const {
    std::vector<double> out(latLon.size(), std::numeric_limits<double>::quiet_NaN());
    if (resolutions) resolutions->assign(latLon.size(), 0.0);
    auto metresPerPixel = [](const TileMatrix& m, const HeightGrid& g, double coslat) {
        double units = (g.maxX - g.minX) / std::max(g.width, 1);
        return m.srid == 4326 ? units * 111319.49 * coslat : units * coslat;
    };
    std::vector<std::shared_ptr<ElevationProvider>> providers;
    int generation = 0;
    {
        std::lock_guard lock(mutex_);
        providers = providers_;
        generation = generation_;
    }
    {
        std::lock_guard lock(queryMutex_);
        if (queryGeneration_ != generation) {
            queryTiles_.clear();
            queryMissing_.clear();
            queryGeneration_ = generation;
        }
    }
    if (providers.empty()) return out;
    for (int pass = 0; pass < 2; ++pass) {
        bool missing = false;
        for (int s = 0; s < static_cast<int>(providers.size()); ++s) {
            const TileMatrix& m = providers[s]->matrix();
            if (m.levels.empty()) continue;
            double maxLat = maxLatitude(m.srid) * 0.9999999;
            std::shared_ptr<const HeightGrid> last;
            int lastZ = -1;
            for (size_t i = 0; i < latLon.size(); ++i) {
                if (!std::isnan(out[i])) continue;
                double lat = latLon[i].first, lon = latLon[i].second;
                if (std::abs(lat) > maxLat) continue;
                double coslat = std::max(std::cos(lat * kDegToRad), 0.01);
                double target = resolutionMeters > 0 ? resolutionMeters : 0.0;
                if (pass == 0 && yields(s, target > 0 ? target : 0.0)) continue;
                double px = projectX(m.srid, lon), py = projectY(m.srid, lat);
                int z = m.maxZoom;
                if (target > 0) {
                    double units = m.srid == 4326 ? target / 111319.49 : target / coslat;
                    z = std::clamp(m.bestLevel(units), m.minZoom, m.maxZoom);
                }
                if (last && lastZ == z && px >= last->minX && px <= last->maxX && py >= last->minY && py <= last->maxY) {
                    float v = last->sample(px, py);
                    if (!std::isnan(v)) {
                        out[i] = v;
                        if (resolutions) (*resolutions)[i] = metresPerPixel(m, *last, coslat);
                        continue;
                    }
                }
                for (int zz = z; zz >= m.minZoom && zz >= 0; --zz) {
                    if (!m.hasLevel(zz)) continue;
                    int tx = static_cast<int>(std::floor((px - m.originX) / m.spanX(zz)));
                    int ty = static_cast<int>(std::floor((m.originY - py) / m.spanY(zz)));
                    if (tx < 0 || ty < 0 || tx >= m.levels[zz].matrixWidth || ty >= m.levels[zz].matrixHeight) continue;
                    int status = 0;
                    auto g = queryTile(providers[s], s, zz, tx, ty, status);
                    if (!g) continue;
                    last = g;
                    lastZ = zz;
                    float v = g->sample(px, py);
                    if (!std::isnan(v)) {
                        out[i] = v;
                        if (resolutions) (*resolutions)[i] = metresPerPixel(m, *g, coslat);
                    }
                    break;
                }
            }
        }
        for (double v : out) missing = missing || std::isnan(v);
        if (!missing) break;
    }
    return out;
}

}
