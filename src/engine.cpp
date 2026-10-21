#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "ote/engine.hpp"
#include "ote/formats.hpp"
#include "ote/image.hpp"
#include "ote/milsym.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>

#include "core/camera.hpp"
#include "render/globe_renderer.hpp"

#include "core/geoid.hpp"
#include "ote/mgrs.hpp"
#include "net/http.hpp"

#include <sokol_gfx.h>

namespace ote {

struct Engine::Impl {
    Camera camera;
    XrAnchor xr;
    void renderStereo(const sg_swapchain* swapchain, const sg_attachments* attachments, int width, int height, double separation);
    GlobeRenderer globe;
    bool dirty = true;
    bool headingLock = true;
    std::unique_ptr<InputController> input;
    std::unordered_map<std::uint64_t, Vec3> tilesetCenters;
    std::unordered_map<std::uint64_t, double> tilesetRadii;
    std::unordered_map<std::uint64_t, Region> pointCloudRegions;
    bool flying = false;
    bool cacheDisabled = false;
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
            camera.holdAltitude = flyTo.holdAltitude;
            if (flyTo.holdAltitude) camera.focus.alt = flyTo.alt;
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
        if (!camera.holdAltitude && std::abs(h - camera.focus.alt) > 0.01) {
            camera.focus.alt = h;
            camera.update();
            return;
        }
        if (camera.tilt > 0) {
            GeoPoint eg = ecefToGeodetic(camera.eye());
            double g = globe.elevationAt(eg.lat, eg.lon);
            if (!std::isnan(g) && eg.alt < g + camera.clearance()) {
                camera.update();
                dirty = true;
            }
        }
    }

    std::uint64_t sanityFrame = 0;
    bool sanityHave = false;
    GeoPoint lastFocus;
    double lastGsd = 0;
    double lastAz = 0;
    double lastTilt = 0;

