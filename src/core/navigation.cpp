#include "otk/navigation.hpp"

#include "otk/mgrs.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>

namespace otk {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kD2R = kPi / 180.0;
constexpr double kA = 6378137.0;
constexpr double kF = 1.0 / 298.257223563;
constexpr double kB = kA * (1.0 - kF);
constexpr double kAuthalic = 6371007.181;
const char* kGarsLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";

double wrap360(double d) {
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}

double wrap180(double d) {
    d = std::fmod(d + 180.0, 360.0);
    if (d < 0) d += 360.0;
    return d - 180.0;
}

std::string upper(const std::string& s) {
    std::string out;
    for (char c : s) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool polarLetter(char c) { return c == 'A' || c == 'B' || c == 'Y' || c == 'Z'; }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

Feature labelPoint(const Coord& at, const std::string& text, const ShapeStyle& style) {
    Feature f;
    f.name = text;
    f.geometries.push_back({GeometryType::Point, {at}, {}});
    f.style.icon = IconShape::Hidden;
    f.style.labelColor = style.label;
    f.style.showLabel = true;
    return f;
}

Feature lineFeature(std::vector<Coord> pts, const ShapeStyle& style, bool closed) {
    Feature f;
    if (closed && style.fill) {
        f.geometries.push_back({GeometryType::Polygon, std::move(pts), {}});
    } else {
        f.geometries.push_back({GeometryType::LineString, std::move(pts), {}});
    }
    f.style.strokeColor = style.stroke;
    f.style.strokeWidth = style.width;
    f.style.fillColor = style.fill;
    f.style.showLabel = false;
    return f;
}

}

GeodesicLine geodesicInverse(double lat1, double lon1, double lat2, double lon2) {
    GeodesicLine out;
    double L = wrap180(lon2 - lon1) * kD2R;
    double U1 = std::atan((1 - kF) * std::tan(lat1 * kD2R));
    double U2 = std::atan((1 - kF) * std::tan(lat2 * kD2R));
    double sinU1 = std::sin(U1), cosU1 = std::cos(U1), sinU2 = std::sin(U2), cosU2 = std::cos(U2);
    double lambda = L, prev = 0;
    double sinSigma = 0, cosSigma = 0, sigma = 0, cos2Alpha = 0, cos2SigmaM = 0, sinLambda = 0, cosLambda = 0;
    bool converged = false;
    for (int i = 0; i < 200; ++i) {
        sinLambda = std::sin(lambda);
        cosLambda = std::cos(lambda);
        double a = cosU2 * sinLambda, b = cosU1 * sinU2 - sinU1 * cosU2 * cosLambda;
        sinSigma = std::sqrt(a * a + b * b);
        if (sinSigma == 0) return out;
        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = std::atan2(sinSigma, cosSigma);
        double sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cos2Alpha = 1 - sinAlpha * sinAlpha;
        cos2SigmaM = cos2Alpha != 0 ? cosSigma - 2 * sinU1 * sinU2 / cos2Alpha : 0;
        double C = kF / 16 * cos2Alpha * (4 + kF * (4 - 3 * cos2Alpha));
        prev = lambda;
        lambda = L + (1 - C) * kF * sinAlpha * (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma * (-1 + 2 * cos2SigmaM * cos2SigmaM)));
        if (std::abs(lambda - prev) < 1e-12) {
            converged = true;
            break;
        }
    }
    if (!converged) {
        double p1 = lat1 * kD2R, p2 = lat2 * kD2R;
        double h = std::sin((p2 - p1) / 2), w = std::sin(L / 2);
        double c = 2 * std::asin(std::min(1.0, std::sqrt(h * h + std::cos(p1) * std::cos(p2) * w * w)));
        out.distance = c * kAuthalic;
        out.initialBearing = wrap360(std::atan2(std::sin(L) * std::cos(p2), std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(L)) / kD2R);
        out.finalBearing = wrap360(std::atan2(std::sin(L) * std::cos(p1), -std::sin(p1) * std::cos(p2) + std::cos(p1) * std::sin(p2) * std::cos(L)) / kD2R);
        return out;
    }
    double u2 = cos2Alpha * (kA * kA - kB * kB) / (kB * kB);
    double A = 1 + u2 / 16384 * (4096 + u2 * (-768 + u2 * (320 - 175 * u2)));
    double B = u2 / 1024 * (256 + u2 * (-128 + u2 * (74 - 47 * u2)));
    double dSigma = B * sinSigma * (cos2SigmaM + B / 4 * (cosSigma * (-1 + 2 * cos2SigmaM * cos2SigmaM) -
                                                         B / 6 * cos2SigmaM * (-3 + 4 * sinSigma * sinSigma) * (-3 + 4 * cos2SigmaM * cos2SigmaM)));
    out.distance = kB * A * (sigma - dSigma);
    out.initialBearing = wrap360(std::atan2(cosU2 * sinLambda, cosU1 * sinU2 - sinU1 * cosU2 * cosLambda) / kD2R);
    out.finalBearing = wrap360(std::atan2(cosU1 * sinLambda, -sinU1 * cosU2 + cosU1 * sinU2 * cosLambda) / kD2R);
    return out;
}

