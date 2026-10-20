#pragma once

#include "core/camera.hpp"
#include "feature/feature_layer.hpp"
#include "ote/features.hpp"

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ote {

struct CpuImage {
    int width = 0;
    int height = 0;
    int format = SG_PIXELFORMAT_RGBA8;
    std::vector<std::vector<std::uint8_t>> levels;
};

struct CpuMaterial {
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 3> emissive{0, 0, 0};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float normalScale = 1.0f;
    int image = -1;
    int normalImage = -1;
    int emissiveImage = -1;
    int ormImage = -1;
    bool ormOcclusion = false;
    bool unlit = false;
};

struct CpuInstances {
    std::vector<float> matrices;
    bool local = false;
};

struct CpuPrimitive {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::array<double, 16> matrix{};
    int material = -1;
    int instances = -1;
};

struct CpuModel {
    std::vector<CpuPrimitive> primitives;
    std::vector<CpuImage> images;
    std::vector<CpuMaterial> materials;
    std::vector<CpuInstances> instances;
    std::string copyright;
    std::vector<std::uint8_t> points;
    std::array<double, 3> pointsOrigin{0, 0, 0};
    std::array<double, 3> rtc{0, 0, 0};
    std::size_t bytes = 0;
};

using ResourceReader = std::function<std::vector<std::uint8_t>(const std::string&)>;

bool parseGltf(const std::uint8_t* data, std::size_t size, const std::string& path, const ResourceReader& read, CpuModel& out);
bool parseTileContent(const std::uint8_t* data, std::size_t size, const std::string& path, const ResourceReader& read, const Mat4& tileTransform,
                      std::vector<CpuModel>& out);
void mergePrimitives(CpuModel& model);
void finishModel(CpuModel& model);

struct GpuMesh {
    struct Primitive {
        sg_buffer vertices{};
        sg_buffer indices{};
        int count = 0;
        int material = -1;
        int instances = -1;
        std::array<double, 16> matrix{};
    };
    struct Instances {
        sg_buffer buffer{};
        int count = 0;
        bool local = false;
    };
    std::vector<Primitive> primitives;
    std::vector<sg_image> images;
    std::vector<sg_view> views;
    std::vector<CpuMaterial> materials;
    std::vector<Instances> instances;
    std::string copyright;
    sg_buffer points{};
    int pointCount = 0;
    std::array<double, 3> pointsOrigin{0, 0, 0};
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
    void draw(const Camera& cam, const GroundFn& ground, bool reuse = false);

    static std::shared_ptr<GpuMesh> upload(const CpuModel& model);
    void begin();
    void drawMesh(const Camera& cam, const GpuMesh& mesh, const Mat4& model, const Mat4& orientation, Vec3 lightDir, int lighting = 0);
    void setSun(bool enabled, Vec3 direction);

private:
    struct Instance {
        std::shared_ptr<GpuMesh> mesh;
        ModelPlacement placement;
    };
    struct Placed {
        const GpuMesh* mesh = nullptr;
        Mat4 base;
        Mat4 orient;
        Vec3 light;
        Vec3 up;
    };
    struct Batch {
        size_t first = 0;
        size_t count = 0;
    };

    std::shared_ptr<GpuMesh> load(const std::string& path);
    void usePipeline(bool instanced);
    void drawPrimitive(const Camera& cam, const GpuMesh& mesh, const GpuMesh::Primitive& p, sg_buffer instances, int instanceOffset,
                       int instanceCount, const Mat4& world, const Mat4& node, const Mat4& rotation, Vec3 light, Vec3 up, int lighting);

    std::unordered_map<std::string, std::weak_ptr<GpuMesh>> meshes_;
    std::unordered_map<std::uint64_t, Instance> instances_;
    std::uint64_t nextId_ = 1;
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    sg_shader instShader_{};
    sg_pipeline instPipeline_{};
    sg_sampler sampler_{};
    sg_image white_{};
    sg_view whiteView_{};
    sg_image black_{};
    sg_view blackView_{};
    sg_image flat_{};
    sg_view flatView_{};
    bool sun_ = false;
    Vec3 sunDir_{0, 0, 1};
    std::vector<Placed> placed_;
    std::vector<Batch> batches_;
    Vec3 frameOrigin_;
    sg_buffer frameInstances_{};
    std::size_t frameInstanceBytes_ = 0;
    int current_ = -1;
    bool ready_ = false;
};

Mat4 yUpToZUp();

}