    void sanity() {
        if (!globe.diagnostics()) return;
        ++sanityFrame;
        Camera& c = camera;
        Vec3 eye = c.eye();
        bool finite = std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z) && std::isfinite(c.gsd) && std::isfinite(c.tilt) &&
                      std::isfinite(c.azimuth) && std::isfinite(c.focus.lat) && std::isfinite(c.focus.lon) && std::isfinite(c.focus.alt);
        const Mat4& vp = c.viewProjRel();
        for (int i = 0; i < 4 && finite; ++i)
            for (int j = 0; j < 4 && finite; ++j) finite = std::isfinite(vp.at(i, j));
        if (!finite) globe.log("violation frame %llu: non-finite camera", static_cast<unsigned long long>(sanityFrame));
        if (c.firstPerson) return;
        GeoPoint eg = ecefToGeodetic(eye);
        double g = globe.elevationAt(eg.lat, eg.lon);
        if (sanityFrame % 30 == 0)
            if (const char* pr = std::getenv("OTE_PROBE")) {
                double px = 0, py = 0;
                Vec3 ph;
                if (std::sscanf(pr, "%lf,%lf", &px, &py) == 2 && pickGround(px, py, ph)) {
                    GeoPoint pg = ecefToGeodetic(ph);
                    globe.explain(c, pg.lat, pg.lon, "probe");
                }
            }
        if (sanityFrame % 30 == 0)
            for (double fx : {0.1, 0.5, 0.9}) {
                Vec3 bh;
                if (pickGround(c.width * fx, c.height - 4.0, bh)) {
                    GeoPoint bg = ecefToGeodetic(bh);
                    globe.explain(c, bg.lat, bg.lon, fx < 0.3 ? "bottom-left" : fx < 0.7 ? "bottom-centre" : "bottom-right");
                }
            }
        if (sanityFrame % 15 == 0) {
            Vec3 ch;
            bool centre = pickGround(c.width * 0.5, c.height * 0.5, ch);
            globe.log("camera frame %llu: eye %.5f %.5f alt %.1f ground %.1f above %.1f near %.2f far %.0f range %.0f focus_alt %.1f tilt %.1f gsd %.2f centre %d",
                      static_cast<unsigned long long>(sanityFrame), eg.lat, eg.lon, eg.alt, g, c.eyeAboveGround(), c.nearPlane(), c.farPlane(), c.range(),
                      c.focus.alt, c.tilt, c.gsd, centre ? 1 : 0);
        }
        if (std::isfinite(g) && eg.alt < g - 1.0)
            globe.log("violation frame %llu: eye %.1f m below ground at %.5f %.5f tilt %.1f gsd %.2f", static_cast<unsigned long long>(sanityFrame),
                      g - eg.alt, eg.lat, eg.lon, c.tilt, c.gsd);
        if (c.tilt < 70.0) {
            Vec3 hit;
            if (!pickGround(c.width * 0.5, c.height * 0.5, hit))
                globe.log("violation frame %llu: screen centre misses the ground at tilt %.1f gsd %.2f", static_cast<unsigned long long>(sanityFrame),
                          c.tilt, c.gsd);
        }
        if (sanityHave && !flying) {
            double dn = (c.focus.lat - lastFocus.lat) * 111320.0;
            double de = std::remainder(c.focus.lon - lastFocus.lon, 360.0) * 111320.0 * std::cos(c.focus.lat * kDegToRad);
            double moved = std::hypot(dn, de);
            double extent = std::max(lastGsd, c.gsd) * std::hypot(static_cast<double>(c.width), static_cast<double>(c.height));
            if (moved > extent * 1.5 && std::abs(c.focus.lat) < 89.0)
                globe.log("violation frame %llu: focus jumped %.0f m in one frame (view extent %.0f m) tilt %.1f", static_cast<unsigned long long>(sanityFrame),
                          moved, extent, c.tilt);
            if (std::abs(c.gsd / std::max(lastGsd, 1e-9)) > 4.0 || std::abs(lastGsd / std::max(c.gsd, 1e-9)) > 4.0)
                globe.log("violation frame %llu: gsd jumped %.2f -> %.2f", static_cast<unsigned long long>(sanityFrame), lastGsd, c.gsd);
        }
        Stats st = globe.stats();
        if (st.blankTiles > 0 && sanityFrame > 120)
            globe.log("violation frame %llu: %d blank tiles", static_cast<unsigned long long>(sanityFrame), st.blankTiles);
        lastFocus = c.focus;
        lastGsd = c.gsd;
        lastAz = c.azimuth;
        lastTilt = c.tilt;
        sanityHave = true;
    }

    double grabLimit() const { return std::max(camera.range() * 3.0, 2000.0); }

    Vec3 upAt(const Vec3& p) const {
        GeoPoint g = ecefToGeodetic(p);
        Vec3 e, n, u;
        enuFrame(g.lat, g.lon, e, n, u);
        return u;
    }

    Vec3 onPlane(double sx, double sy, const Vec3& origin, const Vec3& up, double limit) const {
        Vec3 eye = camera.eye();
        Vec3 d = normalize(camera.rayDirection(sx, sy));
        double denom = dot(d, up);
        double above = dot(eye - origin, up);
        if (above > 0 && denom < -1e-6) {
            double t = -above / denom;
            Vec3 hit = eye + d * t;
            Vec3 foot = eye - up * above;
            if (length(hit - foot) <= limit) return hit;
        }
        Vec3 hd = d - up * denom;
        if (length(hd) < 1e-9) {
            Vec3 e, n, u;
            enuFrame(camera.focus.lat, camera.focus.lon, e, n, u);
            double az = camera.azimuth * kDegToRad;
            hd = n * std::cos(az) + e * std::sin(az);
        }
        hd = normalize(hd);
        Vec3 foot = eye - up * above;
        return foot + hd * limit;
    }

    bool groundAnchor(double sx, double sy, double maxDistance, Vec3& out) const {
        Vec3 hit;
        if (!pickGround(sx, sy, hit)) return false;
        if (length(hit - camera.eye()) > maxDistance) return false;
        out = hit;
        return true;
    }

    void dragPoints(double x0, double y0, double x1, double y1, Vec3& p0, Vec3& p1) const {
        double limit = grabLimit();
        Vec3 origin;
        if (!groundAnchor(x0, y0, limit, origin)) origin = onPlane(x0, y0, camera.focusEcef(), upAt(camera.focusEcef()), limit);
        Vec3 up = upAt(origin);
        p0 = origin;
        p1 = onPlane(x1, y1, origin, up, limit);
    }

    double poleLimit() const { return 89.99; }

    double poleSafeFraction(Vec3 f0, Vec3 axis, double angle, double bound) const {
        if (angle == 0) return 1.0;
        Vec3 k = normalize(axis);
        double r = length(f0);
        if (r <= 0) return 1.0;
        Vec3 f = f0 * (1.0 / r);
        double kf = dot(k, f);
        double A = f.z - k.z * kf, B = cross(k, f).z, C = k.z * kf;
        double R = std::hypot(A, B);
        double phi = std::atan2(B, A);
        double limit = std::sin(bound * kDegToRad);
        double best = std::abs(angle);
        for (double target : {limit, -limit}) {
            double q = target - C;
            if (R < 1e-15 || std::abs(q) > R) continue;
            double d = std::acos(std::clamp(q / R, -1.0, 1.0));
            for (double t0 : {phi + d, phi - d}) {
                for (int wrap = -2; wrap <= 2; ++wrap) {
                    double t = t0 + wrap * 2.0 * kPi;
                    double along = angle > 0 ? t : -t;
                    if (along > 1e-12 && along < best) best = along;
                }
            }
        }
        double z0 = f.z;
        if (std::abs(z0) > limit) return 1.0;
        return std::max(0.0, best / std::abs(angle) - 1e-9);
    }

    void rotateFocus(Vec3 axis, double angle, double maxLonDelta = 360.0) {
        Vec3 f0 = camera.focusEcef();
        Vec3 e, n, u;
        enuFrame(camera.focus.lat, camera.focus.lon, e, n, u);
        double az = camera.azimuth * kDegToRad;
        Vec3 fwd0 = n * std::cos(az) + e * std::sin(az);
        double s = poleSafeFraction(f0, axis, angle, poleLimit());
        GeoPoint g = ecefToGeodetic(rotateAxis(f0, axis, angle * s));
        double dLon = std::fmod(g.lon - camera.focus.lon + 540.0, 360.0) - 180.0;
        if (headingLock && std::abs(dLon) > maxLonDelta) g.lon = camera.focus.lon + std::copysign(maxLonDelta, dLon);
        camera.focus.lat = std::clamp(g.lat, -poleLimit(), poleLimit());
        camera.focus.lon = wrapLon(g.lon);
        if (!headingLock) {
            Vec3 fwd = rotateAxis(fwd0, axis, angle * s);
            enuFrame(camera.focus.lat, camera.focus.lon, e, n, u);
            camera.azimuth = std::atan2(dot(fwd, e), dot(fwd, n)) * kRadToDeg;
        }
        camera.update();
    }

    void shiftFocus(double east, double north, double maxLonDelta) {
        double lat0 = camera.focus.lat;
        double lat = std::clamp(lat0 + north / kWgs84A * kRadToDeg, -poleLimit(), poleLimit());
        double dLon = east / (kWgs84A * std::max(std::cos(lat0 * kDegToRad), 1e-9)) * kRadToDeg;
        if (std::abs(dLon) > maxLonDelta) dLon = std::copysign(maxLonDelta, dLon);
        camera.focus.lat = lat;
        camera.focus.lon = wrapLon(camera.focus.lon + dLon);
        camera.update();
    }

    void moveCamera(Vec3 from, Vec3 to, double maxLonDelta = 360.0) {
        Vec3 axis = cross(from, to);
        double len = length(axis);
        if (len < 1e-12) return;
        rotateFocus(axis, std::atan2(len, dot(from, to)), maxLonDelta);
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->input = std::make_unique<InputController>(*this); }

Engine::~Engine() { shutdown(); }

