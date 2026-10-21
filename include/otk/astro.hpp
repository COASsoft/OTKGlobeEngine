#pragma once

namespace otk {

struct CelestialPosition {
    double azimuth = 0.0;
    double elevation = 0.0;
    double apparentElevation = 0.0;
    double distanceKm = 0.0;
};

struct MoonInfo {
    CelestialPosition position;
    double illuminatedFraction = 0.0;
    double phaseAngle = 0.0;
    bool waxing = false;
};

struct Illuminance {
    double sunLux = 0.0;
    double moonLux = 0.0;
    double totalLux = 0.0;
};

struct SunMoonTimes {
    double astronomicalDawn = 0.0;
    double nauticalDawn = 0.0;
    double civilDawn = 0.0;
    double sunrise = 0.0;
    double sunset = 0.0;
    double civilDusk = 0.0;
    double nauticalDusk = 0.0;
    double astronomicalDusk = 0.0;
    double moonrise = 0.0;
    double moonset = 0.0;
};

CelestialPosition sunPosition(double unixSeconds, double lat, double lon, double heightM = 0.0);
MoonInfo moonPosition(double unixSeconds, double lat, double lon, double heightM = 0.0);
Illuminance groundIlluminance(double unixSeconds, double lat, double lon);
SunMoonTimes sunMoonTimes(double startUnixSeconds, double lat, double lon, double hours = 24.0);

}
