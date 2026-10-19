#pragma once

#include "core/camera.hpp"
#include "feature/feature_layer.hpp"
#include "ote/features.hpp"

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ote {

struct ModelPlacement {
    Coord position;
    AltitudeMode altitudeMode = AltitudeMode::ClampToGround;
    double heading = 0;
    double pitch = 0;
    double roll = 0;
    double scale = 1;
};

class ModelLayer {
public:
    bool init();
    void shutdown();
    std::uint64_t add(const std::string& path, const ModelPlacement& placement);
    bool update(std::uint64_t id, const ModelPlacement& placement);
    bool remove(std::uint64_t id);
    bool empty() const { return instances_.empty(); }
    void draw(const Camera& cam, const GroundFn& ground);

private:
    struct Primitive {
        sg_buffer vertices{};
        sg_buffer indices{};
        int count = 0;
        sg_view texture{};
        sg_image image{};
        std::array<float, 4> color{1, 1, 1, 1};
        std::array<double, 16> matrix{};
    };
    struct Mesh {
        std::vector<Primitive> primitives;
    };
    struct Instance {
        std::shared_ptr<Mesh> mesh;
        ModelPlacement placement;
    };

    std::shared_ptr<Mesh> load(const std::string& path);

    std::unordered_map<std::string, std::weak_ptr<Mesh>> meshes_;
    std::unordered_map<std::uint64_t, Instance> instances_;
    std::uint64_t nextId_ = 1;
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    sg_sampler sampler_{};
    sg_image white_{};
    sg_view whiteView_{};
    bool ready_ = false;
};

}
