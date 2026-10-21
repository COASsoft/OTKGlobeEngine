#pragma once

#include <string>

namespace otk {

struct Utm {
    int zone = 0;
    char band = 0;
    bool north = true;
    double easting = 0;
    double northing = 0;
};

struct Ups {
    char zone = 'Z';
    bool north = true;
    double easting = 0;
    double northing = 0;
};

int utmZone(double lat, double lon);
char utmBand(double lat);
Utm toUtm(double lat, double lon, int forceZone = 0);
void fromUtm(const Utm& utm, double& lat, double& lon);
Ups toUps(double lat, double lon);
void fromUps(const Ups& ups, double& lat, double& lon);
std::string toMgrs(double lat, double lon, int digits = 5);
bool fromMgrs(const std::string& mgrs, double& lat, double& lon);

}
