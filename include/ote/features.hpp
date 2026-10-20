#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct Style {
    std::uint32_t strokeColor = 0xFFFFFFFF;
    float strokeWidth = 2.0f;
    std::uint32_t fillColor = 0x00000000;
    IconShape icon = IconShape::Circle;
    std::uint32_t iconColor = 0xFF00A0FF;
    float iconScale = 1.0f;
    std::shared_ptr<const Image> iconImage;
    std::uint32_t labelColor = 0xFFFFFFFF;
    float labelScale = 1.0f;
    bool showLabel = true;
    std::string sidc;
};

struct Feature {
    std::uint64_t id = 0;
    std::string uid;
    std::string type;
    std::string name;
    std::string description;
    std::vector<std::pair<std::string, std::string>> attributes;
    double time = 0;
    double start = 0;
    double stale = 0;
    std::vector<Geometry> geometries;
    Style style;
    AltitudeMode altitudeMode = AltitudeMode::ClampToGround;
    bool extrude = false;
    bool visible = true;
};

struct ModelPlacement {
    Coord position;
    AltitudeMode altitudeMode = AltitudeMode::ClampToGround;
    double heading = 0;
    double pitch = 0;
    double roll = 0;
    double scale = 1;
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
ParsedFile parseBytes(const std::vector<std::uint8_t>& bytes, const std::string& extension, const std::string& baseDirectory = {});
double parseIsoTime(const std::string& text);
std::string formatIsoTime(double unixSeconds);
const std::string* findAttribute(const Feature& feature, const std::string& key);
void setAttribute(Feature& feature, const std::string& key, const std::string& value);
std::vector<Feature> parseFeatures(const std::string& path);

inline std::uint32_t argb(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | b;
}

}