Coord geodesicDirect(const Coord& from, double bearingDegrees, double distanceMeters, double* finalBearing) {
    double alpha1 = bearingDegrees * kD2R;
    double sinA1 = std::sin(alpha1), cosA1 = std::cos(alpha1);
    double tanU1 = (1 - kF) * std::tan(from.lat * kD2R);
    double cosU1 = 1 / std::sqrt(1 + tanU1 * tanU1), sinU1 = tanU1 * cosU1;
    double sigma1 = std::atan2(tanU1, cosA1);
    double sinAlpha = cosU1 * sinA1;
    double cos2Alpha = 1 - sinAlpha * sinAlpha;
    double u2 = cos2Alpha * (kA * kA - kB * kB) / (kB * kB);
    double A = 1 + u2 / 16384 * (4096 + u2 * (-768 + u2 * (320 - 175 * u2)));
    double B = u2 / 1024 * (256 + u2 * (-128 + u2 * (74 - 47 * u2)));
    double sigma = distanceMeters / (kB * A), prev = 0;
    double cos2SigmaM = 0, sinSigma = 0, cosSigma = 0;
    for (int i = 0; i < 200; ++i) {
        cos2SigmaM = std::cos(2 * sigma1 + sigma);
        sinSigma = std::sin(sigma);
        cosSigma = std::cos(sigma);
        double dSigma = B * sinSigma * (cos2SigmaM + B / 4 * (cosSigma * (-1 + 2 * cos2SigmaM * cos2SigmaM) -
                                                             B / 6 * cos2SigmaM * (-3 + 4 * sinSigma * sinSigma) * (-3 + 4 * cos2SigmaM * cos2SigmaM)));
        prev = sigma;
        sigma = distanceMeters / (kB * A) + dSigma;
        if (std::abs(sigma - prev) < 1e-12) break;
    }
    cos2SigmaM = std::cos(2 * sigma1 + sigma);
    sinSigma = std::sin(sigma);
    cosSigma = std::cos(sigma);
    double tmp = sinU1 * sinSigma - cosU1 * cosSigma * cosA1;
    double lat2 = std::atan2(sinU1 * cosSigma + cosU1 * sinSigma * cosA1, (1 - kF) * std::sqrt(sinAlpha * sinAlpha + tmp * tmp));
    double lambda = std::atan2(sinSigma * sinA1, cosU1 * cosSigma - sinU1 * sinSigma * cosA1);
    double C = kF / 16 * cos2Alpha * (4 + kF * (4 - 3 * cos2Alpha));
    double L = lambda - (1 - C) * kF * sinAlpha * (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma * (-1 + 2 * cos2SigmaM * cos2SigmaM)));
    if (finalBearing) *finalBearing = wrap360(std::atan2(sinAlpha, -tmp) / kD2R);
    return {lat2 / kD2R, wrap180(from.lon + L / kD2R), from.alt};
}

std::vector<Coord> geodesicPath(const Coord& from, const Coord& to, double maxSegmentMeters) {
    GeodesicLine g = geodesicInverse(from.lat, from.lon, to.lat, to.lon);
    int n = std::clamp(static_cast<int>(std::ceil(g.distance / std::max(maxSegmentMeters, 1.0))), 1, 4096);
    std::vector<Coord> out;
    out.reserve(n + 1);
    out.push_back(from);
    for (int i = 1; i < n; ++i) {
        Coord c = geodesicDirect(from, g.initialBearing, g.distance * i / n);
        c.alt = from.alt + (to.alt - from.alt) * i / n;
        out.push_back(c);
    }
    out.push_back(to);
    return out;
}

double geodesicArea(const std::vector<Coord>& ring) {
    if (ring.size() < 3) return 0;
    double sum = 0;
    for (size_t i = 0; i < ring.size(); ++i) {
        const Coord& a = ring[i];
        const Coord& b = ring[(i + 1) % ring.size()];
        sum += wrap180(b.lon - a.lon) * kD2R * (2 + std::sin(a.lat * kD2R) + std::sin(b.lat * kD2R));
    }
    return std::abs(sum) * kAuthalic * kAuthalic / 2;
}

