#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace otk::labels {

enum class Kind : std::uint8_t { Country = 0, State = 1, County = 2, City = 3, Suburb = 4 };
enum class Level : std::uint8_t { Country = 0, Disputed = 1, State = 2, County = 3 };

constexpr double kUnit = 1e-4;
constexpr double kPlaceCell = 5.0;
constexpr double kFineCell = 10.0;
constexpr double kCoarseCell = 30.0;
constexpr int kPlaceCols = 72;
constexpr int kPlaceRows = 36;
constexpr int kFineCols = 36;
constexpr int kFineRows = 18;
constexpr int kCoarseCols = 12;
constexpr int kCoarseRows = 6;
constexpr std::uint8_t kCapital = 1;
constexpr std::uint8_t kRegionalCapital = 2;

struct Place {
    float lat = 0.0f;
    float lon = 0.0f;
    std::uint32_t name = 0;
    std::uint16_t length = 0;
    Kind kind = Kind::City;
    std::uint8_t flags = 0;
    std::uint8_t minZoom = 0;
    std::uint8_t maxZoom = 255;
    std::uint8_t population = 0;
};

struct Run {
    Level level = Level::Country;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::int32_t left = -1;
    std::int32_t right = -1;
};

struct Lines {
    std::vector<std::int32_t> xy;
    std::vector<std::uint8_t> importance;
    std::vector<Run> runs;
};

class Dataset {
public:
    static const Dataset& get();

    bool available() const { return available_; }
    const std::vector<Place>& places() const { return places_; }
    std::string_view name(const Place& p) const { return std::string_view(names_).substr(p.name, p.length); }
    std::uint32_t cellBegin(int cell) const { return cell >= 0 && cell < kPlaceCols * kPlaceRows ? starts_[cell] : 0; }
    std::uint32_t cellEnd(int cell) const { return cell >= 0 && cell < kPlaceCols * kPlaceRows ? starts_[cell + 1] : 0; }
    bool hasFine(int key) const;
    bool hasCoarse(int key) const;
    bool fine(int key, Lines& out) const;
    bool coarse(int key, Lines& out) const;

private:
    struct Block {
        std::uint32_t kind = 0, key = 0, offset = 0, size = 0, raw = 0;
    };

    Dataset();
    const Block* find(std::uint32_t kind, std::uint32_t key) const;
    bool inflate(const Block& b, std::vector<std::uint8_t>& out) const;
    bool decodeLines(const Block& b, bool importance, Lines& out) const;
    bool decodePlaces(const Block& b);

    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
    std::vector<Block> blocks_;
    std::vector<Place> places_;
    std::vector<std::uint32_t> starts_;
    std::string names_;
    std::uint32_t version_ = 0;
    bool available_ = false;
};

}
