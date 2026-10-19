#pragma once

#include "ote/engine.hpp"
#include "raster/tile_matrix.hpp"
#include "raster/tile_provider.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace ote {

class TileCache;

struct TileId {
    int layer = 0;
    int z = 0;
    int x = 0;
    int y = 0;
    bool operator==(const TileId& o) const { return layer == o.layer && z == o.z && x == o.x && y == o.y; }
};

struct TileIdHash {
    size_t operator()(const TileId& t) const {
        std::uint64_t h = (static_cast<std::uint64_t>(t.layer) << 58) ^ (static_cast<std::uint64_t>(t.z) << 52) ^
                          (static_cast<std::uint64_t>(t.x) << 26) ^ static_cast<std::uint64_t>(t.y);
        return std::hash<std::uint64_t>()(h);
    }
};

struct DecodedTile {
    TileId id;
    int generation = 0;
    bool ok = false;
    bool permanent = false;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct TileRange {
    int z = 0;
    int x0 = 0, x1 = -1, y0 = 0, y1 = -1;
    std::int64_t count() const { return static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1); }
};

std::vector<TileRange> regionRanges(const TileMatrix& m, const Region& r, int minZoom, int maxZoom);

class TileLoader {
public:
    explicit TileLoader(int threads);
    ~TileLoader();

    void setCache(TileCache* cache) { cache_ = cache; }
    void setOffline(bool offline) { offline_ = offline; }
    void setLayers(const std::vector<ImageryLayer>& layers);
    std::vector<TileMatrix> matrices() const;
    int generation() const { return generation_; }
    void submit(std::vector<std::pair<TileId, double>>& requests);
    std::vector<DecodedTile> take(size_t max);
    int pending() const;

    int startDownload(int layer, const Region& region, int minZoom, int maxZoom);
    DownloadStatus downloadStatus(int id) const;
    void cancelDownload(int id);

private:
    struct Download {
        int layer = 0;
        int generation = 0;
        std::vector<TileRange> ranges;
        size_t range = 0;
        std::int64_t index = 0;
        std::int64_t inflight = 0;
        DownloadStatus status;
    };

    void run();
    bool nextDownload(int& id, TileId& tile);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::vector<ImageryLayer> layers_;
    std::vector<std::shared_ptr<TileProvider>> providers_;
    std::vector<TileId> queue_;
    std::unordered_set<TileId, TileIdHash> inflight_;
    std::deque<DecodedTile> done_;
    std::map<int, Download> downloads_;
    int nextDownloadId_ = 1;
    TileCache* cache_ = nullptr;
    std::atomic<bool> offline_{false};
    int generation_ = 0;
    bool stop_ = false;
};

}
