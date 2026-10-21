#include "core/prj.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace ote {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct Node {
    std::string key;
    std::vector<std::string> values;
    std::vector<Node> children;
};

void skipSpace(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool parseNode(const std::string& s, size_t& i, Node& out, int depth) {
    if (depth > 16) return false;
    skipSpace(s, i);
    size_t start = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
    out.key = lowered(s.substr(start, i - start));
    skipSpace(s, i);
    if (i >= s.size() || (s[i] != '[' && s[i] != '(')) return !out.key.empty();
    char close = s[i] == '[' ? ']' : ')';
    ++i;
    while (i < s.size()) {
        skipSpace(s, i);
        if (i < s.size() && s[i] == close) {
            ++i;
            return true;
        }
        if (i < s.size() && s[i] == '"') {
            size_t e = s.find('"', i + 1);
            if (e == std::string::npos) return false;
            out.values.push_back(s.substr(i + 1, e - i - 1));
            i = e + 1;
        } else if (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.')) {
            size_t e = i;
            while (e < s.size() && s[e] != ',' && s[e] != close && !std::isspace(static_cast<unsigned char>(s[e]))) ++e;
            out.values.push_back(s.substr(i, e - i));
            i = e;
        } else {
            Node child;
            if (!parseNode(s, i, child, depth + 1)) return false;
            out.children.push_back(std::move(child));
        }
        skipSpace(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
    return false;
}

const Node* findChild(const Node& n, const char* key) {
    for (const Node& c : n.children)
        if (c.key == key) return &c;
    return nullptr;
}

const Node* findDeep(const Node& n, const char* key) {
    if (n.key == key) return &n;
    for (const Node& c : n.children)
        if (const Node* r = findDeep(c, key)) return r;
    return nullptr;
}

double number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

double conformalLat(double t, double e) {
    double phi = kPi / 2 - 2 * std::atan(t);
    for (int i = 0; i < 15; ++i) {
        double es = e * std::sin(phi);
        double next = kPi / 2 - 2 * std::atan(t * std::pow((1 - es) / (1 + es), e / 2));
        if (std::abs(next - phi) < 1e-12) return next;
        phi = next;
    }
    return phi;
}

double tFn(double phi, double e) {
    double es = e * std::sin(phi);
    return std::tan(kPi / 4 - phi / 2) / std::pow((1 - es) / (1 + es), e / 2);
}

double mFn(double phi, double e2) { return std::cos(phi) / std::sqrt(1 - e2 * std::sin(phi) * std::sin(phi)); }

double qFn(double phi, double e, double e2) {
    double s = std::sin(phi);
    if (e < 1e-12) return 2 * s;
    return (1 - e2) * (s / (1 - e2 * s * s) - (1 / (2 * e)) * std::log((1 - e * s) / (1 + e * s)));
}

double meridian(double phi, double a, double e2) {
    double e4 = e2 * e2, e6 = e4 * e2;
    return a * ((1 - e2 / 4 - 3 * e4 / 64 - 5 * e6 / 256) * phi - (3 * e2 / 8 + 3 * e4 / 32 + 45 * e6 / 1024) * std::sin(2 * phi) +
                (15 * e4 / 256 + 45 * e6 / 1024) * std::sin(4 * phi) - (35 * e6 / 3072) * std::sin(6 * phi));
}

}

PrjTransform parsePrj(const std::string& wkt) {
    PrjTransform t;
    size_t i = 0;
    Node root;
    if (!parseNode(wkt, i, root, 0)) {
        t.kind = PrjTransform::Kind::Unsupported;
        return t;
    }
    if (!root.values.empty()) t.name = root.values[0];
    if (const Node* sph = findDeep(root, "spheroid"); sph && sph->values.size() >= 3) {
        t.a = number(sph->values[1]);
        double inv = number(sph->values[2]);
        t.f = inv > 0 ? 1.0 / inv : 0.0;
    } else if (const Node* ell = findDeep(root, "ellipsoid"); ell && ell->values.size() >= 3) {
        t.a = number(ell->values[1]);
        double inv = number(ell->values[2]);
        t.f = inv > 0 ? 1.0 / inv : 0.0;
    }
    if (root.key == "geogcs" || root.key == "geogcrs") {
        t.kind = PrjTransform::Kind::Geographic;
        return t;
    }
    if (root.key != "projcs" && root.key != "projcrs") {
        t.kind = PrjTransform::Kind::Unsupported;
        return t;
    }
    const Node* proj = findChild(root, "projection");
    std::string method = proj && !proj->values.empty() ? lowered(proj->values[0]) : std::string();
    std::string name = lowered(t.name);
    for (const Node& c : root.children) {
        if (c.key == "parameter" && c.values.size() >= 2) {
            std::string k = lowered(c.values[0]);
            double v = number(c.values[1]);
            if (k == "false_easting") t.falseEasting = v;
            else if (k == "false_northing") t.falseNorthing = v;
            else if (k == "central_meridian" || k == "longitude_of_center" || k == "longitude_of_origin") t.lon0 = v;
            else if (k == "latitude_of_origin" || k == "latitude_of_center") t.lat0 = v;
            else if (k == "standard_parallel_1") t.lat1 = v;
            else if (k == "standard_parallel_2") t.lat2 = v;
            else if (k == "scale_factor") t.k0 = v;
        } else if (c.key == "unit" && c.values.size() >= 2) {
            t.unit = number(c.values[1]);
        }
    }
    if (method == "transverse_mercator" || method == "gauss_kruger") t.kind = PrjTransform::Kind::TransverseMercator;
    else if (method.rfind("lambert_conformal_conic", 0) == 0) {
        t.kind = PrjTransform::Kind::LambertConformalConic;
        if (method == "lambert_conformal_conic_1sp" || (t.lat1 == 0 && t.lat2 == 0)) t.lat1 = t.lat2 = t.lat0;
    } else if (method == "albers" || method == "albers_conic_equal_area") t.kind = PrjTransform::Kind::Albers;
    else if (method == "mercator_auxiliary_sphere" || method == "popular_visualisation_pseudo_mercator" || name.find("web_mercator") != std::string::npos ||
             name.find("pseudo-mercator") != std::string::npos || name.find("pseudo_mercator") != std::string::npos)
        t.kind = PrjTransform::Kind::WebMercator;
    else if (method == "mercator" || method == "mercator_1sp" || method == "mercator_2sp") t.kind = PrjTransform::Kind::Mercator;
    else t.kind = PrjTransform::Kind::Unsupported;
    return t;
}

bool PrjTransform::inverse(double x, double y, double& lat, double& lon) const {
    const double e2 = 2 * f - f * f;
    const double e = std::sqrt(e2);
    x *= unit;
    y *= unit;
    const double fe = falseEasting * unit, fn = falseNorthing * unit;
    switch (kind) {
    case Kind::Geographic:
        lat = y;
        lon = x;
        return true;
    case Kind::WebMercator:
        lon = lon0 + (x - fe) / a / kRad;
        lat = (2 * std::atan(std::exp((y - fn) / a)) - kPi / 2) / kRad;
        return true;
    case Kind::Mercator: {
        double k = k0;
        if (lat1 != 0) k = mFn(lat1 * kRad, e2);
        lon = lon0 + (x - fe) / (a * k) / kRad;
        lat = conformalLat(std::exp(-(y - fn) / (a * k)), e) / kRad;
        return true;
    }
    case Kind::TransverseMercator: {
        const double ep2 = e2 / (1 - e2);
        const double m = meridian(lat0 * kRad, a, e2) + (y - fn) / k0;
        const double mu = m / (a * (1 - e2 / 4 - 3 * e2 * e2 / 64 - 5 * e2 * e2 * e2 / 256));
        const double e1 = (1 - std::sqrt(1 - e2)) / (1 + std::sqrt(1 - e2));
        const double phi1 = mu + (3 * e1 / 2 - 27 * e1 * e1 * e1 / 32) * std::sin(2 * mu) +
                            (21 * e1 * e1 / 16 - 55 * e1 * e1 * e1 * e1 / 32) * std::sin(4 * mu) + (151 * e1 * e1 * e1 / 96) * std::sin(6 * mu) +
                            (1097 * e1 * e1 * e1 * e1 / 512) * std::sin(8 * mu);
        const double s = std::sin(phi1), c = std::cos(phi1), tn = std::tan(phi1);
        const double c1 = ep2 * c * c, t1 = tn * tn;
        const double n1 = a / std::sqrt(1 - e2 * s * s);
        const double r1 = a * (1 - e2) / std::pow(1 - e2 * s * s, 1.5);
        const double d = (x - fe) / (n1 * k0);
        const double d2 = d * d;
        lat = phi1 - (n1 * tn / r1) * (d2 / 2 - (5 + 3 * t1 + 10 * c1 - 4 * c1 * c1 - 9 * ep2) * d2 * d2 / 24 +
                                       (61 + 90 * t1 + 298 * c1 + 45 * t1 * t1 - 252 * ep2 - 3 * c1 * c1) * d2 * d2 * d2 / 720);
        lon = lon0 * kRad + (d - (1 + 2 * t1 + c1) * d2 * d / 6 + (5 - 2 * c1 + 28 * t1 - 3 * c1 * c1 + 8 * ep2 + 24 * t1 * t1) * d2 * d2 * d / 120) / c;
        lat /= kRad;
        lon /= kRad;
        return true;
    }
    case Kind::LambertConformalConic: {
        const double p0 = lat0 * kRad, p1 = lat1 * kRad, p2 = lat2 * kRad;
        const double m1 = mFn(p1, e2), m2 = mFn(p2, e2);
        const double t0 = tFn(p0, e), t1 = tFn(p1, e), t2 = tFn(p2, e);
        const double n = std::abs(p1 - p2) < 1e-12 ? std::sin(p1) : (std::log(m1) - std::log(m2)) / (std::log(t1) - std::log(t2));
        const double fF = m1 / (n * std::pow(t1, n));
        const double rho0 = a * fF * k0 * std::pow(t0, n);
        const double dx = x - fe, dy = rho0 - (y - fn);
        const double sign = n < 0 ? -1.0 : 1.0;
        const double rho = sign * std::sqrt(dx * dx + dy * dy);
        const double theta = std::atan2(sign * dx, sign * dy);
        const double t = std::pow(rho / (a * fF * k0), 1.0 / n);
        lat = conformalLat(t, e) / kRad;
        lon = (theta / n) / kRad + lon0;
        return true;
    }
    case Kind::Albers: {
        const double p0 = lat0 * kRad, p1 = lat1 * kRad, p2 = lat2 * kRad;
        const double m1 = mFn(p1, e2), m2 = mFn(p2, e2);
        const double q0 = qFn(p0, e, e2), q1 = qFn(p1, e, e2), q2 = qFn(p2, e, e2);
        const double n = std::abs(p1 - p2) < 1e-12 ? std::sin(p1) : (m1 * m1 - m2 * m2) / (q2 - q1);
        const double c = m1 * m1 + n * q1;
        const double rho0 = a * std::sqrt(c - n * q0) / n;
        const double dx = x - fe, dy = rho0 - (y - fn);
        const double sign = n < 0 ? -1.0 : 1.0;
        const double rho = std::sqrt(dx * dx + dy * dy);
        const double theta = std::atan2(sign * dx, sign * dy);
        const double q = (c - rho * rho * n * n / (a * a)) / n;
        double phi = std::asin(std::clamp(q / 2, -1.0, 1.0));
        for (int i = 0; i < 25 && e > 1e-12; ++i) {
            const double s = std::sin(phi), cs = std::cos(phi);
            const double next = phi + (1 - e2 * s * s) * (1 - e2 * s * s) / (2 * cs) *
                                          (q / (1 - e2) - s / (1 - e2 * s * s) + (1 / (2 * e)) * std::log((1 - e * s) / (1 + e * s)));
            if (std::abs(next - phi) < 1e-12) {
                phi = next;
                break;
            }
            phi = next;
        }
        lat = phi / kRad;
        lon = lon0 + (theta / n) / kRad;
        return true;
    }
    default:
        return false;
    }
}

}
