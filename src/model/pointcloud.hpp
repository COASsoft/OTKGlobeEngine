#pragma once

#include "core/camera.hpp"

#include <sokol_gfx.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace otk {

struct PointCloudInfo {
    std::uint64_t pointCount = 0;
    std::uint64_t loadedPoints = 0;
    int epsg = 0;
    double west = 0, south = 0, east = 0, north = 0;
    double minHeight = 0, maxHeight = 0;
};

class PointCloudLayer {
public:
    bool init();
    void shutdown();
    std::uint64_t add(const std::string& path, std::size_t maxPoints, float pointSize, PointCloudInfo* info);
    bool remove(std::uint64_t id);
    void draw(const Camera& cam, double dpi);
    void drawBuffer(const Camera& cam, sg_buffer points, int count, const Mat4& modelRelEye, float size, double dpi);
    bool empty() const { return clouds_.empty(); }

private:
    struct Cloud {
        sg_buffer instances{};
        int count = 0;
        Vec3 center;
        float size = 2.0f;
    };
    std::unordered_map<std::uint64_t, Cloud> clouds_;
    std::uint64_t nextId_ = 1;
    sg_buffer corners_{};
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    bool ready_ = false;
};

bool readLas(const std::string& path, std::size_t maxPoints, std::vector<double>& ecef, std::vector<std::uint8_t>& rgba,
             PointCloudInfo& info);

}
