#include "feature/text.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cstddef>
#include <memory>

namespace ote {

namespace embedded {
extern const unsigned char font_bold[];
extern const std::size_t font_bold_size;
}

bool GlyphAtlas::init() {
    if (font_) return true;
    auto* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, embedded::font_bold, stbtt_GetFontOffsetForIndex(embedded::font_bold, 0))) {
        delete info;
        return false;
    }
    font_ = info;
    scale_ = stbtt_ScaleForPixelHeight(info, kBase);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    ascent_ = asc * scale_;
    lineHeight_ = (asc - desc + gap) * scale_;
    pixels_.assign(static_cast<size_t>(kSize) * kSize, 0);
    for (std::uint32_t c = 32; c < 127; ++c) glyph(c);
    return true;
}

const GlyphAtlas::Glyph& GlyphAtlas::glyph(std::uint32_t cp) {
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end()) return it->second;
    auto* info = static_cast<stbtt_fontinfo*>(font_);
    Glyph g;
    if (!info) return glyphs_[cp] = g;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, static_cast<int>(cp), &adv, &lsb);
    g.advance = adv * scale_;
    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char* sdf = stbtt_GetCodepointSDF(info, scale_, static_cast<int>(cp), kPad, 128, 128.0f / kPad, &w, &h, &xo, &yo);
    if (sdf && w > 0 && h > 0) {
        if (penX_ + w + 1 >= kSize) {
            penX_ = 1;
            penY_ += rowH_ + 1;
            rowH_ = 0;
        }
        if (penY_ + h + 1 < kSize) {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    pixels_[static_cast<size_t>(penY_ + y) * kSize + penX_ + x] = sdf[y * w + x];
            g.u0 = static_cast<float>(penX_) / kSize;
            g.v0 = static_cast<float>(penY_) / kSize;
            g.u1 = static_cast<float>(penX_ + w) / kSize;
            g.v1 = static_cast<float>(penY_ + h) / kSize;
            g.x0 = static_cast<float>(xo);
            g.y0 = static_cast<float>(yo);
            g.x1 = static_cast<float>(xo + w);
            g.y1 = static_cast<float>(yo + h);
            penX_ += w + 1;
            rowH_ = std::max(rowH_, h);
            dirty_ = true;
        }
    }
    if (sdf) stbtt_FreeSDF(sdf, nullptr);
    return glyphs_[cp] = g;
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
