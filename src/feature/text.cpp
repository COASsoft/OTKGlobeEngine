#include "feature/text.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cstddef>
#include <memory>

namespace otk {

namespace embedded {
extern const unsigned char font_bold[];
extern const std::size_t font_bold_size;
}

bool GlyphAtlas::init() {
    if (font_) return true;
    if (!init(embedded::font_bold, kPad)) return false;
    for (std::uint32_t c = 32; c < 127; ++c) glyph(c);
    return true;
}

bool GlyphAtlas::init(const unsigned char* font, int pad) {
    if (font_) return true;
    auto* info = new stbtt_fontinfo;
    if (!font || !stbtt_InitFont(info, font, stbtt_GetFontOffsetForIndex(font, 0))) {
        delete info;
        return false;
    }
    font_ = info;
    pad_ = std::max(pad, 1);
    scale_ = stbtt_ScaleForPixelHeight(info, kBase);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    ascent_ = asc * scale_;
    lineHeight_ = (asc - desc + gap) * scale_;
    pixels_.assign(static_cast<size_t>(kSize) * kSize, 0);
    return true;
}

void GlyphAtlas::reset() {
    glyphs_.clear();
    std::fill(pixels_.begin(), pixels_.end(), std::uint8_t(0));
    penX_ = penY_ = 1;
    rowH_ = 0;
    full_ = false;
    dirty_ = true;
}

bool GlyphAtlas::place(std::uint32_t cp, int w, int h, const std::uint8_t* sdf, float x0, float y0, float advance) {
    Glyph g;
    g.advance = advance;
    if (w <= 0 || h <= 0 || !sdf || pixels_.empty()) return false;
    if (penX_ + w + 1 >= kSize) {
        penX_ = 1;
        penY_ += rowH_ + 1;
        rowH_ = 0;
    }
    if (penY_ + h + 1 >= kSize) {
        full_ = true;
        glyphs_[cp] = g;
        return false;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) pixels_[static_cast<size_t>(penY_ + y) * kSize + penX_ + x] = sdf[y * w + x];
    g.u0 = static_cast<float>(penX_) / kSize;
    g.v0 = static_cast<float>(penY_) / kSize;
    g.u1 = static_cast<float>(penX_ + w) / kSize;
    g.v1 = static_cast<float>(penY_ + h) / kSize;
    g.x0 = x0;
    g.y0 = y0;
    g.x1 = x0 + static_cast<float>(w);
    g.y1 = y0 + static_cast<float>(h);
    penX_ += w + 1;
    rowH_ = std::max(rowH_, h);
    dirty_ = true;
    glyphs_[cp] = g;
    return true;
}

const GlyphAtlas::Glyph& GlyphAtlas::glyph(std::uint32_t cp) {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return it->second;
    auto* info = static_cast<stbtt_fontinfo*>(font_);
    if (!info) return glyphs_[cp] = Glyph{};
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, static_cast<int>(cp), &adv, &lsb);
    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(info, scale_, static_cast<int>(cp), pad_, 128, 128.0f / pad_, &w, &h, &xo, &yo);
    if (!place(cp, w, h, sdf, static_cast<float>(xo), static_cast<float>(yo), adv * scale_)) {
        auto& g = glyphs_[cp];
        g.advance = adv * scale_;
    }
    if (sdf) stbtt_FreeSDF(sdf, nullptr);
    return glyphs_[cp];
}

float GlyphAtlas::kerning(std::uint32_t a, std::uint32_t b) const {
    auto* info = static_cast<stbtt_fontinfo*>(font_);
    return info ? stbtt_GetCodepointKernAdvance(info, static_cast<int>(a), static_cast<int>(b)) * scale_ : 0.0f;
}

float GlyphAtlas::measure(const std::string& utf8, float size) {
    float w = 0;
    std::uint32_t prev = 0;
    for (std::uint32_t c : decode(utf8)) {
        if (prev) w += kerning(prev, c);
        w += glyph(c).advance;
        prev = c;
    }
    return w * size / kBase;
}

std::vector<std::uint32_t> GlyphAtlas::decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        int n = 0;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c >> 5) == 6) {
            cp = c & 0x1F;
            n = 2;
        } else if ((c >> 4) == 14) {
            cp = c & 0x0F;
            n = 3;
        } else if ((c >> 3) == 30) {
            cp = c & 0x07;
            n = 4;
        } else {
            ++i;
            continue;
        }
        if (i + n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

}
