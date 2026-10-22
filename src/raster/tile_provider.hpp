#pragma once

#include "otk/engine.hpp"
#include "raster/tile_matrix.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace otk {

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
    virtual bool placeholderCheck() const { return remote(); }
    virtual int maxConcurrent() const { return 0; }
    virtual bool cached(int, int, int) { return false; }
    virtual void invalidate(int, int, int) {}
    virtual bool vector() const { return false; }
    virtual std::string cacheKey() const { return {}; }
    virtual bool available(int, int, int) const { return true; }
    virtual bool refresh(int, int, int) { return false; }
    virtual int download(int z, int x, int y, std::int64_t& bytes) {
        FetchResult r = fetch(z, x, y);
        bytes = static_cast<std::int64_t>(r.data.size());
        return r.status;
    }

protected:
    TileMatrix matrix_;
};

std::shared_ptr<TileProvider> makeProvider(const ImageryLayer& layer, TileCache* cache, std::atomic<bool>* offline);
std::string tileUrl(const ImageryLayer& layer, const TileMatrix& m, int z, int x, int y);
std::string stripCredentials(const std::string& url);
std::string layerCacheKey(const ImageryLayer& layer);
std::string layerIdentity(const ImageryLayer& layer);
int fetchUrl(const std::string& url, std::vector<std::uint8_t>& body, const FetchOptions& options, TileCache* cache,
             std::atomic<bool>* offline, const std::string& userAgent);
std::string localPathFromUrl(const std::string& url);
int readLocalUrl(const std::string& url, std::vector<std::uint8_t>& body);

}
