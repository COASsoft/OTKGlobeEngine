#pragma once

#include "ote/features.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ote {

struct GeodesicLine {
    double distance = 0;
    double initialBearing = 0;
    double finalBearing = 0;
};

GeodesicLine geodesicInverse(double lat1, double lon1, double lat2, double lon2);
Coord geodesicDirect(const Coord& from, double bearingDegrees, double distanceMeters, double* finalBearing = nullptr);
std::vector<Coord> geodesicPath(const Coord& from, const Coord& to, double maxSegmentMeters = 1000.0);
double geodesicArea(const std::vector<Coord>& ring);

struct MagneticField {
    double declination = 0;
    double inclination = 0;
    double north = 0;
    double east = 0;
    double down = 0;
    double horizontal = 0;
    double total = 0;
    double validFrom = 0;
    double validTo = 0;
};

MagneticField magneticField(double lat, double lon, double altitudeMeters, double decimalYear);
double currentDecimalYear();
double magneticDeclination(double lat, double lon, double altitudeMeters = 0, double decimalYear = 0);
double gridConvergence(double lat, double lon);

enum class NorthReference { True, Magnetic, Grid };
enum class AngleUnit { Degrees, Mils };
enum class DistanceUnit { Metric, Imperial, Nautical };

double bearingFromTrue(double trueBearing, double lat, double lon, NorthReference reference);
double bearingToTrue(double bearing, double lat, double lon, NorthReference reference);
double degreesToMils(double degrees);
double milsToDegrees(double mils);
std::string formatBearing(double bearing, AngleUnit unit = AngleUnit::Degrees, NorthReference reference = NorthReference::True);
std::string formatDistance(double meters, DistanceUnit unit = DistanceUnit::Metric);

enum class CoordinateFormat { Decimal, DegreesMinutes, DegreesMinutesSeconds, Mgrs, Utm, Gars };

std::string formatCoordinate(double lat, double lon, CoordinateFormat format, int precision = -1);
bool parseCoordinate(const std::string& text, double& lat, double& lon);
std::string toGars(double lat, double lon, int precision = 3);
bool fromGars(const std::string& gars, double& lat, double& lon);

struct ShapeStyle {
    std::uint32_t stroke = 0xFFFFD000;
    std::uint32_t fill = 0;
    float width = 2.0f;
    std::uint32_t label = 0xFFFFFFFF;
    bool labels = true;
};

Feature geodesicCircle(const Coord& center, double radiusMeters, const ShapeStyle& style = {}, int segments = 128);
Feature geodesicEllipse(const Coord& center, double semiMajorMeters, double semiMinorMeters, double orientationDegrees,
                        const ShapeStyle& style = {}, int segments = 128);
Feature sector(const Coord& center, double radiusMeters, double fromBearing, double toBearing, const ShapeStyle& style = {},
               double innerRadiusMeters = 0);
std::vector<Feature> rangeRings(const Coord& center, double spacingMeters, int count, int radials = 0, const ShapeStyle& style = {},
                                DistanceUnit unit = DistanceUnit::Metric);
std::vector<Feature> bullseye(const Coord& center, double radiusMeters, int rings = 3, double bearingStep = 30.0,
                              NorthReference reference = NorthReference::Magnetic, AngleUnit unit = AngleUnit::Degrees,
                              const ShapeStyle& style = {}, DistanceUnit distanceUnit = DistanceUnit::Metric);
std::vector<Feature> rangeBearingLine(const Coord& from, const Coord& to, NorthReference reference = NorthReference::Magnetic,
                                      AngleUnit unit = AngleUnit::Degrees, DistanceUnit distanceUnit = DistanceUnit::Metric,
                                      const ShapeStyle& style = {});

}
