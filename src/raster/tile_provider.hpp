#pragma once

#include "ote/engine.hpp"
#include "raster/tile_matrix.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ote {

class TileCache;

struct FetchResult {
    int status = 0;
    std::vector<std::uint8_t> data;
    bool fromCache = false;
    bool raw = false;
    int width = 0;
    int height = 0;
};

class TileProvider {
public:
    virtual ~TileProvider() = default;
    const TileMatrix& matrix() const { return matrix_; }
    virtual FetchResult fetch(int z, int x, int y) = 0;
    virtual bool remote() const { return false; }
    virtual bool cached(int, int, int) { return false; }

protected:
    TileMatrix matrix_;
};

std::shared_ptr<TileProvider> makeProvider(const ImageryLayer& layer, TileCache* cache, std::atomic<bool>* offline);
std::string tileUrl(const ImageryLayer& layer, const TileMatrix& m, int z, int x, int y);

}