double currentDecimalYear() {
    std::time_t now = std::time(nullptr);
    std::tm t{};
#if defined(_WIN32)
    gmtime_s(&t, &now);
#else
    gmtime_r(&now, &t);
#endif
    int year = t.tm_year + 1900;
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return year + (t.tm_yday + (t.tm_hour + t.tm_min / 60.0) / 24.0) / (leap ? 366.0 : 365.0);
}

double magneticDeclination(double lat, double lon, double altitudeMeters, double decimalYear) {
    return magneticField(lat, lon, altitudeMeters, decimalYear > 0 ? decimalYear : currentDecimalYear()).declination;
}

double gridConvergence(double lat, double lon) {
    if (lat > 84.0) return wrap180(lon);
    if (lat < -80.0) return -wrap180(lon);
    int zone = utmZone(lat, lon);
    double lon0 = (zone - 1) * 6.0 - 180.0 + 3.0;
    double dl = wrap180(lon - lon0) * kD2R, phi = lat * kD2R;
    double s = std::sin(phi), c = std::cos(phi), t = std::tan(phi);
    double ep2 = kF * (2 - kF) / ((1 - kF) * (1 - kF));
    double eta2 = ep2 * c * c;
    double g = dl * s + dl * dl * dl / 3.0 * s * c * c * (1 + 3 * eta2 + 2 * eta2 * eta2) + std::pow(dl, 5) / 15.0 * s * std::pow(c, 4) * (2 - t * t);
    return g / kD2R;
}

double bearingFromTrue(double trueBearing, double lat, double lon, NorthReference reference) {
    switch (reference) {
    case NorthReference::MagneticNorth:
        return wrap360(trueBearing - magneticDeclination(lat, lon));
    case NorthReference::GridNorth:
        return wrap360(trueBearing - gridConvergence(lat, lon));
    default:
        return wrap360(trueBearing);
    }
}

double bearingToTrue(double bearing, double lat, double lon, NorthReference reference) {
    switch (reference) {
    case NorthReference::MagneticNorth:
        return wrap360(bearing + magneticDeclination(lat, lon));
    case NorthReference::GridNorth:
        return wrap360(bearing + gridConvergence(lat, lon));
    default:
        return wrap360(bearing);
    }
}

double degreesToMils(double degrees) { return degrees * 6400.0 / 360.0; }

double milsToDegrees(double mils) { return mils * 360.0 / 6400.0; }

std::string formatBearing(double bearing, AngleUnit unit, NorthReference reference) {
    const char* suffix = reference == NorthReference::MagneticNorth ? "M" : reference == NorthReference::GridNorth ? "G" : "T";
    char buf[48];
    if (unit == AngleUnit::Mils) {
        int mils = static_cast<int>(std::lround(degreesToMils(wrap360(bearing)))) % 6400;
        std::snprintf(buf, sizeof(buf), "%04d mils %s", mils, suffix);
    } else {
        int deg = static_cast<int>(std::lround(wrap360(bearing))) % 360;
        std::snprintf(buf, sizeof(buf), "%03d\xC2\xB0%s", deg, suffix);
    }
    return buf;
}

std::string formatDistance(double meters, DistanceUnit unit) {
    char buf[48];
    if (unit == DistanceUnit::Imperial) {
        double feet = meters / 0.3048;
        if (feet < 5280) std::snprintf(buf, sizeof(buf), "%.0f ft", feet);
        else std::snprintf(buf, sizeof(buf), feet < 52800 ? "%.2f mi" : "%.1f mi", feet / 5280.0);
    } else if (unit == DistanceUnit::Nautical) {
        double nm = meters / 1852.0;
        std::snprintf(buf, sizeof(buf), nm < 10 ? "%.2f NM" : "%.1f NM", nm);
    } else {
        if (meters < 1000) std::snprintf(buf, sizeof(buf), "%.0f m", meters);
        else std::snprintf(buf, sizeof(buf), meters < 10000 ? "%.2f km" : "%.1f km", meters / 1000.0);
    }
    return buf;
}

