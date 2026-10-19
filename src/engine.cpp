#include "ote/engine.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>

#include "core/camera.hpp"
#include "render/globe_renderer.hpp"

#include "core/geoid.hpp"
#include "net/http.hpp"

#include <sokol_gfx.h>

namespace ote {

struct Engine::Impl {
    Camera camera;
    GlobeRenderer globe;
    bool dirty = true;
    std::unordered_map<std::uint64_t, Vec3> tilesetCenters;
    bool flying = false;
    View flyFrom, flyTo;
    double flyDuration = 0;
    double flyPeak = 0;
    std::chrono::steady_clock::time_point flyStart;

    void animate() {
        if (!flying) return;
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - flyStart).count() / flyDuration;
        if (t >= 1.0) {
            t = 1.0;
            flying = false;
        }
        double s = t * t * (3.0 - 2.0 * t);
        Vec3 a = normalize(geodeticToEcef(flyFrom.lat, flyFrom.lon, 0)), b = normalize(geodeticToEcef(flyTo.lat, flyTo.lon, 0));
        double omega = std::acos(std::clamp(dot(a, b), -1.0, 1.0));
        Vec3 p = omega < 1e-9 ? b : a * (std::sin((1 - s) * omega) / std::sin(omega)) + b * (std::sin(s * omega) / std::sin(omega));
        GeoPoint g = ecefToGeodetic(p * kWgs84A);
        double lg0 = std::log(flyFrom.gsd), lg1 = std::log(flyTo.gsd);
        double lg = lg0 + (lg1 - lg0) * s + (flyPeak - std::max(lg0, lg1)) * std::sin(kPi * s);
        double daz = std::fmod(flyTo.azimuth - flyFrom.azimuth + 540.0, 360.0) - 180.0;
        camera.focus.lat = g.lat;
        camera.focus.lon = g.lon;
        camera.gsd = std::exp(lg);
        camera.azimuth = flyFrom.azimuth + daz * s;
        camera.tilt = flyFrom.tilt + (flyTo.tilt - flyFrom.tilt) * s;
        camera.update();
        dirty = true;
    }
    float dpiScale = 1.0f;

    bool pickGround(double sx, double sy, Vec3& out) const {
        Vec3 d = camera.rayDirection(sx, sy);
        double h = camera.focus.alt;
        for (int i = 0; i < 6; ++i) {
            double t;
            if (!rayEllipsoid(camera.eye(), d, h, t)) {
                if (i == 0 && rayEllipsoid(camera.eye(), d, 0.0, t)) h = 0.0;
                else return false;
            }
            out = camera.eye() + d * t;
            GeoPoint g = ecefToGeodetic(out);
            double nh = globe.elevationAt(g.lat, g.lon);
            if (std::abs(nh - h) < 0.5) break;
            h = nh;
        }
        return true;
    }

