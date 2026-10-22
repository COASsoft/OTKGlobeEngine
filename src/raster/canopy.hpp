#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace otk {

class TileCache;

constexpr int kCanopyZoom = 14;
constexpr int kCanopySize = 512;

class CanopySource {
public:
    enum Result { Failed = -1, Empty = 0, Ready = 1 };

    explicit CanopySource(TileCache* cache);
    void setOffline(bool offline) { offline_ = offline; }
    Result heights(int x, int y, std::vector<std::uint8_t>& out);

private:
    struct Header {
        std::vector<std::uint64_t> offsets, counts;
        int across = 0;
        int block = 0;
        bool missing = false;
    };

    bool header(int qx, int qy, Header& out);
    bool range(const std::string& url, std::uint64_t first, std::uint64_t count, std::vector<std::uint8_t>& out, int& status);

    TileCache* cache_;
    std::atomic<bool> offline_{false};
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, Header> headers_;
};

}
