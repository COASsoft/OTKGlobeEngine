#include "core/astro.hpp"

#include "core/geo.hpp"
#include "ote/astro.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace ote {

namespace {

constexpr double kAuKm = 149597870.7;
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kMoonRadiusKm = 1737.4;
constexpr double kMeanMoonDistanceKm = 384400.0;

double rad(double d) { return d * kDegToRad; }
double deg(double r) { return r * kRadToDeg; }
double wrap360(double d) {
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}

double julianDay(double unixSeconds) { return unixSeconds / 86400.0 + 2440587.5; }

double obliquity(double t) {
    double eps0 = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    return eps0 + 0.00256 * std::cos(rad(125.04 - 1934.136 * t));
}

double siderealDeg(double jd) {
    double t = (jd - 2451545.0) / 36525.0;
    return wrap360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - t * t * t / 38710000.0);
}

Vec3 equatorialToEcef(double raDeg, double decDeg, double distanceKm, double jd) {
    double cd = std::cos(rad(decDeg));
    Vec3 inertial{distanceKm * cd * std::cos(rad(raDeg)), distanceKm * cd * std::sin(rad(raDeg)), distanceKm * std::sin(rad(decDeg))};
    double th = rad(siderealDeg(jd));
    return {inertial.x * std::cos(th) + inertial.y * std::sin(th), -inertial.x * std::sin(th) + inertial.y * std::cos(th), inertial.z};
}

Vec3 eclipticToEcef(double lambdaDeg, double betaDeg, double distanceKm, double jd) {
    double t = (jd - 2451545.0) / 36525.0;
    double eps = rad(obliquity(t));
    double l = rad(lambdaDeg), b = rad(betaDeg);
    double ra = std::atan2(std::sin(l) * std::cos(eps) - std::tan(b) * std::sin(eps), std::cos(l));
    double dec = std::asin(std::sin(b) * std::cos(eps) + std::cos(b) * std::sin(eps) * std::sin(l));
    return equatorialToEcef(deg(ra), deg(dec), distanceKm, jd);
}

struct Body {
    Vec3 ecefKm;
    double lambda = 0.0;
};

Body sun(double jd) {
    double t = (jd - 2451545.0) / 36525.0;
    double l0 = wrap360(280.46646 + t * (36000.76983 + 0.0003032 * t));
    double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    double c = std::sin(rad(m)) * (1.914602 - t * (0.004817 + 0.000014 * t)) + std::sin(rad(2 * m)) * (0.019993 - 0.000101 * t) +
               std::sin(rad(3 * m)) * 0.000289;
    double v = m + c;
    double r = 1.000001018 * (1 - e * e) / (1 + e * std::cos(rad(v)));
    double lambda = l0 + c - 0.00569 - 0.00478 * std::sin(rad(125.04 - 1934.136 * t));
    return {eclipticToEcef(lambda, 0.0, r * kAuKm, jd), wrap360(lambda)};
}

Body moon(double jd) {
    double t = (jd - 2451545.0) / 36525.0;
    double lambda = 218.32 + 481267.881 * t + 6.29 * std::sin(rad(135.0 + 477198.87 * t)) - 1.27 * std::sin(rad(259.3 - 413335.36 * t)) +
                    0.66 * std::sin(rad(235.7 + 890534.22 * t)) + 0.21 * std::sin(rad(269.9 + 954397.74 * t)) -
                    0.19 * std::sin(rad(357.5 + 35999.05 * t)) - 0.11 * std::sin(rad(186.5 + 966404.03 * t));
    double beta = 5.13 * std::sin(rad(93.3 + 483202.02 * t)) + 0.28 * std::sin(rad(228.2 + 960400.89 * t)) -
                  0.28 * std::sin(rad(318.3 + 6003.15 * t)) - 0.17 * std::sin(rad(217.6 - 407332.21 * t));
    double parallax = 0.9508 + 0.0518 * std::cos(rad(135.0 + 477198.87 * t)) + 0.0095 * std::cos(rad(259.3 - 413335.36 * t)) +
                      0.0078 * std::cos(rad(235.7 + 890534.22 * t)) + 0.0028 * std::cos(rad(269.9 + 954397.74 * t));
    double distance = kEarthRadiusKm / std::sin(rad(parallax));
    return {eclipticToEcef(lambda, beta, distance, jd), wrap360(lambda)};
}

