#include "render/label_layer.hpp"

#include "core/geo.hpp"
#include "labels/label_data.hpp"

#include "labels.glsl.h"

#include <stb_truetype.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_set>

namespace otk {

namespace embedded {
extern const unsigned char label_font[];
extern const std::size_t label_font_size;
}

namespace {

constexpr std::size_t kLabelBytes = 2 * 1024 * 1024;
constexpr std::size_t kIconBytes = 256 * 1024;
constexpr std::size_t kMaxBatches = 72;
constexpr int kDecodesPerFrame = 4;
constexpr std::uint32_t kDot = 0xE000;
constexpr double kOverviewMeters = 15000.0;
constexpr double kCoarseMeters = 2000.0;
constexpr double kCountyMeters = 500.0;
constexpr double kStateMeters = 6500.0;
constexpr int kOverviewClass = 18;
constexpr int kMaxLabels = 700;
constexpr std::size_t kMaxCandidates = 20000;
constexpr int kIconSize = 64;
constexpr int kIcons = 4;
constexpr int kAtlas = 1024;
constexpr float kFadeIn = 0.25f;
constexpr float kFadeOut = 0.2f;

enum Tier { Fine = 0, Coarse = 1, Overview = 2 };

struct BorderStyle {
    float width, casing, on, off;
    float color[4];
};

constexpr BorderStyle kBorders[4] = {
    {2.0f, 1.0f, 0.0f, 0.0f, {1.0f, 1.0f, 1.0f, 0.95f}},
    {1.8f, 0.9f, 5.0f, 4.0f, {1.0f, 1.0f, 1.0f, 0.9f}},
    {1.6f, 0.9f, 4.0f, 3.0f, {1.0f, 1.0f, 1.0f, 0.9f}},
    {1.3f, 0.8f, 3.5f, 3.0f, {0.92f, 0.92f, 0.92f, 0.8f}},
};
constexpr float kShade[4] = {0.0f, 0.0f, 0.0f, 0.4f};

struct RoadStyle {
    float zoom[6];
    float width[6];
    float casing;
    float on, off;
    float color[4];
};

constexpr RoadStyle kRoads[12] = {
    {{5.5f, 9, 12, 14, 16, 18}, {1.2f, 1.8f, 2.6f, 3.6f, 6.0f, 11.0f}, 0.7f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{9.5f, 11, 12, 14, 16, 18}, {0.9f, 1.2f, 1.6f, 2.2f, 3.6f, 6.5f}, 0.6f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{6.5f, 9, 12, 14, 16, 18}, {1.0f, 1.6f, 2.3f, 3.2f, 5.5f, 10.0f}, 0.7f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{10.5f, 11, 12, 14, 16, 18}, {0.9f, 1.1f, 1.5f, 2.0f, 3.4f, 6.0f}, 0.6f, 0, 0, {0.949f, 0.831f, 0.471f, 0.95f}},
    {{8.5f, 10, 12, 14, 16, 18}, {0.9f, 1.2f, 1.8f, 3.0f, 5.0f, 9.0f}, 0.6f, 0, 0, {0.91f, 0.91f, 0.91f, 0.9f}},
    {{10.0f, 11, 12, 14, 16, 18}, {0.9f, 1.1f, 1.5f, 2.6f, 4.5f, 8.0f}, 0.6f, 0, 0, {0.87f, 0.87f, 0.87f, 0.85f}},
    {{11.0f, 12, 13, 14, 16, 18}, {0.8f, 1.0f, 1.3f, 2.2f, 4.0f, 7.0f}, 0.5f, 0, 0, {0.8f, 0.8f, 0.8f, 0.8f}},
    {{12.5f, 13, 14, 15, 16, 18}, {0.7f, 0.9f, 1.6f, 2.2f, 3.2f, 6.0f}, 0.5f, 0, 0, {0.75f, 0.75f, 0.75f, 0.75f}},
    {{14.5f, 15, 16, 17, 18, 19}, {0.7f, 1.0f, 1.6f, 2.4f, 3.5f, 5.0f}, 0.4f, 0, 0, {0.69f, 0.69f, 0.69f, 0.65f}},
    {{15.0f, 16, 17, 18, 19, 20}, {0.7f, 0.9f, 1.1f, 1.5f, 2.0f, 2.5f}, 0.0f, 3.0f, 2.0f, {0.84f, 0.78f, 0.67f, 0.6f}},
    {{10.0f, 12, 14, 16, 18, 20}, {0.9f, 1.4f, 2.6f, 4.5f, 8.0f, 10.0f}, 0.0f, 4.0f, 3.0f, {0.9f, 0.9f, 0.9f, 0.45f}},
    {{13.0f, 14, 15, 16, 18, 20}, {0.7f, 1.2f, 1.8f, 2.8f, 5.0f, 7.0f}, 0.0f, 3.0f, 3.0f, {0.8f, 0.8f, 0.8f, 0.4f}},
};
constexpr float kRoadShade[4] = {0.08f, 0.08f, 0.08f, 0.55f};

struct LabelVertex {
    float x, y, u, v;
    std::uint32_t fill, halo;
    float a, b;
};

struct TextStyle {
    float size = 12.0f;
    float tracking = 0.0f;
    std::uint32_t fill = 0xFFFFFFFF;
    std::uint32_t halo = 0xC0141414;
    float haloPx = 1.0f;
    float dot = 0.0f;
    float padding = 3.0f;
};

std::uint32_t rgba(int r, int g, int b, float a) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 |
           static_cast<std::uint32_t>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f)) << 24;
}

std::uint32_t faded(std::uint32_t c, float alpha) {
    const float a = static_cast<float>(c >> 24) * std::clamp(alpha, 0.0f, 1.0f);
    return (c & 0x00FFFFFFu) | static_cast<std::uint32_t>(std::lround(a)) << 24;
}

TextStyle scaled(TextStyle s, double dpi) {
    const float k = static_cast<float>(dpi);
    s.size *= k;
    s.tracking *= k;
    s.haloPx *= k;
    s.dot *= k;
    s.padding *= k;
    return s;
}

TextStyle styleFor(const labels::Place& p, double zoom, double dpi) {
    TextStyle s;
    const std::uint32_t dark = rgba(20, 20, 20, 0.78f);
    switch (p.kind) {
    case labels::Kind::Country: s = {18.0f, 0.3f, rgba(255, 255, 255, 1.0f), rgba(20, 20, 20, 0.8f), 1.3f, 0.0f, 6.0f}; break;
    case labels::Kind::State: s = {11.0f, 1.0f, rgba(236, 236, 236, 1.0f), dark, 1.0f, 0.0f, 4.0f}; break;
    case labels::Kind::County: s = {10.5f, 0.3f, rgba(218, 218, 218, 1.0f), rgba(20, 20, 20, 0.72f), 1.0f, 0.0f, 4.0f}; break;
    case labels::Kind::Suburb: s = {10.5f, 0.2f, rgba(228, 228, 228, 1.0f), dark, 1.0f, 0.0f, 3.0f}; break;
    default: {
        const double pop = std::exp2(p.population / 8.0);
        const bool capital = (p.flags & labels::kCapital) != 0;
        const float size = capital || pop >= 1e6 ? 14.0f : pop >= 5e5 ? 13.5f : pop >= 1e5 ? 12.0f : pop >= 1e4 ? 11.5f : 11.0f;
        const float dot = zoom < 10.5 ? (capital ? 3.2f : 2.6f) : 0.0f;
        s = {size, 0.0f, rgba(255, 255, 255, 1.0f), dark, 1.1f, dot, 3.0f};
        break;
    }
    }
    return scaled(s, dpi);
}

