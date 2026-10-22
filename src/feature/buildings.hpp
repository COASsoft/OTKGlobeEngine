#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace otk {

struct BuildingShape {
    std::vector<std::vector<std::array<float, 2>>> rings;
    float height = 0.0f;
    float minHeight = 0.0f;
    std::uint32_t colour = 0;
};

struct BuildingVertex {
    float position[3];
    float normal[3];
    float texcoord[4];
    std::uint32_t tint;
    float base;
    float kind;
};

struct RoofCell {
    double west = 0, south = 0, east = 0, north = 0;
    int x = 0, y = 0, w = 0, h = 0;
};

struct BuildingMesh {
    std::vector<BuildingVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RoofCell> cells;
    int atlas = 0;
    double lat = 0.0, lon = 0.0;
    int buildings = 0;
};

enum BuildingTexture : int {
    FacadeGlass = 0,
    FacadeGlassFine,
    FacadeOffice,
    FacadeGrid,
    FacadeBrick,
    FacadeStone,
    FacadeBrickDark,
    WallSiding,
    WallStucco,
    WallConcrete,
    WallMetal,
    RoofShingles,
    BuildingTextures
};

struct BuildingMaterial {
    float width;
    float height;
    float windows;
};

const BuildingMaterial& buildingMaterial(int texture);
std::vector<std::array<double, 2>> buildingSamplePoints(const std::vector<BuildingShape>& shapes, int z, int x, int y);
BuildingMesh buildBuildingMesh(const std::vector<BuildingShape>& shapes, int z, int x, int y, const std::vector<double>& heights,
                               int maxAtlas);

}
