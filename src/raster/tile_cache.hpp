#pragma once

#include "db/sqlite.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace otk {

class TileCache {
public:
    bool open(const std::string& path);
    bool isOpen() const { return db_.isOpen(); }
    const std::string& path() const { return path_; }
    bool get(const std::string& source, int z, int x, int y, std::vector<std::uint8_t>& data, std::int64_t& expires);
    bool has(const std::string& source, int z, int x, int y);
    void put(const std::string& source, int z, int x, int y, const std::vector<std::uint8_t>& data, std::int64_t expires,
             bool pinned = false);
    void pin(const std::string& source, int z, int x, int y);
    void remove(const std::string& source, int z, int x, int y);
    void setLimit(std::int64_t bytes);
    std::int64_t bytes();
    std::int64_t enforceLimit();
    void forEach(const std::string& source, int z, int x0, int x1, int y0, int y1,
                 const std::function<void(int x, int y, const std::vector<std::uint8_t>&)>& fn);
    void begin();
    void commit();

    static std::int64_t now();

private:
    bool openAt(const std::string& path);
    std::int64_t bytesLocked();
    std::int64_t evictLocked();

    std::mutex mutex_;
    std::string path_;
    db::Database db_;
    db::Statement get_;
    db::Statement has_;
    db::Statement put_;
    db::Statement pin_;
    db::Statement del_;
    db::Statement range_;
    std::int64_t limit_ = 0;
    int putsSinceCheck_ = 0;
    int batch_ = 0;
};

}
