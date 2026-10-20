#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ote {

struct TileData {
    std::vector<std::uint8_t> bytes;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

enum TileStatus : int { TileOk = 200, TileNoContent = 204, TileMissing = 404, TileOffline = -2, TileUnreachable = -3, TileFailed = -1 };

struct FetchOptions {
    bool cache = true;
    int expirySeconds = 0;
    std::vector<std::pair<std::string, std::string>> headers;
};

using UrlFetch = std::function<int(const std::string& url, std::vector<std::uint8_t>& body, const FetchOptions& options)>;

class TileSource {
public:
    virtual ~TileSource() = default;
    virtual int tile(int z, int x, int y, TileData& out, const UrlFetch& fetch) = 0;
    virtual bool remote() const { return true; }
    virtual bool vector() const { return false; }
    virtual std::string identity() const { return {}; }
};

}
