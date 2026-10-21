#include "otk/mgrs.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace otk {

namespace {

constexpr double kA = 6378137.0;
constexpr double kF = 1.0 / 298.257223563;
constexpr double kE2 = kF * (2 - kF);
constexpr double kK0 = 0.9996;
constexpr double kPi = 3.14159265358979323846;
constexpr double kD2R = kPi / 180.0;
const char* kBands = "CDEFGHJKLMNPQRSTUVWX";
const char* kCols[3] = {"ABCDEFGH", "JKLMNPQR", "STUVWXYZ"};
const char* kRows = "ABCDEFGHJKLMNPQRSTUV";

}

int utmZone(double lat, double lon) {
    int zone = static_cast<int>(std::floor((lon + 180.0) / 6.0)) + 1;
    if (zone > 60) zone = 60;
    if (lat >= 56.0 && lat < 64.0 && lon >= 3.0 && lon < 12.0) zone = 32;
    if (lat >= 72.0 && lat < 84.0) {
        if (lon >= 0.0 && lon < 9.0) zone = 31;
        else if (lon >= 9.0 && lon < 21.0) zone = 33;
        else if (lon >= 21.0 && lon < 33.0) zone = 35;
        else if (lon >= 33.0 && lon < 42.0) zone = 37;
    }
    return zone;
}

char utmBand(double lat) {
    if (lat < -80.0 || lat > 84.0) return 0;
    int i = static_cast<int>(std::floor((lat + 80.0) / 8.0));
    if (i > 19) i = 19;
    return kBands[i];
}

Utm toUtm(double lat, double lon, int forceZone) {
    Utm u;
    u.zone = forceZone > 0 ? forceZone : utmZone(lat, lon);
    u.band = utmBand(lat);
    u.north = lat >= 0;
    double lon0 = ((u.zone - 1) * 6 - 180 + 3) * kD2R;
    double phi = lat * kD2R, lam = lon * kD2R;
    double ep2 = kE2 / (1 - kE2);
    double s = std::sin(phi), c = std::cos(phi), t = std::tan(phi);
    double n = kA / std::sqrt(1 - kE2 * s * s);
    double T = t * t, C = ep2 * c * c, A = c * (lam - lon0);
    double e4 = kE2 * kE2, e6 = e4 * kE2;
    double M = kA * ((1 - kE2 / 4 - 3 * e4 / 64 - 5 * e6 / 256) * phi - (3 * kE2 / 8 + 3 * e4 / 32 + 45 * e6 / 1024) * std::sin(2 * phi) +
                     (15 * e4 / 256 + 45 * e6 / 1024) * std::sin(4 * phi) - (35 * e6 / 3072) * std::sin(6 * phi));
    u.easting = kK0 * n * (A + (1 - T + C) * A * A * A / 6 + (5 - 18 * T + T * T + 72 * C - 58 * ep2) * std::pow(A, 5) / 120) + 500000.0;
    u.northing = kK0 * (M + n * t * (A * A / 2 + (5 - T + 9 * C + 4 * C * C) * std::pow(A, 4) / 24 +
                                    (61 - 58 * T + T * T + 600 * C - 330 * ep2) * std::pow(A, 6) / 720));
    if (!u.north) u.northing += 10000000.0;
    return u;
}

void fromUtm(const Utm& u, double& lat, double& lon) {
    double ep2 = kE2 / (1 - kE2);
    double x = u.easting - 500000.0, y = u.north ? u.northing : u.northing - 10000000.0;
    double lon0 = ((u.zone - 1) * 6 - 180 + 3) * kD2R;
    double M = y / kK0;
    double e4 = kE2 * kE2, e6 = e4 * kE2;
    double mu = M / (kA * (1 - kE2 / 4 - 3 * e4 / 64 - 5 * e6 / 256));
    double e1 = (1 - std::sqrt(1 - kE2)) / (1 + std::sqrt(1 - kE2));
    double phi1 = mu + (3 * e1 / 2 - 27 * e1 * e1 * e1 / 32) * std::sin(2 * mu) + (21 * e1 * e1 / 16 - 55 * std::pow(e1, 4) / 32) * std::sin(4 * mu) +
                  (151 * std::pow(e1, 3) / 96) * std::sin(6 * mu) + (1097 * std::pow(e1, 4) / 512) * std::sin(8 * mu);
    double s = std::sin(phi1), c = std::cos(phi1), t = std::tan(phi1);
    double n1 = kA / std::sqrt(1 - kE2 * s * s);
    double r1 = kA * (1 - kE2) / std::pow(1 - kE2 * s * s, 1.5);
    double T1 = t * t, C1 = ep2 * c * c, D = x / (n1 * kK0);
    lat = phi1 - (n1 * t / r1) * (D * D / 2 - (5 + 3 * T1 + 10 * C1 - 4 * C1 * C1 - 9 * ep2) * std::pow(D, 4) / 24 +
                                 (61 + 90 * T1 + 298 * C1 + 45 * T1 * T1 - 252 * ep2 - 3 * C1 * C1) * std::pow(D, 6) / 720);
    lon = lon0 + (D - (1 + 2 * T1 + C1) * D * D * D / 6 + (5 - 2 * C1 + 28 * T1 - 3 * C1 * C1 + 8 * ep2 + 24 * T1 * T1) * std::pow(D, 5) / 120) / c;
    lat /= kD2R;
    lon /= kD2R;
}

