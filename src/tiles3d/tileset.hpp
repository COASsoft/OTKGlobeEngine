#pragma once

#include "core/camera.hpp"
#include "model/model_layer.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ote {

class TileSource;
class TileCache;

class Tileset3D {
public:
    Tileset3D(const std::string& uri, double maxSse, TileCache* cache = nullptr, const std::atomic<bool>* offline = nullptr);
    ~Tileset3D();

    bool valid() const { return root_ != nullptr; }
    void update(const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame);
    void draw(const Camera& cam, ModelLayer& models);
    bool busy() const;
    std::size_t residentBytes() const { return resident_; }
    int drawnTiles() const { return static_cast<int>(draw_.size()); }
    int failedTiles() const { return failed_; }
    Vec3 center() const;

private:
    struct Node {
        Mat4 transform;
        Vec3 center;
        double radius = 0;
        double geometricError = 0;
        bool add = false;
        std::string content;
        std::string base;
        std::vector<std::unique_ptr<Node>> children;
        std::shared_ptr<GpuMesh> mesh;
        std::atomic<int> state{0};
        bool permanent = false;
        double retryAt = 0;
        std::uint64_t failedEpoch = 0;
        std::uint64_t used = 0;
    };
    struct Job {
        Node* node;
        std::string path;
        double priority;
    };
    struct Done {
        Node* node = nullptr;
        std::unique_ptr<CpuModel> model;
        std::unique_ptr<Node> subtree;
        bool ok = false;
        bool transient = false;
    };

    std::unique_ptr<Node> parse(const void* json, const Mat4& parent, const std::string& base, bool parentAdd);
    void select(Node* n, const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame);
    bool contentReady(const Node* n) const;
    bool hasFallback(const Node* n) const;
    void request(Node* n, double priority);
    void run();
    void evict(std::uint64_t frame);

    std::shared_ptr<TileSource> source_;
    std::unique_ptr<Node> root_;
    double maxSse_;
    std::vector<Node*> draw_;
    std::vector<Job> wanted_;
    std::vector<Node*> loaded_;
    std::size_t resident_ = 0;
    int failed_ = 0;
    std::size_t budget_ = 256u * 1024 * 1024;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Job> queue_;
    std::deque<Done> done_;
    std::vector<std::thread> workers_;
    int inflight_ = 0;
    bool stop_ = false;
};

}