bool Engine::init() {
    impl_->camera.groundHeight = [this](double lat, double lon) { return impl_->globe.elevationAt(lat, lon); };
    impl_->camera.update();
    if (impl_->globe.cacheDirectory().empty() && !impl_->cacheDisabled) {
        std::string dir = defaultCacheDirectory();
        std::error_code ec;
        if (!dir.empty()) {
            std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
            if (std::filesystem::is_directory(std::filesystem::u8path(dir), ec)) impl_->globe.setCacheDirectory(dir);
        }
    }
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
    impl_->input->setViewport(width, height, dpiScale);
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::setLayers(const std::vector<ImageryLayer>& layers) {
    impl_->globe.setLayers(layers);
    impl_->dirty = true;
}

std::vector<ImageryLayer> Engine::layers() const { return impl_->globe.layers(); }

void Engine::setLayerOpacity(int index, float opacity) {
    std::vector<ImageryLayer> l = impl_->globe.layers();
    if (index < 0 || index >= static_cast<int>(l.size())) return;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (l[index].opacity == opacity) return;
    l[index].opacity = opacity;
    setLayers(l);
}

void Engine::setLayerVisible(int index, bool visible) {
    std::vector<ImageryLayer> l = impl_->globe.layers();
    if (index < 0 || index >= static_cast<int>(l.size()) || l[index].visible == visible) return;
    l[index].visible = visible;
    setLayers(l);
}

void Engine::setCacheDirectory(const std::string& dir) {
    impl_->globe.setCacheDirectory(dir);
    impl_->dirty = true;
}

std::string Engine::cacheDirectory() const { return impl_->globe.cacheDirectory(); }

void Engine::setCacheLimit(int megabytes) { impl_->globe.cache().setLimit(static_cast<std::int64_t>(std::max(megabytes, 0)) << 20); }

std::int64_t Engine::cacheBytes() const { return impl_->globe.cache().bytes(); }

void Engine::setOfflineBackdrop(bool enabled) {
    impl_->globe.setBackdrop(enabled);
    impl_->globe.retryUnavailable();
    impl_->dirty = true;
}

void Engine::networkChanged() { net::resetReachability(); }

void Engine::setMemoryBudget(int megabytes) { impl_->globe.setMemoryBudget(megabytes); }

void Engine::setOffline(bool offline) {
    impl_->globe.setOffline(offline);
    impl_->dirty = true;
}

std::uint64_t Engine::addFeature(const Feature& f) {
    impl_->dirty = true;
    if (f.style.sidc.empty() && f.type.compare(0, 2, "a-") != 0) return impl_->globe.features().add(f);
    Feature s = f;
    applySymbol(s);
    return impl_->globe.features().add(s);
}

bool Engine::updateFeature(std::uint64_t id, const Feature& f) {
    impl_->dirty = true;
    if (f.style.sidc.empty() && f.type.compare(0, 2, "a-") != 0) return impl_->globe.features().update(id, f);
    Feature s = f;
    applySymbol(s);
    return impl_->globe.features().update(id, s);
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
    ImportResult imported = importData(path);
    ParsedFile& parsed = imported.data;
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
    for (const ModelInstance& m : parsed.models)
        if (impl_->globe.models().addGlb(m.glb, m.placement)) impl_->dirty = true;
    return ids;
}

ExportResult Engine::exportFeatures(const std::vector<std::uint64_t>& ids, const std::string& path, const std::string& format) const {
    std::vector<Feature> out;
    for (std::uint64_t id : ids.empty() ? featureIds() : ids) {
        Feature f;
        if (feature(id, f)) out.push_back(std::move(f));
    }
    return ote::exportFeatures(out, path, format);
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

std::uint64_t Engine::addMesh(const Mesh& mesh, const ModelPlacement& placement) {
    CpuModel model;
    for (const MeshPart& part : mesh.parts) {
        const size_t n = part.positions.size() / 3;
        if (n < 3 || part.indices.empty()) continue;
        std::vector<float> normals = part.normals;
        if (normals.size() != n * 3) {
            normals.assign(n * 3, 0.0f);
            for (size_t t = 0; t + 2 < part.indices.size(); t += 3) {
                std::uint32_t a = part.indices[t], b = part.indices[t + 1], c = part.indices[t + 2];
                if (a >= n || b >= n || c >= n) continue;
                Vec3 pa{part.positions[a * 3], part.positions[a * 3 + 1], part.positions[a * 3 + 2]};
                Vec3 pb{part.positions[b * 3], part.positions[b * 3 + 1], part.positions[b * 3 + 2]};
                Vec3 pc{part.positions[c * 3], part.positions[c * 3 + 1], part.positions[c * 3 + 2]};
                Vec3 f = cross(pb - pa, pc - pa);
                for (std::uint32_t v : {a, b, c}) {
                    normals[v * 3] += static_cast<float>(f.x);
                    normals[v * 3 + 1] += static_cast<float>(f.y);
                    normals[v * 3 + 2] += static_cast<float>(f.z);
                }
            }
        }
        CpuMaterial mat;
        mat.color = {((part.color >> 16) & 0xFF) / 255.0f, ((part.color >> 8) & 0xFF) / 255.0f, (part.color & 0xFF) / 255.0f,
                     ((part.color >> 24) & 0xFF) / 255.0f};
        mat.unlit = part.unlit;
        mat.metallic = 0.0f;
        mat.roughness = 1.0f;
        CpuPrimitive prim;
        prim.material = static_cast<int>(model.materials.size());
        model.materials.push_back(mat);
        prim.matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        prim.vertices.reserve(n * 8);
        for (size_t i = 0; i < n; ++i) {
            float nx = normals[i * 3], ny = normals[i * 3 + 1], nz = normals[i * 3 + 2];
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 0) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nz = 1.0f;
            }
            prim.vertices.insert(prim.vertices.end(), {part.positions[i * 3], part.positions[i * 3 + 2], -part.positions[i * 3 + 1],
                                                       nx, nz, -ny, 0.0f, 0.0f});
        }
        prim.indices = part.indices;
        model.primitives.push_back(std::move(prim));
    }
    if (model.primitives.empty()) return 0;
    finishModel(model);
    impl_->dirty = true;
    return impl_->globe.models().add(ModelLayer::upload(model), placement);
}

int Engine::fetchUrl(const std::string& url, std::vector<std::uint8_t>& body, const FetchOptions& options) const {
    return impl_->globe.fetch(url, body, options);
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
    TilesetOptions options;
    options.maxScreenSpaceError = maxSse;
    return addTileset(path, options);
}

std::vector<std::string> Engine::credits() const { return impl_->globe.credits(); }

