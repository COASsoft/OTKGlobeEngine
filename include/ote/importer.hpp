#pragma once

#include "ote/layers.hpp"

#include <string>
#include <vector>

namespace ote {

enum class ImportTarget { Features, Imagery, Elevation, Tileset, PointCloud, Model, Iconset, Unsupported };

struct ImportPlan {
    std::string path;
    std::string name;
    std::string format;
    ImportTarget target = ImportTarget::Unsupported;
    std::vector<ImageryLayer> imagery;
    std::vector<ElevationLayer> elevation;
    std::string resource;
    std::string message;
};

ImportPlan planImport(const std::string& path);

}
