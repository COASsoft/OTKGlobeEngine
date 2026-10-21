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

struct VectorTile {
    std::vector<VectorLabel> labels;
    std::vector<VectorShape> fills;
    std::vector<VectorShape> lines;
    std::uint32_t background = 0xFFF2EFE9;
    double originLon = 0;
    double originLat = 0;
    std::vector<float> fillVertices;
    std::vector<float> lineVertices;
};

std::shared_ptr<VectorTile> decodeVectorTile(const std::vector<std::uint8_t>& data, int z, int x, int y);
bool looksLikeVectorTile(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> gunzip(const std::vector<std::uint8_t>& data);

}