void Engine::setSunLighting(bool enabled, double unixSeconds) {
    impl_->globe.setSunLighting(enabled, unixSeconds);
    impl_->dirty = true;
}

std::uint64_t Engine::addTileset(const std::string& path, const TilesetOptions& options) {
    Vec3 c;
    double radius = 0;
    std::uint64_t id = impl_->globe.addTileset(path, options, &c, &radius);
    if (id) {
        impl_->tilesetCenters[id] = c;
        impl_->tilesetRadii[id] = radius;
    }
    impl_->dirty = true;
    return id;
}

std::uint64_t Engine::addPointCloud(const std::string& path, std::size_t maxPoints, float size) {
    PointCloudInfo info;
    std::uint64_t id = impl_->globe.points().add(path, maxPoints, size, &info);
    if (id) {
        impl_->tilesetCenters[id | (1ull << 63)] =
            geodeticToEcef((info.south + info.north) * 0.5, (info.west + info.east) * 0.5, (info.minHeight + info.maxHeight) * 0.5);
        impl_->pointCloudRegions[id] = {info.west, info.south, info.east, info.north};
    }
    impl_->dirty = true;
    return id;
}

bool Engine::removePointCloud(std::uint64_t id) {
    impl_->tilesetCenters.erase(id | (1ull << 63));
    impl_->pointCloudRegions.erase(id);
    impl_->dirty = true;
    return impl_->globe.points().remove(id);
}

bool Engine::setTilesetOpacity(std::uint64_t id, float opacity) {
    impl_->dirty = true;
    return impl_->globe.setTilesetOpacity(id, opacity);
}