TextStyle markStyle(const TileMark& m, double dpi) {
    const std::uint32_t dark = rgba(20, 20, 20, 0.78f);
    const std::uint32_t blue = rgba(120, 172, 245, 1.0f);
    TextStyle s;
    switch (m.type) {
    case MarkType::Road: s = {11.0f, 0.0f, rgba(255, 255, 255, 1.0f), dark, 1.0f, 0.0f, 2.0f}; break;
    case MarkType::Shield: {
        const bool interstate = m.network == "us-interstate";
        s = {10.5f, 0.0f, interstate ? rgba(255, 255, 255, 1.0f) : rgba(25, 25, 25, 1.0f), rgba(0, 0, 0, 0.0f), 0.0f, 0.0f, 2.0f};
        break;
    }
    case MarkType::Place:
        s = m.rank == 0 ? TextStyle{10.5f, 1.1f, rgba(234, 234, 234, 1.0f), dark, 1.0f, 0.0f, 3.0f}
                        : TextStyle{11.5f, 0.0f, rgba(255, 255, 255, 1.0f), dark, 1.0f, 0.0f, 3.0f};
        break;
    case MarkType::Park: s = {m.rank == 0 ? 12.0f : 11.5f, 0.0f, rgba(152, 218, 152, 1.0f), dark, 1.0f, 0.0f, 3.0f}; break;
    case MarkType::Water:
        s = m.rank == 0 ? TextStyle{13.0f, 1.5f, blue, rgba(20, 20, 20, 0.65f), 1.0f, 0.0f, 4.0f}
                        : TextStyle{11.5f, 0.0f, blue, dark, 1.0f, 0.0f, 3.0f};
        break;
    case MarkType::Waterway: s = {11.0f, 0.2f, blue, dark, 1.0f, 0.0f, 2.0f}; break;
    }
    return scaled(s, dpi);
}

float markZoom(const TileMark& m) {
    static constexpr float kRoad[8] = {12.0f, 12.5f, 13.0f, 13.5f, 14.0f, 15.0f, 16.5f, 16.5f};
    const int r = m.rank;
    switch (m.type) {
    case MarkType::Shield: return r == 0 ? 7.0f : r == 1 ? 8.5f : 10.5f;
    case MarkType::Road: return kRoad[std::min(r, 7)];
    case MarkType::Place: return r == 0 ? 13.0f : r == 1 ? 12.0f : 11.5f;
    case MarkType::Park: return r == 0 ? 6.0f : 14.5f;
    case MarkType::Water: return r == 0 ? 2.5f : r == 1 ? 8.0f : m.zoom <= 10 ? std::max(8.0f, static_cast<float>(m.zoom)) : 12.0f;
    case MarkType::Waterway: return r == 0 ? 12.0f : r == 1 ? 13.5f : 14.5f;
    }
    return 20.0f;
}

float markBase(const TileMark& m) {
    switch (m.type) {
    case MarkType::Shield: return 1.0f;
    case MarkType::Place: return 2.2f;
    case MarkType::Water: return 2.4f;
    case MarkType::Park: return 2.6f;
    case MarkType::Waterway: return 3.0f;
    default: return 3.2f;
    }
}

std::uint64_t markKey(const TileMark& m) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h = (h ^ (v & 0xFF)) * 1099511628211ull;
            v >>= 8;
        }
    };
    for (unsigned char c : m.text) h = (h ^ c) * 1099511628211ull;
    mix(static_cast<std::uint64_t>(m.type) << 8 | m.rank);
    const auto& p = m.points[m.points.size() / 2];
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(p[0] * 2e4))));
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(p[1] * 2e4))));
    return h & 0x7FFFFFFFFFFFFFFFull;
}

float stop(const float (&zoom)[6], const float (&value)[6], double z) {
    if (z < zoom[0]) return 0.0f;
    for (int i = 1; i < 6; ++i)
        if (z <= zoom[i]) {
            const double t = (z - zoom[i - 1]) / std::max(1e-6f, zoom[i] - zoom[i - 1]);
            return static_cast<float>(value[i - 1] + (value[i] - value[i - 1]) * t);
        }
    return value[5];
}

std::string wrapName(const std::string& name, size_t limit) {
    if (name.size() <= limit || name.find('\n') != std::string::npos) return name;
    size_t best = std::string::npos, score = name.size();
    for (size_t i = name.find(' '); i != std::string::npos; i = name.find(' ', i + 1)) {
        size_t s = std::max(i, name.size() - i - 1);
        if (s < score) {
            score = s;
            best = i;
        }
    }
    if (best == std::string::npos) return name;
    std::string out = name;
    out[best] = '\n';
    return out;
}

std::vector<std::uint32_t> codepoints(std::string_view s) {
    std::vector<std::uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        int n = 1;
        if (c >= 0xF0) cp = c & 0x07u, n = 4;
        else if (c >= 0xE0) cp = c & 0x0Fu, n = 3;
        else if (c >= 0xC0) cp = c & 0x1Fu, n = 2;
        if (i + n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        out.push_back(cp);
        i += n;
    }
    return out;
}

double shieldShape(int type, double u, double v) {
    if (type >= 2) {
        const double hx = 0.94, hy = 0.78, r = 0.34;
        const double qx = std::max(std::abs(u) - (hx - r), 0.0), qy = std::max(std::abs(v) - (hy - r), 0.0);
        return std::hypot(qx, qy) - r;
    }
    const double au = std::abs(u);
    double top = type == 0 ? -0.86 + 0.09 * (1.0 - std::abs(au * 2.0 - 1.0)) : -0.86;
    if (v < top) return top - v;
    if (v <= 0.18) return au - 0.9;
    const double t = std::clamp((v - 0.18) / 0.78, 0.0, 1.0);
    const double half = 0.9 * std::pow(1.0 - t, 0.75);
    return au - half;
}

std::vector<std::uint8_t> makeIcons() {
    const int w = kIconSize * kIcons, h = kIconSize;
    std::vector<std::uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    for (int type = 0; type < kIcons; ++type)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < kIconSize; ++x) {
                double acc[4] = {0, 0, 0, 0};
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const double u = (x + (sx + 0.5) / 4.0) / kIconSize * 2.0 - 1.0;
                        const double v = (y + (sy + 0.5) / 4.0) / kIconSize * 2.0 - 1.0;
                        if (shieldShape(type, u, v) > 0.0) continue;
                        const bool core = shieldShape(type, u / 0.84, (v - 0.01) / 0.84) <= 0.0;
                        double c[3];
                        if (type == 0) {
                            if (!core) c[0] = c[1] = c[2] = 1.0;
                            else if (v < -0.38) c[0] = 0.85, c[1] = 0.22, c[2] = 0.25;
                            else c[0] = 0.18, c[1] = 0.40, c[2] = 0.75;
                        } else {
                            const double edge = type == 3 ? 0.3 : 0.08;
                            if (!core) c[0] = c[1] = c[2] = edge;
                            else c[0] = c[1] = c[2] = 1.0;
                        }
                        acc[0] += c[0];
                        acc[1] += c[1];
                        acc[2] += c[2];
                        acc[3] += 1.0;
                    }
                const size_t i = (static_cast<size_t>(y) * w + static_cast<size_t>(type) * kIconSize + x) * 4;
                if (acc[3] > 0.0) {
                    px[i + 0] = static_cast<std::uint8_t>(std::lround(acc[0] / acc[3] * 255.0));
                    px[i + 1] = static_cast<std::uint8_t>(std::lround(acc[1] / acc[3] * 255.0));
                    px[i + 2] = static_cast<std::uint8_t>(std::lround(acc[2] / acc[3] * 255.0));
                }
                px[i + 3] = static_cast<std::uint8_t>(std::lround(acc[3] / 16.0 * 255.0));
            }
    return px;
}

int shieldType(const std::string& network) {
    if (network == "us-interstate") return 0;
    if (network == "us-highway") return 1;
    if (network == "us-state") return 2;
    return 3;
}

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    float advance = 0;
};

class GlyphCache {
public:
    bool init(const unsigned char* font) {
        if (!font || !stbtt_InitFont(&font_, font, stbtt_GetFontOffsetForIndex(font, 0))) return false;
        pixels_.assign(static_cast<size_t>(kAtlas) * kAtlas * 2, 0);
        for (int i = 0; i < 256; ++i) gamma_[i] = static_cast<std::uint8_t>(std::lround(std::pow(i / 255.0, 0.7) * 255.0));
        ok_ = true;
        return true;
    }