    void followTerrain() {
        if (camera.firstPerson) {
            camera.update();
            return;
        }
        double h = globe.elevationAt(camera.focus.lat, camera.focus.lon);
        if (std::abs(h - camera.focus.alt) > 0.01) {
            camera.focus.alt = h;
            camera.update();
        }
    }

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
    impl_->camera.groundHeight = [this](double lat, double lon) { return impl_->globe.elevationAt(lat, lon); };
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

std::vector<ImageryLayer> Engine::layers() const { return impl_->globe.layers(); }

void Engine::setCacheDirectory(const std::string& dir) {
    impl_->globe.setCacheDirectory(dir);
    impl_->dirty = true;
}

void Engine::setMemoryBudget(int megabytes) { impl_->globe.setMemoryBudget(megabytes); }

void Engine::setOffline(bool offline) {
    impl_->globe.setOffline(offline);
    impl_->dirty = true;
}

std::uint64_t Engine::addFeature(const Feature& f) {
    impl_->dirty = true;
    return impl_->globe.features().add(f);
}

bool Engine::updateFeature(std::uint64_t id, const Feature& f) {
    impl_->dirty = true;
    return impl_->globe.features().update(id, f);
}

bool Engine::removeFeature(std::uint64_t id) {
    impl_->dirty = true;
    return impl_->globe.features().remove(id);
}

void Engine::clearFeatures() {
    impl_->dirty = true;
    impl_->globe.features().clear();
}

bool Engine::feature(std::uint64_t id, Feature& out) const { return impl_->globe.features().get(id, out); }

std::vector<std::uint64_t> Engine::featureIds() const { return impl_->globe.features().ids(); }

std::vector<std::uint64_t> Engine::loadFeatures(const std::string& path) {
    std::vector<std::uint64_t> ids;
    ParsedFile parsed = parseFile(path);
    for (const Feature& f : parsed.features) ids.push_back(addFeature(f));
    for (const GroundOverlay& g : parsed.overlays) {
        ImageOverlay o;
        o.north = g.north;
        o.south = g.south;
        o.east = g.east;
        o.west = g.west;
        o.width = g.width;
        o.height = g.height;
        o.rgba = g.rgba;
        o.opacity = g.opacity;
        addImageOverlay(o);
    }
    return ids;
}

std::uint64_t Engine::hitTest(double sx, double sy, double radiusPx) const {
    const Impl& m = *impl_;
    return m.globe.features().hitTest(
        m.camera, [&m](double lat, double lon) { return m.globe.elevationAt(lat, lon); },
        [&m](double x, double y, double& lat, double& lon) {
            Vec3 p;
            if (!m.pickGround(x, y, p)) return false;
            GeoPoint g = ecefToGeodetic(p);
            lat = g.lat;
            lon = g.lon;
            return true;
        },
        sx, sy, radiusPx);
}

std::uint64_t Engine::addModel(const std::string& path, const ModelPlacement& placement) {
    impl_->dirty = true;
    return impl_->globe.models().add(path, placement);
}

bool Engine::updateModel(std::uint64_t id, const ModelPlacement& placement) {
    impl_->dirty = true;
    return impl_->globe.models().update(id, placement);
}

bool Engine::removeModel(std::uint64_t id) {
    impl_->dirty = true;
    return impl_->globe.models().remove(id);
}

std::uint64_t Engine::addTileset(const std::string& path, double maxSse) {
    Vec3 c;
    std::uint64_t id = impl_->globe.addTileset(path, maxSse, &c);
    if (id) impl_->tilesetCenters[id] = c;
    impl_->dirty = true;
    return id;
}

std::uint64_t Engine::addPointCloud(const std::string& path, std::size_t maxPoints, float size) {
    PointCloudInfo info;
    std::uint64_t id = impl_->globe.points().add(path, maxPoints, size, &info);
    if (id) impl_->tilesetCenters[id | (1ull << 63)] =
        geodeticToEcef((info.south + info.north) * 0.5, (info.west + info.east) * 0.5, (info.minHeight + info.maxHeight) * 0.5);
    impl_->dirty = true;
    return id;
}

bool Engine::removePointCloud(std::uint64_t id) {
    impl_->dirty = true;
    return impl_->globe.points().remove(id);
}

bool Engine::removeTileset(std::uint64_t id) {
    impl_->tilesetCenters.erase(id);
    impl_->dirty = true;
    return impl_->globe.removeTileset(id);
}

bool Engine::tilesetCenter(std::uint64_t id, double& lat, double& lon, double& alt) const {
    auto it = impl_->tilesetCenters.find(id);
    if (it == impl_->tilesetCenters.end()) return false;
    GeoPoint g = ecefToGeodetic(it->second);
    lat = g.lat;
    lon = g.lon;
    alt = g.alt;
    return true;
}

void Engine::setGridEnabled(bool enabled) {
    impl_->globe.setGrid(enabled);
    impl_->dirty = true;
}

std::uint64_t Engine::addImageOverlay(const ImageOverlay& o) {
    impl_->dirty = true;
    return impl_->globe.addOverlay({o.west, o.south, o.east, o.north}, o.width, o.height, o.rgba, o.opacity);
}

bool Engine::removeImageOverlay(std::uint64_t id) {
    impl_->dirty = true;
    return impl_->globe.removeOverlay(id);
}

LineOfSight Engine::lineOfSight(const Coord& from, double fromHeight, const Coord& to, double toHeight) const {
    const GlobeRenderer& g = impl_->globe;
    LineOfSight r;
    Vec3 a = geodeticToEcef(from.lat, from.lon, g.elevationAt(from.lat, from.lon) + fromHeight);
    Vec3 b = geodeticToEcef(to.lat, to.lon, g.elevationAt(to.lat, to.lon) + toHeight);
    double d = length(b - a);
    r.distance = d;
    int n = static_cast<int>(std::clamp(d / 20.0, 32.0, 6000.0));
    for (int i = 1; i < n; ++i) {
        GeoPoint p = ecefToGeodetic(a + (b - a) * (static_cast<double>(i) / n));
        double ground = g.elevationAt(p.lat, p.lon);
        if (p.alt < ground) {
            r.visible = false;
            r.obstruction = {p.lat, p.lon, ground};
            r.distance = d * i / n;
            break;
        }
    }
    return r;
}

std::uint64_t Engine::computeViewshed(const Coord& o, double height, double radius, int res) {
    const GlobeRenderer& g = impl_->globe;
    res = std::clamp(res, 32, 1024);
    double dLat = radius / 111320.0, dLon = radius / (111320.0 * std::max(std::cos(o.lat * kDegToRad), 0.01));
    double north = o.lat + dLat, south = o.lat - dLat, west = o.lon - dLon, east = o.lon + dLon;
    std::vector<float> h(static_cast<size_t>(res) * res);
    for (int j = 0; j < res; ++j)
        for (int i = 0; i < res; ++i)
            h[static_cast<size_t>(j) * res + i] = static_cast<float>(
                g.elevationAt(north - (j + 0.5) * (north - south) / res, west + (i + 0.5) * (east - west) / res));
    int c = res / 2;
    double cell = 2.0 * radius / res;
    double h0 = g.elevationAt(o.lat, o.lon) + height;
    std::vector<std::uint8_t> vis(static_cast<size_t>(res) * res, 0);
    vis[static_cast<size_t>(c) * res + c] = 1;
    auto ray = [&](int tx, int ty) {
        double dx = tx - c, dy = ty - c;
        int steps = static_cast<int>(std::max(std::abs(dx), std::abs(dy)));
        double maxSlope = -1e9;
        for (int s = 1; s <= steps; ++s) {
            int x = c + static_cast<int>(std::lround(dx * s / steps)), y = c + static_cast<int>(std::lround(dy * s / steps));
            if (x < 0 || y < 0 || x >= res || y >= res) break;
            double dist = std::hypot(x - c, y - c) * cell;
            if (dist > radius) break;
            double drop = dist * dist / (2.0 * 6371000.0) * 0.87;
            double slope = (h[static_cast<size_t>(y) * res + x] - drop - h0) / dist;
            if (slope >= maxSlope) vis[static_cast<size_t>(y) * res + x] = 1;
            maxSlope = std::max(maxSlope, slope);
        }
    };
    for (int i = 0; i < res; ++i) {
        ray(i, 0);
        ray(i, res - 1);
        ray(0, i);
        ray(res - 1, i);
    }
    ImageOverlay ov;
    ov.north = north;
    ov.south = south;
    ov.east = east;
    ov.west = west;
    ov.width = ov.height = res;
    ov.rgba.assign(static_cast<size_t>(res) * res * 4, 0);
    for (int j = 0; j < res; ++j)
        for (int i = 0; i < res; ++i) {
            if (std::hypot(i - c, j - c) * cell > radius) continue;
            std::uint8_t* p = &ov.rgba[(static_cast<size_t>(j) * res + i) * 4];
            bool v = vis[static_cast<size_t>(j) * res + i] != 0;
            p[0] = v ? 0 : 230;
            p[1] = v ? 220 : 30;
            p[2] = 0;
            p[3] = 110;
        }
    return addImageOverlay(ov);
}

int Engine::downloadRegion(int layer, const Region& region, int minZoom, int maxZoom) {
    return impl_->globe.loader().startDownload(layer, region, minZoom, maxZoom);
}

std::int64_t Engine::countRegionTiles(int layer, const Region& region, int minZoom, int maxZoom) const {
    const auto& m = impl_->globe.matrices();
    if (layer < 0 || layer >= static_cast<int>(m.size())) return 0;
    std::int64_t n = 0;
    for (const auto& r : regionRanges(m[layer], region, minZoom, maxZoom)) n += r.count();
    return n;
}

DownloadStatus Engine::downloadStatus(int id) const { return impl_->globe.loader().downloadStatus(id); }

void Engine::cancelDownload(int id) { impl_->globe.loader().cancelDownload(id); }

void Engine::setElevation(const std::vector<ElevationLayer>& layers) {
    impl_->globe.setElevation(layers);
    impl_->dirty = true;
}

void Engine::setTerrainEnabled(bool enabled) {
    impl_->globe.setTerrainEnabled(enabled);
    impl_->dirty = true;
}

void Engine::setTerrainExaggeration(double factor) {
    impl_->globe.setTerrainExaggeration(factor);
    impl_->dirty = true;
}

void Engine::setTerrainTransparency(float alpha) {
    impl_->globe.setTerrainAlpha(alpha);
    impl_->dirty = true;
}

void Engine::setDepthPerception(bool enabled) {
    impl_->globe.setDepthPerception(enabled);
    impl_->dirty = true;
}

double Engine::elevationAt(double lat, double lon) const { return impl_->globe.elevationAt(lat, lon); }

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
    if (c.firstPerson) {
        c.fpPosition = {v.lat, v.lon, 0};
        c.fpHeading = v.azimuth;
        c.update();
        impl_->dirty = true;
        return;
    }
    c.focus = {v.lat, v.lon, v.alt};
    c.gsd = v.gsd;
    c.azimuth = v.azimuth;
    c.tilt = v.tilt;
    c.update();
    impl_->dirty = true;
}