std::string toGars(double lat, double lon, int precision) {
    lat = std::clamp(lat, -90.0, 90.0);
    lon = wrap180(lon);
    if (lon == 180.0) lon = -180.0;
    int lonIndex = std::min(static_cast<int>(std::floor((lon + 180.0) * 2.0)), 719);
    int latIndex = std::min(static_cast<int>(std::floor((lat + 90.0) * 2.0)), 359);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%03d%c%c", lonIndex + 1, kGarsLetters[latIndex / 24], kGarsLetters[latIndex % 24]);
    std::string out = buf;
    if (precision < 2) return out;
    double lonIn = (lon + 180.0) * 60.0 - lonIndex * 30.0;
    double latIn = (lat + 90.0) * 60.0 - latIndex * 30.0;
    int col = lonIn >= 15.0 ? 1 : 0;
    int row = latIn >= 15.0 ? 0 : 1;
    out += static_cast<char>('1' + row * 2 + col);
    if (precision < 3) return out;
    double lonQ = lonIn - col * 15.0, latQ = latIn - (row == 0 ? 15.0 : 0.0);
    int kc = std::min(static_cast<int>(lonQ / 5.0), 2);
    int kr = 2 - std::min(static_cast<int>(latQ / 5.0), 2);
    out += static_cast<char>('1' + kr * 3 + kc);
    return out;
}

bool fromGars(const std::string& text, double& lat, double& lon) {
    std::string s;
    for (char c : upper(text))
        if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    if (s.size() < 5 || s.size() > 7) return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    int lonIndex = std::stoi(s.substr(0, 3)) - 1;
    const char* a = std::strchr(kGarsLetters, s[3]);
    const char* b = std::strchr(kGarsLetters, s[4]);
    if (lonIndex < 0 || lonIndex > 719 || !a || !b || !*a || !*b) return false;
    int latIndex = static_cast<int>(a - kGarsLetters) * 24 + static_cast<int>(b - kGarsLetters);
    if (latIndex > 359) return false;
    double west = -180.0 + lonIndex * 0.5, south = -90.0 + latIndex * 0.5, size = 0.5;
    if (s.size() >= 6) {
        int q = s[5] - '1';
        if (q < 0 || q > 3) return false;
        size = 0.25;
        west += (q % 2) * size;
        south += (q / 2 == 0 ? 1 : 0) * size;
    }
    if (s.size() == 7) {
        int k = s[6] - '1';
        if (k < 0 || k > 8) return false;
        size = 0.25 / 3.0;
        west += (k % 3) * size;
        south += (2 - k / 3) * size;
    }
    lat = south + size / 2;
    lon = west + size / 2;
    return true;
}

std::string formatCoordinate(double lat, double lon, CoordinateFormat format, int precision) {
    char buf[96];
    char ns = lat >= 0 ? 'N' : 'S', ew = lon >= 0 ? 'E' : 'W';
    double alat = std::abs(lat), alon = std::abs(wrap180(lon));
    switch (format) {
    case CoordinateFormat::Mgrs:
        return toMgrs(lat, lon, precision < 0 ? 5 : precision);
    case CoordinateFormat::Gars:
        return toGars(lat, lon, precision < 0 ? 3 : precision);
    case CoordinateFormat::Utm: {
        if (lat > 84.0 || lat < -80.0) {
            Ups u = toUps(lat, lon);
            std::snprintf(buf, sizeof(buf), "%c %.0fE %.0fN", u.zone, u.easting, u.northing);
            return buf;
        }
        Utm u = toUtm(lat, lon);
        std::snprintf(buf, sizeof(buf), "%d%c %.0fE %.0fN", u.zone, utmBand(lat), std::floor(u.easting), std::floor(u.northing));
        return buf;
    }
    case CoordinateFormat::DegreesMinutes: {
        int p = precision < 0 ? 4 : precision;
        double lm = (alat - std::floor(alat)) * 60.0, om = (alon - std::floor(alon)) * 60.0;
        int ld = static_cast<int>(alat), od = static_cast<int>(alon);
        double scale = std::pow(10.0, p);
        if (std::round(lm * scale) / scale >= 60.0) {
            lm = 0;
            ++ld;
        }
        if (std::round(om * scale) / scale >= 60.0) {
            om = 0;
            ++od;
        }
        std::snprintf(buf, sizeof(buf), "%c %02d\xC2\xB0 %0*.*f' %c %03d\xC2\xB0 %0*.*f'", ns, ld, p + 3, p, lm, ew, od, p + 3, p, om);
        return buf;
    }
    case CoordinateFormat::DegreesMinutesSeconds: {
        int p = precision < 0 ? 2 : precision;
        auto split = [p](double v, int& d, int& m, double& s) {
            double scale = std::pow(10.0, p);
            double total = std::round(v * 3600.0 * scale) / scale;
            d = static_cast<int>(total / 3600.0);
            total -= d * 3600.0;
            m = static_cast<int>(total / 60.0);
            s = total - m * 60.0;
        };
        int ld, lm, od, om;
        double ls, os;
        split(alat, ld, lm, ls);
        split(alon, od, om, os);
        std::snprintf(buf, sizeof(buf), "%c %02d\xC2\xB0 %02d' %0*.*f\" %c %03d\xC2\xB0 %02d' %0*.*f\"", ns, ld, lm, p + 3, p, ls, ew, od, om,
                      p + 3, p, os);
        return buf;
    }
    default: {
        int p = precision < 0 ? 6 : precision;
        std::snprintf(buf, sizeof(buf), "%.*f\xC2\xB0%c %.*f\xC2\xB0%c", p, alat, ns, p, alon, ew);
        return buf;
    }
    }
}

