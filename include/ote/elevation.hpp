#pragma once

#include "ote/features.hpp"
#include "ote/layers.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ote {

struct LineOfSight {
    bool visible = true;
    Coord obstruction;
    double distance = 0;
};

struct ProfileSample {
    double distance = 0;
    double lat = 0;
    double lon = 0;
    double elevation = 0;
};

class ElevationService {
public:
    explicit ElevationService(int threads = 4);
    ~ElevationService();
    ElevationService(const ElevationService&) = delete;
    ElevationService& operator=(const ElevationService&) = delete;

    bool setCacheDirectory(const std::string& directory);
    void setOffline(bool offline);
    void setSources(const std::vector<ElevationLayer>& layers);
    std::vector<ElevationLayer> sources() const;

    std::vector<double> query(const std::vector<Coord>& points, double resolutionMeters = 0.0,
                              std::vector<double>* resolutions = nullptr) const;
    double heightAt(double lat, double lon, double resolutionMeters = 0.0) const;
    double heightMsl(double lat, double lon, double resolutionMeters = 0.0) const;
    LineOfSight lineOfSight(const Coord& from, double fromHeight, const Coord& to, double toHeight, double resolutionMeters = 0.0) const;
    std::vector<ProfileSample> profile(const std::vector<Coord>& path, int samples, double resolutionMeters = 0.0) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
