#pragma once

#include "ote/engine.hpp"
#include "raster/tile_matrix.hpp"
#include "raster/tile_provider.hpp"
#include "raster/vector_tile.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
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
    std::shared_ptr<VectorTile> vector;
};

struct TileRange {
    int z = 0;
    int x0 = 0, x1 = -1, y0 = 0, y1 = -1;
    std::int64_t count() const { return static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1); }
};

std::vector<TileRange> regionRanges(const TileMatrix& m, const Region& r, int minZoom, int maxZoom);

class TileLoader {
public:
    TileLoader(int localThreads, int networkThreads);
    ~TileLoader();

    void setCache(TileCache* cache) { cache_ = cache; }
    void setOffline(bool offline) { offline_ = offline; }
    void setLayers(const std::vector<ImageryLayer>& layers, const std::vector<int>& reuse = {});
    std::vector<TileMatrix> matrices() const;
    std::vector<bool> vectorLayers() const;
    int generation() const { return generation_; }
    void submit(std::vector<std::pair<TileId, double>>& requests);
    std::vector<DecodedTile> take(size_t max);
    void giveBack(DecodedTile& tile);
    int placeholders() const;
    int pending() const;
    std::string debug() const;

    int startDownload(int layer, const Region& region, int minZoom, int maxZoom);
    DownloadStatus downloadStatus(int id) const;
    void cancelDownload(int id);
    std::int64_t exportTiles(int layer, const Region& region, int minZoom, int maxZoom, const std::string& path, std::string& error);
    std::int64_t importTiles(int layer, const std::string& path, std::string& error);
    std::int64_t staleServed() const { return staleServed_.load(); }

private:
    struct Download {
        int layer = 0;
        int generation = 0;
        std::vector<TileRange> ranges;
        size_t range = 0;
        std::int64_t index = 0;
        std::int64_t inflight = 0;
        std::deque<std::pair<TileId, int>> retry;
        double retryAt = 0;
        DownloadStatus status;
    };

    void runLocal();
    void runNetwork();
    bool nextDownload(int& id, TileId& tile, int& attempt, bool& blocked);
    void finish(const TileId& id, int generation, const std::shared_ptr<TileProvider>& provider, FetchResult& res);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::vector<ImageryLayer> layers_;
    std::vector<std::shared_ptr<TileProvider>> providers_;
    std::vector<TileId> queue_;
    std::vector<TileId> netQueue_;
    std::unordered_set<TileId, TileIdHash> wanted_;
    std::unordered_map<TileId, double, TileIdHash> netPriority_;
    std::condition_variable netCv_;
    std::unordered_set<TileId, TileIdHash> inflight_;
    std::deque<DecodedTile> done_;
    std::unordered_set<TileId, TileIdHash> doneIds_;
    std::unordered_map<TileId, int, TileIdHash> decodeFailures_;
    struct ContentSeen {
        std::vector<TileId> ids;
        bool placeholder = false;
    };
    std::unordered_map<std::uint64_t, ContentSeen> content_;
    int placeholders_ = 0;
    std::unordered_map<int, int> netLayerBusy_;
    int netLayerCap_ = 2;
    std::map<int, Download> downloads_;
    std::deque<TileId> refresh_;
    std::unordered_set<TileId, TileIdHash> refreshSet_;
    int refreshBusy_ = 0;
    std::atomic<std::int64_t> staleServed_{0};
    std::atomic<bool> probing_{false};
    int nextDownloadId_ = 1;
    TileCache* cache_ = nullptr;
    std::atomic<bool> offline_{false};
    int generation_ = 0;
    bool stop_ = false;
};

}