bool parseCoordinate(const std::string& input, double& lat, double& lon) {
    std::string text = upper(trim(input));
    if (text.empty()) return false;
    std::string compact;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) compact += c;
    if (compact.size() >= 5 && compact.size() <= 7 && std::isdigit(static_cast<unsigned char>(compact[0])) &&
        std::isdigit(static_cast<unsigned char>(compact[1])) && std::isdigit(static_cast<unsigned char>(compact[2])) &&
        std::isalpha(static_cast<unsigned char>(compact[3])) && fromGars(compact, lat, lon))
        return true;
    if (fromMgrs(compact, lat, lon)) return true;
    {
        std::vector<std::string> tok;
        std::string cur;
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
                if (!cur.empty()) tok.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) tok.push_back(cur);
        if (tok.size() == 4 && tok[1].size() == 1) {
            tok[0] += tok[1];
            tok.erase(tok.begin() + 1);
        }
        auto number = [](std::string t, double& v) {
            while (!t.empty() && (t.back() == 'E' || t.back() == 'N' || t.back() == 'M')) t.pop_back();
            if (t.empty()) return false;
            for (char c : t)
                if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return false;
            v = std::stod(t);
            return true;
        };
        double e = 0, n = 0;
        if (tok.size() == 3 && tok[0].size() >= 2 && std::isalpha(static_cast<unsigned char>(tok[0].back())) && number(tok[1], e) &&
            number(tok[2], n)) {
            std::string zs = tok[0].substr(0, tok[0].size() - 1);
            char band = tok[0].back();
            bool digits = !zs.empty() && std::all_of(zs.begin(), zs.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
            int zone = digits ? std::stoi(zs) : 0;
            if (zone >= 1 && zone <= 60 && e >= 100000 && e < 1000000 && n >= 0 && n <= 10000000 && band >= 'C' && band <= 'X') {
                Utm u;
                u.zone = zone;
                u.band = band;
                u.north = band >= 'N';
                u.easting = e;
                u.northing = n;
                fromUtm(u, lat, lon);
                return std::isfinite(lat) && std::isfinite(lon);
            }
        }
        if (tok.size() == 3 && tok[0].size() == 1 && polarLetter(tok[0][0]) && number(tok[1], e) && number(tok[2], n)) {
            Ups u;
            u.zone = tok[0][0];
            u.north = u.zone == 'Y' || u.zone == 'Z';
            u.easting = e;
            u.northing = n;
            fromUps(u, lat, lon);
            return std::isfinite(lat) && std::isfinite(lon);
        }
    }
    struct Group {
        std::vector<double> values;
        std::vector<bool> signs;
        char hemi = 0;
    };
    std::vector<Group> groups(1);
    bool letterLeads = false;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == 'N' || c == 'S' || c == 'E' || c == 'W') {
            Group& g = groups.back();
            if (!g.values.empty() && !letterLeads && !g.hemi) {
                g.hemi = c;
                groups.emplace_back();
            } else if (g.values.empty()) {
                g.hemi = c;
                letterLeads = true;
            } else {
                groups.emplace_back();
                groups.back().hemi = c;
            }
            ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
            size_t used = 0;
            double v = 0;
            try {
                v = std::stod(text.substr(i), &used);
            } catch (...) {
                return false;
            }
            Group* g = &groups.back();
            if (letterLeads && g->hemi && g->values.size() >= 3) {
                groups.emplace_back();
                g = &groups.back();
            }
            g->signs.push_back(v < 0 || text[i] == '-');
            g->values.push_back(std::abs(v));
            i += used;
            continue;
        }
        if (c == ',' || c == ';' || c == '/') {
            if (!groups.back().values.empty()) groups.emplace_back();
            ++i;
            continue;
        }
        ++i;
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& g) { return g.values.empty(); }), groups.end());
    if (groups.size() == 1 && !groups[0].hemi) {
        std::vector<double> v = groups[0].values;
        if (v.size() == 2 || v.size() == 4 || v.size() == 6) {
            size_t half = v.size() / 2;
            std::vector<bool> sg = groups[0].signs;
            Group a, b;
            a.values.assign(v.begin(), v.begin() + static_cast<long>(half));
            a.signs.assign(sg.begin(), sg.begin() + static_cast<long>(half));
            b.values.assign(v.begin() + static_cast<long>(half), v.end());
            b.signs.assign(sg.begin() + static_cast<long>(half), sg.end());
            groups = {a, b};
        }
    }
    if (groups.size() != 2) return false;
    auto value = [](const Group& g, bool& ok) {
        double d = g.values[0], m = g.values.size() > 1 ? g.values[1] : 0, s = g.values.size() > 2 ? g.values[2] : 0;
        ok = g.values.size() <= 3 && m < 60 && s < 60;
        double v = d + m / 60.0 + s / 3600.0;
        return ((!g.signs.empty() && g.signs[0]) || g.hemi == 'S' || g.hemi == 'W') ? -v : v;
    };
    bool ok1 = false, ok2 = false;
    double a = value(groups[0], ok1), b = value(groups[1], ok2);
    if (!ok1 || !ok2) return false;
    bool swap = groups[0].hemi == 'E' || groups[0].hemi == 'W' || groups[1].hemi == 'N' || groups[1].hemi == 'S';
    lat = swap ? b : a;
    lon = swap ? a : b;
    return std::abs(lat) <= 90.0 && std::abs(lon) <= 180.0;
}

