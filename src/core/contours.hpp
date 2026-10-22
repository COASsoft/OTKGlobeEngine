#pragma once

#include <array>
#include <vector>

namespace otk {

struct ContourPath {
    long index = 0;
    bool major = false;
    std::vector<std::array<double, 2>> points;
};

std::vector<ContourPath> traceContours(const std::vector<float>& heights, int cols, int rows, double west, double south, double east,
                                       double north, double step, int majorEvery, long firstIndex, double simplifyCells);

}
