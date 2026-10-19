#include "ote/engine.hpp"

#include "core/camera.hpp"
#include "render/globe_renderer.hpp"

namespace ote {

struct Engine::Impl {
    Camera camera;
    GlobeRenderer globe;
    bool dirty = true;
    float dpiScale = 1.0f;

    void moveCamera(Vec3 from, Vec3 to) {
        Vec3 axis = cross(from, to);
        double len = length(axis);
        if (len < 1e-12) return;
        double angle = std::atan2(len, dot(from, to));
        Vec3 f = rotateAxis(camera.focusEcef(), axis, angle);
        GeoPoint g = ecefToGeodetic(f);
        camera.focus.lat = g.lat;
        camera.focus.lon = g.lon;
        camera.update();
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() { shutdown(); }

bool Engine::init() {
    impl_->camera.update();
    return impl_->globe.init();
}

void Engine::shutdown() {
    if (impl_) impl_->globe.shutdown();
}

void Engine::setViewport(int width, int height, float dpiScale) {
    impl_->camera.width = std::max(width, 1);
    impl_->camera.height = std::max(height, 1);
    impl_->dpiScale = dpiScale;
    impl_->globe.setDpiScale(dpiScale);
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::setLayers(const std::vector<ImageryLayer>& layers) {
    impl_->globe.setLayers(layers);
    impl_->dirty = true;
}

void Engine::setImageryScale(double scale) {
    impl_->globe.setImageryScale(scale);
    impl_->dirty = true;
}

View Engine::view() const {
    const Camera& c = impl_->camera;
    return {c.focus.lat, c.focus.lon, c.focus.alt, c.gsd, c.azimuth, c.tilt};
}

void Engine::setView(const View& v) {
    Camera& c = impl_->camera;
    c.focus = {v.lat, v.lon, v.alt};
    c.gsd = v.gsd;
    c.azimuth = v.azimuth;
    c.tilt = v.tilt;
    c.update();
    impl_->dirty = true;
}

bool Engine::pick(double sx, double sy, double& lat, double& lon) const {
    Vec3 p;
    if (!impl_->camera.screenToEcef(sx, sy, p)) return false;
    GeoPoint g = ecefToGeodetic(p);
    lat = g.lat;
    lon = g.lon;
    return true;
}

void Engine::pan(double x0, double y0, double x1, double y1) {
    Camera& c = impl_->camera;
    Vec3 p0, p1;
    if (c.screenToEcef(x0, y0, p0) && c.screenToEcef(x1, y1, p1)) {
        impl_->moveCamera(p1, p0);
    } else {
        Vec3 e, n, u;
        enuFrame(c.focus.lat, c.focus.lon, e, n, u);
        double az = c.azimuth * kDegToRad;
        double dx = (x0 - x1) * c.gsd, dy = (y1 - y0) * c.gsd;
        double east = dx * std::cos(az) - dy * std::sin(az);
        double north = dx * std::sin(az) + dy * std::cos(az);
        c.focus.lat += north / 111320.0;
        c.focus.lon += east / (111320.0 * std::max(std::cos(c.focus.lat * kDegToRad), 0.01));
        c.update();
    }
    impl_->dirty = true;
}

void Engine::zoomAt(double sx, double sy, double factor) {
    Camera& c = impl_->camera;
    Vec3 before;
    bool hit = c.screenToEcef(sx, sy, before);
    c.gsd *= factor;
    c.update();
    Vec3 after;
    if (hit && c.screenToEcef(sx, sy, after)) impl_->moveCamera(after, before);
    impl_->dirty = true;
}

void Engine::rotate(double degrees) {
    impl_->camera.azimuth += degrees;
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::tiltBy(double degrees) {
    impl_->camera.tilt += degrees;
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::frame(const sg_swapchain& swapchain) {
    impl_->globe.render(impl_->camera, swapchain);
    impl_->dirty = false;
}

bool Engine::needsRedraw() const { return impl_->dirty || impl_->globe.busy(); }

Stats Engine::stats() const { return impl_->globe.stats(); }

std::vector<ImageryLayer> defaultLayers() {
    ImageryLayer imagery;
    imagery.name = "World Imagery";
    imagery.url = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}";
    imagery.maxZoom = 19;
    return {imagery};
}

}
