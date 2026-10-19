#pragma once

#include "feature/feature_layer.hpp"

#include <vector>

namespace ote {

void buildGrid(const GeoRect& tile, double texelDeg, double dpi, std::vector<float>& verts);

}
