#include "ote/milsym.hpp"

#include <stb_truetype.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace ote {

namespace embedded {
extern const unsigned char font_bold[];
}

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Pt {
    double x, y;
};

struct Rgba {
    float r, g, b, a;
};

constexpr Rgba kBlack{0, 0, 0, 1};
constexpr Rgba kWhite{1, 1, 1, 1};

class Canvas {
public:
    Canvas(int size, int ss) : size_(size), ss_(ss), w_(size * ss), px_(static_cast<size_t>(w_) * w_ * 4, 0.0f) {}

    void fill(const std::vector<std::vector<Pt>>& rings, Rgba c) {
        double minY = 1e9, maxY = -1e9;
        for (const auto& r : rings)
            for (const Pt& p : r) {
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        int y0 = std::max(0, static_cast<int>(std::floor(minY * ss_))), y1 = std::min(w_ - 1, static_cast<int>(std::ceil(maxY * ss_)));
        std::vector<std::pair<double, int>> xs;
        for (int sy = y0; sy <= y1; ++sy) {
            double y = (sy + 0.5) / ss_;
            xs.clear();
            for (const auto& r : rings)
                for (size_t i = 0; i < r.size(); ++i) {
                    Pt a = r[i], b = r[(i + 1) % r.size()];
                    if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                        double t = (y - a.y) / (b.y - a.y);
                        xs.push_back({(a.x + t * (b.x - a.x)) * ss_, a.y < b.y ? 1 : -1});
                    }
                }
            std::sort(xs.begin(), xs.end());
            int winding = 0;
            for (size_t i = 0; i + 1 < xs.size(); ++i) {
                winding += xs[i].second;
                if (winding == 0) continue;
                int x0 = std::max(0, static_cast<int>(std::ceil(xs[i].first - 0.5)));
                int x1 = std::min(w_ - 1, static_cast<int>(std::floor(xs[i + 1].first - 0.5)));
                for (int sx = x0; sx <= x1; ++sx) blend(sx, sy, c, 1.0f);
            }
        }
    }

    void fill(const std::vector<Pt>& ring, Rgba c) { fill(std::vector<std::vector<Pt>>{ring}, c); }

    void disc(Pt c, double r, Rgba col) {
        std::vector<Pt> ring;
        int n = std::max(12, static_cast<int>(r * ss_ * 2));
        for (int i = 0; i < n; ++i) ring.push_back({c.x + r * std::cos(2 * kPi * i / n), c.y + r * std::sin(2 * kPi * i / n)});
        fill(ring, col);
    }

    void segment(Pt a, Pt b, double width, Rgba c) {
        double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
        if (len < 1e-9) return;
        double nx = -dy / len * width / 2, ny = dx / len * width / 2;
        fill({{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}}, c);
    }

    void stroke(const std::vector<Pt>& pts, bool closed, double width, Rgba c, double dash = 0) {
        if (pts.size() < 2) return;
        std::vector<Pt> p = pts;
        if (closed) p.push_back(pts.front());
        if (dash <= 0) {
            for (size_t i = 0; i + 1 < p.size(); ++i) segment(p[i], p[i + 1], width, c);
            for (size_t i = 0; i < p.size(); ++i) disc(p[i], width / 2, c);
            return;
        }
        double phase = 0;
        bool on = true;
        for (size_t i = 0; i + 1 < p.size(); ++i) {
            Pt a = p[i], b = p[i + 1];
            double len = std::hypot(b.x - a.x, b.y - a.y), t = 0;
            while (t < len) {
                double step = std::min(dash - phase, len - t);
                if (on) {
                    Pt s{a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len};
                    Pt e{a.x + (b.x - a.x) * (t + step) / len, a.y + (b.y - a.y) * (t + step) / len};
                    segment(s, e, width, c);
                }
                t += step;
                phase += step;
                if (phase >= dash - 1e-9) {
                    phase = 0;
                    on = !on;
                }
            }
        }
    }

