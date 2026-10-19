#include "elevation/elevation.hpp"

#include "gdal/gdal_sources.hpp"
#include "raster/tile_provider.hpp"

#include <stb_image.h>

#include <algorithm>
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

class TerrariumProvider : public ElevationProvider {
public:
    TerrariumProvider(const ElevationLayer& layer, TileCache* cache, std::atomic<bool>* offline) {
        ImageryLayer l;
        l.name = layer.name;
        l.url = layer.url;
        l.maxZoom = layer.maxZoom;
        l.cache = layer.cache;
        inner_ = makeProvider(l, cache, offline);
        matrix_ = TileMatrix::webMercator(0, layer.maxZoom, 256);
    }

    int fetch(int z, int x, int y, HeightGrid& out) override {
        FetchResult r = inner_->fetch(z, x, y);
        if (r.status != 200 || r.data.empty()) return r.status;
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory(r.data.data(), static_cast<int>(r.data.size()), &w, &h, &n, 3);
        if (!px) return 404;
        out.width = w;
        out.height = h;
        out.values.resize(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < out.values.size(); ++i) {
            const unsigned char* p = px + i * 3;
            out.values[i] = static_cast<float>(p[0] * 256.0 + p[1] + p[2] / 256.0 - 32768.0);
        }
        stbi_image_free(px);
        matrix_.tileBounds(z, x, y, out.minX, out.minY, out.maxX, out.maxY);
        return 200;
    }

private:
    std::shared_ptr<TileProvider> inner_;
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
        } else if (l.type == ElevationType::Gdal) {
            if (auto p = makeGdalElevation(l)) providers.push_back(p);
        } else {
            providers.push_back(std::make_shared<TerrariumProvider>(l, cache, offline));
        }
    }
    std::lock_guard lock(mutex_);
    layers_ = layers;
    providers_ = std::move(providers);
    queue_.clear();
    done_.clear();
    grids_.clear();
    used_.clear();
    failed_.clear();
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

bool ElevationManager::gridFor(double west, double south, double east, double north, int posts, double priority,
                               std::vector<float>& heights, bool sampleHeights) {
    heights.assign(sampleHeights ? static_cast<size_t>(posts) * posts : 0, kNaN);
    bool complete = true;
    ++tick_;
    for (int s = 0; s < static_cast<int>(providers_.size()); ++s) {
        const TileMatrix& m = providers_[s]->matrix();
        double maxLat = maxLatitude(m.srid);
        double so = std::max(south, -maxLat), no = std::min(north, maxLat);
        if (so >= no) continue;
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
                    requests_.push_back({k, priority});
                }
        if (!sampleHeights) continue;
        const HeightGrid* last = nullptr;
        bool lastExact = false;
        for (int j = 0; j < posts; ++j) {
            double lat = south + (north - south) * j / (posts - 1);
            double py = projectY(m.srid, lat);
            for (int i = 0; i < posts; ++i) {
                float& h = heights[static_cast<size_t>(j) * posts + i];
                if (!std::isnan(h)) continue;
                double lon = west + (east - west) * i / (posts - 1);
                if (std::abs(lat) > maxLat) continue;
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
        if (std::abs(lat) > maxLatitude(m.srid)) continue;
        double px = projectX(m.srid, lon), py = projectY(m.srid, lat);
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
        if (!inflight_.count(it->first) && seen.insert(it->first).second) queue_.push_back(it->first);
    std::reverse(queue_.begin(), queue_.end());
    if (!queue_.empty()) cv_.notify_all();
}

bool ElevationManager::processResults(size_t max) {
    std::vector<Result> results;
    {
        std::lock_guard lock(mutex_);
        while (!done_.empty() && results.size() < max) {
            if (done_.front().generation == generation_) results.push_back(std::move(done_.front()));
            done_.pop_front();
        }
    }
    for (auto& r : results) {
        if (r.status == 200) grids_[r.key] = std::make_shared<HeightGrid>(std::move(r.grid));
        else if (r.status == 404 || r.status == 204 || r.status == 400) failed_.insert(r.key);
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
        done_.push_back(std::move(r));
    }
}

}