Feature geodesicCircle(const Coord& center, double radiusMeters, const ShapeStyle& style, int segments) {
    segments = std::max(segments, 8);
    std::vector<Coord> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) pts.push_back(geodesicDirect(center, 360.0 * (i % segments) / segments, radiusMeters));
    Feature f = lineFeature(std::move(pts), style, true);
    f.type = "u-d-c-c";
    setAttribute(f, "radius_m", std::to_string(radiusMeters));
    return f;
}

Feature geodesicEllipse(const Coord& center, double semiMajorMeters, double semiMinorMeters, double orientationDegrees,
                        const ShapeStyle& style, int segments) {
    segments = std::max(segments, 8);
    std::vector<Coord> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        double t = 2 * kPi * (i % segments) / segments;
        double along = semiMajorMeters * std::cos(t), across = semiMinorMeters * std::sin(t);
        double d = std::hypot(along, across);
        double bearing = orientationDegrees + std::atan2(across, along) / kD2R;
        pts.push_back(d > 0 ? geodesicDirect(center, bearing, d) : center);
    }
    Feature f = lineFeature(std::move(pts), style, true);
    f.type = "u-d-c-e";
    return f;
}

Feature sector(const Coord& center, double radiusMeters, double fromBearing, double toBearing, const ShapeStyle& style,
               double innerRadiusMeters) {
    double sweep = wrap360(toBearing - fromBearing);
    if (sweep == 0) sweep = 360;
    int n = std::max(4, static_cast<int>(std::ceil(sweep / 2.0)));
    std::vector<Coord> pts;
    if (innerRadiusMeters <= 0) pts.push_back(center);
    for (int i = 0; i <= n; ++i) pts.push_back(geodesicDirect(center, fromBearing + sweep * i / n, radiusMeters));
    if (innerRadiusMeters > 0)
        for (int i = n; i >= 0; --i) pts.push_back(geodesicDirect(center, fromBearing + sweep * i / n, innerRadiusMeters));
    pts.push_back(pts.front());
    Feature f = lineFeature(std::move(pts), style, true);
    f.type = "u-d-f";
    return f;
}

std::vector<Feature> rangeRings(const Coord& center, double spacingMeters, int count, int radials, const ShapeStyle& style,
                                DistanceUnit unit) {
    std::vector<Feature> out;
    count = std::clamp(count, 1, 100);
    ShapeStyle ring = style;
    ring.fill = 0;
    for (int i = 1; i <= count; ++i) {
        double r = spacingMeters * i;
        out.push_back(geodesicCircle(center, r, ring));
        out.back().name = formatDistance(r, unit);
        if (style.labels) out.push_back(labelPoint(geodesicDirect(center, 0.0, r), formatDistance(r, unit), style));
    }
    for (int k = 0; k < radials; ++k) {
        double b = 360.0 * k / radials;
        out.push_back(lineFeature(geodesicPath(center, geodesicDirect(center, b, spacingMeters * count), spacingMeters * count / 32.0), ring, false));
    }
    return out;
}

