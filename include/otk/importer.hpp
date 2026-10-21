#pragma once

#include "otk/layers.hpp"

#include <string>
#include <vector>

namespace otk {

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
    Region region;
    bool hasRegion = false;
};

ImportPlan planImport(const std::string& path);

}
