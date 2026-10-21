#include "core/geo.hpp"

namespace otk {

Vec3 geodeticToEcef(double latDeg, double lonDeg, double h) {
    double lat = latDeg * kDegToRad;
    double lon = lonDeg * kDegToRad;
    double sl = std::sin(lat), cl = std::cos(lat);
    double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sl * sl);
    return {(n + h) * cl * std::cos(lon), (n + h) * cl * std::sin(lon), (n * (1.0 - kWgs84E2) + h) * sl};
}

GeoPoint ecefToGeodetic(Vec3 p) {
    double lon = std::atan2(p.y, p.x);
    double r = std::sqrt(p.x * p.x + p.y * p.y);
    double ep2 = (kWgs84A * kWgs84A - kWgs84B * kWgs84B) / (kWgs84B * kWgs84B);
    double th = std::atan2(p.z * kWgs84A, r * kWgs84B);
    double st = std::sin(th), ct = std::cos(th);
    double lat = std::atan2(p.z + ep2 * kWgs84B * st * st * st, r - kWgs84E2 * kWgs84A * ct * ct * ct);
    double sl = std::sin(lat);
    double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sl * sl);
    double cl = std::cos(lat);
    double h = std::abs(cl) > 1e-10 ? r / cl - n : std::abs(p.z) - kWgs84B;
    return {lat * kRadToDeg, lon * kRadToDeg, h};
}

void enuFrame(double latDeg, double lonDeg, Vec3& east, Vec3& north, Vec3& up) {
    double lat = latDeg * kDegToRad, lon = lonDeg * kDegToRad;
    double sl = std::sin(lat), cl = std::cos(lat), so = std::sin(lon), co = std::cos(lon);
    east = {-so, co, 0};
    north = {-sl * co, -sl * so, cl};
    up = {cl * co, cl * so, sl};
}

bool rayEllipsoid(Vec3 o, Vec3 d, double h, double& t) {
    double a = kWgs84A + h, b = kWgs84B + h;
    Vec3 os{o.x / a, o.y / a, o.z / b};
    Vec3 ds{d.x / a, d.y / a, d.z / b};
    double qa = dot(ds, ds), qb = 2.0 * dot(os, ds), qc = dot(os, os) - 1.0;
    double disc = qb * qb - 4.0 * qa * qc;
    if (disc < 0) return false;
    double sq = std::sqrt(disc);
    double t0 = (-qb - sq) / (2.0 * qa);
    double t1 = (-qb + sq) / (2.0 * qa);
    if (t0 > 0) {
        t = t0;
        return true;
    }
    if (t1 > 0) {
        t = t1;
        return true;
    }
    return false;
}

Vec3 rotateAxis(Vec3 v, Vec3 axis, double angle) {
    Vec3 k = normalize(axis);
    double c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0 - c));
}

double wrapLon(double lon) {
    while (lon > 180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;
    return lon;
}

}
