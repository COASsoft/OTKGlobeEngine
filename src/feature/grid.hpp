#pragma once

#include "feature/feature_layer.hpp"

#include <vector>

namespace ote {

void buildGrid(const GeoRect& tile, double texelDeg, double dpi, double spacing, std::vector<float>& verts);
void buildGraticule(const GeoRect& tile, double texelDeg, double dpi, double stepDeg, bool gars, std::vector<float>& verts);
double graticuleStep(double minDeg, bool gars);

}
