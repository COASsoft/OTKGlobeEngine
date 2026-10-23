#include "raster/tile_loader.hpp"

#include "core/thread_name.hpp"

#include <map>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "raster/tile_cache.hpp"
#include "raster/tile_package.hpp"
#include "net/http.hpp"

#include "otk/image.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace otk {

namespace {

constexpr size_t kRefreshCapacity = 4096;
constexpr int kRefreshThreads = 2;
constexpr int kDownloadAttempts = 8;

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool transient(int status) { return status == 0 || status == -3 || status == 429 || status >= 500; }

}

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

TileLoader::TileLoader(int localThreads, int networkThreads) {
    for (int i = 0; i < localThreads; ++i)
        workers_.emplace_back([this] {
            setThreadName("otk-local");
            runLocal();
        });
    netLayerCap_ = std::max(2, networkThreads * 2 / 3);
    for (int i = 0; i < networkThreads; ++i)
        workers_.emplace_back([this] {
            setThreadName("otk-net");
            runNetwork();
        });
}

TileLoader::~TileLoader() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    netCv_.notify_all();
    for (auto& t : workers_) t.join();
}

void TileLoader::setLayers(const std::vector<ImageryLayer>& layers, const std::vector<int>& reuse) {
    std::vector<std::shared_ptr<TileProvider>> old;
    {
        std::lock_guard lock(mutex_);
        old = providers_;
    }
    std::vector<std::shared_ptr<TileProvider>> providers;
    for (size_t i = 0; i < layers.size(); ++i) {
        int from = i < reuse.size() ? reuse[i] : -1;
        if (from >= 0 && from < static_cast<int>(old.size())) providers.push_back(old[from]);
        else providers.push_back(makeProvider(layers[i], cache_, &offline_));
    }
    std::lock_guard lock(mutex_);
    layers_ = layers;
    providers_ = std::move(providers);
    queue_.clear();
    for (const TileId& t : netQueue_) inflight_.erase(t);
    netQueue_.clear();
    done_.clear();
    doneIds_.clear();
    refresh_.clear();
    refreshSet_.clear();
    ++generation_;
}

std::vector<bool> TileLoader::vectorLayers() const {
    std::lock_guard lock(mutex_);
    std::vector<bool> out;
    for (const auto& p : providers_) out.push_back(p->vector());
    return out;
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
    wanted_.clear();
    netPriority_.clear();
    for (const auto& r : requests) {
        wanted_.insert(r.first);
        netPriority_[r.first] = r.second;
        if (!inflight_.count(r.first) && !doneIds_.count(r.first)) queue_.push_back(r.first);
    }
    auto stale = std::remove_if(netQueue_.begin(), netQueue_.end(), [this](const TileId& t) {
        if (wanted_.count(t)) return false;
        inflight_.erase(t);
        return true;
    });
    netQueue_.erase(stale, netQueue_.end());
    if (!queue_.empty()) cv_.notify_all();
}

std::vector<DecodedTile> TileLoader::take(size_t max) {
    std::lock_guard lock(mutex_);
    std::vector<DecodedTile> out;
    if (done_.size() > max) {
        auto rank = [this](const DecodedTile& d) {
            if (d.generation != generation_) return -1e300;
            auto p = netPriority_.find(d.id);
            return p == netPriority_.end() ? 1e299 : p->second;
        };
        std::stable_sort(done_.begin(), done_.end(), [&](const DecodedTile& a, const DecodedTile& b) { return rank(a) < rank(b); });
    }
    while (!done_.empty() && out.size() < max) {
        doneIds_.erase(done_.front().id);
        if (done_.front().generation == generation_) out.push_back(std::move(done_.front()));
        done_.pop_front();
    }
    return out;
}

std::string TileLoader::debug() const {
    std::lock_guard lock(mutex_);
    int netWanted = 0;
    for (const TileId& t : netQueue_) netWanted += wanted_.count(t) ? 1 : 0;
    std::string s = "queue " + std::to_string(queue_.size()) + " net " + std::to_string(netQueue_.size()) + " netwanted " +
                    std::to_string(netWanted) + " inflight " + std::to_string(inflight_.size()) + " wanted " +
                    std::to_string(wanted_.size()) + " done " + std::to_string(done_.size());
    std::map<int, int> queued;
    for (const TileId& t : netQueue_) ++queued[t.layer];
    s += " layers";
    for (const auto& [layer, n] : queued) s += " " + std::to_string(layer) + ":q" + std::to_string(n);
    for (const auto& [layer, n] : netLayerBusy_)
        if (n > 0) s += " " + std::to_string(layer) + ":b" + std::to_string(n);
    int shown = 0;
    for (const TileId& t : inflight_) {
        if (shown++ >= 4) break;
        s += " [" + std::to_string(t.layer) + ":" + std::to_string(t.z) + "/" + std::to_string(t.x) + "/" + std::to_string(t.y) +
             (wanted_.count(t) ? " w" : " -") + "]";
    }
    return s;
}

