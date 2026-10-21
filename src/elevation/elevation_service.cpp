#include "ote/elevation.hpp"

#include "core/geo.hpp"
#include "core/geoid.hpp"
#include "elevation/elevation.hpp"
#include "raster/tile_cache.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>

namespace ote {

struct ElevationService::Impl {
    explicit Impl(int threads) : manager(std::max(threads, 1)) {}
    ElevationManager manager;
    TileCache cache;
    std::atomic<bool> offline{false};
    std::vector<ElevationLayer> layers;
    mutable std::mutex mutex;
};

ElevationService::ElevationService(int threads) : impl_(std::make_unique<Impl>(threads)) {}

ElevationService::~ElevationService() = default;

bool ElevationService::setCacheDirectory(const std::string& directory) {
    std::lock_guard lock(impl_->mutex);
    std::string path = directory;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    if (!impl_->cache.open(path + "tiles.sqlite")) return false;
    impl_->manager.setSources(impl_->layers, &impl_->cache, &impl_->offline);
    return true;
}

void ElevationService::setOffline(bool offline) { impl_->offline = offline; }

void ElevationService::setSources(const std::vector<ElevationLayer>& layers) {
    std::lock_guard lock(impl_->mutex);
    impl_->layers = layers;
    impl_->manager.setSources(layers, impl_->cache.isOpen() ? &impl_->cache : nullptr, &impl_->offline);
}

std::vector<ElevationLayer> ElevationService::sources() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->layers;
}

std::vector<double> ElevationService::query(const std::vector<Coord>& points, double resolutionMeters, std::vector<double>* resolutions) const {
    std::vector<std::pair<double, double>> ll;
    ll.reserve(points.size());
    for (const Coord& c : points) ll.emplace_back(c.lat, c.lon);
    return impl_->manager.query(ll, resolutionMeters, resolutions);
}

double ElevationService::heightAt(double lat, double lon, double resolutionMeters) const {
    std::vector<double> h = query({{lat, lon, 0}}, resolutionMeters);
    return h.empty() ? std::numeric_limits<double>::quiet_NaN() : h[0];
}

double ElevationService::heightMsl(double lat, double lon, double resolutionMeters) const {
    double h = heightAt(lat, lon, resolutionMeters);
    return std::isnan(h) ? h : h - geoidHeight(lat, lon);
}

LineOfSight ElevationService::lineOfSight(const Coord& from, double fromHeight, const Coord& to, double toHeight, double resolutionMeters) const {
    LineOfSight r;
    std::vector<double> ends = query({from, to}, resolutionMeters);
    const double h0 = ends.size() == 2 && !std::isnan(ends[0]) ? ends[0] : 0.0;
    const double h1 = ends.size() == 2 && !std::isnan(ends[1]) ? ends[1] : 0.0;
    Vec3 a = geodeticToEcef(from.lat, from.lon, h0 + fromHeight);
    Vec3 b = geodeticToEcef(to.lat, to.lon, h1 + toHeight);
    const double d = length(b - a);
    r.distance = d;
    const int n = static_cast<int>(std::clamp(d / 20.0, 32.0, 6000.0));
    std::vector<GeoPoint> ray;
    std::vector<Coord> ground;
    ray.reserve(static_cast<size_t>(n));
    ground.reserve(static_cast<size_t>(n));
    for (int i = 1; i < n; ++i) {
        GeoPoint p = ecefToGeodetic(a + (b - a) * (static_cast<double>(i) / n));
        ray.push_back(p);
        ground.push_back({p.lat, p.lon, 0});
    }
    std::vector<double> heights = query(ground, resolutionMeters);
    for (size_t i = 0; i < ray.size() && i < heights.size(); ++i) {
        if (std::isnan(heights[i]) || ray[i].alt >= heights[i]) continue;
        r.visible = false;
        r.obstruction = {ray[i].lat, ray[i].lon, heights[i]};
        r.distance = d * static_cast<double>(i + 1) / n;
        break;
    }
    return r;
}

std::vector<ProfileSample> ElevationService::profile(const std::vector<Coord>& path, int samples, double resolutionMeters) const {
    std::vector<ProfileSample> out;
    if (path.size() < 2 || samples < 2) return out;
    std::vector<double> cumulative{0.0};
    for (size_t i = 1; i < path.size(); ++i)
        cumulative.push_back(cumulative.back() +
                             length(geodeticToEcef(path[i].lat, path[i].lon, 0) - geodeticToEcef(path[i - 1].lat, path[i - 1].lon, 0)));
    const double total = cumulative.back();
    std::vector<Coord> points;
    size_t seg = 1;
    for (int k = 0; k < samples; ++k) {
        double d = total * k / (samples - 1);
        while (seg + 1 < cumulative.size() && cumulative[seg] < d) ++seg;
        double len = cumulative[seg] - cumulative[seg - 1];
        double t = len > 0 ? (d - cumulative[seg - 1]) / len : 0;
        Vec3 a = geodeticToEcef(path[seg - 1].lat, path[seg - 1].lon, 0), b = geodeticToEcef(path[seg].lat, path[seg].lon, 0);
        GeoPoint g = ecefToGeodetic(a + (b - a) * t);
        out.push_back({d, g.lat, g.lon, 0.0});
        points.push_back({g.lat, g.lon, 0});
    }
    std::vector<double> heights = query(points, resolutionMeters);
    for (size_t i = 0; i < out.size() && i < heights.size(); ++i) out[i].elevation = heights[i];
    return out;
}

}
