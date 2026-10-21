#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace otk {

enum class PlaceKind { Country, State, County, City, Suburb };

struct Place {
    std::string name;
    double lat = 0.0;
    double lon = 0.0;
    PlaceKind kind = PlaceKind::City;
    double minZoom = 0.0;
    double distanceMeters = 0.0;
    bool capital = false;
};

bool placesAvailable();
std::vector<Place> placesNear(double lat, double lon, double radiusMeters, std::size_t limit = 20,
                              const std::vector<PlaceKind>& kinds = {PlaceKind::City});

}