void TileLoader::giveBack(DecodedTile& tile) {
    std::lock_guard lock(mutex_);
    if (doneIds_.insert(tile.id).second) done_.push_front(std::move(tile));
}

int TileLoader::placeholders() const {
    std::lock_guard lock(mutex_);
    return placeholders_;
}

int TileLoader::pending() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size() + inflight_.size());
}

bool TileLoader::hasResults() const {
    std::lock_guard lock(mutex_);
    return !done_.empty();
}

int TileLoader::startDownload(int layer, const Region& region, int minZoom, int maxZoom, std::int64_t maxBytes) {
    std::lock_guard lock(mutex_);
    if (layer < 0 || layer >= static_cast<int>(providers_.size())) return 0;
    Download d;
    d.layer = layer;
    d.maxBytes = maxBytes;
    d.generation = generation_;
    d.ranges = regionRanges(providers_[layer]->matrix(), region, minZoom, maxZoom);
    for (const auto& r : d.ranges) d.status.total += r.count();
    d.status.finished = d.status.total == 0 || !providers_[layer]->remote();
    int id = nextDownloadId_++;
    downloads_[id] = std::move(d);
    netCv_.notify_all();
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
    it->second.retry.clear();
    it->second.status.retrying = 0;
}

std::int64_t TileLoader::exportTiles(int layer, const Region& region, int minZoom, int maxZoom, const std::string& path,
                                     std::string& error) {
    std::shared_ptr<TileProvider> provider;
    std::string name;
    {
        std::lock_guard lock(mutex_);
        if (layer < 0 || layer >= static_cast<int>(providers_.size())) {
            error = "no such layer";
            return -1;
        }
        provider = providers_[layer];
        name = layers_[layer].name;
    }
    const TileMatrix& m = provider->matrix();
    TilePackageWriter writer;
    if (!writer.open(path, m, name, error)) return -1;
    std::int64_t written = 0;
    std::string key = provider->cacheKey();
    for (const TileRange& r : regionRanges(m, region, std::max(minZoom, m.minZoom), std::min(maxZoom, m.maxZoom))) {
        for (int y = r.y0; y <= r.y1; ++y) {
            if (!key.empty() && cache_) {
                cache_->forEach(key, r.z, r.x0, r.x1, y, y, [&](int x, int yy, const std::vector<std::uint8_t>& data) {
                    if (writer.put(r.z, x, yy, data)) ++written;
                });
                continue;
            }
            if (provider->vector() || provider->remote()) continue;
            for (int x = r.x0; x <= r.x1; ++x) {
                FetchResult res;
                if (!provider->fetchLocal(r.z, x, y, res) || res.status != 200 || res.raw || res.data.empty()) continue;
                if (writer.put(r.z, x, y, res.data)) ++written;
            }
        }
    }
    double west = std::max(region.west, -180.0), east = std::min(region.east, 180.0);
    double south = std::max(region.south, -maxLatitude(m.srid)), north = std::min(region.north, maxLatitude(m.srid));
    writer.extend(west, south, east, north);
    writer.close();
    return written;
}