bool Engine::removeTileset(std::uint64_t id) {
    impl_->tilesetCenters.erase(id);
    impl_->tilesetRadii.erase(id);
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

bool Engine::tilesetRegion(std::uint64_t id, Region& out) const {
    auto c = impl_->tilesetCenters.find(id);
    auto r = impl_->tilesetRadii.find(id);
    if (c == impl_->tilesetCenters.end() || r == impl_->tilesetRadii.end()) return false;
    GeoPoint g = ecefToGeodetic(c->second);
    const double dLat = r->second / 110574.0;
    const double dLon = r->second / (111320.0 * std::max(std::cos(g.lat * kDegToRad), 0.01));
    if (dLat >= 90.0 || dLon >= 180.0) {
        out = {-180, -90, 180, 90};
        return true;
    }
    out = {g.lon - dLon, std::max(g.lat - dLat, -90.0), g.lon + dLon, std::min(g.lat + dLat, 90.0)};
    if (out.west < -180.0) out.west += 360.0;
    if (out.east > 180.0) out.east -= 360.0;
    return true;
}

bool Engine::pointCloudRegion(std::uint64_t id, Region& out) const {
    auto it = impl_->pointCloudRegions.find(id);
    if (it == impl_->pointCloudRegions.end()) return false;
    out = it->second;
    return true;
}

bool Engine::featuresRegion(const std::vector<std::uint64_t>& ids, Region& out) const {
    double south = 90, north = -90, west = 180, east = -180, westShifted = 360, eastShifted = 0;
    bool any = false;
    Feature f;
    for (std::uint64_t id : ids) {
        if (!impl_->globe.features().get(id, f)) continue;
        for (const Geometry& g : f.geometries)
            for (const Coord& p : g.points) {
                if (!std::isfinite(p.lat) || !std::isfinite(p.lon)) continue;
                any = true;
                south = std::min(south, p.lat);
                north = std::max(north, p.lat);
                west = std::min(west, p.lon);
                east = std::max(east, p.lon);
                double shifted = p.lon < 0 ? p.lon + 360.0 : p.lon;
                westShifted = std::min(westShifted, shifted);
                eastShifted = std::max(eastShifted, shifted);
            }
    }
    if (!any) return false;
    if (eastShifted - westShifted < east - west) {
        west = westShifted > 180.0 ? westShifted - 360.0 : westShifted;
        east = eastShifted > 180.0 ? eastShifted - 360.0 : eastShifted;
    }
    out = {west, south, east, north};
    return true;
}

View Engine::viewForRegion(const Region& region) const {
    const Camera& c = impl_->camera;
    double west = region.west, east = region.east;
    if (east < west) east += 360.0;
    const double south = std::clamp(std::min(region.south, region.north), -90.0, 90.0);
    const double north = std::clamp(std::max(region.south, region.north), -90.0, 90.0);
    const double midLat = 0.5 * (south + north);
    double midLon = 0.5 * (west + east);
    if (midLon > 180.0) midLon -= 360.0;
    const double widthM = (east - west) * 111320.0 * std::max(std::cos(midLat * kDegToRad), 0.01);
    const double heightM = (north - south) * 110574.0;
    const double extent = std::max({widthM / std::max(c.width, 1), heightM / std::max(c.height, 1), 300.0 / std::max(std::min(c.width, c.height), 1)});
    View v;
    v.lat = midLat;
    v.lon = midLon;
    v.alt = 0.0;
    v.gsd = std::min(extent * 1.2, 40000.0);
    v.azimuth = 0.0;
    v.tilt = 0.0;
    return v;
}

void Engine::setGridEnabled(bool enabled) {
    impl_->globe.setGrid(enabled);
    impl_->dirty = true;
}

void Engine::setGridType(GridType type) {
    impl_->globe.setGridType(static_cast<int>(type));
    impl_->dirty = true;
}

void Engine::setGridEnabled(GridType type, bool enabled) {
    impl_->globe.setGridLayer(static_cast<int>(type), enabled);
    impl_->dirty = true;
}

bool Engine::gridEnabled(GridType type) const { return impl_->globe.gridOn(static_cast<int>(type)); }

std::vector<GridLabel> Engine::gridLabels() const {
    std::vector<GridLabel> out;
    const Camera& c = impl_->camera;
    if (c.firstPerson) return out;
    const double W = c.width, H = c.height;
    auto inside = [&](double x, double y) { return x >= 0 && y >= 0 && x <= W && y <= H; };
    auto place = [&](GridLabel label, const std::vector<std::pair<double, double>>& line) {
        bool have = false, prevOk = false;
        double bx = 0, by = 0, px = 0, py = 0;
        auto consider = [&](double x, double y) {
            if (!have || (label.vertical ? y < by : x < bx)) {
                bx = x;
                by = y;
                have = true;
            }
        };
        for (const auto& [lat, lon] : line) {
            double sx, sy;
            double h = impl_->globe.elevationAt(lat, lon);
            if (!std::isfinite(h)) h = 0;
            if (!project(lat, lon, h, sx, sy)) {
                prevOk = false;
                continue;
            }
            bool in = inside(sx, sy);
            if (prevOk && in != inside(px, py)) {
                double ix = in ? sx : px, iy = in ? sy : py, ax = in ? px : sx, ay = in ? py : sy;
                double t = 1.0;
                auto clip = [&](double from, double to, double edge) {
                    if ((to < edge) != (from < edge) && to != from) t = std::min(t, (edge - from) / (to - from));
                };
                clip(ix, ax, 0.0);
                clip(ix, ax, W);
                clip(iy, ay, 0.0);
                clip(iy, ay, H);
                consider(ix + (ax - ix) * t, iy + (ay - iy) * t);
            }
            if (in) consider(sx, sy);
            px = sx;
            py = sy;
            prevOk = true;
        }
        if (!have) return;
        label.x = bx;
        label.y = by;
        out.push_back(label);
    };
    Region r = viewRegion();
    double south = std::max(r.south, -85.0), north = std::min(r.north, 85.0);
    if (impl_->globe.gridOn(1)) {
        double sp = impl_->globe.gridStep(c, 1);
        if (sp > 0 && (r.east - r.west) / sp <= 120 && (north - south) / sp <= 120) {
            const int n = 64;
            for (double m = std::ceil(r.west / sp) * sp; m <= r.east + 1e-9; m += sp) {
                std::vector<std::pair<double, double>> line;
                for (int i = 0; i <= n; ++i) line.emplace_back(south + (north - south) * i / n, m);
                GridLabel l;
                l.grid = GridType::LatLon;
                l.vertical = true;
                l.value = std::remainder(m, 360.0);
                l.spacing = sp;
                place(l, line);
            }
            for (double p = std::ceil(south / sp) * sp; p <= north + 1e-9; p += sp) {
                std::vector<std::pair<double, double>> line;
                for (int i = 0; i <= n; ++i) line.emplace_back(p, r.west + (r.east - r.west) * i / n);
                GridLabel l;
                l.grid = GridType::LatLon;
                l.vertical = false;
                l.value = p;
                l.spacing = sp;
                place(l, line);
            }
        }
    }
    if (impl_->globe.gridOn(0) && std::abs(c.focus.lat) < 84.0) {
        double sp = impl_->globe.gridStep(c, 0);
        if (sp > 0) {
            int z0 = std::max(1, static_cast<int>(std::floor((r.west + 180.0) / 6.0)) + 1);
            int z1 = std::min(60, static_cast<int>(std::floor((r.east + 180.0 - 1e-9) / 6.0)) + 1);
            bool hemiNorth = c.focus.lat >= 0;
            for (int zone = z0; zone <= z1; ++zone) {
                double zw = -180.0 + (zone - 1) * 6.0, ze = zw + 6.0;
                double w = std::max(r.west, zw), e = std::min(r.east, ze);
                if (w >= e) continue;
                double minE = 1e18, maxE = -1e18, minN = 1e18, maxN = -1e18;
                for (double la : {south, 0.5 * (south + north), north})
                    for (double lo : {w, 0.5 * (w + e), e}) {
                        Utm u = toUtm(la, lo, zone);
                        double nn = u.northing + (u.north == hemiNorth ? 0.0 : (hemiNorth ? -10000000.0 : 10000000.0));
                        minE = std::min(minE, u.easting);
                        maxE = std::max(maxE, u.easting);
                        minN = std::min(minN, nn);
                        maxN = std::max(maxN, nn);
                    }
                minE = std::floor(minE / sp) * sp;
                maxE = std::ceil(maxE / sp) * sp;
                minN = std::floor(minN / sp) * sp;
                maxN = std::ceil(maxN / sp) * sp;
                if ((maxE - minE) / sp > 80 || (maxN - minN) / sp > 80) continue;
                auto at = [&](double easting, double northing, double& lat, double& lon) {
                    Utm u;
                    u.zone = zone;
                    u.north = hemiNorth;
                    u.easting = easting;
                    u.northing = northing;
                    fromUtm(u, lat, lon);
                    return lon >= zw - 1e-9 && lon <= ze + 1e-9;
                };
                const int n = 48;
                for (double ee = minE; ee <= maxE + 1e-6; ee += sp) {
                    std::vector<std::pair<double, double>> line;
                    for (int i = 0; i <= n; ++i) {
                        double lat, lon;
                        if (at(ee, minN + (maxN - minN) * i / n, lat, lon)) line.emplace_back(lat, lon);
                    }
                    GridLabel l;
                    l.grid = GridType::Mgrs;
                    l.vertical = true;
                    l.value = ee;
                    l.spacing = sp;
                    l.zone = zone;
                    l.north = hemiNorth;
                    if (line.size() >= 2) place(l, line);
                }
                for (double nn = minN; nn <= maxN + 1e-6; nn += sp) {
                    std::vector<std::pair<double, double>> line;
                    for (int i = 0; i <= n; ++i) {
                        double lat, lon;
                        if (at(minE + (maxE - minE) * i / n, nn, lat, lon)) line.emplace_back(lat, lon);
                    }
                    GridLabel l;
                    l.grid = GridType::Mgrs;
                    l.vertical = false;
                    l.value = nn;
                    l.spacing = sp;
                    l.zone = zone;
                    l.north = hemiNorth;
                    if (line.size() >= 2) place(l, line);
                }
            }
        }
    }
    return out;
}


void Engine::setTerrainSlope(bool enabled, float opacity, double maxSlopeDegrees) {
    impl_->globe.setSlope(enabled, opacity, maxSlopeDegrees);
    impl_->dirty = true;
}

Region Engine::viewRegion() const {
    const Camera& c = impl_->camera;
    double south = 90, north = -90, west = 180, east = -180;
    bool any = false;
    for (int j = 0; j <= 8; ++j)
        for (int i = 0; i <= 8; ++i) {
            double lat, lon;
            if (!pick(c.width * i / 8.0, c.height * j / 8.0, lat, lon)) continue;
            any = true;
            south = std::min(south, lat);
            north = std::max(north, lat);
            west = std::min(west, lon);
            east = std::max(east, lon);
        }
    if (!any) return {-180, -90, 180, 90};
    if (east - west > 180.0) return {-180, south, 180, north};
    return {west, south, east, north};
}

double Engine::terrainHeightMsl(double lat, double lon) const {
    double h = impl_->globe.terrainHeight(lat, lon);
    return std::isnan(h) ? h : h - geoidOffset(lat, lon);
}

std::vector<std::uint64_t> Engine::addContours(const Region& region, double interval, int majorEvery, int resolution, std::uint32_t color,
                                               DistanceUnit unit) {
    resolution = std::clamp(resolution, 16, 1024);
    double w = region.west, e = region.east, s = std::max(region.south, -90.0), n = std::min(region.north, 90.0);
    if (!(e > w) || !(n > s)) return {};
    double aspect = (e - w) * std::cos(0.5 * (s + n) * 3.14159265358979323846 / 180.0) / (n - s);
    int cols = aspect >= 1 ? resolution : std::max(16, static_cast<int>(resolution * aspect));
    int rows = aspect >= 1 ? std::max(16, static_cast<int>(resolution / aspect)) : resolution;
    std::vector<float> heights(static_cast<size_t>(cols) * rows);
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i)
            heights[static_cast<size_t>(j) * cols + i] =
                static_cast<float>(terrainHeightMsl(s + (n - s) * j / (rows - 1), w + (e - w) * i / (cols - 1)));
    ShapeStyle style;
    style.stroke = color;
    style.width = 1.5f;
    style.label = color | 0xFF000000u;
    std::vector<std::uint64_t> ids;
    for (Feature& f : contourLines(heights, cols, rows, w, s, e, n, interval, majorEvery, style, unit)) ids.push_back(addFeature(f));
    return ids;
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

std::int64_t Engine::exportTiles(int layer, const Region& region, int minZoom, int maxZoom, const std::string& path,
                                 std::string* error) const {
    std::string e;
    std::int64_t n = impl_->globe.loader().exportTiles(layer, region, minZoom, maxZoom, path, e);
    if (error) *error = e;
    return n;
}

std::int64_t Engine::importTiles(int layer, const std::string& path, std::string* error) {
    std::string e;
    std::int64_t n = impl_->globe.loader().importTiles(layer, path, e);
    if (error) *error = e;
    if (n > 0) {
        impl_->globe.retryUnavailable();
        impl_->dirty = true;
    }
    return n;
}

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

void Engine::setDiagnostics(bool enabled) { impl_->globe.setDiagnostics(enabled); }

double Engine::elevationAt(double lat, double lon) const { return impl_->globe.elevationAt(lat, lon); }

std::vector<double> Engine::queryElevation(const std::vector<Coord>& points, double resolutionMeters, std::vector<double>* resolutions) const {
    std::vector<std::pair<double, double>> ll;
    ll.reserve(points.size());
    for (const Coord& c : points) ll.emplace_back(c.lat, c.lon);
    return impl_->globe.queryElevation(ll, resolutionMeters, resolutions);
}

void Engine::setImageryScale(double scale) {
    impl_->globe.setImageryScale(scale);
    impl_->dirty = true;
}

void Engine::setLodMorph(double fraction) {
    impl_->globe.setLodMorph(fraction);
    impl_->dirty = true;
}

double Engine::lodMorph() const { return impl_->globe.lodMorph(); }

View Engine::view() const {
    const Camera& c = impl_->camera;
    return {c.focus.lat, c.focus.lon, c.focus.alt, c.gsd, c.azimuth, c.tilt, c.holdAltitude};
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
    c.holdAltitude = v.holdAltitude;
    c.update();
    impl_->dirty = true;
}

bool Engine::pick(double sx, double sy, double& lat, double& lon) const {
    double alt = 0;
    return pick(sx, sy, lat, lon, alt);
}

bool Engine::pick(double sx, double sy, double& lat, double& lon, double& alt) const {
    Vec3 p;
    if (!impl_->pickGround(sx, sy, p)) return false;
    GeoPoint g = ecefToGeodetic(p);
    lat = g.lat;
    lon = g.lon;
    alt = g.alt;
    return true;
}

bool Engine::project(double lat, double lon, double alt, double& sx, double& sy) const {
    const Camera& c = impl_->camera;
    Vec3 p = geodeticToEcef(lat, lon, alt);
    Vec3 eye = c.eye();
    if (!c.firstPerson && dot(normalize(p), normalize(eye - p)) < -0.05) return false;
    return c.ecefToScreen(p, sx, sy);
}

void Engine::setFieldOfView(double degrees) {
    Camera& c = impl_->camera;
    double range = c.range();
    c.fovY = std::clamp(degrees, 5.0, 120.0) * kDegToRad;
    c.setRange(range);
    c.update();
    impl_->dirty = true;
}

double Engine::fieldOfView() const { return impl_->camera.fovY * kRadToDeg; }

double Engine::range() const { return impl_->camera.range(); }

void Engine::setSkyVisible(bool visible) {
    impl_->globe.setSkyVisible(visible);
    impl_->dirty = true;
}

void Engine::setBackgroundColor(std::uint32_t argb) {
    impl_->globe.setBackground(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f);
    impl_->dirty = true;
}

void Engine::setGroundColor(std::uint32_t argb) {
    impl_->globe.setGroundColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f);
    impl_->dirty = true;
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
    double lonCap = 180.0 * std::hypot(x1 - x0, y1 - y0) / std::max(static_cast<double>(std::min(c.width, c.height)), 1.0);
    Vec3 p0, p1;
    impl_->dragPoints(x0, y0, x1, y1, p0, p1);
    if (!impl_->headingLock) {
        impl_->moveCamera(p1, p0, lonCap);
    } else {
        Vec3 e, n, u;
        enuFrame(c.focus.lat, c.focus.lon, e, n, u);
        Vec3 move = p0 - p1;
        impl_->shiftFocus(dot(move, e), dot(move, n), lonCap);
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
    bool anchored = impl_->groundAnchor(sx, sy, c.range() * 2.0, before);
    double reach = anchored ? length(before - c.focusEcef()) * std::abs(1.0 - factor) * 1.5 + 1.0 : 0.0;
    double shiftPx = std::hypot(sx - c.width * 0.5, sy - c.height * 0.5) * std::abs(1.0 - 1.0 / factor);
    double lonCap = 180.0 * shiftPx / std::max(static_cast<double>(std::min(c.width, c.height)), 1.0);
    c.gsd *= factor;
    c.update();
    if (anchored) {
        Vec3 after = impl_->onPlane(sx, sy, before, impl_->upAt(before), impl_->grabLimit() * 2.0);
        Vec3 move = before - after;
        double len = length(move);
        if (len > reach) {
            move = move * (reach / len);
            after = before - move;
        }
        if (impl_->headingLock) {
            Vec3 e, n, u;
            enuFrame(c.focus.lat, c.focus.lon, e, n, u);
            impl_->shiftFocus(dot(move, e), dot(move, n), lonCap);
        } else {
            impl_->moveCamera(after, before, lonCap);
        }
    }
    impl_->dirty = true;
}

void Engine::rotateAt(double sx, double sy, double degrees) {
    Camera& c = impl_->camera;
    if (c.firstPerson) {
        look(-degrees, 0);
        return;
    }
    Vec3 before;
    bool anchored = impl_->groundAnchor(sx, sy, c.range() * 2.0, before);
    double reach = anchored ? length(before - c.focusEcef()) * 2.0 * std::abs(std::sin(degrees * kDegToRad * 0.5)) * 1.5 + 1.0 : 0.0;
    c.azimuth += degrees;
    c.update();
    if (anchored) {
        Vec3 after = impl_->onPlane(sx, sy, before, impl_->upAt(before), impl_->grabLimit() * 2.0);
        Vec3 e, n, u;
        enuFrame(c.focus.lat, c.focus.lon, e, n, u);
        Vec3 move = before - after;
        double len = length(move);
        if (len > reach) move = move * (reach / len);
        impl_->shiftFocus(dot(move, e), dot(move, n), 30.0);
    }
    impl_->dirty = true;
}

void Engine::setHeadingLockedPan(bool enabled) { impl_->headingLock = enabled; }

InputController& Engine::input() { return *impl_->input; }

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
    m.camera.holdAltitude = false;
    m.dirty = true;
}

