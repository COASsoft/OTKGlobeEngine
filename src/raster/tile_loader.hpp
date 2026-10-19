#pragma once

#include "ote/engine.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ote {

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

std::string tileUrl(const ImageryLayer& layer, int z, int x, int y);

class TileLoader {
public:
    explicit TileLoader(int threads);
    ~TileLoader();

    void setLayers(const std::vector<ImageryLayer>& layers);
    int generation() const { return generation_; }
    void submit(std::vector<std::pair<TileId, double>>& requests);
    std::vector<DecodedTile> take(size_t max);
    int pending() const;

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::vector<ImageryLayer> layers_;
    std::vector<TileId> queue_;
    std::unordered_set<TileId, TileIdHash> inflight_;
    std::deque<DecodedTile> done_;
    int generation_ = 0;
    bool stop_ = false;
};

}
