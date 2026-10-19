#pragma once

#include "db/sqlite.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ote {

class TileCache {
public:
    bool open(const std::string& path);
    bool isOpen() const { return db_.isOpen(); }
    bool get(const std::string& source, int z, int x, int y, std::vector<std::uint8_t>& data, std::int64_t& expires);
    bool has(const std::string& source, int z, int x, int y);
    void put(const std::string& source, int z, int x, int y, const std::vector<std::uint8_t>& data, std::int64_t expires);

    static std::int64_t now();

private:
    std::mutex mutex_;
    db::Database db_;
    db::Statement get_;
    db::Statement has_;
    db::Statement put_;
};

}