std::int64_t TileLoader::importTiles(int layer, const std::string& path, std::string& error) {
    std::shared_ptr<TileProvider> provider;
    {
        std::lock_guard lock(mutex_);
        if (layer < 0 || layer >= static_cast<int>(providers_.size())) {
            error = "no such layer";
            return -1;
        }
        provider = providers_[layer];
    }
    std::string key = provider->cacheKey();
    if (key.empty() || !cache_ || !cache_->isOpen()) {
        error = "layer has no tile cache (set a cache directory and use an online layer with caching on)";
        return -1;
    }
    const TileMatrix& target = provider->matrix();
    TileMatrix source;
    std::vector<int> levelMap;
    bool mapped = false;
    std::int64_t imported = 0, skipped = 0;
    auto mapLevels = [&]() {
        mapped = true;
        levelMap.assign(source.levels.size(), -1);
        double tol = source.srid == 4326 ? 1e-7 : 1e-2;
        if (source.srid != target.srid || std::abs(source.originX - target.originX) > tol || std::abs(source.originY - target.originY) > tol)
            return;
        for (int sz = 0; sz < static_cast<int>(source.levels.size()); ++sz) {
            if (!source.hasLevel(sz)) continue;
            const TileLevel& a = source.levels[sz];
            for (int tz = 0; tz < static_cast<int>(target.levels.size()); ++tz) {
                const TileLevel& b = target.levels[tz];
                if (a.tileWidth == b.tileWidth && std::abs(a.resolutionX - b.resolutionX) <= b.resolutionX * 1e-6) {
                    levelMap[sz] = tz;
                    break;
                }
            }
        }
    };
    cache_->begin();
    int batch = 0;
    bool ok = readTilePackage(path, source, [&](int z, int x, int y, const std::vector<std::uint8_t>& data) {
        if (!mapped) mapLevels();
        int tz = z >= 0 && z < static_cast<int>(levelMap.size()) ? levelMap[z] : -1;
        if (tz < 0 || data.empty()) {
            ++skipped;
            return true;
        }
        cache_->put(key, tz, x, y, data, TileCache::now() + (std::int64_t(1) << 40), true);
        ++imported;
        if (++batch >= 2000) {
            cache_->commit();
            cache_->begin();
            batch = 0;
        }
        return true;
    }, error);
    cache_->commit();
    if (!ok) return -1;
    if (imported == 0 && skipped > 0) error = "package tiling does not match the layer's tile matrix";
    return imported;
}

bool TileLoader::nextDownload(int& id, TileId& tile, int& attempt, bool& blocked) {
    double t = seconds();
    for (auto& [k, d] : downloads_) {
        if (d.status.finished || d.generation != generation_) continue;
        if (d.layer < 0 || d.layer >= static_cast<int>(providers_.size())) continue;
        const auto& provider = providers_[d.layer];
        const int own = provider->maxConcurrent();
        if (own > 0 && d.inflight >= own) {
            blocked = true;
            continue;
        }
        while (d.range < d.ranges.size()) {
            const TileRange& r = d.ranges[d.range];
            if (d.index < r.count()) {
                int w = r.x1 - r.x0 + 1;
                TileId next{d.layer, r.z, r.x0 + static_cast<int>(d.index % w), r.y0 + static_cast<int>(d.index / w)};
                if (!provider->available(next.z, next.x, next.y)) {
                    blocked = true;
                    break;
                }
                tile = next;
                ++d.index;
                ++d.inflight;
                id = k;
                attempt = 0;
                return true;
            }
            ++d.range;
            d.index = 0;
        }
        if (d.range < d.ranges.size() || d.retry.empty()) continue;
        if (t < d.retryAt) {
            blocked = true;
            continue;
        }
        auto [next, tries] = d.retry.front();
        if (!provider->available(next.z, next.x, next.y)) {
            d.retryAt = t + 2.0;
            blocked = true;
            continue;
        }
        d.retry.pop_front();
        d.status.retrying = static_cast<std::int64_t>(d.retry.size());
        tile = next;
        ++d.inflight;
        id = k;
        attempt = tries;
        return true;
    }
    return false;
}

