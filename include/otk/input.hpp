#pragma once

#include <chrono>
#include <functional>
#include <vector>

namespace otk {

class Engine;

enum class PointerType { Mouse, Touch, Pen };

enum class MouseButton { NoButton, Left, Right, Middle };

enum Modifier : int { ModShift = 1, ModCtrl = 2, ModAlt = 4, ModSuper = 8 };

enum class Key {
    Left,
    Right,
    Up,
    Down,
    ZoomIn,
    ZoomOut,
    RotateLeft,
    RotateRight,
    TiltUp,
    TiltDown,
    ResetNorth,
    ResetTilt
};

struct InputSettings {
    bool rotate = true;
    bool tilt = true;
    bool inertia = true;
    bool doubleTapZoom = true;
    bool twoFingerTapZoomOut = true;
    bool doubleTapDragZoom = true;
    bool preciseScrollPans = true;
    double dragThresholdDp = 6.0;
    double doubleTapSeconds = 0.3;
    double doubleTapSlopDp = 40.0;
    double longPressSeconds = 0.55;
    double wheelZoomPerNotch = 1.35;
    double rotateThresholdDegrees = 10.0;
    double tiltDegreesPerScreen = 120.0;
    double rotateDegreesPerScreen = 360.0;
    double keyPanScreensPerSecond = 0.6;
    double keyZoomPerSecond = 2.5;
    double keyRotateDegreesPerSecond = 75.0;
    double keyTiltDegreesPerSecond = 45.0;
    double inertiaDecay = 4.0;
};

class InputController {
public:
    using TapHandler = std::function<void(double x, double y, int taps, PointerType type)>;
    using PressHandler = std::function<void(double x, double y, PointerType type)>;

    explicit InputController(Engine& engine);

    void setSettings(const InputSettings& settings) { settings_ = settings; }
    const InputSettings& settings() const { return settings_; }
    void setViewport(double width, double height, double dpiScale);

    void pointerDown(int id, double x, double y, PointerType type = PointerType::Touch, MouseButton button = MouseButton::Left,
                     int modifiers = 0);
    void pointerMove(int id, double x, double y, int modifiers = 0);
    void pointerUp(int id, double x, double y);
    void pointerCancel(int id);
    void cancelAll();
    void wheel(double x, double y, double dx, double dy, bool precise = false, int modifiers = 0);
    void magnify(double x, double y, double scale);
    void rotateGesture(double x, double y, double degrees);
    void keyDown(Key key, int modifiers = 0);
    void keyUp(Key key);
    void zoomAnimated(double x, double y, double factor, double seconds = 0.25);

    void update();
    bool active() const;

    void onTap(TapHandler handler) { tapHandler_ = std::move(handler); }
    void onLongPress(PressHandler handler) { longPressHandler_ = std::move(handler); }

private:
    using Clock = std::chrono::steady_clock;
    enum class Mode { Idle, Pending, Pan, RotateTilt, ZoomDrag, MultiPending, Multi, Tilt };
    struct Pointer {
        int id = 0;
        PointerType type = PointerType::Touch;
        MouseButton button = MouseButton::Left;
        int modifiers = 0;
        double x = 0, y = 0;
        double startX = 0, startY = 0;
        double lastX = 0, lastY = 0;
        Clock::time_point down;
    };
    struct Sample {
        Clock::time_point t;
        double x, y;
    };

    Pointer* find(int id);
    void beginMulti();
    void multiMove();
    void endPointer(int id, bool cancelled);
    double px(double dp) const { return dp * dpi_; }
    double seconds(Clock::time_point a, Clock::time_point b) const { return std::chrono::duration<double>(b - a).count(); }
    void centroid(double& x, double& y) const;

    Engine& engine_;
    InputSettings settings_;
    double width_ = 1, height_ = 1, dpi_ = 1;
    std::vector<Pointer> pointers_;
    Mode mode_ = Mode::Idle;
    bool moved_ = false;
    bool longPressFired_ = false;
    int maxPointers_ = 0;
    Clock::time_point gestureStart_;
    double lastCx_ = 0, lastCy_ = 0, lastDist_ = 0, lastAngle_ = 0;
    double startCx_ = 0, startCy_ = 0, startDist_ = 0;
    double rotateAccum_ = 0;
    bool rotating_ = false;
    std::vector<Sample> samples_;
    double velX_ = 0, velY_ = 0;
    bool flinging_ = false;
    Clock::time_point lastTapTime_;
    double lastTapX_ = -1e9, lastTapY_ = -1e9;
    PointerType lastTapType_ = PointerType::Touch;
    bool tapArmed_ = false;
    double zoomDragAnchorX_ = 0, zoomDragAnchorY_ = 0;
    double zoomAnimX_ = 0, zoomAnimY_ = 0, zoomAnimLog_ = 0, zoomAnimRate_ = 0;
    bool keys_[12] = {};
    Clock::time_point lastUpdate_;
    bool updated_ = false;
    TapHandler tapHandler_;
    PressHandler longPressHandler_;
};

}