CelestialPosition topocentric(const Vec3& bodyKm, double lat, double lon, double heightM) {
    Vec3 observer = geodeticToEcef(lat, lon, heightM) * 0.001;
    Vec3 rel = bodyKm - observer;
    Vec3 e, n, u;
    enuFrame(lat, lon, e, n, u);
    double dist = length(rel);
    CelestialPosition p;
    p.distanceKm = dist;
    p.elevation = deg(std::asin(std::clamp(dot(rel, u) / dist, -1.0, 1.0)));
    p.azimuth = wrap360(deg(std::atan2(dot(rel, e), dot(rel, n))));
    double h = p.elevation;
    p.apparentElevation = h > -1.0 ? h + 1.02 / std::tan(rad(h + 10.3 / (h + 5.11))) / 60.0 : h;
    return p;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double poly(double x, double a, double b, double c, double d) { return a + x * (b + x * (c + x * d)); }

double sunLux(double h) {
    double x = h / 90.0;
    if (h >= 20) return std::pow(10.0, poly(x, 3.74, 3.97, -4.07, 1.47));
    if (h >= 5) return std::pow(10.0, poly(x, 3.05, 13.28, -45.98, 64.33));
    if (h >= -0.8) return std::pow(10.0, poly(x, 2.88, 22.26, -207.64, 1034.30));
    if (h >= -5) return std::pow(10.0, poly(x, 2.88, 21.81, -258.11, -858.36));
    if (h >= -12) return std::pow(10.0, poly(x, 2.70, 12.17, -431.69, -1899.83));
    if (h >= -18) return std::pow(10.0, poly(x, 13.84, 262.72, 1447.42, 2797.93));
    return 0.0;
}

double moonLux(double h, double phaseAngle, double distanceKm) {
    if (h < -0.8) return 0.0;
    double x = h / 90.0;
    double base = h >= 20 ? poly(x, -1.95, 4.06, -4.24, 1.56) : h >= 5 ? poly(x, -2.58, 12.58, -42.58, 59.06) : poly(x, -2.79, 24.27, -252.95, 1321.29);
    double phase = -8.68e-3 * phaseAngle - 2.2e-9 * std::pow(phaseAngle, 4.0);
    double ratio = kMeanMoonDistanceKm / distanceKm;
    return std::pow(10.0, base + phase) * ratio * ratio;
}

double moonThreshold(double distanceKm) { return -0.5667 - deg(std::asin(kMoonRadiusKm / distanceKm)); }

}

Vec3 sunDirectionEcef(double unixSeconds) {
    if (unixSeconds <= 0) unixSeconds = nowSeconds();
    return normalize(sun(julianDay(unixSeconds)).ecefKm);
}

CelestialPosition sunPosition(double unixSeconds, double lat, double lon, double heightM) {
    return topocentric(sun(julianDay(unixSeconds)).ecefKm, lat, lon, heightM);
}

MoonInfo moonPosition(double unixSeconds, double lat, double lon, double heightM) {
    double jd = julianDay(unixSeconds);
    Body s = sun(jd), m = moon(jd);
    MoonInfo info;
    info.position = topocentric(m.ecefKm, lat, lon, heightM);
    double rs = length(s.ecefKm), rm = length(m.ecefKm);
    double psi = std::acos(std::clamp(dot(s.ecefKm, m.ecefKm) / (rs * rm), -1.0, 1.0));
    double i = std::atan2(rs * std::sin(psi), rm - rs * std::cos(psi));
    info.phaseAngle = deg(i);
    info.illuminatedFraction = (1.0 + std::cos(i)) * 0.5;
    info.waxing = wrap360(m.lambda - s.lambda) < 180.0;
    return info;
}

Illuminance groundIlluminance(double unixSeconds, double lat, double lon) {
    Illuminance out;
    out.sunLux = sunLux(sunPosition(unixSeconds, lat, lon).elevation);
    MoonInfo m = moonPosition(unixSeconds, lat, lon);
    out.moonLux = moonLux(m.position.elevation, m.phaseAngle, m.position.distanceKm);
    out.totalLux = out.sunLux + out.moonLux;
    return out;
}

SunMoonTimes sunMoonTimes(double start, double lat, double lon, double hours) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    SunMoonTimes out;
    double* rising[] = {&out.astronomicalDawn, &out.nauticalDawn, &out.civilDawn, &out.sunrise};
    double* setting[] = {&out.astronomicalDusk, &out.nauticalDusk, &out.civilDusk, &out.sunset};
    const double sunLevels[] = {-18.0, -12.0, -6.0, -0.833};
    for (int k = 0; k < 4; ++k) *rising[k] = *setting[k] = nan;
    out.moonrise = out.moonset = nan;
    auto sunAlt = [&](double t) { return sunPosition(t, lat, lon).elevation; };
    auto moonAlt = [&](double t) {
        MoonInfo m = moonPosition(t, lat, lon);
        return m.position.elevation - moonThreshold(m.position.distanceKm);
    };
    auto search = [&](auto&& f, double level, double& up, double& down) {
        const double step = 600.0, end = start + hours * 3600.0;
        double t0 = start, f0 = f(t0) - level;
        while (t0 < end && (std::isnan(up) || std::isnan(down))) {
            double t1 = std::min(t0 + step, end), f1 = f(t1) - level;
            if ((f0 < 0) != (f1 < 0)) {
                double a = t0, b = t1, fa = f0;
                for (int i = 0; i < 40 && b - a > 0.5; ++i) {
                    double mid = 0.5 * (a + b), fm = f(mid) - level;
                    if ((fa < 0) == (fm < 0)) {
                        a = mid;
                        fa = fm;
                    } else {
                        b = mid;
                    }
                }
                double when = 0.5 * (a + b);
                if (f0 < 0 && std::isnan(up)) up = when;
                if (f0 >= 0 && std::isnan(down)) down = when;
            }
            t0 = t1;
            f0 = f1;
        }
    };
    for (int k = 0; k < 4; ++k) search(sunAlt, sunLevels[k], *rising[k], *setting[k]);
    search(moonAlt, 0.0, out.moonrise, out.moonset);
    return out;
}

}
