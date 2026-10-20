#pragma once

#include <cstdint>
#include <cstdio>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ote {

class PmTilesArchive {
public:
    enum class TileType { Unknown = 0, Mvt = 1, Png = 2, Jpeg = 3, Webp = 4, Avif = 5 };

    struct Header {
        std::uint64_t rootOffset = 0, rootLength = 0;
        std::uint64_t metadataOffset = 0, metadataLength = 0;
        std::uint64_t leafOffset = 0, leafLength = 0;
        std::uint64_t dataOffset = 0, dataLength = 0;
        std::uint64_t addressedTiles = 0;
        int internalCompression = 0;
        int tileCompression = 0;
        TileType tileType = TileType::Unknown;
        int minZoom = 0;
        int maxZoom = 0;
        double west = -180, south = -85.0511287798, east = 180, north = 85.0511287798;
        int centerZoom = 0;
        double centerLon = 0, centerLat = 0;
    };

    static std::shared_ptr<PmTilesArchive> open(const std::string& path, std::string* error = nullptr);
    ~PmTilesArchive();

    const Header& header() const { return header_; }
    std::string metadata() const;
    int status(int z, int x, int y, std::vector<std::uint8_t>& out) const;
    bool vector() const { return header_.tileType == TileType::Mvt; }
    int tileSize() const { return tileSize_; }

    static std::uint64_t tileId(int z, std::uint32_t x, std::uint32_t y);

private:
    struct Entry {
        std::uint64_t tileId = 0;
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        std::uint32_t runLength = 0;
    };
    using Directory = std::shared_ptr<const std::vector<Entry>>;

    PmTilesArchive() = default;
    bool read(std::uint64_t offset, std::uint64_t length, std::vector<std::uint8_t>& out) const;
    bool decompress(int compression, std::vector<std::uint8_t>& data) const;
    Directory directory(std::uint64_t offset, std::uint64_t length) const;

    Header header_;
    int tileSize_ = 256;
    std::FILE* file_ = nullptr;
    std::uint64_t fileSize_ = 0;
    mutable std::mutex mutex_;
    mutable std::map<std::uint64_t, std::pair<Directory, std::list<std::uint64_t>::iterator>> dirs_;
    mutable std::list<std::uint64_t> lru_;
};

}
