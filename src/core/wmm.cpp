#include "otk/navigation.hpp"

#include <cmath>

namespace otk {

namespace {

struct Coefficient {
    int n, m;
    double g, h, gdot, hdot;
};

constexpr double kEpoch = 2025.0;
constexpr int kOrder = 12;
constexpr Coefficient kWmm[] = {
    {1, 0, -29351.8, 0.0, 12.0, 0.0},
    {1, 1, -1410.8, 4545.4, 9.7, -21.5},
    {2, 0, -2556.6, 0.0, -11.6, 0.0},
    {2, 1, 2951.1, -3133.6, -5.2, -27.7},
    {2, 2, 1649.3, -815.1, -8.0, -12.1},
    {3, 0, 1361.0, 0.0, -1.3, 0.0},
    {3, 1, -2404.1, -56.6, -4.2, 4.0},
    {3, 2, 1243.8, 237.5, 0.4, -0.3},
    {3, 3, 453.6, -549.5, -15.6, -4.1},
    {4, 0, 895.0, 0.0, -1.6, 0.0},
    {4, 1, 799.5, 278.6, -2.4, -1.1},
    {4, 2, 55.7, -133.9, -6.0, 4.1},
    {4, 3, -281.1, 212.0, 5.6, 1.6},
    {4, 4, 12.1, -375.6, -7.0, -4.4},
    {5, 0, -233.2, 0.0, 0.6, 0.0},
    {5, 1, 368.9, 45.4, 1.4, -0.5},
    {5, 2, 187.2, 220.2, 0.0, 2.2},
    {5, 3, -138.7, -122.9, 0.6, 0.4},
    {5, 4, -142.0, 43.0, 2.2, 1.7},
    {5, 5, 20.9, 106.1, 0.9, 1.9},
    {6, 0, 64.4, 0.0, -0.2, 0.0},
    {6, 1, 63.8, -18.4, -0.4, 0.3},
    {6, 2, 76.9, 16.8, 0.9, -1.6},
    {6, 3, -115.7, 48.8, 1.2, -0.4},
    {6, 4, -40.9, -59.8, -0.9, 0.9},
    {6, 5, 14.9, 10.9, 0.3, 0.7},
    {6, 6, -60.7, 72.7, 0.9, 0.9},
    {7, 0, 79.5, 0.0, -0.0, 0.0},
    {7, 1, -77.0, -48.9, -0.1, 0.6},
    {7, 2, -8.8, -14.4, -0.1, 0.5},
    {7, 3, 59.3, -1.0, 0.5, -0.8},
    {7, 4, 15.8, 23.4, -0.1, 0.0},
    {7, 5, 2.5, -7.4, -0.8, -1.0},
    {7, 6, -11.1, -25.1, -0.8, 0.6},
    {7, 7, 14.2, -2.3, 0.8, -0.2},
    {8, 0, 23.2, 0.0, -0.1, 0.0},
    {8, 1, 10.8, 7.1, 0.2, -0.2},
    {8, 2, -17.5, -12.6, 0.0, 0.5},
    {8, 3, 2.0, 11.4, 0.5, -0.4},
    {8, 4, -21.7, -9.7, -0.1, 0.4},
    {8, 5, 16.9, 12.7, 0.3, -0.5},
    {8, 6, 15.0, 0.7, 0.2, -0.6},
    {8, 7, -16.8, -5.2, -0.0, 0.3},
    {8, 8, 0.9, 3.9, 0.2, 0.2},
    {9, 0, 4.6, 0.0, -0.0, 0.0},
    {9, 1, 7.8, -24.8, -0.1, -0.3},
    {9, 2, 3.0, 12.2, 0.1, 0.3},
    {9, 3, -0.2, 8.3, 0.3, -0.3},
    {9, 4, -2.5, -3.3, -0.3, 0.3},
    {9, 5, -13.1, -5.2, 0.0, 0.2},
    {9, 6, 2.4, 7.2, 0.3, -0.1},
    {9, 7, 8.6, -0.6, -0.1, -0.2},
    {9, 8, -8.7, 0.8, 0.1, 0.4},
    {9, 9, -12.9, 10.0, -0.1, 0.1},
    {10, 0, -1.3, 0.0, 0.1, 0.0},
    {10, 1, -6.4, 3.3, 0.0, 0.0},
    {10, 2, 0.2, 0.0, 0.1, -0.0},
    {10, 3, 2.0, 2.4, 0.1, -0.2},
    {10, 4, -1.0, 5.3, -0.0, 0.1},
    {10, 5, -0.6, -9.1, -0.3, -0.1},
    {10, 6, -0.9, 0.4, 0.0, 0.1},
    {10, 7, 1.5, -4.2, -0.1, 0.0},
    {10, 8, 0.9, -3.8, -0.1, -0.1},
    {10, 9, -2.7, 0.9, -0.0, 0.2},
    {10, 10, -3.9, -9.1, -0.0, -0.0},
    {11, 0, 2.9, 0.0, 0.0, 0.0},
    {11, 1, -1.5, 0.0, -0.0, -0.0},
    {11, 2, -2.5, 2.9, 0.0, 0.1},
    {11, 3, 2.4, -0.6, 0.0, -0.0},
    {11, 4, -0.6, 0.2, 0.0, 0.1},
    {11, 5, -0.1, 0.5, -0.1, -0.0},
    {11, 6, -0.6, -0.3, 0.0, -0.0},
    {11, 7, -0.1, -1.2, -0.0, 0.1},
    {11, 8, 1.1, -1.7, -0.1, -0.0},
    {11, 9, -1.0, -2.9, -0.1, 0.0},
    {11, 10, -0.2, -1.8, -0.1, 0.0},
    {11, 11, 2.6, -2.3, -0.1, 0.0},
    {12, 0, -2.0, 0.0, 0.0, 0.0},
    {12, 1, -0.2, -1.3, 0.0, -0.0},
    {12, 2, 0.3, 0.7, -0.0, 0.0},
    {12, 3, 1.2, 1.0, -0.0, -0.1},
    {12, 4, -1.3, -1.4, -0.0, 0.1},
    {12, 5, 0.6, -0.0, -0.0, -0.0},
    {12, 6, 0.6, 0.6, 0.1, -0.0},
    {12, 7, 0.5, -0.1, -0.0, -0.0},
    {12, 8, -0.1, 0.8, 0.0, 0.0},
    {12, 9, -0.4, 0.1, 0.0, -0.0},
    {12, 10, -0.2, -1.0, -0.1, -0.0},
    {12, 11, -1.3, 0.1, -0.0, 0.0},
    {12, 12, -0.7, 0.2, -0.1, -0.1}};

}

MagneticField magneticField(double lat, double lon, double altitudeMeters, double decimalYear) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double d2r = kPi / 180.0;
    constexpr double a = 6378.137;
    constexpr double f = 1.0 / 298.257223563;
    constexpr double e2 = f * (2.0 - f);
    constexpr double re = 6371.2;
    double t = decimalYear - kEpoch;
    lat = std::fmax(-89.9999, std::fmin(89.9999, lat));
    double phi = lat * d2r, lambda = lon * d2r, hkm = altitudeMeters / 1000.0;
    double sinPhi = std::sin(phi), cosPhi = std::cos(phi);
    double rc = a / std::sqrt(1.0 - e2 * sinPhi * sinPhi);
    double p = (rc + hkm) * cosPhi;
    double z = (rc * (1.0 - e2) + hkm) * sinPhi;
    double r = std::sqrt(p * p + z * z);
    double phic = std::asin(z / r);
    double ct = std::sin(phic), st = std::cos(phic);
    double g[kOrder + 1][kOrder + 1] = {}, h[kOrder + 1][kOrder + 1] = {};
    for (const Coefficient& c : kWmm) {
        g[c.n][c.m] = c.g + t * c.gdot;
        h[c.n][c.m] = c.h + t * c.hdot;
    }
    double schmidt[kOrder + 1][kOrder + 1] = {};
    schmidt[0][0] = 1.0;
    for (int n = 1; n <= kOrder; ++n) {
        schmidt[n][0] = schmidt[n - 1][0] * (2.0 * n - 1.0) / n;
        for (int m = 1; m <= n; ++m)
            schmidt[n][m] = schmidt[n][m - 1] * std::sqrt((n - m + 1.0) * (m == 1 ? 2.0 : 1.0) / (n + m));
    }
    double P[kOrder + 1][kOrder + 1] = {}, dP[kOrder + 1][kOrder + 1] = {};
    P[0][0] = 1.0;
    for (int n = 1; n <= kOrder; ++n) {
        for (int m = 0; m <= n; ++m) {
            if (n == m) {
                P[n][m] = st * P[n - 1][m - 1];
                dP[n][m] = st * dP[n - 1][m - 1] + ct * P[n - 1][m - 1];
            } else if (n == 1) {
                P[n][m] = ct * P[n - 1][m];
                dP[n][m] = ct * dP[n - 1][m] - st * P[n - 1][m];
            } else {
                double k = ((n - 1.0) * (n - 1.0) - m * m) / ((2.0 * n - 1.0) * (2.0 * n - 3.0));
                P[n][m] = ct * P[n - 1][m] - k * P[n - 2][m];
                dP[n][m] = ct * dP[n - 1][m] - st * P[n - 1][m] - k * dP[n - 2][m];
            }
        }
    }
    double br = 0, bt = 0, bp = 0;
    double ratio = re / r;
    double power = ratio * ratio;
    for (int n = 1; n <= kOrder; ++n) {
        power *= ratio;
        for (int m = 0; m <= n; ++m) {
            double gs = g[n][m] * schmidt[n][m], hs = h[n][m] * schmidt[n][m];
            double cm = std::cos(m * lambda), sm = std::sin(m * lambda);
            br += power * (n + 1) * (gs * cm + hs * sm) * P[n][m];
            bt -= power * (gs * cm + hs * sm) * dP[n][m];
            bp += power * m * (gs * sm - hs * cm) * P[n][m];
        }
    }
    bp /= st;
    double xs = -bt, ys = bp, zs = -br;
    double psi = phic - phi;
    MagneticField out;
    out.north = xs * std::cos(psi) - zs * std::sin(psi);
    out.east = ys;
    out.down = xs * std::sin(psi) + zs * std::cos(psi);
    out.horizontal = std::sqrt(out.north * out.north + out.east * out.east);
    out.total = std::sqrt(out.horizontal * out.horizontal + out.down * out.down);
    out.declination = std::atan2(out.east, out.north) / d2r;
    out.inclination = std::atan2(out.down, out.horizontal) / d2r;
    out.validFrom = kEpoch;
    out.validTo = kEpoch + 5.0;
    return out;
}

}