    void text(const std::string& s, Pt center, double height, Rgba c) {
        static stbtt_fontinfo info;
        static bool ok = stbtt_InitFont(&info, embedded::font_bold, stbtt_GetFontOffsetForIndex(embedded::font_bold, 0)) != 0;
        if (!ok || s.empty()) return;
        float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(height * ss_));
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        double width = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&info, static_cast<unsigned char>(s[i]), &adv, &lsb);
            width += adv * scale;
            if (i + 1 < s.size()) width += stbtt_GetCodepointKernAdvance(&info, s[i], s[i + 1]) * scale;
        }
        double x = center.x * ss_ - width / 2;
        double baseline = center.y * ss_ + (asc + desc) * scale / 2;
        for (size_t i = 0; i < s.size(); ++i) {
            int cp = static_cast<unsigned char>(s[i]);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&info, cp, scale, scale, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> bmp(static_cast<size_t>(gw) * gh);
                stbtt_MakeCodepointBitmap(&info, bmp.data(), gw, gh, gw, scale, scale, cp);
                int ox = static_cast<int>(std::lround(x)) + x0, oy = static_cast<int>(std::lround(baseline)) + y0;
                for (int yy = 0; yy < gh; ++yy)
                    for (int xx = 0; xx < gw; ++xx)
                        if (bmp[static_cast<size_t>(yy) * gw + xx]) blend(ox + xx, oy + yy, c, bmp[static_cast<size_t>(yy) * gw + xx] / 255.0f);
            }
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&info, cp, &adv, &lsb);
            x += adv * scale;
            if (i + 1 < s.size()) x += stbtt_GetCodepointKernAdvance(&info, s[i], s[i + 1]) * scale;
        }
    }

    Image resolve() const {
        Image img;
        img.width = img.height = size_;
        img.rgba.resize(static_cast<size_t>(size_) * size_ * 4);
        float inv = 1.0f / (ss_ * ss_);
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x) {
                float acc[4] = {0, 0, 0, 0};
                for (int j = 0; j < ss_; ++j)
                    for (int i = 0; i < ss_; ++i) {
                        const float* p = &px_[(static_cast<size_t>(y * ss_ + j) * w_ + x * ss_ + i) * 4];
                        for (int k = 0; k < 4; ++k) acc[k] += p[k];
                    }
                float a = acc[3] * inv;
                std::uint8_t* d = &img.rgba[(static_cast<size_t>(y) * size_ + x) * 4];
                for (int k = 0; k < 3; ++k) d[k] = static_cast<std::uint8_t>(std::clamp(a > 0 ? acc[k] * inv / a : 0.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                d[3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        return img;
    }

private:
    void blend(int x, int y, Rgba c, float cov) {
        if (x < 0 || y < 0 || x >= w_ || y >= w_) return;
        float a = c.a * cov;
        float* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
        p[0] = c.r * a + p[0] * (1 - a);
        p[1] = c.g * a + p[1] * (1 - a);
        p[2] = c.b * a + p[2] * (1 - a);
        p[3] = a + p[3] * (1 - a);
    }

    int size_, ss_, w_;
    std::vector<float> px_;
};

enum class FrameShape { Rectangle, Circle, Diamond, Square, Quatrefoil };
enum class FrameCut { Full, OpenBottom, OpenTop };

struct Frame {
    std::vector<Pt> outline;
    bool closedOutline = true;
    std::vector<Pt> fillRing;
    double left = 0, right = 0, top = 0, bottom = 0;
    double iconLeft = 0, iconRight = 0, iconTop = 0, iconBottom = 0;
};

std::vector<Pt> ellipsePts(double cx, double cy, double rx, double ry, double a0, double a1, int n) {
    std::vector<Pt> out;
    for (int i = 0; i <= n; ++i) {
        double t = a0 + (a1 - a0) * i / n;
        out.push_back({cx + rx * std::cos(t), cy + ry * std::sin(t)});
    }
    return out;
}

Frame makeFrame(FrameShape shape, FrameCut cut) {
    Frame f;
    switch (shape) {
    case FrameShape::Rectangle:
        f.left = -0.75, f.right = 0.75, f.top = -0.5, f.bottom = 0.5;
        f.outline = {{-0.75, -0.5}, {0.75, -0.5}, {0.75, 0.5}, {-0.75, 0.5}};
        f.iconLeft = -0.75, f.iconRight = 0.75, f.iconTop = -0.5, f.iconBottom = 0.5;
        break;
    case FrameShape::Circle:
        if (cut == FrameCut::OpenBottom) {
            f.outline = ellipsePts(0, 0.25, 0.6, 0.75, kPi, 2 * kPi, 48);
            f.left = -0.6, f.right = 0.6, f.top = -0.5, f.bottom = 0.25;
            f.iconLeft = -0.4, f.iconRight = 0.4, f.iconTop = -0.3, f.iconBottom = 0.25;
        } else if (cut == FrameCut::OpenTop) {
            f.outline = ellipsePts(0, -0.25, 0.6, 0.75, 0, kPi, 48);
            f.left = -0.6, f.right = 0.6, f.top = -0.25, f.bottom = 0.5;
            f.iconLeft = -0.4, f.iconRight = 0.4, f.iconTop = -0.25, f.iconBottom = 0.3;
        } else {
            f.outline = ellipsePts(0, 0, 0.6, 0.6, 0, 2 * kPi, 64);
            f.outline.pop_back();
            f.left = -0.6, f.right = 0.6, f.top = -0.6, f.bottom = 0.6;
            f.iconLeft = -0.42, f.iconRight = 0.42, f.iconTop = -0.42, f.iconBottom = 0.42;
        }
        break;
    case FrameShape::Diamond:
        if (cut == FrameCut::OpenBottom) {
            f.outline = {{-0.72, 0.3}, {-0.72, 0.0}, {0, -0.72}, {0.72, 0.0}, {0.72, 0.3}};
            f.left = -0.72, f.right = 0.72, f.top = -0.72, f.bottom = 0.3;
            f.iconLeft = -0.4, f.iconRight = 0.4, f.iconTop = -0.3, f.iconBottom = 0.3;
        } else if (cut == FrameCut::OpenTop) {
            f.outline = {{-0.72, -0.3}, {-0.72, 0.0}, {0, 0.72}, {0.72, 0.0}, {0.72, -0.3}};
            f.left = -0.72, f.right = 0.72, f.top = -0.3, f.bottom = 0.72;
            f.iconLeft = -0.4, f.iconRight = 0.4, f.iconTop = -0.3, f.iconBottom = 0.3;
        } else {
            f.outline = {{0, -0.72}, {0.72, 0}, {0, 0.72}, {-0.72, 0}};
            f.left = -0.72, f.right = 0.72, f.top = -0.72, f.bottom = 0.72;
            f.iconLeft = -0.42, f.iconRight = 0.42, f.iconTop = -0.42, f.iconBottom = 0.42;
        }
        break;
    case FrameShape::Square:
        if (cut == FrameCut::OpenBottom) {
            f.outline = {{-0.55, 0.35}, {-0.55, -0.55}, {0.55, -0.55}, {0.55, 0.35}};
            f.left = -0.55, f.right = 0.55, f.top = -0.55, f.bottom = 0.35;
        } else if (cut == FrameCut::OpenTop) {
            f.outline = {{-0.55, -0.35}, {-0.55, 0.55}, {0.55, 0.55}, {0.55, -0.35}};
            f.left = -0.55, f.right = 0.55, f.top = -0.35, f.bottom = 0.55;
        } else {
            f.outline = {{-0.55, -0.55}, {0.55, -0.55}, {0.55, 0.55}, {-0.55, 0.55}};
            f.left = -0.55, f.right = 0.55, f.top = -0.55, f.bottom = 0.55;
        }
        f.iconLeft = f.left, f.iconRight = f.right, f.iconTop = f.top, f.iconBottom = f.bottom;
        break;
    case FrameShape::Quatrefoil: {
        const double d = 0.38, r = 0.38;
        auto radius = [&](double th) {
            double best = 0;
            for (int k = 0; k < 4; ++k) {
                double phi = k * kPi / 2, s = std::sin(th - phi), c = std::cos(th - phi);
                double disc = r * r - d * d * s * s;
                if (disc >= 0) best = std::max(best, d * c + std::sqrt(disc));
            }
            return best;
        };
        double a0 = 0, a1 = 2 * kPi;
        if (cut == FrameCut::OpenBottom) a0 = kPi, a1 = 2 * kPi;
        if (cut == FrameCut::OpenTop) a0 = 0, a1 = kPi;
        int n = cut == FrameCut::Full ? 128 : 64;
        for (int i = 0; i <= n; ++i) {
            double th = a0 + (a1 - a0) * i / n;
            double rr = radius(th);
            f.outline.push_back({rr * std::cos(th), rr * std::sin(th)});
        }
        if (cut == FrameCut::Full) f.outline.pop_back();
        f.left = -0.76, f.right = 0.76;
        f.top = cut == FrameCut::OpenTop ? 0 : -0.76;
        f.bottom = cut == FrameCut::OpenBottom ? 0 : 0.76;
        f.iconLeft = -0.45, f.iconRight = 0.45, f.iconTop = cut == FrameCut::OpenTop ? 0 : -0.45, f.iconBottom = cut == FrameCut::OpenBottom ? 0 : 0.45;
        if (cut != FrameCut::Full) {
            f.iconTop = cut == FrameCut::OpenTop ? 0.0 : -0.45;
            f.iconBottom = cut == FrameCut::OpenBottom ? 0.0 : 0.45;
        }
        break;
    }
    }
    f.closedOutline = cut == FrameCut::Full;
    f.fillRing = f.outline;
    return f;
}

Affiliation affiliationFrom(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'P':
    case 'G':
        return Affiliation::Pending;
    case 'U':
    case 'W':
        return Affiliation::Unknown;
    case 'A':
    case 'M':
        return Affiliation::AssumedFriend;
    case 'F':
    case 'D':
        return Affiliation::Friend;
    case 'N':
    case 'L':
        return Affiliation::Neutral;
    case 'S':
        return Affiliation::Suspect;
    case 'H':
        return Affiliation::Hostile;
    case 'J':
        return Affiliation::Joker;
    case 'K':
        return Affiliation::Faker;
    case 'O':
        return Affiliation::NoAffiliation;
    default:
        return Affiliation::Unknown;
    }
}

Dimension dimensionFrom(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'P':
        return Dimension::Space;
    case 'A':
        return Dimension::Air;
    case 'G':
        return Dimension::Ground;
    case 'S':
        return Dimension::SeaSurface;
    case 'U':
        return Dimension::Subsurface;
    case 'F':
        return Dimension::Sof;
    default:
        return Dimension::Other;
    }
}

