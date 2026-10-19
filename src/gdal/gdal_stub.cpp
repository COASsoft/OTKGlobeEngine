#include "gdal/gdal_sources.hpp"

namespace ote {

bool gdalAvailable() { return false; }

std::shared_ptr<TileProvider> makeGdalRaster(const ImageryLayer&) { return nullptr; }

std::shared_ptr<ElevationProvider> makeGdalElevation(const ElevationLayer&) { return nullptr; }

std::vector<Feature> parseWithOgr(const std::string&) { return {}; }

bool transformToGeographic(int, const std::string&, double*, double*, double*, std::size_t) { return false; }

}