bool Engine::pick(double sx, double sy, double& lat, double& lon) const {
    Vec3 p;
    if (!impl_->pickGround(sx, sy, p)) return false;
    GeoPoint g = ecefToGeodetic(p);
    lat = g.lat;
    lon = g.lon;
    return true;
}

void Engine::setFirstPerson(bool enabled, double height) {
    Camera& c = impl_->camera;
    if (enabled == c.firstPerson) return;
    if (enabled) {
        c.fpPosition = {c.focus.lat, c.focus.lon, 0};
        c.fpHeight = height;
        c.fpHeading = c.azimuth;
        c.fpPitch = 0;
        c.firstPerson = true;
    } else {
        c.firstPerson = false;
        c.focus = {c.fpPosition.lat, c.fpPosition.lon, impl_->globe.elevationAt(c.fpPosition.lat, c.fpPosition.lon)};
        c.azimuth = c.fpHeading;
        c.tilt = 60;
        c.setRange(400);
    }
    c.update();
    impl_->dirty = true;
}

bool Engine::firstPerson() const { return impl_->camera.firstPerson; }

void Engine::walk(double forward, double right) {
    Camera& c = impl_->camera;
    if (!c.firstPerson) return;
    double h = c.fpHeading * kDegToRad;
    double north = forward * std::cos(h) - right * std::sin(h);
    double east = forward * std::sin(h) + right * std::cos(h);
    c.fpPosition.lat += north / 111320.0;
    c.fpPosition.lon += east / (111320.0 * std::max(std::cos(c.fpPosition.lat * kDegToRad), 0.01));
    c.update();
    impl_->dirty = true;
}

