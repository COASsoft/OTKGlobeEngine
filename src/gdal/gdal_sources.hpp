#pragma once

#include "elevation/elevation.hpp"
#include "ote/features.hpp"
#include "raster/tile_provider.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ote {

bool gdalAvailable();
std::shared_ptr<TileProvider> makeGdalRaster(const ImageryLayer& layer);
std::shared_ptr<ElevationProvider> makeGdalElevation(const ElevationLayer& layer);
std::vector<Feature> parseWithOgr(const std::string& path);
bool transformToGeographic(int epsg, const std::string& wkt, double* x, double* y, double* z, std::size_t n);

}