Ups toUps(double lat, double lon) {
    const double e = std::sqrt(kE2);
    const double k0 = 0.994;
    Ups u;
    u.north = lat >= 0;
    double phi = std::abs(lat) * kD2R, lambda = lon * kD2R;
    double es = e * std::sin(phi);
    double t = std::tan(kPi / 4 - phi / 2) / std::pow((1 - es) / (1 + es), e / 2);
    double rho = 2 * kA * k0 * t / std::sqrt(std::pow(1 + e, 1 + e) * std::pow(1 - e, 1 - e));
    u.easting = 2000000.0 + rho * std::sin(lambda);
    u.northing = u.north ? 2000000.0 - rho * std::cos(lambda) : 2000000.0 + rho * std::cos(lambda);
    u.zone = u.north ? (u.easting >= 2000000.0 ? 'Z' : 'Y') : (u.easting >= 2000000.0 ? 'B' : 'A');
    return u;
}

void fromUps(const Ups& u, double& lat, double& lon) {
    const double e = std::sqrt(kE2);
    const double k0 = 0.994;
    double dx = u.easting - 2000000.0, dy = u.northing - 2000000.0;
    double rho = std::sqrt(dx * dx + dy * dy);
    double t = rho * std::sqrt(std::pow(1 + e, 1 + e) * std::pow(1 - e, 1 - e)) / (2 * kA * k0);
    double chi = kPi / 2 - 2 * std::atan(t);
    double e2 = kE2, e4 = e2 * e2, e6 = e4 * e2, e8 = e4 * e4;
    double phi = chi + (e2 / 2 + 5 * e4 / 24 + e6 / 12 + 13 * e8 / 360) * std::sin(2 * chi) +
                 (7 * e4 / 48 + 29 * e6 / 240 + 811 * e8 / 11520) * std::sin(4 * chi) + (7 * e6 / 120 + 81 * e8 / 1120) * std::sin(6 * chi) +
                 (4279 * e8 / 161280) * std::sin(8 * chi);
    lat = (u.north ? phi : -phi) / kD2R;
    lon = (rho == 0 ? 0.0 : (u.north ? std::atan2(dx, -dy) : std::atan2(dx, dy))) / kD2R;
}

namespace {

struct PolarZone {
    char letter;
    int low;
    double falseEasting;
    double falseNorthing;
};

constexpr PolarZone kPolar[4] = {{'A', 'J' - 'A', 800000.0, 800000.0},
                                 {'B', 0, 2000000.0, 800000.0},
                                 {'Y', 'J' - 'A', 800000.0, 1300000.0},
                                 {'Z', 0, 2000000.0, 1300000.0}};

const PolarZone* polarZone(char c) {
    for (const PolarZone& z : kPolar)
        if (z.letter == c) return &z;
    return nullptr;
}

std::string polarMgrs(double lat, double lon, int digits) {
    Ups u = toUps(lat, lon);
    const PolarZone* z = polarZone(u.zone);
    if (!z) return {};
    int row = static_cast<int>((u.northing - z->falseNorthing) / 100000.0);
    if (row > 'H' - 'A') ++row;
    if (row > 'N' - 'A') ++row;
    int col = z->low + static_cast<int>((u.easting - z->falseEasting) / 100000.0);
    if (u.easting < 2000000.0) {
        if (col > 'L' - 'A') col += 3;
        if (col > 'U' - 'A') col += 2;
    } else {
        if (col > 'C' - 'A') col += 2;
        if (col > 'H' - 'A') col += 1;
        if (col > 'L' - 'A') col += 3;
    }
    if (row < 0 || row > 25 || col < 0 || col > 25) return {};
    digits = digits < 0 ? 0 : digits > 5 ? 5 : digits;
    double div = std::pow(10.0, 5 - digits);
    long e = static_cast<long>(std::floor(std::fmod(u.easting, 100000.0) / div));
    long n = static_cast<long>(std::floor(std::fmod(u.northing, 100000.0) / div));
    char buf[64];
    if (digits == 0) std::snprintf(buf, sizeof(buf), "%c %c%c", u.zone, 'A' + col, 'A' + row);
    else std::snprintf(buf, sizeof(buf), "%c %c%c %0*ld %0*ld", u.zone, 'A' + col, 'A' + row, digits, e, digits, n);
    return buf;
}

bool polarFromMgrs(const std::string& s, double& lat, double& lon) {
    if (s.size() < 3) return false;
    const PolarZone* z = polarZone(s[0]);
    if (!z) return false;
    int col = s[1] - 'A', row = s[2] - 'A';
    if (col < 0 || col > 25 || row < 0 || row > 25) return false;
    std::string digits = s.substr(3);
    if (digits.size() % 2) return false;
    for (char c : digits)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    size_t half = digits.size() / 2;
    double scale = std::pow(10.0, 5 - static_cast<int>(half));
    double e = half ? std::stod(digits.substr(0, half)) * scale : 0;
    double n = half ? std::stod(digits.substr(half)) * scale : 0;
    double northing = row * 100000.0 + z->falseNorthing;
    if (row > 'I' - 'A') northing -= 100000.0;
    if (row > 'O' - 'A') northing -= 100000.0;
    double easting = (col - z->low) * 100000.0 + z->falseEasting;
    if (z->low != 0) {
        if (col > 'L' - 'A') easting -= 300000.0;
        if (col > 'U' - 'A') easting -= 200000.0;
    } else {
        if (col > 'C' - 'A') easting -= 200000.0;
        if (col > 'I' - 'A') easting -= 100000.0;
        if (col > 'L' - 'A') easting -= 300000.0;
    }
    Ups u;
    u.zone = s[0];
    u.north = s[0] == 'Y' || s[0] == 'Z';
    u.easting = easting + e;
    u.northing = northing + n;
    fromUps(u, lat, lon);
    return true;
}

}

