#include "core/geoid.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ote {

namespace embedded {
extern const unsigned char geoid_egm96[];
extern const std::size_t geoid_egm96_size;
}

namespace {

double at(int row, int col) {
    std::int16_t v;
    std::memcpy(&v, embedded::geoid_egm96 + (static_cast<size_t>(row) * 361 + col) * 2, 2);
    return v * 0.1;
}

}

double geoidHeight(double lat, double lon) {
    if (embedded::geoid_egm96_size < 361 * 181 * 2) return 0.0;
    double x = std::fmod(lon + 360.0, 360.0);
    double y = std::clamp(90.0 - lat, 0.0, 180.0);
    int c0 = std::min(static_cast<int>(x), 359), r0 = std::min(static_cast<int>(y), 179);
    double tx = x - c0, ty = y - r0;
    return (at(r0, c0) * (1 - tx) + at(r0, c0 + 1) * tx) * (1 - ty) + (at(r0 + 1, c0) * (1 - tx) + at(r0 + 1, c0 + 1) * tx) * ty;
}

}