bool Engine::animating() const { return impl_->flying; }

void Engine::stopAnimation() { impl_->flying = false; }

void Engine::setTiltLimits(double maxTiltDegrees, bool limitByRange) {
    impl_->camera.maxTilt = std::clamp(maxTiltDegrees, 0.0, 89.9);
    impl_->camera.rangeTiltLimit = limitByRange;
    impl_->camera.update();
    impl_->dirty = true;
}

void Engine::setCacheEnabled(bool enabled) { impl_->cacheDisabled = !enabled; }

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
    impl_->input->update();
    impl_->animate();
    impl_->followTerrain();
    impl_->globe.render(impl_->camera, &swapchain, nullptr, swapchain.width, swapchain.height);
    impl_->sanity();
    impl_->dirty = false;
    impl_->globe.setCpuMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count());
}

void Engine::setXrAnchor(const XrAnchor& anchor) {
    impl_->xr = anchor;
    if (!(impl_->xr.scale > 0)) impl_->xr.scale = 1.0;
    impl_->dirty = true;
}

XrAnchor Engine::xrAnchor() const { return impl_->xr; }

void Engine::frameXr(const XrView* views, const sg_swapchain* targets, int count) {
    if (!views || !targets || count <= 0) return;
    impl_->input->update();
    const XrAnchor& a = impl_->xr;
    Vec3 origin = geodeticToEcef(a.lat, a.lon, a.alt);
    Vec3 e, n, u;
    enuFrame(a.lat, a.lon, e, n, u);
    double h = a.heading * kDegToRad;
    Vec3 forward = n * std::cos(h) + e * std::sin(h), right = e * std::cos(h) - n * std::sin(h);
    auto toWorld = [&](Vec3 v) { return right * v.x + u * v.y - forward * v.z; };
    auto rotate = [](const double q[4], Vec3 v) {
        Vec3 qv{q[0], q[1], q[2]};
        Vec3 t = cross(qv, v) * 2.0;
        return v + t * q[3] + cross(qv, t);
    };
    std::vector<Camera> cams(static_cast<size_t>(count), impl_->camera);
    std::vector<RenderView> rv(static_cast<size_t>(count));
    Vec3 center;
    double tl = 0, tr = 0, tu = 0, td = 0;
    int maxW = 1, maxH = 1;
    for (int i = 0; i < count; ++i) {
        const XrView& v = views[i];
        Vec3 p = origin + toWorld({v.position[0], v.position[1], v.position[2]}) * a.scale;
        double l = std::tan(v.angleLeft), r = std::tan(v.angleRight), up = std::tan(v.angleUp), dn = std::tan(v.angleDown);
        cams[i].setPose(p, toWorld(rotate(v.orientation, {1, 0, 0})), toWorld(rotate(v.orientation, {0, 1, 0})),
                        toWorld(rotate(v.orientation, {0, 0, 1})), l, r, up, dn);
        cams[i].width = std::max(1, v.width > 0 ? v.width : targets[i].width);
        cams[i].height = std::max(1, v.height > 0 ? v.height : targets[i].height);
        maxW = std::max(maxW, cams[i].width);
        maxH = std::max(maxH, cams[i].height);
        center = center + p;
        tl = i == 0 ? l : std::min(tl, l);
        tr = i == 0 ? r : std::max(tr, r);
        tu = i == 0 ? up : std::max(tu, up);
        td = i == 0 ? dn : std::min(td, dn);
        bool sameTarget = i > 0 && targets[i].d3d11.render_view == targets[i - 1].d3d11.render_view &&
                          targets[i].gl.framebuffer == targets[i - 1].gl.framebuffer &&
                          targets[i].metal.current_drawable == targets[i - 1].metal.current_drawable;
        rv[i] = {&cams[i], &targets[i], nullptr, v.x, v.y, v.width, v.height, !sameTarget};
    }
    Camera select = impl_->camera;
    const double grow = 1.1;
    select.setPose(center * (1.0 / count), toWorld(rotate(views[0].orientation, {1, 0, 0})), toWorld(rotate(views[0].orientation, {0, 1, 0})),
                   toWorld(rotate(views[0].orientation, {0, 0, 1})), tl * grow, tr * grow, tu * grow, td * grow);
    impl_->globe.render(select, nullptr, nullptr, maxW, maxH, &rv);
    impl_->dirty = false;
}