struct DFunction {
    const char* entity;
    const char* function;
};

constexpr DFunction kLandUnits[] = {{"121100", "UCI---"}, {"121102", "UCIZ--"}, {"120500", "UCA---"}, {"120400", "UCAA--"},
                                    {"121300", "UCR---"}, {"130300", "UCF---"}, {"130800", "UCFM--"}, {"130100", "UCD---"},
                                    {"140700", "UCE---"}, {"120600", "UCV---"}, {"140100", "UUA---"}, {"141200", "UULM--"},
                                    {"111000", "UUS---"}, {"110000", "UH----"}};

char echelonFromD(const std::string& two) {
    static const std::map<std::string, char> m = {{"11", 'A'}, {"12", 'B'}, {"13", 'C'}, {"14", 'D'}, {"15", 'E'}, {"16", 'F'}, {"17", 'G'},
                                                  {"18", 'H'}, {"21", 'I'}, {"22", 'J'}, {"23", 'K'}, {"24", 'L'}, {"25", 'M'}};
    auto it = m.find(two);
    return it == m.end() ? '-' : it->second;
}

bool starts(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

}

SymbolCode parseSidc(const std::string& raw) {
    SymbolCode code;
    std::string s;
    for (char c : raw)
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (s.size() >= 20 && std::all_of(s.begin(), s.begin() + 20, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        const char* ids = "PUAFNSH";
        int id = s[3] - '0';
        code.affiliation = id >= 0 && id <= 6 ? affiliationFrom(ids[id]) : Affiliation::Unknown;
        code.exercise = s[2] == '1';
        std::string set = s.substr(4, 2);
        if (set == "01" || set == "02") code.dimension = Dimension::Air;
        else if (set == "05" || set == "06") code.dimension = Dimension::Space;
        else if (set == "30") code.dimension = Dimension::SeaSurface;
        else if (set == "35" || set == "36") code.dimension = Dimension::Subsurface;
        else code.dimension = Dimension::Ground;
        code.planned = s[6] == '1';
        const char mods[] = {'-', 'F', 'A', 'C', 'E', 'G', 'B', 'D'};
        int hq = s[7] - '0';
        code.modifier = hq >= 0 && hq <= 7 ? mods[hq] : '-';
        code.echelon = echelonFromD(s.substr(8, 2));
        std::string entity = s.substr(10, 6);
        if (set == "10") {
            code.function = "U-----";
            for (const DFunction& d : kLandUnits)
                if (entity == d.entity) code.function = d.function;
        } else if (set == "15") {
            code.function = "E-----";
        } else if (set == "20") {
            code.function = "I-----";
        } else if (set == "01") {
            code.function = entity.compare(0, 4, "1101") == 0 ? "MF----" : entity.compare(0, 4, "1102") == 0 ? "MH----" : "M-----";
        } else {
            code.function = "------";
        }
        code.valid = true;
        return code;
    }
    if (s.size() < 4) return code;
    code.affiliation = affiliationFrom(s[1]);
    code.exercise = s[1] == 'G' || s[1] == 'W' || s[1] == 'D' || s[1] == 'L' || s[1] == 'M';
    code.dimension = dimensionFrom(s[2]);
    code.planned = s[3] == 'A';
    code.function = s.size() > 4 ? s.substr(4, std::min<size_t>(6, s.size() - 4)) : std::string();
    while (code.function.size() < 6) code.function += '-';
    for (char& c : code.function)
        if (c == '*' || c == '_') c = '-';
    code.modifier = s.size() > 10 && s[10] != '*' ? s[10] : '-';
    code.echelon = s.size() > 11 && s[11] != '*' ? s[11] : '-';
    code.valid = s[0] == 'S' || s[0] == 'O' || s[0] == 'E';
    return code;
}

std::string sidcFromCotType(const std::string& type) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : type) {
        if (c == '-') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur);
    if (parts.size() < 3 || parts[0] != "a" || parts[1].size() != 1 || parts[2].size() != 1) return {};
    std::string sidc = "S";
    sidc += static_cast<char>(std::toupper(static_cast<unsigned char>(parts[1][0])));
    sidc += static_cast<char>(std::toupper(static_cast<unsigned char>(parts[2][0])));
    sidc += 'P';
    std::string fn;
    for (size_t i = 3; i < parts.size() && fn.size() < 6; ++i)
        if (parts[i].size() == 1) fn += static_cast<char>(std::toupper(static_cast<unsigned char>(parts[i][0])));
    while (fn.size() < 6) fn += '-';
    return sidc + fn + "-----";
}

