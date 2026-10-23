#pragma once

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace otk {

class Engine;

class Controls {
public:
    enum Phase { Down, Move, Up, Cancel, Hover, Wheel };

    explicit Controls(Engine& engine);
    ~Controls();
    Controls(const Controls&) = delete;
    Controls& operator=(const Controls&) = delete;

    void setFlags(std::uint32_t flags);
    void setPadding(float left, float top, float right, float bottom);
    std::uint32_t flags() const { return flags_; }
    bool pointer(Phase phase, int id, double x, double y);
    void draw(int width, int height, double dpi);
    bool dirty() const { return dirty_; }
    void shutdown();

private:
    enum class Item { None, ZoomIn, ZoomOut, Compass, Tilt, Layers, Panel, Basemap, Labels, Buildings, Contours, Terrain, Download, Scale, Credits };
    struct Target {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        Item item = Item::None;
        int index = -1;
    };
    struct Vertex {
        float x, y, u, v;
        float a[4];
        float b[4];
        std::uint32_t fill, edge;
        float kind, border;
    };
    struct Font;

    bool ready();
    Target hit(double x, double y) const;
    void activate(const Target& t);
    void quad(float x0, float y0, float x1, float y1, float ox, float oy, const std::array<float, 4>& a, const std::array<float, 4>& b,
              std::uint32_t fill, std::uint32_t edge, float kind, float border);
    void box(float x0, float y0, float x1, float y1, float radius, std::uint32_t fill, std::uint32_t edge = 0, float border = 0);
    void shadow(float x0, float y0, float x1, float y1, float radius, float blur, std::uint32_t color);
    void line(float ax, float ay, float bx, float by, float width, std::uint32_t color);
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t color);
    void circle(float cx, float cy, float r, std::uint32_t fill, std::uint32_t edge = 0, float border = 0);
    float text(std::string_view s, float x, float baseline, int size, std::uint32_t color);
    float measure(std::string_view s, int size) const;
    std::string fit(const std::string& s, float width, int size) const;
    void button(const Target& t, bool active = false);
    void drawZoom();
    void drawCompass(double azimuth);
    void drawTilt(bool flat);
    void drawLayersButton();
    void drawPanel();
    void drawStrip(double metersPerPixel);
    void drawCredits();

    Engine& engine_;
    std::uint32_t flags_ = 0x3Fu;
    std::unique_ptr<Font> font_;
    bool failed_ = false;
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    sg_buffer buffer_{};
    sg_sampler sampler_{};
    std::vector<Vertex> verts_;
    std::vector<Target> targets_;
    float dpi_ = 1.0f;
    std::array<float, 4> padding_{};
    float top_ = 0, right_ = 0, bottom_ = 0;
    int width_ = 0, height_ = 0;
    bool panel_ = false;
    bool creditsOpen_ = false;
    bool imperial_ = false;
    int downloadId_ = 0;
    int pressId_ = -1;
    Target pressed_;
    Target hover_;
    bool dirty_ = true;
    mutable std::unordered_map<std::string, std::string> fits_;
};

}
