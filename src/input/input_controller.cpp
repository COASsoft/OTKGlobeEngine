#include "otk/input.hpp"

#include <algorithm>
#include <cmath>

#include "otk/engine.hpp"

namespace otk {

namespace {

constexpr double kDeg = 57.29577951308232;

double wrapDegrees(double d) {
    d = std::fmod(d + 540.0, 360.0);
    if (d < 0) d += 360.0;
    return d - 180.0;
}

int keyIndex(Key k) { return static_cast<int>(k); }

}

InputController::InputController(Engine& engine) : engine_(engine) {}

void InputController::setViewport(double width, double height, double dpiScale) {
    width_ = std::max(width, 1.0);
    height_ = std::max(height, 1.0);
    dpi_ = std::max(dpiScale, 0.5);
}

InputController::Pointer* InputController::find(int id) {
    for (auto& p : pointers_)
        if (p.id == id) return &p;
    return nullptr;
}

void InputController::centroid(double& x, double& y) const {
    x = y = 0;
    if (pointers_.empty()) return;
    for (const auto& p : pointers_) {
        x += p.x;
        y += p.y;
    }
    x /= static_cast<double>(pointers_.size());
    y /= static_cast<double>(pointers_.size());
}

void InputController::beginMulti() {
    mode_ = Mode::MultiPending;
    for (auto& p : pointers_) {
        p.startX = p.x;
        p.startY = p.y;
    }
    centroid(startCx_, startCy_);
    lastCx_ = startCx_;
    lastCy_ = startCy_;
    const Pointer& a = pointers_[0];
    const Pointer& b = pointers_[1];
    startDist_ = lastDist_ = std::hypot(b.x - a.x, b.y - a.y);
    lastAngle_ = std::atan2(b.y - a.y, b.x - a.x) * kDeg;
    rotateAccum_ = 0;
    rotating_ = false;
    samples_.clear();
}

void InputController::pointerDown(int id, double x, double y, PointerType type, MouseButton button, int modifiers) {
    flinging_ = false;
    velX_ = velY_ = 0;
    if (Pointer* existing = find(id)) {
        existing->x = x;
        existing->y = y;
        return;
    }
    Clock::time_point now = Clock::now();
    Pointer p;
    p.id = id;
    p.type = type;
    p.button = button;
    p.modifiers = modifiers;
    p.x = p.startX = p.lastX = x;
    p.y = p.startY = p.lastY = y;
    p.down = now;
    pointers_.push_back(p);
    if (pointers_.size() == 1) {
        gestureStart_ = now;
        moved_ = false;
        longPressFired_ = false;
        maxPointers_ = 1;
        samples_.clear();
        samples_.push_back({now, x, y});
        bool second = tapArmed_ && type != PointerType::Mouse && settings_.doubleTapDragZoom && lastTapType_ == type &&
                      seconds(lastTapTime_, now) < settings_.doubleTapSeconds &&
                      std::hypot(x - lastTapX_, y - lastTapY_) < px(settings_.doubleTapSlopDp);
        if (second) {
            mode_ = Mode::ZoomDrag;
            zoomDragAnchorX_ = x;
            zoomDragAnchorY_ = y;
        } else {
            mode_ = Mode::Pending;
        }
    } else {
        maxPointers_ = std::max(maxPointers_, static_cast<int>(pointers_.size()));
        tapArmed_ = false;
        beginMulti();
    }
}

void InputController::pointerMove(int id, double x, double y, int modifiers) {
    Pointer* p = find(id);
    if (!p) return;
    double prevX = p->x, prevY = p->y;
    p->x = x;
    p->y = y;
    p->modifiers = modifiers;
    if (pointers_.size() >= 2) {
        multiMove();
        return;
    }
    Clock::time_point now = Clock::now();
    double threshold = px(settings_.dragThresholdDp);
    if (mode_ == Mode::Pending) {
        if (std::hypot(x - p->startX, y - p->startY) <= threshold) return;
        moved_ = true;
        tapArmed_ = false;
        bool rotateTilt = p->type == PointerType::Mouse &&
                          (p->button == MouseButton::Right || p->button == MouseButton::Middle ||
                           (p->modifiers & (ModCtrl | ModAlt)) != 0);
        if (rotateTilt) {
            mode_ = Mode::RotateTilt;
            prevX = p->startX;
            prevY = p->startY;
        } else {
            mode_ = Mode::Pan;
            engine_.pan(p->startX, p->startY, x, y);
            samples_.push_back({now, x, y});
            return;
        }
    }
    if (mode_ == Mode::Pan) {
        engine_.pan(prevX, prevY, x, y);
        samples_.push_back({now, x, y});
        while (samples_.size() > 2 && seconds(samples_.front().t, now) > 0.12) samples_.erase(samples_.begin());
    } else if (mode_ == Mode::RotateTilt) {
        double dx = x - prevX, dy = y - prevY;
        if (settings_.rotate && dx != 0)
            engine_.rotate(-dx / width_ * settings_.rotateDegreesPerScreen);
        if (settings_.tilt && dy != 0) engine_.tiltBy(-dy / height_ * settings_.tiltDegreesPerScreen);
    } else if (mode_ == Mode::ZoomDrag) {
        if (!moved_ && std::hypot(x - p->startX, y - p->startY) <= threshold) return;
        if (!moved_) {
            moved_ = true;
            prevY = p->startY;
        }
        double dy = y - prevY;
        if (dy != 0) engine_.zoomAt(zoomDragAnchorX_, zoomDragAnchorY_, std::exp(-dy / (height_ * 0.3)));
    }
}

void InputController::multiMove() {
    double cx, cy;
    centroid(cx, cy);
    const Pointer& a = pointers_[0];
    const Pointer& b = pointers_[1];
    double d = std::hypot(b.x - a.x, b.y - a.y);
    double angle = std::atan2(b.y - a.y, b.x - a.x) * kDeg;
    double threshold = px(settings_.dragThresholdDp);
    if (mode_ == Mode::MultiPending) {
        double spread = std::abs(d - startDist_);
        double travel = std::hypot(cx - startCx_, cy - startCy_);
        double turn = std::abs(wrapDegrees(angle - lastAngle_));
        double dy0 = a.y - a.startY, dy1 = b.y - b.startY, dx0 = a.x - a.startX, dx1 = b.x - b.startX;
        bool parallelVertical = dy0 * dy1 > 0 && std::min(std::abs(dy0), std::abs(dy1)) > threshold &&
                                std::abs(dy0) > 1.5 * std::abs(dx0) && std::abs(dy1) > 1.5 * std::abs(dx1) &&
                                spread < threshold * 2.5 && turn < 8.0;
        if (settings_.tilt && pointers_.size() >= 3 && std::abs(cy - startCy_) > threshold) {
            mode_ = Mode::Tilt;
        } else if (settings_.tilt && parallelVertical) {
            mode_ = Mode::Tilt;
        } else if (spread > threshold * 1.5 || travel > threshold * 1.5 || turn > settings_.rotateThresholdDegrees) {
            mode_ = Mode::Multi;
        } else {
            return;
        }
        moved_ = true;
        lastCx_ = startCx_;
        lastCy_ = startCy_;
        lastDist_ = startDist_;
    }
    if (mode_ == Mode::Tilt) {
        engine_.tiltBy(-(cy - lastCy_) / height_ * settings_.tiltDegreesPerScreen);
    } else if (mode_ == Mode::Multi) {
        engine_.pan(lastCx_, lastCy_, cx, cy);
        if (lastDist_ > 1.0 && d > 1.0) engine_.zoomAt(cx, cy, lastDist_ / d);
        if (settings_.rotate) {
            double da = wrapDegrees(angle - lastAngle_);
            if (!rotating_) {
                rotateAccum_ += da;
                if (std::abs(rotateAccum_) > settings_.rotateThresholdDegrees) {
                    rotating_ = true;
                    da = rotateAccum_ - std::copysign(settings_.rotateThresholdDegrees, rotateAccum_);
                } else {
                    da = 0;
                }
            }
            if (rotating_ && da != 0) engine_.rotateAt(cx, cy, -da);
        }
    }
    lastCx_ = cx;
    lastCy_ = cy;
    lastDist_ = d;
    lastAngle_ = angle;
}

void InputController::pointerUp(int id, double x, double y) {
    if (Pointer* p = find(id)) {
        if (p->x != x || p->y != y) pointerMove(id, x, y, p->modifiers);
    }
    endPointer(id, false);
}

void InputController::pointerCancel(int id) { endPointer(id, true); }

void InputController::cancelAll() {
    pointers_.clear();
    mode_ = Mode::Idle;
    flinging_ = false;
    samples_.clear();
}

void InputController::endPointer(int id, bool cancelled) {
    auto it = std::find_if(pointers_.begin(), pointers_.end(), [id](const Pointer& p) { return p.id == id; });
    if (it == pointers_.end()) return;
    Pointer p = *it;
    double cx, cy;
    centroid(cx, cy);
    pointers_.erase(it);
    Clock::time_point now = Clock::now();
    if (pointers_.empty()) {
        Mode ended = mode_;
        mode_ = Mode::Idle;
        if (cancelled) {
            samples_.clear();
            tapArmed_ = false;
            return;
        }
        bool primary = p.type != PointerType::Mouse || p.button == MouseButton::Left;
        if (ended == Mode::Pending && !moved_ && maxPointers_ == 1 && !longPressFired_ && primary) {
            bool second = tapArmed_ && lastTapType_ == p.type && seconds(lastTapTime_, now) < settings_.doubleTapSeconds &&
                          std::hypot(p.x - lastTapX_, p.y - lastTapY_) < px(settings_.doubleTapSlopDp);
            if (second) {
                tapArmed_ = false;
                if (settings_.doubleTapZoom) zoomAnimated(p.x, p.y, 0.5);
                if (tapHandler_) tapHandler_(p.x, p.y, 2, p.type);
            } else {
                tapArmed_ = true;
                lastTapTime_ = now;
                lastTapX_ = p.x;
                lastTapY_ = p.y;
                lastTapType_ = p.type;
                if (tapHandler_) tapHandler_(p.x, p.y, 1, p.type);
            }
        } else if (ended == Mode::ZoomDrag) {
            tapArmed_ = false;
            if (!moved_) {
                if (settings_.doubleTapZoom) zoomAnimated(zoomDragAnchorX_, zoomDragAnchorY_, 0.5);
                if (tapHandler_) tapHandler_(p.x, p.y, 2, p.type);
            }
        } else if (!moved_ && maxPointers_ == 2 && seconds(gestureStart_, now) < 0.4 && settings_.twoFingerTapZoomOut) {
            tapArmed_ = false;
            zoomAnimated(cx, cy, 2.0);
        } else if (ended == Mode::Pan && settings_.inertia && samples_.size() >= 2) {
            const Sample& first = samples_.front();
            const Sample& last = samples_.back();
            double dt = seconds(first.t, last.t);
            if (dt > 0.01 && seconds(last.t, now) < 0.06) {
                double vx = (last.x - first.x) / dt, vy = (last.y - first.y) / dt;
                double speed = std::hypot(vx, vy);
                double cap = px(5000.0);
                if (speed > px(120.0)) {
                    if (speed > cap) {
                        vx *= cap / speed;
                        vy *= cap / speed;
                    }
                    velX_ = vx;
                    velY_ = vy;
                    flinging_ = true;
                }
            }
        }
        samples_.clear();
        return;
    }
    if (pointers_.size() == 1) {
        Pointer& r = pointers_[0];
        r.startX = r.x;
        r.startY = r.y;
        mode_ = moved_ ? Mode::Pan : Mode::Pending;
        samples_.clear();
        samples_.push_back({now, r.x, r.y});
        return;
    }
    bool wasMoved = moved_;
    beginMulti();
    if (wasMoved) mode_ = Mode::Multi;
}

void InputController::wheel(double x, double y, double dx, double dy, bool precise, int modifiers) {
    flinging_ = false;
    if ((modifiers & ModShift) && settings_.tilt) {
        engine_.tiltBy(dy * (precise ? 0.15 : 4.0));
        return;
    }
    if ((modifiers & ModAlt) && settings_.rotate) {
        engine_.rotate(dy * (precise ? 0.3 : 7.5));
        return;
    }
    if (precise && !(modifiers & ModCtrl)) {
        if (settings_.preciseScrollPans) {
            engine_.pan(x, y, x + dx, y + dy);
        } else {
            engine_.zoomAt(x, y, std::exp(-dy * 0.004));
        }
        return;
    }
    if (precise) {
        engine_.zoomAt(x, y, std::exp(-dy * 0.01));
        return;
    }
    if (dy != 0) zoomAnimated(x, y, std::pow(settings_.wheelZoomPerNotch, -dy), 0.18);
}

void InputController::magnify(double x, double y, double scale) {
    if (scale > 0) engine_.zoomAt(x, y, 1.0 / scale);
}

void InputController::rotateGesture(double x, double y, double degrees) {
    if (settings_.rotate) engine_.rotateAt(x, y, degrees);
}

void InputController::zoomAnimated(double x, double y, double factor, double seconds) {
    if (factor <= 0) return;
    if (zoomAnimLog_ == 0 || std::hypot(x - zoomAnimX_, y - zoomAnimY_) > px(30.0)) {
        zoomAnimX_ = x;
        zoomAnimY_ = y;
    }
    zoomAnimLog_ += std::log(factor);
    zoomAnimRate_ = std::max(seconds, 0.02) / 3.0;
}

void InputController::keyDown(Key key, int modifiers) {
    int i = keyIndex(key);
    if (i < 0 || i >= static_cast<int>(sizeof(keys_) / sizeof(keys_[0]))) return;
    flinging_ = false;
    if (key == Key::ResetNorth || key == Key::ResetTilt) {
        View v = engine_.view();
        if (key == Key::ResetNorth) v.azimuth = 0;
        else v.tilt = 0;
        engine_.flyTo(v, 0.4);
        return;
    }
    if (!keys_[i]) {
        if (key == Key::ZoomIn) zoomAnimated(width_ * 0.5, height_ * 0.5, 0.5, 0.25);
        if (key == Key::ZoomOut) zoomAnimated(width_ * 0.5, height_ * 0.5, 2.0, 0.25);
    }
    keys_[i] = key != Key::ZoomIn && key != Key::ZoomOut;
    (void)modifiers;
}

void InputController::keyUp(Key key) {
    int i = keyIndex(key);
    if (i >= 0 && i < static_cast<int>(sizeof(keys_) / sizeof(keys_[0]))) keys_[i] = false;
}

void InputController::update() {
    Clock::time_point now = Clock::now();
    double dt = updated_ ? std::clamp(seconds(lastUpdate_, now), 0.0, 0.1) : 0.0;
    lastUpdate_ = now;
    updated_ = true;
    if (pointers_.size() == 1 && mode_ == Mode::Pending && !longPressFired_ && pointers_[0].type != PointerType::Mouse &&
        seconds(pointers_[0].down, now) >= settings_.longPressSeconds) {
        longPressFired_ = true;
        tapArmed_ = false;
        if (longPressHandler_) longPressHandler_(pointers_[0].x, pointers_[0].y, pointers_[0].type);
    }
    if (dt <= 0) return;
    double cx = width_ * 0.5, cy = height_ * 0.5;
    if (flinging_) {
        engine_.pan(cx, cy, cx + velX_ * dt, cy + velY_ * dt);
        double decay = std::exp(-settings_.inertiaDecay * dt);
        velX_ *= decay;
        velY_ *= decay;
        if (std::hypot(velX_, velY_) < px(15.0)) flinging_ = false;
    }
    if (zoomAnimLog_ != 0) {
        double step = zoomAnimLog_ * (1.0 - std::exp(-dt / zoomAnimRate_));
        if (std::abs(zoomAnimLog_ - step) < 0.002) step = zoomAnimLog_;
        engine_.zoomAt(zoomAnimX_, zoomAnimY_, std::exp(step));
        zoomAnimLog_ -= step;
        if (std::abs(zoomAnimLog_) < 1e-9) zoomAnimLog_ = 0;
    }
    auto held = [&](Key k) { return keys_[keyIndex(k)]; };
    double h = (held(Key::Right) ? 1.0 : 0.0) - (held(Key::Left) ? 1.0 : 0.0);
    double v = (held(Key::Down) ? 1.0 : 0.0) - (held(Key::Up) ? 1.0 : 0.0);
    double r = (held(Key::RotateRight) ? 1.0 : 0.0) - (held(Key::RotateLeft) ? 1.0 : 0.0);
    double t = (held(Key::TiltUp) ? 1.0 : 0.0) - (held(Key::TiltDown) ? 1.0 : 0.0);
    if (engine_.firstPerson()) {
        if (v != 0) engine_.walk(-v * 20.0 * dt, 0);
        if (h != 0 || r != 0) engine_.look((h + r) * settings_.keyRotateDegreesPerSecond * dt, 0);
        if (t != 0) engine_.look(0, t * settings_.keyTiltDegreesPerSecond * dt);
        return;
    }
    if (h != 0 || v != 0) {
        double step = settings_.keyPanScreensPerSecond * std::min(width_, height_) * dt;
        engine_.pan(cx, cy, cx - h * step, cy - v * step);
    }
    if (r != 0 && settings_.rotate) engine_.rotate(r * settings_.keyRotateDegreesPerSecond * dt);
    if (t != 0 && settings_.tilt) engine_.tiltBy(t * settings_.keyTiltDegreesPerSecond * dt);
}

bool InputController::active() const {
    for (bool k : keys_)
        if (k) return true;
    return flinging_ || zoomAnimLog_ != 0 ||
           (pointers_.size() == 1 && mode_ == Mode::Pending && !longPressFired_ && pointers_[0].type != PointerType::Mouse);
}

}