    void reset() {
        glyphs_.clear();
        std::fill(pixels_.begin(), pixels_.end(), std::uint8_t(0));
        penX_ = penY_ = 1;
        rowH_ = 0;
        full_ = false;
        dirty_ = true;
    }

    float scale(float px) const { return stbtt_ScaleForMappingEmToPixels(&font_, px); }

    float kerning(std::uint32_t a, std::uint32_t b, float px) const {
        return ok_ ? stbtt_GetCodepointKernAdvance(&font_, static_cast<int>(a), static_cast<int>(b)) * scale(px) : 0.0f;
    }

    const Glyph& glyph(std::uint32_t cp, float px, float halo) {
        const std::uint64_t key = cp | static_cast<std::uint64_t>(std::lround(px * 4.0f)) << 32 | static_cast<std::uint64_t>(std::lround(halo * 4.0f)) << 48;
        auto it = glyphs_.find(key);
        if (it != glyphs_.end()) return it->second;
        Glyph g;
        if (!ok_) return glyphs_[key] = g;
        const int pad = static_cast<int>(std::ceil(halo)) + 2;
        std::vector<std::uint8_t> cover;
        int w = 0, h = 0, ox = 0, oy = 0;
        if (cp == kDot) {
            const float r = px;
            w = h = static_cast<int>(std::ceil(r * 2.0f)) + 2;
            ox = oy = -w / 2;
            cover.assign(static_cast<size_t>(w) * h, 0);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    int inside = 0;
                    for (int s = 0; s < 16; ++s) {
                        const float dx = x + (s % 4 + 0.5f) / 4.0f + ox, dy = y + (s / 4 + 0.5f) / 4.0f + oy;
                        inside += dx * dx + dy * dy <= r * r ? 1 : 0;
                    }
                    cover[static_cast<size_t>(y) * w + x] = static_cast<std::uint8_t>(inside * 255 / 16);
                }
        } else {
            const float s = scale(px);
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font_, static_cast<int>(cp), &adv, &lsb);
            g.advance = adv * s;
            int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
            stbtt_GetCodepointBitmapBox(&font_, static_cast<int>(cp), s, s, &ix0, &iy0, &ix1, &iy1);
            w = ix1 - ix0;
            h = iy1 - iy0;
            ox = ix0;
            oy = iy0;
            if (w > 0 && h > 0) {
                cover.assign(static_cast<size_t>(w) * h, 0);
                stbtt_MakeCodepointBitmap(&font_, cover.data(), w, h, w, s, s, static_cast<int>(cp));
                for (auto& c : cover) c = gamma_[c];
            }
        }
        if (w <= 0 || h <= 0) return glyphs_[key] = g;
        const int W = w + 2 * pad, H = h + 2 * pad;
        if (penX_ + W + 1 >= kAtlas) {
            penX_ = 1;
            penY_ += rowH_ + 1;
            rowH_ = 0;
        }
        if (penY_ + H + 1 >= kAtlas) {
            full_ = true;
            return glyphs_[key] = g;
        }
        std::vector<float> fill(static_cast<size_t>(W) * H, 0.0f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) fill[static_cast<size_t>(y + pad) * W + x + pad] = cover[static_cast<size_t>(y) * w + x] / 255.0f;
        const int r = static_cast<int>(std::ceil(halo + 1.0f));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float best = 0.0f;
                if (halo > 0.0f)
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            const int sx = x + dx, sy = y + dy;
                            if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
                            const float weight = std::clamp(halo + 1.0f - std::sqrt(static_cast<float>(dx * dx + dy * dy)), 0.0f, 1.0f);
                            best = std::max(best, std::min(1.0f, fill[static_cast<size_t>(sy) * W + sx] * 2.0f) * weight);
                        }
                std::uint8_t* dst = &pixels_[(static_cast<size_t>(penY_ + y) * kAtlas + penX_ + x) * 2];
                dst[0] = static_cast<std::uint8_t>(std::lround(fill[static_cast<size_t>(y) * W + x] * 255.0f));
                dst[1] = static_cast<std::uint8_t>(std::lround(best * 255.0f));
            }
        g.u0 = static_cast<float>(penX_) / kAtlas;
        g.v0 = static_cast<float>(penY_) / kAtlas;
        g.u1 = static_cast<float>(penX_ + W) / kAtlas;
        g.v1 = static_cast<float>(penY_ + H) / kAtlas;
        g.x0 = ox - pad;
        g.y0 = oy - pad;
        g.w = W;
        g.h = H;
        penX_ += W + 1;
        rowH_ = std::max(rowH_, H);
        dirty_ = true;
        return glyphs_[key] = g;
    }

    float width(std::string_view text, const TextStyle& s) {
        float w = 0.0f;
        std::uint32_t prev = 0;
        int n = 0;
        for (std::uint32_t cp : codepoints(text)) {
            if (prev) w += kerning(prev, cp, s.size);
            w += glyph(cp, s.size, s.haloPx).advance;
            prev = cp;
            ++n;
        }
        return w + s.tracking * std::max(n - 1, 0);
    }

    bool full() const { return full_; }
    bool dirty() const { return dirty_; }
    void clean() { dirty_ = false; }
    const std::vector<std::uint8_t>& pixels() const { return pixels_; }

private:
    stbtt_fontinfo font_{};
    bool ok_ = false;
    std::unordered_map<std::uint64_t, Glyph> glyphs_;
    std::vector<std::uint8_t> pixels_;
    std::uint8_t gamma_[256] = {};
    int penX_ = 1, penY_ = 1, rowH_ = 0;
    bool full_ = false;
    bool dirty_ = true;
};

enum class Layout : std::uint8_t { Side, Center, Along, Shield };

struct Item {
    std::uint64_t key = 0;
    Vec3 anchor{};
    std::string text;
    TextStyle style;
    Layout layout = Layout::Center;
    float ox = 0, oy = 0;
    float angle = 0;
    int shield = -1;
    float shieldW = 0, shieldH = 0;
};

struct Fade {
    float opacity = 0.0f;
    std::uint64_t placed = 0;
    double height = 0.0;
    bool hasHeight = false;
    Item item;
};

}

struct LabelLayer::Text {
    GlyphCache glyphs;
    std::unordered_map<std::uint64_t, Fade> fades;
    std::chrono::steady_clock::time_point last{};
    bool animating = false;
};

LabelLayer::LabelLayer() : text_(std::make_unique<Text>()) {}

LabelLayer::~LabelLayer() = default;

