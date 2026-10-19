#pragma once

#include "core/math.hpp"

namespace ote {

constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kWgs84B = kWgs84A * (1.0 - kWgs84F);
constexpr double kWgs84E2 = kWgs84F * (2.0 - kWgs84F);
constexpr double kMercatorMaxLat = 85.05112877980659;

struct GeoPoint {
    double lat = 0;
    double lon = 0;
    double alt = 0;
};

Vec3 geodeticToEcef(double latDeg, double lonDeg, double h);
inline Vec3 geodeticToEcef(const GeoPoint& p) { return geodeticToEcef(p.lat, p.lon, p.alt); }
GeoPoint ecefToGeodetic(Vec3 p);
void enuFrame(double latDeg, double lonDeg, Vec3& east, Vec3& north, Vec3& up);
bool rayEllipsoid(Vec3 origin, Vec3 dir, double h, double& t);
Vec3 rotateAxis(Vec3 v, Vec3 axis, double angle);
double wrapLon(double lon);

inline double mercatorY(double latDeg) {
    double l = latDeg * kDegToRad;
    return std::log(std::tan(kPi * 0.25 + l * 0.5));
}

inline double inverseMercatorY(double y) {
    return (2.0 * std::atan(std::exp(y)) - kPi * 0.5) * kRadToDeg;
}

}
