#pragma once

#include "elevation/elevation.hpp"

#include <cstdint>
#include <vector>

namespace ote {

bool decodeQuantizedMesh(const std::vector<std::uint8_t>& data, double west, double south, double east, double north,
                         int gridSize, HeightGrid& out);

}