void TileLoader::finish(const TileId& id, int gen, const std::shared_ptr<TileProvider>& provider, FetchResult& res) {
    DecodedTile tile;
    tile.id = id;
    tile.generation = gen;
    auto undecodable = [&]() {
        provider->invalidate(id.z, id.x, id.y);
        std::lock_guard lock(mutex_);
        if (decodeFailures_.size() > 20000) decodeFailures_.clear();
        tile.permanent = ++decodeFailures_[id] >= 3;
    };
    if (res.status == 200 && provider->vector()) {
        bool overlay = false;
        {
            std::lock_guard lock(mutex_);
            overlay = id.layer >= 0 && id.layer < static_cast<int>(layers_.size()) && layers_[id.layer].labels;
        }
        tile.vector = overlay ? decodeOverlayTile(res.data, id.z, id.x, id.y) : decodeVectorTile(res.data, id.z, id.x, id.y);
        tile.ok = tile.vector != nullptr;
        if (!tile.ok) undecodable();
    } else if (res.status == 200 && res.raw) {
        tile.ok = true;
        tile.width = res.width;
        tile.height = res.height;
        tile.rgba = std::move(res.data);
    } else if (res.status == 200 && !res.data.empty()) {
        if (id.z >= 9 && provider->placeholderCheck()) {
            std::uint64_t hash = 1469598103934665603ull;
            for (unsigned char c : res.data) hash = (hash ^ c) * 1099511628211ull;
            hash ^= static_cast<std::uint64_t>(id.layer) << 56;
            std::vector<TileId> revoke;
            bool placeholder = false;
            {
                std::lock_guard lock(mutex_);
                if (content_.size() > 50000) content_.clear();
                ContentSeen& seen = content_[hash];
                if (!seen.placeholder && std::find(seen.ids.begin(), seen.ids.end(), id) == seen.ids.end()) {
                    seen.ids.push_back(id);
                    if (seen.ids.size() >= 3) {
                        seen.placeholder = true;
                        ++placeholders_;
#if defined(__ANDROID__)
                        if (placeholders_ <= 8)
                            __android_log_print(ANDROID_LOG_INFO, "OTKGlobeEngine", "placeholder layer %d z%d %d/%d bytes %zu status %d", id.layer, id.z,
                                                id.x, id.y, res.data.size(), res.status);
#endif
                        revoke = seen.ids;
                        seen.ids.clear();
                        seen.ids.shrink_to_fit();
                    }
                }
                placeholder = seen.placeholder;
                for (const TileId& r : revoke) {
                    if (r == id) continue;
                    DecodedTile gone;
                    gone.id = r;
                    gone.generation = gen;
                    gone.permanent = true;
                    if (doneIds_.insert(r).second) done_.push_back(std::move(gone));
                }
            }
            if (placeholder) {
                std::lock_guard lock(mutex_);
                inflight_.erase(id);
                tile.permanent = true;
                if (doneIds_.insert(id).second) done_.push_back(std::move(tile));
                return;
            }
        }
        Image img;
        if (decodeImage(res.data.data(), res.data.size(), img)) {
            tile.ok = true;
            tile.width = img.width;
            tile.height = img.height;
            tile.rgba = std::move(img.rgba);
        } else {
            undecodable();
        }
    } else if (res.status == 404 || res.status == 204 || res.status == 400 || res.status == -2 || res.status == -3 ||
               (res.status == 200 && res.data.empty())) {
        tile.permanent = true;
    }
    std::lock_guard lock(mutex_);
    inflight_.erase(id);
    if (doneIds_.insert(id).second) done_.push_back(std::move(tile));
}

void TileLoader::runLocal() {
    for (;;) {
        TileId id;
        std::shared_ptr<TileProvider> provider;
        int gen = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            id = queue_.back();
            queue_.pop_back();
            if (id.layer < 0 || id.layer >= static_cast<int>(providers_.size())) continue;
            provider = providers_[id.layer];
            gen = generation_;
            inflight_.insert(id);
        }
        FetchResult res;
        if (provider->fetchLocal(id.z, id.x, id.y, res)) {
            if (res.stale) {
                staleServed_.fetch_add(1);
                if (provider->remote() && !offline_.load()) {
                    std::lock_guard lock(mutex_);
                    if (gen == generation_ && refreshSet_.size() < kRefreshCapacity && refreshSet_.insert(id).second) {
                        refresh_.push_back(id);
                        netCv_.notify_one();
                    }
                }
            }
            finish(id, gen, provider, res);
            continue;
        }
        std::lock_guard lock(mutex_);
        netQueue_.push_back(id);
        netCv_.notify_one();
    }
}

