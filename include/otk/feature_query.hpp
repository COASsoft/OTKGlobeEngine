#pragma once

#include "otk/features.hpp"
#include "otk/layers.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace otk {

struct FeatureQuery {
    bool useRegion = false;
    Region region;
    bool useRadius = false;
    Coord center;
    double radiusMeters = 0;
    std::string typePrefix;
    std::string nameContains;
    std::vector<std::pair<std::string, std::string>> attributes;
    double activeAt = 0;
    std::size_t limit = 0;
};

bool featureRegion(const Feature& feature, Region& out);
bool matchesQuery(const Feature& feature, const FeatureQuery& query);
std::vector<std::size_t> queryFeatures(const std::vector<Feature>& features, const FeatureQuery& query);

}