void Engine::Impl::renderStereo(const sg_swapchain* swapchain, const sg_attachments* attachments, int width, int height, double separation) {
    int half = std::max(1, width / 2);
    camera.width = half;
    camera.height = std::max(1, height);
    sg_backend backend = sg_query_backend();
    camera.zeroToOneDepth = backend != SG_BACKEND_GLCORE && backend != SG_BACKEND_GLES3;
    camera.update();
    double sep = separation > 0 ? separation : camera.eyeAboveGround() / 30.0;
    double converge = std::max(length(camera.focusEcef() - camera.eye()), 1.0);
    double ty = std::tan(camera.fovY * 0.5), tx = ty * half / camera.height;
    std::vector<Camera> cams(2, camera);
    std::vector<RenderView> rv(2);
    for (int i = 0; i < 2; ++i) {
        double offset = (i == 0 ? -0.5 : 0.5) * sep;
        double shift = offset / converge;
        cams[i].setPose(camera.eye() + camera.axisRight() * offset, camera.axisRight(), camera.axisUp(), camera.axisBack(), -tx - shift, tx - shift,
                        ty, -ty);
        cams[i].width = half;
        cams[i].height = camera.height;
        rv[i] = {&cams[i], swapchain, attachments, i * half, 0, half, camera.height, i == 0};
    }
    globe.render(camera, nullptr, nullptr, half, camera.height, &rv);
}