std::vector<Feature> bullseye(const Coord& center, double radiusMeters, int rings, double bearingStep, NorthReference reference,
                              AngleUnit unit, const ShapeStyle& style, DistanceUnit distanceUnit) {
    std::vector<Feature> out;
    rings = std::clamp(rings, 1, 50);
    ShapeStyle line = style;
    line.fill = 0;
    for (int i = 1; i <= rings; ++i) {
        double r = radiusMeters * i / rings;
        out.push_back(geodesicCircle(center, r, line));
        if (style.labels) out.push_back(labelPoint(geodesicDirect(center, bearingToTrue(bearingStep / 2, center.lat, center.lon, reference), r),
                                                   formatDistance(r, distanceUnit), style));
    }
    bearingStep = std::clamp(bearingStep, 1.0, 180.0);
    for (double b = 0; b < 359.999; b += bearingStep) {
        double t = bearingToTrue(b, center.lat, center.lon, reference);
        out.push_back(lineFeature(geodesicPath(center, geodesicDirect(center, t, radiusMeters), radiusMeters / 32.0), line, false));
        if (style.labels) out.push_back(labelPoint(geodesicDirect(center, t, radiusMeters * 1.08), formatBearing(b, unit, reference), style));
    }
    Feature c = labelPoint(center, "Bullseye", style);
    c.type = "u-r-b-bullseye";
    c.style.icon = IconShape::Circle;
    c.style.iconColor = style.stroke;
    out.push_back(c);
    return out;
}

std::vector<Feature> rangeBearingLine(const Coord& from, const Coord& to, NorthReference reference, AngleUnit unit,
                                      DistanceUnit distanceUnit, const ShapeStyle& style) {
    std::vector<Feature> out;
    GeodesicLine g = geodesicInverse(from.lat, from.lon, to.lat, to.lon);
    double bearing = bearingFromTrue(g.initialBearing, from.lat, from.lon, reference);
    Feature line = lineFeature(geodesicPath(from, to, std::max(g.distance / 64.0, 50.0)), style, false);
    line.type = "u-rb-a";
    line.name = formatBearing(bearing, unit, reference) + " " + formatDistance(g.distance, distanceUnit);
    setAttribute(line, "distance_m", std::to_string(g.distance));
    setAttribute(line, "bearing_true", std::to_string(g.initialBearing));
    setAttribute(line, "bearing", std::to_string(bearing));
    out.push_back(line);
    if (style.labels) out.push_back(labelPoint(geodesicDirect(from, g.initialBearing, g.distance / 2), line.name, style));
    return out;
}

