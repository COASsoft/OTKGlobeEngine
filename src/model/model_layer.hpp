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

struct CpuPrimitive {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<double, 16> matrix{};
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<std::uint8_t> image;
};

struct CpuModel {
    std::vector<CpuPrimitive> primitives;
    std::array<double, 3> rtc{0, 0, 0};
    std::size_t bytes = 0;
};

bool parseGltf(const std::uint8_t* data, std::size_t size, const std::string& baseDir, CpuModel& out);
bool parseB3dm(const std::uint8_t* data, std::size_t size, CpuModel& out);

struct GpuMesh {
    struct Primitive {
        sg_buffer vertices{};
        sg_buffer indices{};
        int count = 0;
        sg_view texture{};
        sg_image image{};
        std::array<float, 4> color{1, 1, 1, 1};
        std::array<double, 16> matrix{};
    };
    std::vector<Primitive> primitives;
    std::array<double, 3> rtc{0, 0, 0};
    std::size_t bytes = 0;
    ~GpuMesh();
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

    static std::shared_ptr<GpuMesh> upload(const CpuModel& model);
    void begin();
    void drawMesh(const Camera& cam, const GpuMesh& mesh, const Mat4& model, const Mat4& orientation, Vec3 lightDir);

private:
    struct Instance {
        std::shared_ptr<GpuMesh> mesh;
        ModelPlacement placement;
    };

    std::shared_ptr<GpuMesh> load(const std::string& path);

    std::unordered_map<std::string, std::weak_ptr<GpuMesh>> meshes_;
    std::unordered_map<std::uint64_t, Instance> instances_;
    std::uint64_t nextId_ = 1;
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    sg_sampler sampler_{};
    sg_image white_{};
    sg_view whiteView_{};
    bool ready_ = false;
};

Mat4 yUpToZUp();

}
