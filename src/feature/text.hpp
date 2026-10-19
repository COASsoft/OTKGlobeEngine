#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ote {

class GlyphAtlas {
public:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float advance = 0;
    };

    static constexpr int kSize = 1024;
    static constexpr float kBase = 32.0f;
    static constexpr int kPad = 5;

    bool init();
    const Glyph& glyph(std::uint32_t cp);
    float kerning(std::uint32_t a, std::uint32_t b) const;
    float ascent() const { return ascent_; }
    float lineHeight() const { return lineHeight_; }
    bool dirty() const { return dirty_; }
    void clean() { dirty_ = false; }
    const std::vector<std::uint8_t>& pixels() const { return pixels_; }
    float measure(const std::string& utf8, float size);

    static std::vector<std::uint32_t> decode(const std::string& utf8);

private:
    void* font_ = nullptr;
    float scale_ = 1;
    float ascent_ = 0;
    float lineHeight_ = 0;
    int penX_ = 1, penY_ = 1, rowH_ = 0;
    bool dirty_ = true;
    std::vector<std::uint8_t> pixels_;
    std::unordered_map<std::uint32_t, Glyph> glyphs_;
};

}
