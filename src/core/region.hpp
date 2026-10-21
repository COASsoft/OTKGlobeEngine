#pragma once

#include "otk/layers.hpp"

#include <algorithm>
#include <cmath>

namespace otk {

struct RegionBuilder {
    double south = 90, north = -90, west = 180, east = -180, westShifted = 360, eastShifted = 0;
    bool any = false;

    void add(double lat, double lon) {
        if (!std::isfinite(lat) || !std::isfinite(lon)) return;
        any = true;
        south = std::min(south, lat);
        north = std::max(north, lat);
        west = std::min(west, lon);
        east = std::max(east, lon);
        double shifted = lon < 0 ? lon + 360.0 : lon;
        westShifted = std::min(westShifted, shifted);
        eastShifted = std::max(eastShifted, shifted);
    }

    bool get(Region& out) const {
        if (!any) return false;
        out = {west, south, east, north};
        if (eastShifted - westShifted < east - west) {
            out.west = westShifted > 180.0 ? westShifted - 360.0 : westShifted;
            out.east = eastShifted > 180.0 ? eastShifted - 360.0 : eastShifted;
        }
        return true;
    }
};

inline bool regionsIntersect(const Region& a, const Region& b) {
    if (a.north < b.south || b.north < a.south) return false;
    auto spans = [](const Region& r, double (&s)[2][2]) {
        if (r.west <= r.east) {
            s[0][0] = r.west;
            s[0][1] = r.east;
            s[1][0] = 1;
            s[1][1] = 0;
        } else {
            s[0][0] = r.west;
            s[0][1] = 180.0;
            s[1][0] = -180.0;
            s[1][1] = r.east;
        }
    };
    double sa[2][2], sb[2][2];
    spans(a, sa);
    spans(b, sb);
    for (auto& x : sa)
        for (auto& y : sb)
            if (x[0] <= x[1] && y[0] <= y[1] && x[0] <= y[1] && y[0] <= x[1]) return true;
    return false;
}

}
