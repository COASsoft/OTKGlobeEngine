#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace otk {

struct VectorShape {
    std::uint32_t color = 0;
    float width = 0;
    std::vector<std::array<float, 2>> points;
    std::vector<std::uint32_t> breaks;
    std::vector<std::array<float, 2>> triangles;
};

struct VectorLabel {
    double lon = 0;
    double lat = 0;
    std::string text;
    double priority = 0;
    std::uint32_t color = 0xFF202020;
    float size = 12.0f;
};

enum class MarkType : std::uint8_t { Road, Shield, Place, Park, Water, Waterway };

enum class RoadClass : std::uint8_t { Motorway, MotorwayLink, Trunk, TrunkLink, Primary, Secondary, Tertiary, Minor, Service, Path, Tunnel, MinorTunnel };

struct TileMark {
    MarkType type = MarkType::Place;
    std::uint8_t rank = 0;
    std::uint8_t zoom = 0;
    std::string text;
    std::string network;
    std::vector<std::array<double, 2>> points;
};

struct VectorTile {
    std::vector<VectorLabel> labels;
    std::vector<VectorShape> fills;
    std::vector<VectorShape> lines;
    std::uint32_t background = 0xFFF2EFE9;
    double originLon = 0;
    double originLat = 0;
    std::vector<float> fillVertices;
    std::vector<float> lineVertices;
    bool overlay = false;
    std::vector<float> roads;
    std::vector<TileMark> marks;
};

std::shared_ptr<VectorTile> decodeVectorTile(const std::vector<std::uint8_t>& data, int z, int x, int y);
std::shared_ptr<VectorTile> decodeOverlayTile(const std::vector<std::uint8_t>& data, int z, int x, int y);
std::string shortRoadName(const std::string& name);
bool looksLikeVectorTile(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> gunzip(const std::vector<std::uint8_t>& data);

}
