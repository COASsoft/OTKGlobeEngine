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
    bool stale = false;
    bool raw = false;
    int width = 0;
    int height = 0;
};

class TileProvider {
public:
    virtual ~TileProvider() = default;
    const TileMatrix& matrix() const { return matrix_; }
    virtual FetchResult fetch(int z, int x, int y) = 0;
    virtual bool fetchLocal(int z, int x, int y, FetchResult& out) {
        out = fetch(z, x, y);
        return true;
    }
    virtual bool remote() const { return false; }
    virtual bool cached(int, int, int) { return false; }
    virtual void invalidate(int, int, int) {}
    virtual bool vector() const { return false; }
    virtual std::string cacheKey() const { return {}; }
    virtual bool available(int, int, int) const { return true; }
    virtual bool refresh(int, int, int) { return false; }
    virtual int download(int z, int x, int y) { return fetch(z, x, y).status; }

protected:
    TileMatrix matrix_;
};

std::shared_ptr<TileProvider> makeProvider(const ImageryLayer& layer, TileCache* cache, std::atomic<bool>* offline);
std::string tileUrl(const ImageryLayer& layer, const TileMatrix& m, int z, int x, int y);
std::string stripCredentials(const std::string& url);
std::string layerCacheKey(const ImageryLayer& layer);

}
