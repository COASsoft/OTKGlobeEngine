#pragma once

#include "raster/tile_matrix.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ote {

class TilePackageWriter {
public:
    TilePackageWriter();
    ~TilePackageWriter();
    bool open(const std::string& path, const TileMatrix& matrix, const std::string& name, std::string& error);
    bool put(int z, int x, int y, const std::vector<std::uint8_t>& data);
    void extend(double west, double south, double east, double north);
    bool close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

using TileVisitor = std::function<bool(int z, int x, int y, const std::vector<std::uint8_t>& data)>;

bool readTilePackage(const std::string& path, TileMatrix& matrix, const TileVisitor& visit, std::string& error);
std::string tileFormat(const std::vector<std::uint8_t>& data);
bool sameTiling(const TileMatrix& a, const TileMatrix& b);

}