bool LabelLayer::init() {
    if (ready_) return true;
    if (!text_->glyphs.init(embedded::label_font)) return false;
    loading_ = std::async(std::launch::async, [] { return labels::Dataset::get().available(); });
    sg_backend backend = sg_query_backend();
    lineShader_ = sg_make_shader(bline_shader_desc(backend));
    labelShader_ = sg_make_shader(label_shader_desc(backend));
    iconShader_ = sg_make_shader(icon_shader_desc(backend));

    auto blend = [](sg_color_target_state& c) {
        c.blend.enabled = true;
        c.blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        c.blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        c.blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        c.blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    };

    sg_pipeline_desc lp{};
    lp.shader = lineShader_;
    lp.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    lp.layout.attrs[ATTR_bline_corner].buffer_index = 0;
    lp.layout.attrs[ATTR_bline_corner].format = SG_VERTEXFORMAT_FLOAT2;
    lp.layout.attrs[ATTR_bline_seg].buffer_index = 1;
    lp.layout.attrs[ATTR_bline_seg].format = SG_VERTEXFORMAT_FLOAT4;
    lp.layout.attrs[ATTR_bline_info].buffer_index = 1;
    lp.layout.attrs[ATTR_bline_info].format = SG_VERTEXFORMAT_FLOAT2;
    lp.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    blend(lp.colors[0]);
    lp.depth.pixel_format = SG_PIXELFORMAT_NONE;
    lp.sample_count = 1;
    linePipeline_ = sg_make_pipeline(&lp);

    auto overlay = [&](sg_shader shader) {
        sg_pipeline_desc tp{};
        tp.shader = shader;
        tp.layout.attrs[ATTR_label_position].format = SG_VERTEXFORMAT_FLOAT2;
        tp.layout.attrs[ATTR_label_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
        tp.layout.attrs[ATTR_label_color0].format = SG_VERTEXFORMAT_UBYTE4N;
        tp.layout.attrs[ATTR_label_color1].format = SG_VERTEXFORMAT_UBYTE4N;
        tp.layout.attrs[ATTR_label_texcoord1].format = SG_VERTEXFORMAT_FLOAT2;
        blend(tp.colors[0]);
        tp.depth.compare = SG_COMPAREFUNC_ALWAYS;
        tp.depth.write_enabled = false;
        return sg_make_pipeline(&tp);
    };
    labelPipeline_ = overlay(labelShader_);
    iconPipeline_ = overlay(iconShader_);

    const float quad[12] = {0, -1, 1, -1, 1, 1, 0, -1, 1, 1, 0, 1};
    sg_buffer_desc qd{};
    qd.usage.vertex_buffer = true;
    qd.data = SG_RANGE(quad);
    quad_ = sg_make_buffer(&qd);

    for (sg_buffer& b : labelBuffers_) {
        sg_buffer_desc bd{};
        bd.usage.vertex_buffer = true;
        bd.usage.write_transient = true;
        bd.size = kLabelBytes;
        b = sg_make_buffer(&bd);
    }
    for (sg_buffer& b : iconBuffers_) {
        sg_buffer_desc bd{};
        bd.usage.vertex_buffer = true;
        bd.usage.write_transient = true;
        bd.size = kIconBytes;
        b = sg_make_buffer(&bd);
    }

    std::vector<std::uint8_t> icons = makeIcons();
    sg_image_desc id{};
    id.width = kIconSize * kIcons;
    id.height = kIconSize;
    id.pixel_format = SG_PIXELFORMAT_RGBA8;
    id.data.mip_levels[0] = {icons.data(), icons.size()};
    iconImage_ = sg_make_image(&id);
    sg_view_desc iv{};
    iv.texture.image = iconImage_;
    iconView_ = sg_make_view(&iv);

    sg_sampler_desc sm{};
    sm.min_filter = SG_FILTER_LINEAR;
    sm.mag_filter = SG_FILTER_LINEAR;
    sm.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sm.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_ = sg_make_sampler(&sm);
    uploadAtlas();
    ready_ = true;
    return true;
}

void LabelLayer::shutdown() {
    if (!ready_) return;
    if (sg_isvalid()) {
        for (auto& [k, b] : batches_)
            if (b.buffer.id) sg_destroy_buffer(b.buffer);
        for (sg_pipeline p : {linePipeline_, labelPipeline_, iconPipeline_}) sg_destroy_pipeline(p);
        for (sg_shader s : {lineShader_, labelShader_, iconShader_}) sg_destroy_shader(s);
        sg_destroy_buffer(quad_);
        for (sg_buffer b : labelBuffers_) sg_destroy_buffer(b);
        for (sg_buffer b : iconBuffers_) sg_destroy_buffer(b);
        if (atlasView_.id) sg_destroy_view(atlasView_);
        if (atlasImage_.id) sg_destroy_image(atlasImage_);
        sg_destroy_view(iconView_);
        sg_destroy_image(iconImage_);
        sg_destroy_sampler(sampler_);
    }
    batches_.clear();
    text_->fades.clear();
    if (loading_.valid()) loading_.wait();
    ready_ = false;
}

bool LabelLayer::available() {
    if (!ready_) return false;
    if (!loaded_ && loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        usable_ = loading_.get();
        loaded_ = true;
    }
    return loaded_ && usable_;
}

bool LabelLayer::animating() const { return ready_ && text_->animating; }

void LabelLayer::beginFrame() {
    ++frame_;
    decodes_ = 0;
    trim();
}

void LabelLayer::uploadAtlas() {
    GlyphCache& cache = text_->glyphs;
    if (!cache.dirty() && atlasImage_.id) return;
    if (atlasView_.id) sg_destroy_view(atlasView_);
    if (atlasImage_.id) sg_destroy_image(atlasImage_);
    sg_image_desc d{};
    d.width = kAtlas;
    d.height = kAtlas;
    d.pixel_format = SG_PIXELFORMAT_RG8;
    d.data.mip_levels[0] = {cache.pixels().data(), cache.pixels().size()};
    atlasImage_ = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = atlasImage_;
    atlasView_ = sg_make_view(&v);
    cache.clean();
}

void LabelLayer::trim() {
    while (batches_.size() > kMaxBatches) {
        auto oldest = batches_.end();
        for (auto it = batches_.begin(); it != batches_.end(); ++it)
            if (oldest == batches_.end() || it->second.used < oldest->second.used) oldest = it;
        if (oldest == batches_.end() || oldest->second.used + 2 >= frame_) break;
        if (oldest->second.buffer.id) sg_destroy_buffer(oldest->second.buffer);
        batches_.erase(oldest);
    }
}

void LabelLayer::keys(const GeoRect& tile, double unitDeg, std::vector<std::pair<int, int>>& out) const {
    out.clear();
    const double meters = unitDeg * 111320.0;
    const int tier = meters >= kOverviewMeters ? Overview : meters >= kCoarseMeters ? Coarse : Fine;
    const double size = tier == Fine ? labels::kFineCell : labels::kCoarseCell;
    const int cols = tier == Fine ? labels::kFineCols : labels::kCoarseCols;
    const int rows = tier == Fine ? labels::kFineRows : labels::kCoarseRows;
    const double margin = unitDeg * 4.0 + (tier == Fine ? 0.3 : 1.1);
    const int c0 = std::clamp(static_cast<int>(std::floor((tile.west - margin + 180.0) / size)), 0, cols - 1);
    const int c1 = std::clamp(static_cast<int>(std::floor((tile.east + margin + 180.0) / size)), 0, cols - 1);
    const int r0 = std::clamp(static_cast<int>(std::floor((tile.south - margin + 90.0) / size)), 0, rows - 1);
    const int r1 = std::clamp(static_cast<int>(std::floor((tile.north + margin + 90.0) / size)), 0, rows - 1);
    const labels::Dataset& d = labels::Dataset::get();
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            const int cell = r * cols + c;
            if (tier == Fine ? d.hasFine(cell) : d.hasCoarse(cell)) out.emplace_back(tier, cell);
        }
}

LabelLayer::Batch* LabelLayer::batch(int tier, int cell, bool load) {
    const int id = tier * 100000 + cell;
    auto it = batches_.find(id);
    if (it != batches_.end()) {
        it->second.used = frame_;
        return &it->second;
    }
    if (!load || decodes_ >= kDecodesPerFrame) return nullptr;
    ++decodes_;
    const double size = tier == Fine ? labels::kFineCell : labels::kCoarseCell;
    const int cols = tier == Fine ? labels::kFineCols : labels::kCoarseCols;
    Batch b;
    b.west = -180.0 + (cell % cols) * size;
    b.south = -90.0 + (cell / cols) * size;
    b.east = b.west + size;
    b.north = b.south + size;
    b.used = frame_;
    labels::Lines lines;
    const labels::Dataset& d = labels::Dataset::get();
    if (tier == Fine ? d.fine(cell, lines) : d.coarse(cell, lines)) {
        std::vector<float> inst;
        inst.reserve(lines.xy.size() * 3);
        const int minClass = tier == Overview ? kOverviewClass : 0;
        for (const labels::Run& r : lines.runs) {
            if (r.count < 2) continue;
            const double coslat = std::max(std::cos(lines.xy[2 * r.first + 1] * labels::kUnit * kDegToRad), 0.05);
            double along = 0.0;
            std::int64_t prev = -1;
            for (std::uint32_t i = 0; i < r.count; ++i) {
                const std::uint32_t v = r.first + i;
                const bool keep = i == 0 || i + 1 == r.count || minClass == 0 || lines.importance.size() <= v || lines.importance[v] >= minClass;
                if (!keep) continue;
                if (prev >= 0) {
                    const double ax = lines.xy[2 * prev] * labels::kUnit, ay = lines.xy[2 * prev + 1] * labels::kUnit;
                    const double bx = lines.xy[2 * v] * labels::kUnit, by = lines.xy[2 * v + 1] * labels::kUnit;
                    inst.insert(inst.end(), {static_cast<float>(ax - b.west), static_cast<float>(ay - b.south), static_cast<float>(bx - b.west),
                                             static_cast<float>(by - b.south), static_cast<float>(along), static_cast<float>(r.level)});
                    along += std::hypot((bx - ax) * coslat, by - ay);
                }
                prev = v;
            }
        }
        if (!inst.empty()) {
            sg_buffer_desc bd{};
            bd.usage.vertex_buffer = true;
            bd.data = {inst.data(), inst.size() * sizeof(float)};
            b.buffer = sg_make_buffer(&bd);
            b.count = static_cast<int>(inst.size() / 6);
        }
    }
    return &(batches_[id] = b);
}