std::string toMgrs(double lat, double lon, int digits) {
    char band = utmBand(lat);
    if (!band) return polarMgrs(lat, lon, digits);
    Utm u = toUtm(lat, lon);
    int set = (u.zone - 1) % 3;
    int col = static_cast<int>(std::floor(u.easting / 100000.0));
    int row = static_cast<int>(std::floor(u.northing / 100000.0)) % 20;
    if (col < 1 || col > 8) return {};
    if (u.zone % 2 == 0) row = (row + 5) % 20;
    digits = digits < 0 ? 0 : digits > 5 ? 5 : digits;
    double div = std::pow(10.0, 5 - digits);
    long e = static_cast<long>(std::floor(std::fmod(u.easting, 100000.0) / div));
    long n = static_cast<long>(std::floor(std::fmod(u.northing, 100000.0) / div));
    char buf[64];
    if (digits == 0) std::snprintf(buf, sizeof(buf), "%d%c %c%c", u.zone, band, kCols[set][col - 1], kRows[row]);
    else std::snprintf(buf, sizeof(buf), "%d%c %c%c %0*ld %0*ld", u.zone, band, kCols[set][col - 1], kRows[row], digits, e, digits, n);
    return buf;
}

bool fromMgrs(const std::string& text, double& lat, double& lon) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (!s.empty() && std::isalpha(static_cast<unsigned char>(s[0]))) return polarFromMgrs(s, lat, lon);
    size_t i = 0;
    int zone = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) zone = zone * 10 + (s[i++] - '0');
    if (zone < 1 || zone > 60 || i + 3 > s.size()) return false;
    char band = s[i++], colL = s[i++], rowL = s[i++];
    const char* bp = std::strchr(kBands, band);
    if (!bp) return false;
    std::string digits = s.substr(i);
    if (digits.size() % 2) return false;
    size_t half = digits.size() / 2;
    double scale = std::pow(10.0, 5 - static_cast<int>(half));
    double e = half ? std::stod(digits.substr(0, half)) * scale : 0;
    double n = half ? std::stod(digits.substr(half)) * scale : 0;
    int set = (zone - 1) % 3;
    const char* cp = std::strchr(kCols[set], colL);
    const char* rp = std::strchr(kRows, rowL);
    if (!cp || !rp) return false;
    int col = static_cast<int>(cp - kCols[set]) + 1;
    int row = static_cast<int>(rp - kRows);
    if (zone % 2 == 0) row = (row + 15) % 20;
    Utm u;
    u.zone = zone;
    u.band = band;
    double bandLat = -80.0 + (bp - kBands) * 8.0;
    u.north = bandLat >= 0;
    u.easting = col * 100000.0 + e;
    double northing = row * 100000.0 + n;
    Utm base = toUtm(bandLat, ((zone - 1) * 6 - 180 + 3), zone);
    double minN = base.northing - 100000.0;
    while (northing < minN) northing += 2000000.0;
    u.northing = northing;
    fromUtm(u, lat, lon);
    return true;
}

}
