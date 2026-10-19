#include "core/camera.hpp"

#include <algorithm>

namespace ote {

double Camera::maxTiltForRange(double r) const {
    const double r0 = 2.0e5, r1 = 1.5e7;
    if (r <= r0) return kMaxTilt;
    if (r >= r1) return 15.0;
    double t = std::log(r / r0) / std::log(r1 / r0);
    return kMaxTilt + (15.0 - kMaxTilt) * t;
}

void Camera::setRange(double r) {
    gsd = r * std::tan(fovY * 0.5) / (height * 0.5);
}

void Camera::clamp() {
    focus.lat = std::clamp(focus.lat, -89.9999, 89.9999);
    focus.lon = wrapLon(focus.lon);
    azimuth = std::fmod(azimuth, 360.0);
    if (azimuth < 0) azimuth += 360.0;
    gsd = std::max(gsd, kMinGsd);
    if (range() > kMaxRange) setRange(kMaxRange);
    tilt = std::clamp(tilt, 0.0, maxTiltForRange(range()));
}

Vec3 Camera::computeEye(double tiltDeg) const {
    Vec3 e, n, u;
    enuFrame(focus.lat, focus.lon, e, n, u);
    double az = azimuth * kDegToRad, t = tiltDeg * kDegToRad;
    Vec3 fwd = n * std::cos(az) + e * std::sin(az);
    return focusEcef_ + (u * std::cos(t) - fwd * std::sin(t)) * range();
}

void Camera::updateFirstPerson() {
    fpPosition.lat = std::clamp(fpPosition.lat, -89.9999, 89.9999);
    fpPosition.lon = wrapLon(fpPosition.lon);
    fpPitch = std::clamp(fpPitch, -89.0, 89.0);
    fpHeading = std::fmod(fpHeading + 360.0, 360.0);
    fpHeight = std::max(fpHeight, 0.5);
    double g = ground(fpPosition);
    eye_ = geodeticToEcef(fpPosition.lat, fpPosition.lon, g + fpHeight);
    Vec3 e, n, u;
    enuFrame(fpPosition.lat, fpPosition.lon, e, n, u);
    double h = fpHeading * kDegToRad, p = fpPitch * kDegToRad;
    Vec3 fwd = (n * std::cos(h) + e * std::sin(h)) * std::cos(p) + u * std::sin(p);
    Vec3 right = e * std::cos(h) - n * std::sin(h);
    Vec3 up = normalize(cross(right, fwd));
    viewRel_ = lookDirection(fwd, up);
    eyeAlt_ = g + fpHeight;
    eyeAboveGround_ = fpHeight;
    double ahead = std::max(fpHeight * 50.0, 200.0);
    GeoPoint f = ecefToGeodetic(eye_ + fwd * ahead);
    focus = {f.lat, f.lon, ground(f)};
    focusEcef_ = geodeticToEcef(focus);
    azimuth = fpHeading;
    tilt = std::clamp(90.0 + fpPitch, 0.0, kMaxTilt);
    double hh = std::max(eyeAlt_, 1.0);
    double horizon = std::sqrt(hh * (2.0 * kWgs84A + hh)) + std::sqrt(9000.0 * (2.0 * kWgs84A + 9000.0));
    near_ = std::clamp(fpHeight * 0.25, 0.1, 50.0);
    far_ = horizon * 1.05 + 1000.0;
    gsd = pixelSizeAt(ahead);
    double aspect = static_cast<double>(width) / std::max(height, 1);
    proj_ = perspective(fovY, aspect, near_, far_, zeroToOneDepth);
    viewProjRel_ = proj_ * viewRel_;
    invert(viewProjRel_, invViewProjRel_);
}

void Camera::update() {
    if (firstPerson) {
        updateFirstPerson();
        return;
    }
    if (lock2d) tilt = 0.0;
    clamp();
    focusEcef_ = geodeticToEcef(focus);
    eye_ = computeEye(tilt);
    GeoPoint eg = ecefToGeodetic(eye_);
    double g = ground(eg);
    if (eg.alt < g + clearance() && tilt > 0) {
        double lo = 0, hi = tilt;
        for (int i = 0; i < 30; ++i) {
            double mid = 0.5 * (lo + hi);
            GeoPoint m = ecefToGeodetic(computeEye(mid));
            if (m.alt < ground(m) + clearance()) hi = mid;
            else lo = mid;
        }
        tilt = lo;
        eye_ = computeEye(tilt);
        eg = ecefToGeodetic(eye_);
        g = ground(eg);
    }
    if (eg.alt < g + clearance()) {
        setRange(range() + (g + clearance() - eg.alt));
        eye_ = computeEye(tilt);
        eg = ecefToGeodetic(eye_);
    }
    eyeAlt_ = eg.alt;
    eyeAboveGround_ = std::max(eg.alt - g, 1.0);

    Vec3 e, n, u;
    enuFrame(focus.lat, focus.lon, e, n, u);
    double az = azimuth * kDegToRad;
    Vec3 right = e * std::cos(az) - n * std::sin(az);
    Vec3 dir = normalize(focusEcef_ - eye_);
    Vec3 up = normalize(cross(right, dir));
    viewRel_ = lookDirection(dir, up);

    double h = std::max(eyeAlt_, 1.0);
    double horizon = std::sqrt(h * (2.0 * kWgs84A + h)) + std::sqrt(9000.0 * (2.0 * kWgs84A + 9000.0));
    near_ = std::clamp(eyeAboveGround_ * 0.05, 0.5, 1.0e6);
    far_ = std::max(horizon * 1.05 + 1000.0, range() * 1.5);
    double aspect = static_cast<double>(width) / std::max(height, 1);
    proj_ = perspective(fovY, aspect, near_, far_, zeroToOneDepth);
    viewProjRel_ = proj_ * viewRel_;
    invert(viewProjRel_, invViewProjRel_);
}

Vec3 Camera::rayDirection(double sx, double sy) const {
    double nx = 2.0 * sx / width - 1.0;
    double ny = 1.0 - 2.0 * sy / height;
    Vec4 p = invViewProjRel_ * Vec4{nx, ny, 1.0, 1.0};
    return normalize(Vec3{p.x / p.w, p.y / p.w, p.z / p.w});
}

bool Camera::screenToEcef(double sx, double sy, Vec3& out, double h) const {
    Vec3 d = rayDirection(sx, sy);
    double t;
    if (!rayEllipsoid(eye_, d, h, t)) return false;
    out = eye_ + d * t;
    return true;
}

bool Camera::ecefToScreen(Vec3 p, double& sx, double& sy) const {
    Vec3 r = p - eye_;
    Vec4 c = viewProjRel_ * Vec4{r.x, r.y, r.z, 1.0};
    if (c.w <= 0) return false;
    sx = (c.x / c.w * 0.5 + 0.5) * width;
    sy = (0.5 - c.y / c.w * 0.5) * height;
    return true;
}

}
