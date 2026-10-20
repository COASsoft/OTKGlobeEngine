#pragma once

#include "ote/engine.hpp"
#include "raster/tile_loader.hpp"
#include "raster/tile_matrix.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ote {

class TileCache;

struct HeightGrid {
    int width = 0;
    int height = 0;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    std::vector<float> values;
    float sample(double x, double y) const;
};

class ElevationProvider {
public:
    virtual ~ElevationProvider() = default;
    const TileMatrix& matrix() const { return matrix_; }
    virtual int fetch(int z, int x, int y, HeightGrid& out) = 0;

protected:
    TileMatrix matrix_;
};

class ElevationManager {
public:
    explicit ElevationManager(int threads);
    ~ElevationManager();

    void setSources(const std::vector<ElevationLayer>& layers, TileCache* cache, std::atomic<bool>* offline);
    const std::vector<ElevationLayer>& sources() const { return layers_; }
    bool empty() const { return providers_.empty(); }
    int version() const { return version_; }

    bool gridFor(double west, double south, double east, double north, int posts, double priority,
                 std::vector<float>& heights, bool sampleHeights = true);
    double sample(double lat, double lon) const;
    std::vector<double> query(const std::vector<std::pair<double, double>>& latLon, double resolutionMeters,
                              std::vector<double>* resolutions = nullptr) const;
    void beginFrame() { requests_.clear(); }
    void submit();
    bool processResults(size_t max);
    int pending() const;
    void clearFailures();

private:
    struct Key {
        int source, z, x, y;
        bool operator==(const Key& o) const { return source == o.source && z == o.z && x == o.x && y == o.y; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return TileIdHash()(TileId{k.source, k.z, k.x, k.y});
        }
    };
    struct Result {
        Key key;
        int generation = 0;
        int status = 0;
        HeightGrid grid;
    };

    const HeightGrid* find(int source, int z, int x, int y) const;
    double finestMeters(int source) const;
    bool yields(int source, double targetMeters) const;
    std::shared_ptr<const HeightGrid> queryTile(const std::shared_ptr<ElevationProvider>& provider, int source, int z, int x, int y, int& status) const;
    const HeightGrid* best(int source, int z, double px, double py, bool& exact) const;
    void run();

    std::vector<ElevationLayer> layers_;
    std::vector<std::shared_ptr<ElevationProvider>> providers_;
    std::unordered_map<Key, std::shared_ptr<HeightGrid>, KeyHash> grids_;
    std::unordered_map<Key, std::uint64_t, KeyHash> used_;
    std::unordered_set<Key, KeyHash> failed_;
    std::unordered_map<Key, std::pair<double, int>, KeyHash> retry_;
    std::vector<std::pair<Key, double>> requests_;
    std::uint64_t tick_ = 0;
    int version_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::vector<Key> queue_;
    std::unordered_set<Key, KeyHash> inflight_;
    std::deque<Result> done_;
    std::unordered_set<Key, KeyHash> doneKeys_;
    int generation_ = 0;
    bool stop_ = false;
    std::vector<double> finest_;

    mutable std::mutex queryMutex_;
    mutable std::unordered_map<Key, std::pair<std::shared_ptr<const HeightGrid>, std::uint64_t>, KeyHash> queryTiles_;
    mutable std::unordered_map<Key, int, KeyHash> queryMissing_;
    mutable std::uint64_t queryTick_ = 0;
    mutable int queryGeneration_ = -1;
};

}