bool LabelLayer::prepare(const GeoRect& tile, double unitDeg) {
    if (!available()) return true;
    keys(tile, unitDeg, scratchKeys_);
    bool ok = true;
    for (const auto& [tier, cell] : scratchKeys_)
        if (!batch(tier, cell, true)) ok = false;
    return ok;
}

void LabelLayer::drawRoads(const std::vector<RoadBatch>& batches, const GeoRect& tile, double unitDeg, double dpi) {
    if (!ready_ || batches.empty()) return;
    const double lat = (tile.south + tile.north) * 0.5;
    const double coslat = std::max(std::cos(lat * kDegToRad), 0.05);
    const double zoom = std::log2(156543.034 * std::max(std::cos(lat * kDegToRad), 1e-3) / std::max(unitDeg * 111320.0 * dpi, 1e-6));
    bline_params_t p{};
    p.frame[0] = static_cast<float>(coslat);
    p.frame[1] = static_cast<float>(unitDeg);
    p.frame[2] = static_cast<float>(dpi);
    std::memcpy(p.shade, kRoadShade, sizeof(p.shade));
    bool any = false;
    for (int i = 0; i < 12; ++i) {
        const RoadStyle& s = kRoads[i];
        const float w = stop(s.zoom, s.width, zoom);
        const float appear = std::clamp(static_cast<float>((zoom - s.zoom[0]) / 0.75), 0.0f, 1.0f);
        any = any || w > 0.0f;
        p.styles[4 + i][0] = w;
        p.styles[4 + i][1] = w > 0.0f ? s.casing : 0.0f;
        p.styles[4 + i][2] = s.on;
        p.styles[4 + i][3] = s.off;
        std::memcpy(p.colors[4 + i], s.color, sizeof(p.colors[4 + i]));
        p.colors[4 + i][3] *= 0.35f + 0.65f * appear;
    }
    if (!any) return;
    sg_apply_pipeline(linePipeline_);
    for (int pass = 0; pass < 2; ++pass) {
        p.frame[3] = static_cast<float>(pass);
        for (const RoadBatch& b : batches) {
            if (b.count <= 0 || sg_query_buffer_state(b.buffer) != SG_RESOURCESTATE_VALID) continue;
            sg_bindings bind{};
            bind.vertex_buffers[0] = quad_;
            bind.vertex_buffers[1] = b.buffer;
            sg_apply_bindings(&bind);
            std::memcpy(p.xform, b.xform, sizeof(p.xform));
            sg_apply_uniforms(UB_bline_params, SG_RANGE(p));
            sg_draw(0, 6, b.count);
        }
    }
}

void LabelLayer::drawSurface(const GeoRect& tile, double unitDeg, double dpi) {
    if (!available()) return;
    keys(tile, unitDeg, scratchKeys_);
    if (scratchKeys_.empty()) return;
    const double span = tile.north - tile.south;
    if (span <= 0.0) return;
    const double coslat = std::max(std::cos((tile.south + tile.north) * 0.5 * kDegToRad), 0.05);
    const double meters = unitDeg * 111320.0;
    bline_params_t p{};
    p.frame[0] = static_cast<float>(coslat);
    p.frame[1] = static_cast<float>(unitDeg);
    p.frame[2] = static_cast<float>(dpi);
    p.frame[3] = 2.0f;
    std::memcpy(p.shade, kShade, sizeof(p.shade));
    for (int l = 0; l < 4; ++l) {
        const BorderStyle& s = kBorders[l];
        const bool hidden = (l == 3 && meters > kCountyMeters) || (l == 2 && meters > kStateMeters);
        p.styles[l][0] = hidden ? 0.0f : s.width;
        p.styles[l][1] = s.casing;
        p.styles[l][2] = s.on;
        p.styles[l][3] = s.off;
        std::memcpy(p.colors[l], s.color, sizeof(p.colors[l]));
    }
    bool applied = false;
    for (const auto& [tier, cell] : scratchKeys_) {
        Batch* b = batch(tier, cell, false);
        if (!b || b->count <= 0 || sg_query_buffer_state(b->buffer) != SG_RESOURCESTATE_VALID) continue;
        if (!applied) {
            sg_apply_pipeline(linePipeline_);
            applied = true;
        }
        sg_bindings bind{};
        bind.vertex_buffers[0] = quad_;
        bind.vertex_buffers[1] = b->buffer;
        sg_apply_bindings(&bind);
        p.xform[0] = static_cast<float>(2.0 / span);
        p.xform[1] = static_cast<float>(2.0 / span);
        p.xform[2] = static_cast<float>((b->west - tile.west) / span * 2.0 - 1.0);
        p.xform[3] = static_cast<float>((b->south - tile.south) / span * 2.0 - 1.0);
        sg_apply_uniforms(UB_bline_params, SG_RANGE(p));
        sg_draw(0, 6, b->count);
    }
}

