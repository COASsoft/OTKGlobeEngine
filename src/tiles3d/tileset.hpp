#pragma once

#include "core/camera.hpp"
#include "model/model_layer.hpp"
#include "model/pointcloud.hpp"
#include "ote/engine.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
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
    Tileset3D(const std::string& uri, const TilesetOptions& options, TileCache* cache = nullptr, const std::atomic<bool>* offline = nullptr);
    ~Tileset3D();

    bool valid() const { return root_ != nullptr; }
    void update(const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame);
    void draw(const Camera& cam, ModelLayer& models, PointCloudLayer& points, double dpi);
    void setOpacity(float opacity) { opacity_ = opacity; }
    float opacity() const { return opacity_; }
    bool busy() const;
    std::size_t residentBytes() const { return resident_; }
    int drawnTiles() const { return static_cast<int>(draw_.size()); }
    int failedTiles() const { return failed_; }
    Vec3 center() const;
    double radius() const;
    const std::vector<std::pair<std::string, int>>& credits() const { return credits_; }

    struct Implicit;
    struct Subtree;

private:
    float opacity_ = 1.0f;
    struct Node {
        Mat4 transform;
        Vec3 center;
        double radius = 0;
        double geometricError = 0;
        bool add = false;
        std::string content;
        std::string base;
        std::string query;
        std::vector<std::unique_ptr<Node>> children;
        std::vector<std::shared_ptr<GpuMesh>> meshes;
        std::atomic<int> state{0};
        bool permanent = false;
        double retryAt = 0;
        std::uint64_t failedEpoch = 0;
        std::uint64_t used = 0;
        std::shared_ptr<const Implicit> implicit;
        std::shared_ptr<const Subtree> subtree;
        int level = 0;
        std::uint32_t x = 0, y = 0, z = 0;
        int rootLevel = 0;
        std::uint32_t rootX = 0, rootY = 0, rootZ = 0;
        std::atomic<int> subtreeState{2};
        bool subtreePermanent = false;
        double subtreeRetryAt = 0;
        bool expanded = false;
    };
    struct Job {
        Node* node;
        std::string path;
        double priority;
        bool subtree = false;
    };
    struct Done {
        Node* node = nullptr;
        std::vector<CpuModel> models;
        std::unique_ptr<Node> subtree;
        std::shared_ptr<const Subtree> availability;
        bool isSubtree = false;
        bool ok = false;
        bool transient = false;
    };

    std::unique_ptr<Node> parse(const void* json, const Mat4& parent, const std::string& base, const std::string& query, bool parentAdd);
    std::unique_ptr<Node> implicitNode(const std::shared_ptr<const Implicit>& im, int level, std::uint32_t x, std::uint32_t y, std::uint32_t z) const;
    void expand(Node* n);
    void select(Node* n, const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame);
    bool contentReady(const Node* n) const;
    bool hasFallback(const Node* n) const;
    void request(Node* n, double priority);
    void run();
    void evict(std::uint64_t frame);
    std::string contentPath(const Node* n, const std::string& rel) const;
    void collectCredits();

    std::shared_ptr<TileSource> source_;
    std::unique_ptr<Node> root_;
    double maxSse_;
    int lighting_ = 0;
    bool remote_ = false;
    std::vector<std::string> attributions_;
    std::vector<std::pair<std::string, int>> credits_;
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