void TileLoader::runNetwork() {
    for (;;) {
        TileId id;
        std::shared_ptr<TileProvider> provider;
        int gen = 0;
        int download = 0;
        int attempt = 0;
        bool refreshing = false;
        {
            std::unique_lock lock(mutex_);
            bool woke = netCv_.wait_for(lock, std::chrono::seconds(3), [this] {
                if (stop_ || !netQueue_.empty()) return true;
                if (!refresh_.empty() && refreshBusy_ < kRefreshThreads) return true;
                for (auto& [k, d] : downloads_)
                    if (!d.status.finished && d.generation == generation_ && (d.range < d.ranges.size() || !d.retry.empty())) return true;
                return false;
            });
            if (stop_) return;
            if (!woke) {
                if (net::hostsDown() > 0 && !probing_.exchange(true)) {
                    lock.unlock();
                    net::probeDueHosts("OTKGlobeEngine/0.1");
                    probing_ = false;
                }
                continue;
            }
            bool picked = false;
            bool capped = false;
            while (!netQueue_.empty() && !picked) {
                auto best = netQueue_.end();
                auto fallback = netQueue_.end();
                double bp = 1e300, fp = 1e300;
                for (auto it = netQueue_.begin(); it != netQueue_.end(); ++it) {
                    if (!wanted_.count(*it)) {
                        best = it;
                        break;
                    }
                    auto p = netPriority_.find(*it);
                    double v = p == netPriority_.end() ? 1e299 : p->second;
                    int cap = netLayerCap_;
                    int own = 0;
                    if (it->layer >= 0 && it->layer < static_cast<int>(providers_.size())) {
                        own = providers_[it->layer]->maxConcurrent();
                        if (own > 0) cap = std::min(cap, own);
                    }
                    if (netLayerBusy_[it->layer] >= cap) {
                        capped = true;
                        if (own > 0 && netLayerBusy_[it->layer] >= own) continue;
                        if (v < fp) {
                            fp = v;
                            fallback = it;
                        }
                        continue;
                    }
                    if (v < bp) {
                        bp = v;
                        best = it;
                    }
                }
                if (best == netQueue_.end()) {
                    bool othersBusy = false;
                    if (fallback != netQueue_.end())
                        for (const auto& [layer, busy] : netLayerBusy_)
                            if (layer != fallback->layer && busy > 0) othersBusy = true;
                    if (fallback != netQueue_.end() && !othersBusy) best = fallback;
                }
                if (best == netQueue_.end()) break;
                id = *best;
                netQueue_.erase(best);
                if (!wanted_.count(id)) {
                    inflight_.erase(id);
                    continue;
                }
                picked = true;
            }
            bool blocked = false;
            if (!picked && !nextDownload(download, id, attempt, blocked)) {
                if (!refresh_.empty() && refreshBusy_ < kRefreshThreads) {
                    id = refresh_.front();
                    refresh_.pop_front();
                    refreshSet_.erase(id);
                    if (id.layer < 0 || id.layer >= static_cast<int>(providers_.size())) continue;
                    if (!providers_[id.layer]->available(id.z, id.x, id.y)) continue;
                    refreshing = true;
                    ++refreshBusy_;
                } else {
                    if (capped) netCv_.wait_for(lock, std::chrono::milliseconds(50));
                    else if (blocked) netCv_.wait_for(lock, std::chrono::milliseconds(500));
                    continue;
                }
            }
            if (picked) ++netLayerBusy_[id.layer];
            if (id.layer < 0 || id.layer >= static_cast<int>(providers_.size())) {
                inflight_.erase(id);
                continue;
            }
            provider = providers_[id.layer];
            gen = generation_;
        }

        if (refreshing) {
            provider->refresh(id.z, id.x, id.y);
            std::lock_guard lock(mutex_);
            --refreshBusy_;
            continue;
        }
        if (download) {
            std::int64_t bytes = 0;
            int status = provider->download(id.z, id.x, id.y, bytes);
            std::lock_guard lock(mutex_);
            auto it = downloads_.find(download);
            if (it != downloads_.end()) {
                Download& d = it->second;
                --d.inflight;
                d.bytes += bytes;
                if (status == 200) {
                    ++d.status.done;
                } else if (transient(status) && attempt + 1 < kDownloadAttempts && !d.status.cancelled) {
                    d.retry.push_back({id, attempt + 1});
                    d.retryAt = std::max(d.retryAt, seconds() + (status == -3 ? 5.0 : 1.0 + attempt));
                    d.status.retrying = static_cast<std::int64_t>(d.retry.size());
                } else {
                    ++d.status.failed;
                }
                if (d.maxBytes > 0 && d.bytes >= d.maxBytes) {
                    d.range = d.ranges.size();
                    d.retry.clear();
                    d.status.retrying = 0;
                }
                if (d.range >= d.ranges.size() && d.inflight == 0 && d.retry.empty()) d.status.finished = true;
            }
            continue;
        }
        FetchResult res = provider->fetch(id.z, id.x, id.y);
        {
            std::lock_guard lock(mutex_);
            if (--netLayerBusy_[id.layer] < 0) netLayerBusy_[id.layer] = 0;
        }
        netCv_.notify_one();
        finish(id, gen, provider, res);
    }
}

}
