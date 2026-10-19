#pragma once

#include <algorithm>
#include <cmath>

#include "core/geo.hpp"

#include <functional>

namespace ote {

class Camera {
public:
    GeoPoint focus{20.0, -40.0, 0.0};
    double gsd = 20000.0;
    double azimuth = 0.0;
    double tilt = 0.0;
    int width = 1;
    int height = 1;
    double fovY = 45.0 * kDegToRad;
    bool zeroToOneDepth = false;
    std::function<double(double, double)> groundHeight;
    bool firstPerson = false;
    GeoPoint fpPosition;
    double fpHeight = 2.0;
    double fpHeading = 0.0;
    double fpPitch = 0.0;
    bool lock2d = false;

    static constexpr double kMinGsd = 0.02;
    static constexpr double kMaxRange = 4.0e7;
    static constexpr double kMaxTilt = 84.0;
    static constexpr double kCollideRadius = 10.0;

    void update();
    void updateFirstPerson();

    double range() const { return gsd * (height * 0.5) / std::tan(fovY * 0.5); }
    double clearance() const { return std::max(kCollideRadius, range() * 0.02); }
    double maxTiltForRange(double r) const;
    void setRange(double r);
    void clamp();

    Vec3 eye() const { return eye_; }
    Vec3 focusEcef() const { return focusEcef_; }
    const Mat4& viewProjRel() const { return viewProjRel_; }
    const Mat4& invViewProjRel() const { return invViewProjRel_; }
    const Mat4& projection() const { return proj_; }
    double nearPlane() const { return near_; }
    double farPlane() const { return far_; }
    double eyeAltitude() const { return eyeAlt_; }
    double eyeAboveGround() const { return eyeAboveGround_; }
    double pixelSizeAt(double distance) const { return 2.0 * distance * std::tan(fovY * 0.5) / height; }

    Vec3 rayDirection(double sx, double sy) const;
    bool screenToEcef(double sx, double sy, Vec3& out, double h = 0.0) const;
    bool ecefToScreen(Vec3 p, double& sx, double& sy) const;

private:
    Vec3 computeEye(double tiltDeg) const;

    Vec3 eye_;
    Vec3 focusEcef_;
    Mat4 viewRel_;
    Mat4 proj_;
    Mat4 viewProjRel_;
    Mat4 invViewProjRel_;
    double near_ = 1.0;
    double far_ = 1.0;
    double eyeAlt_ = 0.0;
    double eyeAboveGround_ = 0.0;
    double ground(const GeoPoint& g) const {
        double h = groundHeight ? groundHeight(g.lat, g.lon) : 0.0;
        return std::isfinite(h) ? h : (std::isfinite(focus.alt) ? focus.alt : 0.0);
    }
};

}