void Engine::look(double heading, double pitch) {
    Camera& c = impl_->camera;
    if (!c.firstPerson) return;
    c.fpHeading += heading;
    c.fpPitch += pitch;
    c.update();
    impl_->dirty = true;
}

void Engine::set3D(bool enabled) {
    Camera& c = impl_->camera;
    c.lock2d = !enabled;
    if (!enabled && c.firstPerson) setFirstPerson(false);
    impl_->globe.setTerrainEnabled(enabled);
    c.update();
    impl_->dirty = true;
}

bool Engine::is3D() const { return !impl_->camera.lock2d; }

void Engine::pan(double x0, double y0, double x1, double y1) {
    Camera& c = impl_->camera;
    if (c.firstPerson) {
        look((x0 - x1) * 0.15, (y1 - y0) * 0.15);
        return;
    }
    Vec3 p0, p1;
    if (impl_->pickGround(x0, y0, p0) && impl_->pickGround(x1, y1, p1)) {
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
    if (c.firstPerson) {
        walk((1.0 - factor) * std::max(c.fpHeight * 10.0, 20.0), 0);
        return;
    }
    Vec3 before;
    bool hit = impl_->pickGround(sx, sy, before);
    c.gsd *= factor;
    c.update();
    Vec3 after;
    if (hit && impl_->pickGround(sx, sy, after)) impl_->moveCamera(after, before);
    impl_->dirty = true;
}

void Engine::rotate(double degrees) {
    if (impl_->camera.firstPerson) {
        look(degrees, 0);
        return;
    }
    impl_->camera.azimuth += degrees;
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::tiltBy(double degrees) {
    if (impl_->camera.firstPerson) {
        look(0, degrees);
        return;
    }
    impl_->camera.tilt += degrees;
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::flyTo(const View& target, double seconds) {
    Impl& m = *impl_;
    m.flyFrom = view();
    m.flyTo = target;
    m.flyDuration = std::max(seconds, 0.05);
    Vec3 a = geodeticToEcef(m.flyFrom.lat, m.flyFrom.lon, 0), b = geodeticToEcef(target.lat, target.lon, 0);
    double distance = length(b - a);
    double needed = std::log(std::max(distance / std::max(m.camera.height, 1) * 1.5, 1.0));
    m.flyPeak = std::max({std::log(m.flyFrom.gsd), std::log(target.gsd), needed});
    m.flyStart = std::chrono::steady_clock::now();
    m.flying = true;
    m.dirty = true;
}

bool Engine::animating() const { return impl_->flying; }

void Engine::setElevationHeatmap(bool enabled, double minHeight, double maxHeight, float opacity) {
    impl_->globe.setHeatmap(enabled, minHeight, maxHeight, opacity);
    impl_->dirty = true;
}

std::vector<ProfileSample> Engine::elevationProfile(const std::vector<Coord>& path, int samples) const {
    std::vector<ProfileSample> out;
    if (path.size() < 2 || samples < 2) return out;
    std::vector<double> cumulative{0.0};
    for (size_t i = 1; i < path.size(); ++i)
        cumulative.push_back(cumulative.back() + length(geodeticToEcef(path[i].lat, path[i].lon, 0) -
                                                        geodeticToEcef(path[i - 1].lat, path[i - 1].lon, 0)));
    double total = cumulative.back();
    size_t seg = 1;
    for (int k = 0; k < samples; ++k) {
        double d = total * k / (samples - 1);
        while (seg + 1 < cumulative.size() && cumulative[seg] < d) ++seg;
        double len = cumulative[seg] - cumulative[seg - 1];
        double t = len > 0 ? (d - cumulative[seg - 1]) / len : 0;
        Vec3 a = geodeticToEcef(path[seg - 1].lat, path[seg - 1].lon, 0), b = geodeticToEcef(path[seg].lat, path[seg].lon, 0);
        GeoPoint g = ecefToGeodetic(a + (b - a) * t);
        out.push_back({d, g.lat, g.lon, impl_->globe.elevationAt(g.lat, g.lon)});
    }
    return out;
}

void Engine::frame(const sg_swapchain& swapchain) {
    auto cpuStart = std::chrono::steady_clock::now();
    impl_->animate();
    impl_->followTerrain();
    impl_->globe.render(impl_->camera, &swapchain, nullptr, swapchain.width, swapchain.height);
    impl_->dirty = false;
    impl_->globe.setCpuMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count());
}

void Engine::frameOffscreen(const sg_attachments& attachments, int width, int height) {
    impl_->animate();
    impl_->followTerrain();
    impl_->globe.render(impl_->camera, nullptr, &attachments, width, height);
    impl_->dirty = false;
}

bool Engine::needsRedraw() const { return impl_->dirty || impl_->flying || impl_->globe.busy(); }

Stats Engine::stats() const { return impl_->globe.stats(); }

void setAndroidJavaVm(void* vm) { net::setJavaVm(vm); }

double geoidOffset(double lat, double lon) { return geoidHeight(lat, lon); }

void configureGfx(sg_desc& desc) {
    desc.buffer_pool_size = 4096;
    desc.image_pool_size = 4096;
    desc.view_pool_size = 32768;
    desc.sampler_pool_size = 64;
    desc.shader_pool_size = 64;
    desc.pipeline_pool_size = 128;
}

std::vector<ElevationLayer> defaultElevation() {
    ElevationLayer t;
    t.name = "Terrain Tiles";
    t.url = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";
    t.maxZoom = 15;
    return {t};
}

std::vector<ImageryLayer> defaultLayers() {
    ImageryLayer imagery;
    imagery.name = "World Imagery";
    imagery.url = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}";
    imagery.maxZoom = 19;
    return {imagery};
}

}