std::string cotTypeFromSidc(const std::string& sidc) {
    SymbolCode c = parseSidc(sidc);
    if (!c.valid || sidc.size() < 4) return {};
    std::string s;
    for (char ch : sidc) s += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    bool numeric = s.size() >= 20 && std::isdigit(static_cast<unsigned char>(s[0]));
    const char* aff = "puafnshjko";
    std::string t = "a-";
    t += aff[static_cast<int>(c.affiliation)];
    t += '-';
    const char* dims = "PAGSUFX";
    t += dims[static_cast<int>(c.dimension)];
    for (char f : c.function) {
        if (f == '-') break;
        t += '-';
        t += f;
    }
    (void)numeric;
    return t;
}

Image renderSymbolImage(const std::string& sidc, int size) {
    SymbolCode code = parseSidc(sidc);
    size = std::clamp(size, 16, 512);
    Canvas cv(size, 4);
    const double u = size / 2.3;
    const double cx = size / 2.0, cy = size / 2.0;
    auto P = [&](double x, double y) { return Pt{cx + x * u, cy + y * u}; };
    auto map = [&](const std::vector<Pt>& in) {
        std::vector<Pt> out;
        out.reserve(in.size());
        for (const Pt& p : in) out.push_back(P(p.x, p.y));
        return out;
    };
    const double line = std::max(1.0, size / 40.0);

    Affiliation a = code.affiliation;
    bool friendly = a == Affiliation::Friend || a == Affiliation::AssumedFriend;
    bool hostile = a == Affiliation::Hostile || a == Affiliation::Suspect || a == Affiliation::Joker || a == Affiliation::Faker;
    bool neutral = a == Affiliation::Neutral;
    bool dashedAffiliation = a == Affiliation::AssumedFriend || a == Affiliation::Suspect || a == Affiliation::Pending;
    Rgba fillColor = friendly ? Rgba{128 / 255.f, 224 / 255.f, 1, 1}
                     : hostile ? Rgba{1, 128 / 255.f, 128 / 255.f, 1}
                     : neutral ? Rgba{170 / 255.f, 1, 170 / 255.f, 1}
                               : Rgba{1, 1, 128 / 255.f, 1};
    if (a == Affiliation::NoAffiliation) fillColor = Rgba{0.85f, 0.85f, 0.85f, 1};

    const std::string& fn = code.function;
    char kind = fn.empty() ? '-' : fn[0];
    FrameCut cut = FrameCut::Full;
    if (code.dimension == Dimension::Air || code.dimension == Dimension::Space) cut = FrameCut::OpenBottom;
    if (code.dimension == Dimension::Subsurface) cut = FrameCut::OpenTop;
    FrameShape shape;
    if (hostile) shape = FrameShape::Diamond;
    else if (neutral) shape = FrameShape::Square;
    else if (friendly) {
        bool ground = code.dimension == Dimension::Ground || code.dimension == Dimension::Sof || code.dimension == Dimension::Other;
        if (cut != FrameCut::Full || code.dimension == Dimension::SeaSurface || (ground && kind == 'E')) shape = FrameShape::Circle;
        else shape = FrameShape::Rectangle;
    } else shape = FrameShape::Quatrefoil;
    Frame frame = makeFrame(shape, cut);

    std::vector<Pt> fillRing = map(frame.fillRing);
    cv.fill(fillRing, fillColor);
    bool dashed = code.planned || dashedAffiliation;
    if (dashed) {
        cv.stroke(map(frame.outline), frame.closedOutline, line * 1.2, kWhite);
        cv.stroke(map(frame.outline), frame.closedOutline, line * 1.2, kBlack, line * 3.0);
    } else {
        cv.stroke(map(frame.outline), frame.closedOutline, line * 1.2, kBlack);
    }
    if (kind == 'I' && code.dimension == Dimension::Ground) {
        double h = (frame.bottom - frame.top) * 0.16;
        cv.fill(map({{frame.left * 0.7, frame.top - h}, {frame.right * 0.7, frame.top - h}, {frame.right * 0.7, frame.top}, {frame.left * 0.7, frame.top}}), kBlack);
    }

    const double L = frame.iconLeft, R = frame.iconRight, T = frame.iconTop, B = frame.iconBottom;
    const double W = R - L, H = B - T, mx = (L + R) / 2, my = (T + B) / 2;
    auto seg = [&](double x0, double y0, double x1, double y1) { cv.segment(P(x0, y0), P(x1, y1), line, kBlack); };
    auto label = [&](const std::string& t, double scale = 1.0) {
        double h = std::min(H * 0.55, W * 0.9 / std::max<size_t>(t.size(), 1) * 1.6) * scale;
        cv.text(t, P(mx, my), h * u, kBlack);
    };
    auto ellipse = [&](double rx, double ry, double ox = 0, double oy = 0) {
        auto pts = ellipsePts(mx + ox, my + oy, rx, ry, 0, 2 * kPi, 48);
        pts.pop_back();
        cv.stroke(map(pts), true, line, kBlack);
    };
    auto infantry = [&]() {
        if (shape == FrameShape::Rectangle) {
            seg(L, T, R, B);
            seg(L, B, R, T);
        } else {
            seg(L * 0.7, T * 0.7, R * 0.7, B * 0.7);
            seg(L * 0.7, B * 0.7, R * 0.7, T * 0.7);
        }
    };
    auto armor = [&]() { ellipse(W * 0.32, H * 0.22); };

    bool drewIcon = true;
    if (code.dimension == Dimension::Ground && kind == 'U') {
        if (starts(fn, "UCIZ")) {
            infantry();
            armor();
        } else if (starts(fn, "UCIM")) {
            infantry();
            seg(mx, T, mx, B);
        } else if (starts(fn, "UCI")) {
            infantry();
        } else if (starts(fn, "UCAA")) {
            seg(L + W * 0.15, B, mx, T + H * 0.12);
            seg(mx, T + H * 0.12, R - W * 0.15, B);
        } else if (starts(fn, "UCA")) {
            armor();
        } else if (starts(fn, "UCR")) {
            if (shape == FrameShape::Rectangle) seg(L, B, R, T);
            else seg(L * 0.7, B * 0.7, R * 0.7, T * 0.7);
        } else if (starts(fn, "UCFM")) {
            cv.stroke(map(ellipsePts(mx, my + H * 0.18, H * 0.1, H * 0.1, 0, 2 * kPi, 24)), true, line, kBlack);
            seg(mx, my + H * 0.08, mx, T + H * 0.12);
            seg(mx, T + H * 0.12, mx - H * 0.1, T + H * 0.26);
            seg(mx, T + H * 0.12, mx + H * 0.1, T + H * 0.26);
        } else if (starts(fn, "UCF")) {
            cv.disc(P(mx, my), H * 0.13 * u, kBlack);
        } else if (starts(fn, "UCD")) {
            auto arc = ellipsePts(mx, B, W * 0.32, H * 0.42, kPi, 2 * kPi, 32);
            cv.stroke(map(arc), false, line, kBlack);
        } else if (starts(fn, "UCE")) {
            double y0 = my - H * 0.12, y1 = my + H * 0.14, x0 = mx - W * 0.22, x1 = mx + W * 0.22;
            seg(x0, y0, x1, y0);
            seg(x0, y0, x0, y1);
            seg(mx, y0, mx, y1);
            seg(x1, y0, x1, y1);
        } else if (starts(fn, "UCV")) {
            cv.fill(map({{mx - W * 0.25, my - H * 0.2}, {mx, my}, {mx - W * 0.25, my + H * 0.2}}), kBlack);
            cv.fill(map({{mx + W * 0.25, my - H * 0.2}, {mx, my}, {mx + W * 0.25, my + H * 0.2}}), kBlack);
        } else if (starts(fn, "UUS")) {
            seg(L + W * 0.15, T + H * 0.2, mx - W * 0.05, my + H * 0.08);
            seg(mx - W * 0.05, my + H * 0.08, mx + W * 0.05, my - H * 0.08);
            seg(mx + W * 0.05, my - H * 0.08, R - W * 0.15, B - H * 0.2);
        } else if (starts(fn, "USM")) {
            seg(mx, T + H * 0.15, mx, B - H * 0.15);
            seg(mx - H * 0.35, my, mx + H * 0.35, my);
        } else if (starts(fn, "USS")) {
            seg(L, B - H * 0.3, R, B - H * 0.3);
        } else if (starts(fn, "USX")) {
            seg(mx - W * 0.2, my, mx + W * 0.2, my);
            cv.stroke(map(ellipsePts(mx - W * 0.26, my, H * 0.09, H * 0.12, kPi * 0.5, kPi * 1.5, 16)), false, line, kBlack);
            cv.stroke(map(ellipsePts(mx + W * 0.26, my, H * 0.09, H * 0.12, -kPi * 0.5, kPi * 0.5, 16)), false, line, kBlack);
        } else if (starts(fn, "UST")) {
            double r = H * 0.28;
            cv.stroke(map(ellipsePts(mx, my, r, r, 0, 2 * kPi, 32)), true, line, kBlack);
            seg(mx - r, my, mx + r, my);
            seg(mx, my - r, mx, my + r);
            seg(mx - r * 0.7, my - r * 0.7, mx + r * 0.7, my + r * 0.7);
            seg(mx - r * 0.7, my + r * 0.7, mx + r * 0.7, my - r * 0.7);
        } else if (starts(fn, "UUA")) {
            label("CBRN");
        } else if (starts(fn, "UUL")) {
            label("MP");
        } else if (starts(fn, "UUM")) {
            label("MI");
        } else if (starts(fn, "UUE")) {
            label("EOD");
        } else if (starts(fn, "UH")) {
            label("HQ");
        } else if (starts(fn, "USA")) {
            label("ADM");
        } else {
            drewIcon = false;
        }
    } else if (code.dimension == Dimension::Ground && kind == 'E') {
        if (starts(fn, "EVAT")) {
            ellipse(W * 0.34, H * 0.2);
            seg(mx - W * 0.25, my, mx + W * 0.25, my);
        } else if (starts(fn, "EVA")) {
            ellipse(W * 0.34, H * 0.2);
        } else if (starts(fn, "EWM")) {
            label("MSL");
        } else if (starts(fn, "EWA")) {
            label("AD");
        } else if (starts(fn, "EWH") || starts(fn, "EWG")) {
            cv.disc(P(mx, my), H * 0.12 * u, kBlack);
        } else if (starts(fn, "EV")) {
            label("VEH", 0.8);
        } else {
            drewIcon = false;
        }
    } else if (code.dimension == Dimension::Air || code.dimension == Dimension::Space) {
        if (starts(fn, "MFQ") || starts(fn, "MV")) label("UAV");
        else if (starts(fn, "MF")) {
            double s = H * 0.9;
            cv.fill(map({{mx, my - s * 0.45}, {mx + s * 0.06, my - s * 0.1}, {mx + s * 0.45, my + s * 0.05}, {mx + s * 0.45, my + s * 0.13},
                         {mx + s * 0.06, my + s * 0.05}, {mx + s * 0.05, my + s * 0.3}, {mx + s * 0.16, my + s * 0.4}, {mx - s * 0.16, my + s * 0.4},
                         {mx - s * 0.05, my + s * 0.3}, {mx - s * 0.06, my + s * 0.05}, {mx - s * 0.45, my + s * 0.13}, {mx - s * 0.45, my + s * 0.05},
                         {mx - s * 0.06, my - s * 0.1}}),
                    kBlack);
        } else if (starts(fn, "MH")) {
            cv.fill(map({{mx - W * 0.3, my - H * 0.25}, {mx, my}, {mx - W * 0.3, my + H * 0.25}}), kBlack);
            cv.fill(map({{mx + W * 0.3, my - H * 0.25}, {mx, my}, {mx + W * 0.3, my + H * 0.25}}), kBlack);
        } else if (starts(fn, "W")) label("MSL");
        else if (starts(fn, "C")) label("C");
        else drewIcon = false;
    } else if (code.dimension == Dimension::SeaSurface) {
        if (starts(fn, "CLCV")) label("CV");
        else if (starts(fn, "CLDD")) label("DD");
        else if (starts(fn, "CLFF")) label("FF");
        else if (starts(fn, "CLCC")) label("CG");
        else if (starts(fn, "CLBB")) label("BB");
        else if (starts(fn, "C")) label("CBT", 0.8);
        else if (starts(fn, "X")) label("MER", 0.8);
        else drewIcon = false;
    } else if (code.dimension == Dimension::Subsurface) {
        if (starts(fn, "S")) label("SUB", 0.8);
        else if (starts(fn, "W")) label("TOR", 0.8);
        else drewIcon = false;
    } else if (code.dimension == Dimension::Sof) {
        label("SOF", 0.8);
    } else {
        drewIcon = false;
    }
    (void)drewIcon;

    if (a == Affiliation::Joker || a == Affiliation::Faker) cv.text(a == Affiliation::Joker ? "J" : "K", P(frame.right + 0.12, frame.top + 0.05), 0.32 * u, kBlack);
    if (code.exercise) cv.text("X", P(frame.right + 0.12, frame.top + 0.05), 0.32 * u, kBlack);

    double above = frame.top;
    char mod = code.modifier;
    bool hq = mod == 'A' || mod == 'B' || mod == 'C' || mod == 'D';
    bool tf = mod == 'B' || mod == 'D' || mod == 'E' || mod == 'G';
    bool fd = mod == 'C' || mod == 'D' || mod == 'F' || mod == 'G';
    if (mod == 'H' && code.dimension == Dimension::Ground) {
        double h = (frame.bottom - frame.top) * 0.16;
        cv.fill(map({{frame.left * 0.7, frame.top - h}, {frame.right * 0.7, frame.top - h}, {frame.right * 0.7, frame.top}, {frame.left * 0.7, frame.top}}), kBlack);
        above -= h;
    }
    std::string ech;
    int dots = 0, bars = 0;
    switch (code.echelon) {
    case 'A':
        ech = "\xC3\x98";
        break;
    case 'B':
        dots = 1;
        break;
    case 'C':
        dots = 2;
        break;
    case 'D':
        dots = 3;
        break;
    case 'E':
        bars = 1;
        break;
    case 'F':
        bars = 2;
        break;
    case 'G':
        bars = 3;
        break;
    case 'H':
        ech = "X";
        break;
    case 'I':
        ech = "XX";
        break;
    case 'J':
        ech = "XXX";
        break;
    case 'K':
        ech = "XXXX";
        break;
    case 'L':
        ech = "XXXXX";
        break;
    case 'M':
        ech = "XXXXXX";
        break;
    default:
        break;
    }
    double echH = 0.24, echY = above - 0.06 - echH / 2;
    double echW = 0;
    if (dots) {
        double r = 0.055, gap = 0.17;
        echW = (dots - 1) * gap + 2 * r;
        for (int i = 0; i < dots; ++i) cv.disc(P(-(dots - 1) * gap / 2 + i * gap, echY), r * u, kBlack);
    } else if (bars) {
        double gap = 0.11;
        echW = (bars - 1) * gap + 0.05;
        for (int i = 0; i < bars; ++i) {
            double x = -(bars - 1) * gap / 2 + i * gap;
            seg(x, echY - echH / 2, x, echY + echH / 2);
        }
    } else if (ech == "\xC3\x98") {
        echW = 0.22;
        cv.stroke(map(ellipsePts(0, echY, 0.1, 0.1, 0, 2 * kPi, 24)), true, line, kBlack);
        seg(-0.14, echY + 0.14, 0.14, echY - 0.14);
    } else if (!ech.empty()) {
        echW = 0.17 * ech.size();
        cv.text(ech, P(0, echY), echH * 1.1 * u, kBlack);
    }
    double topOfEchelon = echW > 0 ? echY - echH / 2 : above;
    if (tf) {
        double half = std::max(echW / 2 + 0.1, 0.22);
        double y0 = topOfEchelon - 0.06;
        cv.stroke(map({{-half, above}, {-half, y0}, {half, y0}, {half, above}}), false, line, kBlack);
        topOfEchelon = y0;
    }
    if (fd) {
        double y0 = topOfEchelon - 0.22;
        cv.stroke(map({{frame.left, above}, {0, y0}, {frame.right, above}}), false, line, kBlack, line * 2.5);
    }
    if (hq) {
        double x = shape == FrameShape::Rectangle || shape == FrameShape::Square ? frame.left : frame.left * 0.98;
        double y = shape == FrameShape::Rectangle || shape == FrameShape::Square ? frame.bottom : 0.0;
        seg(x, y, x, frame.bottom + 0.42);
    }
    return cv.resolve();
}

std::shared_ptr<const Image> symbolIcon(const std::string& sidc, int sizePx) {
    static std::mutex mutex;
    static std::map<std::pair<std::string, int>, std::shared_ptr<const Image>> cache;
    std::lock_guard lock(mutex);
    auto key = std::make_pair(sidc, sizePx);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (cache.size() > 512) cache.clear();
    auto img = std::make_shared<const Image>(renderSymbolImage(sidc, sizePx));
    cache[key] = img;
    return img;
}

void applySymbol(Feature& f) {
    if (f.style.sidc.empty() && f.type.size() > 4 && f.type.compare(0, 2, "a-") == 0) f.style.sidc = sidcFromCotType(f.type);
    if (f.style.sidc.empty() || f.style.iconImage) return;
    if (!parseSidc(f.style.sidc).valid) return;
    f.style.iconImage = symbolIcon(f.style.sidc);
    f.style.iconColor = 0xFFFFFFFF;
    if (f.style.iconScale == 1.0f) f.style.iconScale = 1.8f;
}

}