std::vector<Feature> contourLines(const std::vector<float>& h, int cols, int rows, double west, double south, double east, double north,
                                  double interval, int majorEvery, const ShapeStyle& style, DistanceUnit unit) {
    std::vector<Feature> out;
    if (cols < 2 || rows < 2 || h.size() < static_cast<size_t>(cols) * rows || interval <= 0) return out;
    float lo = 1e30f, hi = -1e30f;
    for (float v : h)
        if (std::isfinite(v)) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    if (lo > hi) return out;
    double scale = unit == DistanceUnit::Metric ? 1.0 : 0.3048;
    double step = interval * scale;
    long first = static_cast<long>(std::ceil(lo / step)), last = static_cast<long>(std::floor(hi / step));
    if (last - first > 2000) return out;
    auto at = [&](int i, int j) { return h[static_cast<size_t>(j) * cols + i]; };
    auto lonOf = [&](double i) { return west + (east - west) * i / (cols - 1); };
    auto latOf = [&](double j) { return south + (north - south) * j / (rows - 1); };
    for (long k = first; k <= last; ++k) {
        double level = k * step;
        bool major = majorEvery > 0 && k % majorEvery == 0;
        struct Seg {
            long long a = 0, b = 0;
            Coord pa, pb;
        };
        std::vector<Seg> segs;
        auto edgeKey = [&](int i, int j, int dir) { return (static_cast<long long>(j) * cols + i) * 2 + dir; };
        auto cross = [&](int i0, int j0, int i1, int j1) {
            double v0 = at(i0, j0), v1 = at(i1, j1);
            double t = (level - v0) / (v1 - v0);
            return Coord{latOf(j0 + (j1 - j0) * t), lonOf(i0 + (i1 - i0) * t), level};
        };
        for (int j = 0; j + 1 < rows; ++j)
            for (int i = 0; i + 1 < cols; ++i) {
                float v00 = at(i, j), v10 = at(i + 1, j), v01 = at(i, j + 1), v11 = at(i + 1, j + 1);
                if (!std::isfinite(v00) || !std::isfinite(v10) || !std::isfinite(v01) || !std::isfinite(v11)) continue;
                int c = (v00 > level ? 1 : 0) | (v10 > level ? 2 : 0) | (v11 > level ? 4 : 0) | (v01 > level ? 8 : 0);
                if (c == 0 || c == 15) continue;
                long long eb = edgeKey(i, j, 0), et = edgeKey(i, j + 1, 0), el = edgeKey(i, j, 1), er = edgeKey(i + 1, j, 1);
                auto pB = [&] { return cross(i, j, i + 1, j); };
                auto pT = [&] { return cross(i, j + 1, i + 1, j + 1); };
                auto pL = [&] { return cross(i, j, i, j + 1); };
                auto pR = [&] { return cross(i + 1, j, i + 1, j + 1); };
                auto add = [&](long long a, Coord ca, long long b, Coord cb) { segs.push_back({a, b, ca, cb}); };
                double center = (v00 + v10 + v01 + v11) / 4.0;
                switch (c) {
                case 1: case 14: add(el, pL(), eb, pB()); break;
                case 2: case 13: add(eb, pB(), er, pR()); break;
                case 3: case 12: add(el, pL(), er, pR()); break;
                case 4: case 11: add(er, pR(), et, pT()); break;
                case 6: case 9: add(eb, pB(), et, pT()); break;
                case 7: case 8: add(el, pL(), et, pT()); break;
                case 5:
                    if (center > level) {
                        add(el, pL(), et, pT());
                        add(eb, pB(), er, pR());
                    } else {
                        add(el, pL(), eb, pB());
                        add(er, pR(), et, pT());
                    }
                    break;
                case 10:
                    if (center > level) {
                        add(el, pL(), eb, pB());
                        add(er, pR(), et, pT());
                    } else {
                        add(el, pL(), et, pT());
                        add(eb, pB(), er, pR());
                    }
                    break;
                default: break;
                }
            }
        if (segs.empty()) continue;
        std::unordered_multimap<long long, size_t> byEdge;
        for (size_t s = 0; s < segs.size(); ++s) {
            byEdge.emplace(segs[s].a, s);
            byEdge.emplace(segs[s].b, s);
        }
        std::vector<bool> used(segs.size(), false);
        auto next = [&](long long edge, size_t from) -> long {
            auto range = byEdge.equal_range(edge);
            for (auto it = range.first; it != range.second; ++it)
                if (it->second != from && !used[it->second]) return static_cast<long>(it->second);
            return -1;
        };
        for (size_t s = 0; s < segs.size(); ++s) {
            if (used[s]) continue;
            used[s] = true;
            std::vector<Coord> chain{segs[s].pa, segs[s].pb};
            long long tail = segs[s].b, head = segs[s].a;
            for (long n = next(tail, s); n >= 0; n = next(tail, static_cast<size_t>(n))) {
                used[n] = true;
                bool forward = segs[n].a == tail;
                chain.push_back(forward ? segs[n].pb : segs[n].pa);
                tail = forward ? segs[n].b : segs[n].a;
            }
            std::vector<Coord> front;
            for (long n = next(head, s); n >= 0; n = next(head, static_cast<size_t>(n))) {
                used[n] = true;
                bool forward = segs[n].b == head;
                front.push_back(forward ? segs[n].pa : segs[n].pb);
                head = forward ? segs[n].a : segs[n].b;
            }
            if (!front.empty()) chain.insert(chain.begin(), front.rbegin(), front.rend());
            if (chain.size() < 3 && !major) continue;
            Feature f;
            f.type = "u-d-f";
            f.geometries.push_back({GeometryType::LineString, std::move(chain), {}});
            f.style.strokeColor = style.stroke;
            f.style.strokeWidth = major ? style.width * 1.6f : style.width * 0.8f;
            f.style.labelColor = style.label;
            char buf[32];
            std::snprintf(buf, sizeof(buf), unit == DistanceUnit::Metric ? "%.0f m" : "%.0f ft", k * interval);
            f.name = buf;
            f.style.showLabel = major && style.labels;
            setAttribute(f, "contour", std::to_string(k * interval));
            setAttribute(f, "major", major ? "1" : "0");
            out.push_back(std::move(f));
        }
    }
    return out;
}

}