void LabelLayer::drawLabels(const Camera& cam, const GroundFn& ground, double dpi, const std::vector<GeoRect>& visible,
                            const std::vector<const VectorTile*>& overlay, int view) {
    if (!ready_ || cam.width <= 0 || cam.height <= 0) return;
    Text& T = *text_;
    GlyphCache& glyphs = T.glyphs;
    const bool primary = view == 0;
    const auto now = std::chrono::steady_clock::now();
    const float dt = T.last.time_since_epoch().count() == 0 ? 0.0f
                                                             : std::clamp(std::chrono::duration<float>(now - T.last).count(), 0.0f, 0.1f);
    if (primary) T.last = now;
    const bool bundled = available();
    const labels::Dataset* data = bundled ? &labels::Dataset::get() : nullptr;
    if (glyphs.full()) glyphs.reset();
    const double W = cam.width, H = cam.height;
    const Vec3 eye = cam.eye();
    const GeoPoint eyeGeo = ecefToGeodetic(eye);
    const Mat4& vp = cam.viewProjRel();
    auto project = [&](const Vec3& p, double& sx, double& sy) {
        Vec3 r = p - eye;
        Vec4 c = vp * Vec4{r.x, r.y, r.z, 1.0};
        if (c.w <= 1e-6) return false;
        sx = (c.x / c.w * 0.5 + 0.5) * W;
        sy = (0.5 - c.y / c.w * 0.5) * H;
        return true;
    };
    auto zoomAt = [&](double lat, double dist) {
        const double mpp = std::max(cam.pixelSizeAt(std::max(dist, 1.0)) * dpi, 1e-6);
        return std::log2(156543.034 * std::max(std::cos(lat * kDegToRad), 1e-3) / mpp);
    };
    auto recent = [&](std::uint64_t key) {
        auto it = T.fades.find(key);
        return it != T.fades.end() && it->second.placed + 1 >= frame_ && it->second.opacity > 0.0f;
    };
    auto height = [&](std::uint64_t key, double lat, double lon) {
        const double h = ground(lat, lon);
        auto it = T.fades.find(key);
        if (it == T.fades.end() || !it->second.hasHeight) return h;
        return it->second.height + (h - it->second.height) * std::min(1.0, dt * 6.0);
    };

    struct Candidate {
        const labels::Place* place = nullptr;
        const TileMark* mark = nullptr;
        std::uint64_t key = 0;
        double height = 0.0;
        float x = 0, y = 0, zoom = 0, priority = 0;
    };
    std::vector<Candidate> candidates;
    std::unordered_set<std::string_view> names;

    if (bundled && !visible.empty()) {
        std::vector<int> cells;
        for (const GeoRect& r : visible) {
            const int c0 = std::clamp(static_cast<int>(std::floor((r.west + 180.0) / labels::kPlaceCell)), 0, labels::kPlaceCols - 1);
            const int c1 = std::clamp(static_cast<int>(std::floor((r.east + 180.0) / labels::kPlaceCell)), 0, labels::kPlaceCols - 1);
            const int r0 = std::clamp(static_cast<int>(std::floor((r.south + 90.0) / labels::kPlaceCell)), 0, labels::kPlaceRows - 1);
            const int r1 = std::clamp(static_cast<int>(std::floor((r.north + 90.0) / labels::kPlaceCell)), 0, labels::kPlaceRows - 1);
            for (int row = r0; row <= r1; ++row)
                for (int col = c0; col <= c1; ++col) cells.push_back(row * labels::kPlaceCols + col);
        }
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        for (int cell : cells) {
            const double south = -90.0 + (cell / labels::kPlaceCols) * labels::kPlaceCell;
            const double west = -180.0 + (cell % labels::kPlaceCols) * labels::kPlaceCell;
            const double plat = std::clamp(eyeGeo.lat, south, south + labels::kPlaceCell);
            const double plon = std::clamp(eyeGeo.lon, west, west + labels::kPlaceCell);
            const double cellZoom = zoomAt(plat, length(geodeticToEcef(plat, plon, 0.0) - eye)) + 0.5;
            const int band = static_cast<int>(std::floor(cellZoom));
            for (std::uint32_t i = data->cellBegin(cell); i < data->cellEnd(cell) && candidates.size() < kMaxCandidates; ++i) {
                const labels::Place& p = data->places()[i];
                if (p.minZoom / 10 > band) break;
                const double minz = p.minZoom * 0.1;
                if (minz > cellZoom) continue;
                Vec3 e = geodeticToEcef(p.lat, p.lon, 0.0);
                if (dot(normalize(e), eye - e) <= 0.0) continue;
                double sx, sy;
                if (!project(e, sx, sy) || sx < -200 || sy < -100 || sx > W + 200 || sy > H + 100) continue;
                const std::uint64_t key = 0x8000000000000000ull | i;
                const bool held = recent(key);
                const double h = height(key, p.lat, p.lon);
                e = geodeticToEcef(p.lat, p.lon, h);
                if (!project(e, sx, sy) || sx < -40 || sy < -20 || sx > W + 40 || sy > H + 20) continue;
                const double z = zoomAt(p.lat, length(e - eye));
                const double slack = held ? 0.3 : 0.0;
                if (z < minz - slack || (p.maxZoom != 255 && z >= p.maxZoom * 0.1 + slack)) continue;
                float base = 0.8f;
                switch (p.kind) {
                case labels::Kind::Country: base = 0.0f; break;
                case labels::Kind::State: base = 1.4f; break;
                case labels::Kind::Suburb: base = 3.0f; break;
                case labels::Kind::County: base = 4.0f; break;
                default: break;
                }
                Candidate c;
                c.place = &p;
                c.key = key;
                c.height = h;
                c.x = static_cast<float>(sx);
                c.y = static_cast<float>(sy);
                c.zoom = static_cast<float>(z);
                c.priority = base + static_cast<float>(minz) - (held ? 0.6f : 0.0f);
                candidates.push_back(c);
                names.insert(data->name(p));
            }
        }
    }

    for (const VectorTile* tile : overlay) {
        if (candidates.size() >= kMaxCandidates) break;
        for (const TileMark& m : tile->marks) {
            if (m.points.empty() || m.text.empty()) continue;
            if (m.type == MarkType::Place && m.rank == 1 && names.count(m.text)) continue;
            const auto& anchor = m.points[m.points.size() / 2];
            Vec3 e = geodeticToEcef(anchor[1], anchor[0], 0.0);
            if (dot(normalize(e), eye - e) <= 0.0) continue;
            double sx, sy;
            if (!project(e, sx, sy) || sx < -400 || sy < -300 || sx > W + 400 || sy > H + 300) continue;
            const std::uint64_t key = markKey(m);
            const bool held = recent(key);
            const float minz = markZoom(m);
            const double z = zoomAt(anchor[1], length(e - eye));
            if (z < minz - (held ? 0.3 : 0.0)) continue;
            const double h = height(key, anchor[1], anchor[0]);
            e = geodeticToEcef(anchor[1], anchor[0], h);
            if (!project(e, sx, sy)) continue;
            const bool line = m.type == MarkType::Road || m.type == MarkType::Waterway || m.type == MarkType::Shield;
            if (!line && (sx < -40 || sy < -20 || sx > W + 40 || sy > H + 20)) continue;
            Candidate c;
            c.mark = &m;
            c.key = key;
            c.height = h;
            c.x = static_cast<float>(sx);
            c.y = static_cast<float>(sy);
            c.zoom = static_cast<float>(z);
            c.priority = markBase(m) + minz - (held ? 0.6f : 0.0f);
            candidates.push_back(c);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.key < b.key;
    });

    constexpr int kGrid = 64;
    const int gw = static_cast<int>(W / kGrid) + 1, gh = static_cast<int>(H / kGrid) + 1;
    std::vector<std::vector<std::array<float, 4>>> grid(static_cast<size_t>(gw) * gh);
    auto cellsOf = [&](const std::array<float, 4>& b, auto&& fn) {
        const int x0 = std::clamp(static_cast<int>(b[0] / kGrid), 0, gw - 1), x1 = std::clamp(static_cast<int>(b[2] / kGrid), 0, gw - 1);
        const int y0 = std::clamp(static_cast<int>(b[1] / kGrid), 0, gh - 1), y1 = std::clamp(static_cast<int>(b[3] / kGrid), 0, gh - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (fn(grid[static_cast<size_t>(y) * gw + x])) return true;
        return false;
    };
    auto free = [&](const std::array<float, 4>& b) {
        return !cellsOf(b, [&](const std::vector<std::array<float, 4>>& list) {
            for (const auto& o : list)
                if (b[0] < o[2] && o[0] < b[2] && b[1] < o[3] && o[1] < b[3]) return true;
            return false;
        });
    };
    auto take = [&](const std::array<float, 4>& b) {
        cellsOf(b, [&](std::vector<std::array<float, 4>>& list) {
            list.push_back(b);
            return false;
        });
    };
    std::unordered_map<std::string, std::vector<std::array<float, 2>>> placedText;
    auto repeated = [&](const std::string& key, float x, float y, float spacing) {
        auto it = placedText.find(key);
        if (it == placedText.end()) return false;
        for (const auto& q : it->second)
            if (std::hypot(q[0] - x, q[1] - y) < spacing) return true;
        return false;
    };
    auto splitLines = [](std::string_view text, std::vector<std::string_view>& out) {
        out.clear();
        for (size_t start = 0;;) {
            size_t end = text.find('\n', start);
            out.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
    };

    std::vector<Item> placed;
    std::vector<std::string_view> lines;
    std::vector<std::array<double, 3>> screen;
    for (const Candidate& c : candidates) {
        if (static_cast<int>(placed.size()) >= kMaxLabels) break;
        const TileMark* m = c.mark;
        const TextStyle s = m ? markStyle(*m, dpi) : styleFor(*c.place, c.zoom, dpi);
        const float em = s.size;
        Item item;
        item.key = c.key;
        item.style = s;

        if (m && (m->type == MarkType::Road || m->type == MarkType::Waterway)) {
            if (repeated(m->text, c.x, c.y, 260.0f * static_cast<float>(dpi))) continue;
            screen.clear();
            bool ok = true;
            for (const auto& p : m->points) {
                double sx, sy;
                if (!project(geodeticToEcef(p[1], p[0], c.height), sx, sy)) {
                    ok = false;
                    break;
                }
                screen.push_back({sx, sy, 0.0});
            }
            if (!ok || screen.size() < 2) continue;
            for (size_t i = 1; i < screen.size(); ++i)
                screen[i][2] = screen[i - 1][2] + std::hypot(screen[i][0] - screen[i - 1][0], screen[i][1] - screen[i - 1][1]);
            const float w = glyphs.width(m->text, s);
            const double total = screen.back()[2];
            const double need = w + 8.0 * dpi;
            if (total < need) continue;
            auto at = [&](double d, double& x, double& y, size_t& seg, double& t) {
                size_t i = 1;
                while (i + 1 < screen.size() && screen[i][2] < d) ++i;
                const double len = std::max(screen[i][2] - screen[i - 1][2], 1e-9);
                t = std::clamp((d - screen[i - 1][2]) / len, 0.0, 1.0);
                x = screen[i - 1][0] + (screen[i][0] - screen[i - 1][0]) * t;
                y = screen[i - 1][1] + (screen[i][1] - screen[i - 1][1]) * t;
                seg = i;
            };
            bool found = false;
            double cx = 0, cy = 0, angle = 0, mid = 0;
            for (int k = 0; k < 7 && !found; ++k) {
                const double offset = k == 0 ? 0.0 : ((k + 1) / 2) * need * 0.6 * (k % 2 ? 1.0 : -1.0);
                mid = total * 0.5 + offset;
                const double d0 = mid - need * 0.5, d1 = mid + need * 0.5;
                if (d0 < 0.0 || d1 > total) continue;
                double x0, y0, x1, y1, t;
                size_t seg;
                at(d0, x0, y0, seg, t);
                at(d1, x1, y1, seg, t);
                const double cl = std::hypot(x1 - x0, y1 - y0);
                if (cl < need * 0.9) continue;
                double worst = 0.0;
                for (const auto& q : screen) {
                    if (q[2] <= d0 || q[2] >= d1) continue;
                    worst = std::max(worst, std::abs((x1 - x0) * (y0 - q[1]) - (x0 - q[0]) * (y1 - y0)) / cl);
                }
                if (worst > 3.0 * dpi) continue;
                cx = (x0 + x1) * 0.5;
                cy = (y0 + y1) * 0.5;
                angle = std::atan2(y1 - y0, x1 - x0);
                found = true;
            }
            if (!found || cx < 0 || cy < 0 || cx > W || cy > H) continue;
            if (angle > kPi * 0.5) angle -= kPi;
            if (angle < -kPi * 0.5) angle += kPi;
            const float ca = static_cast<float>(std::cos(angle)), sa = static_cast<float>(std::sin(angle));
            const float hh = em * 1.25f;
            const int pieces = std::max(1, static_cast<int>(std::ceil(w / hh)));
            std::vector<std::array<float, 4>> boxes;
            for (int i = 0; i < pieces; ++i) {
                const float along = -w * 0.5f + (i + 0.5f) * w / pieces;
                const float px = static_cast<float>(cx) + along * ca, py = static_cast<float>(cy) + along * sa;
                const float r = std::max(w / pieces, hh) * 0.5f + s.padding;
                boxes.push_back({px - r, py - r, px + r, py + r});
            }
            bool clear = true;
            for (const auto& b : boxes) clear = clear && free(b);
            if (!clear) continue;
            for (const auto& b : boxes) take(b);
            placedText[m->text].push_back({static_cast<float>(cx), static_cast<float>(cy)});
            double seg_t, px, py;
            size_t seg;
            at(mid, px, py, seg, seg_t);
            const auto& a = m->points[seg - 1];
            const auto& b = m->points[seg];
            item.anchor = geodeticToEcef(a[1] + (b[1] - a[1]) * seg_t, a[0] + (b[0] - a[0]) * seg_t, c.height);
            item.text = m->text;
            item.layout = Layout::Along;
            item.angle = static_cast<float>(angle);
            item.ox = static_cast<float>(cx - px);
            item.oy = static_cast<float>(cy - py);
            placed.push_back(std::move(item));
            continue;
        }

        if (m && m->type == MarkType::Shield) {
            const std::string key = "#" + m->network + m->text;
            if (repeated(key, c.x, c.y, 220.0f * static_cast<float>(dpi))) continue;
            if (c.x < 0 || c.y < 0 || c.x > W || c.y > H) continue;
            const int type = shieldType(m->network);
            const float h = 20.0f * static_cast<float>(dpi);
            const float tw = glyphs.width(m->text, s);
            const float w = std::max(h * (type <= 1 ? 1.0f : 1.15f), tw + 7.0f * static_cast<float>(dpi));
            const std::array<float, 4> box{c.x - w * 0.5f - s.padding, c.y - h * 0.5f - s.padding, c.x + w * 0.5f + s.padding, c.y + h * 0.5f + s.padding};
            if (!free(box)) continue;
            take(box);
            placedText[key].push_back({c.x, c.y});
            const auto& p = m->points[m->points.size() / 2];
            item.anchor = geodeticToEcef(p[1], p[0], c.height);
            item.text = m->text;
            item.layout = Layout::Shield;
            item.shield = type;
            item.shieldW = w;
            item.shieldH = h;
            placed.push_back(std::move(item));
            continue;
        }

        std::string text;
        if (m) {
            text = m->type == MarkType::Park ? wrapName(m->text, 14) : m->text;
            if (repeated(text, c.x, c.y, 500.0f * static_cast<float>(dpi))) continue;
        } else {
            text = std::string(data->name(*c.place));
        }
        splitLines(text, lines);
        float w = 0.0f;
        for (std::string_view l : lines) w = std::max(w, glyphs.width(l, s));
        const float pitch = em * 1.2f;
        const float n = static_cast<float>(lines.size());
        const float top = -(n - 1.0f) * pitch * 0.5f - em * 0.72f;
        const float bottom = (n - 1.0f) * pitch * 0.5f + em * 0.24f;
        std::array<float, 4> box{};
        bool ok = false;
        if (s.dot > 0.0f) {
            const float gap = s.dot + 3.5f * static_cast<float>(dpi);
            const std::array<float, 4> dotBox{c.x - s.dot - 1, c.y - s.dot - 1, c.x + s.dot + 1, c.y + s.dot + 1};
            for (int side = 0; side < 2 && !ok; ++side) {
                const float textX = side == 0 ? c.x + gap : c.x - gap - w;
                box = {std::min(textX, dotBox[0]) - s.padding, c.y + top - s.padding, std::max(textX + w, dotBox[2]) + s.padding,
                       c.y + bottom + s.padding};
                ok = box[0] >= 0 && box[2] <= W && box[1] >= 0 && box[3] <= H && free(box);
                if (ok) {
                    item.layout = Layout::Side;
                    item.ox = textX - c.x;
                    item.oy = 0.0f;
                }
            }
        } else {
            const bool area = c.place && (c.place->kind == labels::Kind::Country || c.place->kind == labels::Kind::State ||
                                          c.place->kind == labels::Kind::County);
            const float h = bottom - top;
            const float shifts[5][2] = {{0.0f, 0.0f}, {0.0f, -0.9f * h}, {0.0f, 0.9f * h}, {-0.3f * w, 0.0f}, {0.3f * w, 0.0f}};
            for (int k = 0; k < (area ? 5 : 1) && !ok; ++k) {
                const float cx = c.x + shifts[k][0], cy = c.y + shifts[k][1];
                box = {cx - w * 0.5f - s.padding, cy + top - s.padding, cx + w * 0.5f + s.padding, cy + bottom + s.padding};
                ok = box[0] >= -w * 0.25f && box[2] <= W + w * 0.25f && box[1] >= 0 && box[3] <= H && free(box);
                if (ok) {
                    item.layout = Layout::Center;
                    item.ox = shifts[k][0];
                    item.oy = shifts[k][1];
                }
            }
        }
        if (!ok) continue;
        take(box);
        if (m) placedText[text].push_back({c.x, c.y});
        if (m) {
            const auto& p = m->points[m->points.size() / 2];
            item.anchor = geodeticToEcef(p[1], p[0], c.height);
        } else {
            item.anchor = geodeticToEcef(c.place->lat, c.place->lon, c.height);
        }
        item.text = std::move(text);
        placed.push_back(std::move(item));
    }

    std::vector<std::pair<const Item*, float>> draw;
    std::unordered_set<std::uint64_t> shown;
    if (primary) {
        for (const Item& it : placed) {
            Fade& f = T.fades[it.key];
            f.opacity = std::min(1.0f, f.opacity + (dt > 0.0f ? dt / kFadeIn : 1.0f));
            f.placed = frame_;
            f.item = it;
            f.hasHeight = true;
            f.height = ecefToGeodetic(it.anchor).alt;
            shown.insert(it.key);
        }
        T.animating = false;
        for (auto it = T.fades.begin(); it != T.fades.end();) {
            Fade& f = it->second;
            if (!shown.count(it->first)) {
                f.opacity -= dt / kFadeOut;
                if (f.opacity <= 0.0f) {
                    it = T.fades.erase(it);
                    continue;
                }
                T.animating = true;
            } else if (f.opacity < 1.0f) {
                T.animating = true;
            }
            draw.emplace_back(&f.item, f.opacity);
            ++it;
        }
    } else {
        for (const Item& it : placed) draw.emplace_back(&it, 1.0f);
    }

    std::vector<LabelVertex> verts, icons;
    verts.reserve(8192);
    auto nx = [&](double x) { return static_cast<float>(x / W * 2.0 - 1.0); };
    auto ny = [&](double y) { return static_cast<float>(1.0 - y / H * 2.0); };
    auto quadTo = [&](std::vector<LabelVertex>& out, const float (&px)[4], const float (&py)[4], float u0, float v0, float u1, float v1, std::uint32_t fill,
                      std::uint32_t halo) {
        const LabelVertex a{nx(px[0]), ny(py[0]), u0, v0, fill, halo, 0.0f, 0.0f};
        const LabelVertex b{nx(px[1]), ny(py[1]), u1, v0, fill, halo, 0.0f, 0.0f};
        const LabelVertex c{nx(px[2]), ny(py[2]), u1, v1, fill, halo, 0.0f, 0.0f};
        const LabelVertex d{nx(px[3]), ny(py[3]), u0, v1, fill, halo, 0.0f, 0.0f};
        out.insert(out.end(), {a, b, c, a, c, d});
    };
    auto run = [&](std::string_view text, float pen, float baseline, const TextStyle& s, float alpha, bool snap, float cx, float cy, float ca, float sa) {
        const std::uint32_t fill = faded(s.fill, alpha), halo = faded(s.halo, alpha);
        std::uint32_t prev = 0;
        if (snap) baseline = std::round(baseline);
        for (std::uint32_t cp : codepoints(text)) {
            const Glyph& g = glyphs.glyph(cp, s.size, s.haloPx);
            if (prev) pen += glyphs.kerning(prev, cp, s.size);
            prev = cp;
            if (g.w > 0 && g.u1 > g.u0) {
                const float gx = (snap ? std::round(pen) : pen) + static_cast<float>(g.x0);
                const float gy = baseline + static_cast<float>(g.y0);
                const float lx[4] = {gx, gx + g.w, gx + g.w, gx};
                const float ly[4] = {gy, gy, gy + g.h, gy + g.h};
                float px[4], py[4];
                for (int k = 0; k < 4; ++k) {
                    px[k] = cx + lx[k] * ca - ly[k] * sa;
                    py[k] = cy + lx[k] * sa + ly[k] * ca;
                }
                quadTo(verts, px, py, g.u0, g.v0, g.u1, g.v1, fill, halo);
            }
            pen += g.advance + s.tracking;
        }
    };

    for (const auto& [item, alpha] : draw) {
        if (alpha <= 0.01f || (verts.size() + 600) * sizeof(LabelVertex) > kLabelBytes) continue;
        double sx, sy;
        if (dot(normalize(item->anchor), eye - item->anchor) <= 0.0 || !project(item->anchor, sx, sy)) continue;
        const TextStyle& s = item->style;
        const float em = s.size;
        const float ax = static_cast<float>(sx), ay = static_cast<float>(sy);
        if (item->layout == Layout::Along) {
            const float w = glyphs.width(item->text, s);
            run(item->text, -w * 0.5f, em * 0.36f, s, alpha, false, ax + item->ox, ay + item->oy, std::cos(item->angle), std::sin(item->angle));
            continue;
        }
        if (item->layout == Layout::Shield) {
            const float w = item->shieldW, h = item->shieldH;
            const float x0 = std::round(ax - w * 0.5f), y0 = std::round(ay - h * 0.5f);
            const float u0 = (item->shield * kIconSize + 0.5f) / (kIconSize * kIcons), u1 = ((item->shield + 1) * kIconSize - 0.5f) / (kIconSize * kIcons);
            const float px[4] = {x0, x0 + w, x0 + w, x0};
            const float py[4] = {y0, y0, y0 + h, y0 + h};
            quadTo(icons, px, py, u0, 0.5f / kIconSize, u1, 1.0f - 0.5f / kIconSize, faded(rgba(255, 255, 255, 1.0f), alpha), 0);
            const float tw = glyphs.width(item->text, s);
            const float shift = item->shield <= 1 ? h * 0.1f : 0.0f;
            run(item->text, std::round(x0 + (w - tw) * 0.5f), y0 + h * 0.5f + em * 0.36f + shift, s, alpha, true, 0.0f, 0.0f, 1.0f, 0.0f);
            continue;
        }
        std::vector<std::string_view> rows;
        splitLines(item->text, rows);
        const float pitch = em * 1.2f;
        const float n = static_cast<float>(rows.size());
        if (item->layout == Layout::Side && s.dot > 0.0f) {
            const Glyph& g = glyphs.glyph(kDot, s.dot, s.haloPx);
            if (g.u1 > g.u0) {
                const float gx = std::round(ax) + g.x0, gy = std::round(ay) + g.y0;
                const float px[4] = {gx, gx + g.w, gx + g.w, gx};
                const float py[4] = {gy, gy, gy + g.h, gy + g.h};
                quadTo(verts, px, py, g.u0, g.v0, g.u1, g.v1, faded(rgba(242, 242, 242, 1.0f), alpha), faded(s.halo, alpha));
            }
        }
        for (size_t li = 0; li < rows.size(); ++li) {
            const float lw = glyphs.width(rows[li], s);
            const float pen = item->layout == Layout::Side ? ax + item->ox : ax + item->ox - lw * 0.5f;
            const float baseline = ay + item->oy - (n - 1.0f) * pitch * 0.5f + static_cast<float>(li) * pitch + em * 0.36f;
            run(rows[li], pen, baseline, s, alpha, true, 0.0f, 0.0f, 1.0f, 0.0f);
        }
    }
    uploadAtlas();
    const size_t slot = static_cast<size_t>(std::clamp(view, 0, 3));
    if (!icons.empty()) {
        if (icons.size() * sizeof(LabelVertex) > kIconBytes) icons.resize(kIconBytes / sizeof(LabelVertex) / 6 * 6);
        sg_write_buffer_desc wd{};
        wd.src.data = {icons.data(), icons.size() * sizeof(LabelVertex)};
        wd.dst.buffer = iconBuffers_[slot];
        wd.size = icons.size() * sizeof(LabelVertex);
        sg_write_buffer_transient(&wd);
        sg_apply_pipeline(iconPipeline_);
        sg_bindings b{};
        b.vertex_buffers[0] = iconBuffers_[slot];
        b.views[VIEW_tex] = iconView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_draw(0, static_cast<int>(icons.size()), 1);
    }
    if (verts.empty()) return;
    sg_write_buffer_desc wd{};
    wd.src.data = {verts.data(), verts.size() * sizeof(LabelVertex)};
    wd.dst.buffer = labelBuffers_[slot];
    wd.size = verts.size() * sizeof(LabelVertex);
    sg_write_buffer_transient(&wd);
    sg_apply_pipeline(labelPipeline_);
    sg_bindings b{};
    b.vertex_buffers[0] = labelBuffers_[slot];
    b.views[VIEW_tex] = atlasView_;
    b.samplers[SMP_smp] = sampler_;
    sg_apply_bindings(&b);
    sg_draw(0, static_cast<int>(verts.size()), 1);
}

}
