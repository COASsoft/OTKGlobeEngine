#pragma once

#include "otk/features.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace otk {

struct UasMetadata {
    static constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
    double time = kNone;
    std::string missionId;
    std::string platformTail;
    std::string platformDesignation;
    std::string imageSensor;
    std::string imageCoordinateSystem;
    double platformHeading = kNone;
    double platformPitch = kNone;
    double platformRoll = kNone;
    double sensorLat = kNone;
    double sensorLon = kNone;
    double sensorAlt = kNone;
    double hfov = kNone;
    double vfov = kNone;
    double sensorRelAzimuth = kNone;
    double sensorRelElevation = kNone;
    double sensorRelRoll = kNone;
    double slantRange = kNone;
    double targetWidth = kNone;
    double frameCenterLat = kNone;
    double frameCenterLon = kNone;
    double frameCenterElevation = kNone;
    double cornerLat[4] = {kNone, kNone, kNone, kNone};
    double cornerLon[4] = {kNone, kNone, kNone, kNone};
    int version = 0;
    bool checksumOk = false;
};

std::vector<UasMetadata> parseUasKlv(const std::uint8_t* data, std::size_t size);
std::vector<UasMetadata> demuxUasMetadata(const std::uint8_t* data, std::size_t size);
std::vector<UasMetadata> readUasMetadata(const std::string& path);
std::vector<std::uint8_t> encodeUasKlv(const UasMetadata& metadata);
bool uasFootprint(const UasMetadata& metadata, double lat[4], double lon[4]);
std::vector<Feature> uasFeatures(const std::vector<UasMetadata>& packets, int footprintEvery = 30);

}
