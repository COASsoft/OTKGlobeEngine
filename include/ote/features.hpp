#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ote {

struct Coord {
    double lat = 0;
    double lon = 0;
    double alt = 0;
};

enum class GeometryType { Point, LineString, Polygon };

enum class AltitudeMode { ClampToGround, RelativeToGround, Absolute };

enum class IconShape { Circle, Pin, Square, Triangle, Hidden };

struct Geometry {
    GeometryType type = GeometryType::Point;
    std::vector<Coord> points;
    std::vector<std::vector<Coord>> holes;
};

struct Style {
    std::uint32_t strokeColor = 0xFFFFFFFF;
    float strokeWidth = 2.0f;
    std::uint32_t fillColor = 0x00000000;
    IconShape icon = IconShape::Circle;
    std::uint32_t iconColor = 0xFF00A0FF;
    float iconScale = 1.0f;
    std::uint32_t labelColor = 0xFFFFFFFF;
    float labelScale = 1.0f;
    bool showLabel = true;
};

struct Feature {
    std::uint64_t id = 0;
    std::string name;
    std::string description;
    std::vector<Geometry> geometries;
    Style style;
    AltitudeMode altitudeMode = AltitudeMode::ClampToGround;
    bool extrude = false;
    bool visible = true;
};

struct GroundOverlay {
    std::string name;
    double north = 0;
    double south = 0;
    double east = 0;
    double west = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    float opacity = 1.0f;
};

struct ParsedFile {
    std::vector<Feature> features;
    std::vector<GroundOverlay> overlays;
};

ParsedFile parseFile(const std::string& path);
std::vector<Feature> parseFeatures(const std::string& path);

inline std::uint32_t argb(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | b;
}

}
