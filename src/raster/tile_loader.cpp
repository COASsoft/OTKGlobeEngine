#include "raster/tile_loader.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace ote {

std::vector<TileRange> regionRanges(const TileMatrix& m, const Region& r, int minZoom, int maxZoom) {
    std::vector<TileRange> out;
    double maxLat = maxLatitude(m.srid);
    double south = std::max(r.south, -maxLat), north = std::min(r.north, maxLat);
    if (south >= north || r.west >= r.east) return out;
    double x0 = projectX(m.srid, r.west), x1 = projectX(m.srid, r.east);
    double yTop = projectY(m.srid, north), yBottom = projectY(m.srid, south);
    for (int z = std::max(minZoom, 0); z <= maxZoom; ++z) {
        if (!m.hasLevel(z)) continue;
        const TileLevel& l = m.levels[z];
        TileRange t;
        t.z = z;
        t.x0 = std::clamp(static_cast<int>(std::floor((x0 - m.originX) / m.spanX(z))), 0, l.matrixWidth - 1);
        t.x1 = std::clamp(static_cast<int>(std::floor((x1 - m.originX) / m.spanX(z) - 1e-9)), 0, l.matrixWidth - 1);
        t.y0 = std::clamp(static_cast<int>(std::floor((m.originY - yTop) / m.spanY(z))), 0, l.matrixHeight - 1);
        t.y1 = std::clamp(static_cast<int>(std::floor((m.originY - yBottom) / m.spanY(z) - 1e-9)), 0, l.matrixHeight - 1);
        if (t.x1 >= t.x0 && t.y1 >= t.y0) out.push_back(t);
    }
    return out;
}

TileLoader::TileLoader(int threads) {
    for (int i = 0; i < threads; ++i) workers_.emplace_back([this] { run(); });
}

TileLoader::~TileLoader() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
}

void TileLoader::setLayers(const std::vector<ImageryLayer>& layers) {
    std::vector<std::shared_ptr<TileProvider>> providers;
    for (const auto& l : layers) providers.push_back(makeProvider(l, cache_, &offline_));
    std::lock_guard lock(mutex_);
    layers_ = layers;
    providers_ = std::move(providers);
    queue_.clear();
    done_.clear();
    ++generation_;
}

std::vector<TileMatrix> TileLoader::matrices() const {
    std::lock_guard lock(mutex_);
    std::vector<TileMatrix> out;
    for (const auto& p : providers_) out.push_back(p->matrix());
    return out;
}

void TileLoader::submit(std::vector<std::pair<TileId, double>>& requests) {
    std::sort(requests.begin(), requests.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::lock_guard lock(mutex_);
    queue_.clear();
    queue_.reserve(requests.size());
    for (const auto& r : requests)
        if (!inflight_.count(r.first)) queue_.push_back(r.first);
    if (!queue_.empty()) cv_.notify_all();
}

std::vector<DecodedTile> TileLoader::take(size_t max) {
    std::lock_guard lock(mutex_);
    std::vector<DecodedTile> out;
    while (!done_.empty() && out.size() < max) {
        if (done_.front().generation == generation_) out.push_back(std::move(done_.front()));
        done_.pop_front();
    }
    return out;
}

int TileLoader::pending() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size() + inflight_.size());
}

int TileLoader::startDownload(int layer, const Region& region, int minZoom, int maxZoom) {
    std::lock_guard lock(mutex_);
    if (layer < 0 || layer >= static_cast<int>(providers_.size())) return 0;
    Download d;
    d.layer = layer;
    d.generation = generation_;
    d.ranges = regionRanges(providers_[layer]->matrix(), region, minZoom, maxZoom);
    for (const auto& r : d.ranges) d.status.total += r.count();
    d.status.finished = d.status.total == 0 || !providers_[layer]->remote();
    int id = nextDownloadId_++;
    downloads_[id] = std::move(d);
    cv_.notify_all();
    return id;
}

DownloadStatus TileLoader::downloadStatus(int id) const {
    std::lock_guard lock(mutex_);
    auto it = downloads_.find(id);
    return it == downloads_.end() ? DownloadStatus{} : it->second.status;
}

void TileLoader::cancelDownload(int id) {
    std::lock_guard lock(mutex_);
    auto it = downloads_.find(id);
    if (it == downloads_.end()) return;
    it->second.status.cancelled = true;
    it->second.status.finished = true;
}

bool TileLoader::nextDownload(int& id, TileId& tile) {
    for (auto& [k, d] : downloads_) {
        if (d.status.finished || d.generation != generation_) continue;
        while (d.range < d.ranges.size()) {
            const TileRange& r = d.ranges[d.range];
            if (d.index < r.count()) {
                int w = r.x1 - r.x0 + 1;
                tile = {d.layer, r.z, r.x0 + static_cast<int>(d.index % w), r.y0 + static_cast<int>(d.index / w)};
                ++d.index;
                ++d.inflight;
                id = k;
                return true;
            }
            ++d.range;
            d.index = 0;
        }
    }
    return false;
}

void TileLoader::run() {
    for (;;) {
        TileId id;
        std::shared_ptr<TileProvider> provider;
        int gen = 0;
        int download = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                if (stop_ || !queue_.empty()) return true;
                for (auto& [k, d] : downloads_)
                    if (!d.status.finished && d.generation == generation_ && d.range < d.ranges.size()) return true;
                return false;
            });
            if (stop_) return;
            if (!queue_.empty()) {
                id = queue_.back();
                queue_.pop_back();
                inflight_.insert(id);
            } else if (!nextDownload(download, id)) {
                continue;
            }
            if (id.layer < 0 || id.layer >= static_cast<int>(providers_.size())) {
                inflight_.erase(id);
                continue;
            }
            provider = providers_[id.layer];
            gen = generation_;
        }

        if (download) {
            bool ok = provider->cached(id.z, id.x, id.y) || provider->fetch(id.z, id.x, id.y).status == 200;
            std::lock_guard lock(mutex_);
            auto it = downloads_.find(download);
            if (it != downloads_.end()) {
                Download& d = it->second;
                --d.inflight;
                ++(ok ? d.status.done : d.status.failed);
                if (d.range >= d.ranges.size() && d.inflight == 0) d.status.finished = true;
            }
            continue;
        }

        DecodedTile tile;
        tile.id = id;
        tile.generation = gen;
        FetchResult res = provider->fetch(id.z, id.x, id.y);
        if (res.status == 200 && res.raw) {
            tile.ok = true;
            tile.width = res.width;
            tile.height = res.height;
            tile.rgba = std::move(res.data);
        } else if (res.status == 200 && !res.data.empty()) {
            int w = 0, h = 0, n = 0;
            unsigned char* px = stbi_load_from_memory(res.data.data(), static_cast<int>(res.data.size()), &w, &h, &n, 4);
            if (px) {
                tile.ok = true;
                tile.width = w;
                tile.height = h;
                tile.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
                stbi_image_free(px);
            } else {
                tile.permanent = true;
            }
        } else if (res.status == 404 || res.status == 204 || res.status == 400 || (res.status == 200 && res.data.empty())) {
            tile.permanent = true;
        }
        std::lock_guard lock(mutex_);
        inflight_.erase(id);
        done_.push_back(std::move(tile));
    }
}

}
