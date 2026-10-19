#pragma once

#include <memory>
#include <string>
#include <vector>

struct sg_swapchain;
struct sg_attachments;

namespace ote {

struct ImageryLayer {
    std::string name;
    std::string url;
    std::vector<std::string> servers;
    int minZoom = 0;
    int maxZoom = 19;
    bool tms = false;
    float opacity = 1.0f;
};

struct View {
    double lat = 20.0;
    double lon = -40.0;
    double alt = 0.0;
    double gsd = 20000.0;
    double azimuth = 0.0;
    double tilt = 0.0;
};

struct Stats {
    int terrainTiles = 0;
    int surfaceRenders = 0;
    int imageryTextures = 0;
    int pendingRequests = 0;
    int maxLevel = 0;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init();
    void shutdown();

    void setViewport(int width, int height, float dpiScale);
    void setLayers(const std::vector<ImageryLayer>& layers);
    void setImageryScale(double scale);

    View view() const;
    void setView(const View& v);

    bool pick(double sx, double sy, double& lat, double& lon) const;
    void pan(double x0, double y0, double x1, double y1);
    void zoomAt(double sx, double sy, double factor);
    void rotate(double degrees);
    void tiltBy(double degrees);

    void frame(const sg_swapchain& swapchain);
    void frameOffscreen(const sg_attachments& attachments, int width, int height);
    bool needsRedraw() const;
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<ImageryLayer> defaultLayers();

}