void Engine::frameStereo(const sg_swapchain& swapchain, double eyeSeparation) {
    impl_->input->update();
    impl_->animate();
    impl_->followTerrain();
    impl_->renderStereo(&swapchain, nullptr, swapchain.width, swapchain.height, eyeSeparation);
    impl_->sanity();
    impl_->dirty = false;
}

void Engine::frameStereoOffscreen(const sg_attachments& attachments, int width, int height, double eyeSeparation) {
    impl_->input->update();
    impl_->animate();
    impl_->followTerrain();
    impl_->renderStereo(nullptr, &attachments, width, height, eyeSeparation);
    impl_->sanity();
    impl_->dirty = false;
}

void Engine::frameOffscreen(const sg_attachments& attachments, int width, int height) {
    impl_->input->update();
    impl_->animate();
    impl_->followTerrain();
    impl_->globe.render(impl_->camera, nullptr, &attachments, width, height);
    impl_->sanity();
    impl_->dirty = false;
}

bool Engine::needsRedraw() const {
    return impl_->dirty || impl_->flying || impl_->globe.busy() || impl_->input->active() || impl_->globe.reachabilityChanged();
}

Stats Engine::stats() const { return impl_->globe.stats(); }

void setAndroidJavaVm(void* vm) { net::setJavaVm(vm); }

double geoidOffset(double lat, double lon) { return geoidHeight(lat, lon); }

std::string defaultCacheDirectory() {
#if defined(_WIN32)
    if (const char* la = std::getenv("LOCALAPPDATA"); la && *la) return std::string(la) + "/OpenTAKEngine";
#elif defined(__ANDROID__)
    return {};
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) return std::string(home) + "/Library/Application Support/OpenTAKEngine";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::string(xdg) + "/OpenTAKEngine";
    if (const char* home = std::getenv("HOME"); home && *home) return std::string(home) + "/.cache/OpenTAKEngine";
#endif
    return {};
}

void configureGfx(sg_desc& desc) {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    desc.buffer_pool_size = 6144;
    desc.image_pool_size = 4096;
    desc.view_pool_size = 40960;
#else
    desc.buffer_pool_size = 12288;
    desc.image_pool_size = 6144;
    desc.view_pool_size = 65536;
#endif
    if (const char* v = std::getenv("OTE_GFX_BUFFERS")) desc.buffer_pool_size = std::max(64, std::atoi(v));
    if (const char* v = std::getenv("OTE_GFX_IMAGES")) desc.image_pool_size = std::max(64, std::atoi(v));
    if (const char* v = std::getenv("OTE_GFX_VIEWS")) desc.view_pool_size = std::max(256, std::atoi(v));
    desc.sampler_pool_size = 64;
    desc.shader_pool_size = 64;
    desc.pipeline_pool_size = 128;
}

std::vector<ElevationLayer> defaultElevation() {
    ElevationLayer t;
    if (webpSupported()) {
        t.name = "Mapterhorn";
        t.url = "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp";
        t.tileSize = 512;
        t.maxZoom = 16;
    } else {
        t.name = "Terrain Tiles";
        t.url = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";
        t.maxZoom = 15;
    }
    return {t};
}

std::vector<ImageryLayer> defaultLayers() {
    ImageryLayer imagery;
    imagery.name = "World Imagery";
    imagery.url = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}";
    imagery.maxZoom = 19;
    return {imagery, polarFillLayer()};
}

ImageryLayer polarFillLayer() {
    ImageryLayer polar;
    polar.name = "NASA Blue Marble Shaded Relief and Bathymetry (polar fill)";
    polar.url = "https://gibs.earthdata.nasa.gov/wmts/epsg4326/best/BlueMarble_ShadedRelief_Bathymetry/default/500m/{z}/{y}/{x}.jpeg";
    polar.srid = 4326;
    polar.tileSize = 512;
    polar.level0Resolution = 0.5625;
    polar.maxZoom = 7;
    polar.polarFill = true;
    const float north[3] = {1.79f, 2.27f, 1.26f}, south[3] = {1.02f, 1.03f, 1.075f};
    for (int i = 0; i < 3; ++i) {
        polar.polarTintNorth[i] = north[i];
        polar.polarTintSouth[i] = south[i];
    }
    return polar;
}

}
