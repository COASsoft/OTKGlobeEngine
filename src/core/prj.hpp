#pragma once

#include <string>

namespace otk {

struct PrjTransform {
    enum class Kind { Geographic, TransverseMercator, LambertConformalConic, Albers, Mercator, WebMercator, Unsupported };
    Kind kind = Kind::Geographic;
    double a = 6378137.0;
    double f = 1.0 / 298.257223563;
    double lat0 = 0;
    double lon0 = 0;
    double lat1 = 0;
    double lat2 = 0;
    double k0 = 1.0;
    double falseEasting = 0;
    double falseNorthing = 0;
    double unit = 1.0;
    std::string name;

    bool projected() const { return kind != Kind::Geographic && kind != Kind::Unsupported; }
    bool inverse(double x, double y, double& lat, double& lon) const;
};

PrjTransform parsePrj(const std::string& wkt);

}
