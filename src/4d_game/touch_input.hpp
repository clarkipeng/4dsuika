#pragma once
// Touch controls for the browser build (docs/controls.md, Touch). Gestures
// become the same mouse and wheel input the desktop game handles:
//   one finger      aim while it moves, drop when it lifts; on a HUD button or
//                   in a menu it is a left press and release
//   two fingers     drag: orbit (right drag); pinch: zoom (Shift+wheel);
//                   twist: the wheel (turn the slice, or the drop's w in 4D eye)
//   three fingers   drag: turn freely into 4D (middle drag)
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>

#include <cmath>

class TouchInput {
public:
    // The game's input entry points (main.cpp); touch only calls these.
    struct Sink {
        void (*cursor)(double x, double y);
        void (*button)(int button, int action);
        void (*wheel)(double notches, bool shift);
        bool (*active)();       // in play, not a menu
        bool (*overButton)();   // the cursor is over something clickable
        bool (*eyeView)();
        void (*used)();         // a touch arrived: show touch hints
    };

    void attach(const Sink& sink) {
        sink_ = sink;
        emscripten_set_touchstart_callback("#canvas", this, true, &TouchInput::onEvent);
        emscripten_set_touchmove_callback("#canvas", this, true, &TouchInput::onEvent);
        emscripten_set_touchend_callback("#canvas", this, true, &TouchInput::onEvent);
        emscripten_set_touchcancel_callback("#canvas", this, true, &TouchInput::onEvent);
    }

private:
    enum class Mode { None, Aim, Press, Two, Three, Done };
    struct Finger { long id; float x, y; };

    Sink sink_{};
    Mode mode_ = Mode::None;
    Finger fingers_[3] = {};
    int count_ = 0;
    // Two-finger gesture: the last spread and angle, and what has been armed.
    float spread_ = 0.0f, angle_ = 0.0f, spreadStart_ = 0.0f, angleMoved_ = 0.0f;
    bool pinching_ = false, twisting_ = false;

    static EM_BOOL onEvent(int type, const EmscriptenTouchEvent* e, void* user) {
        static_cast<TouchInput*>(user)->handle(type, *e);
        return EM_TRUE; // no scrolling, zoom, or emulated mouse events
    }

    void handle(int type, const EmscriptenTouchEvent& e) {
        sink_.used();
        const bool ended = type == EMSCRIPTEN_EVENT_TOUCHEND || type == EMSCRIPTEN_EVENT_TOUCHCANCEL;
        for (int i = 0; i < e.numTouches; ++i) {
            const EmscriptenTouchPoint& t = e.touches[i];
            if (!t.isChanged) continue;
            const float x = float(t.targetX), y = float(t.targetY);
            if (type == EMSCRIPTEN_EVENT_TOUCHSTART) down(t.identifier, x, y);
            else if (ended) up(t.identifier, type == EMSCRIPTEN_EVENT_TOUCHCANCEL);
            else moveFinger(t.identifier, x, y);
        }
        if (type == EMSCRIPTEN_EVENT_TOUCHMOVE) moved();
    }

    int find(long id) const {
        for (int i = 0; i < count_; ++i) if (fingers_[i].id == id) return i;
        return -1;
    }

    void midpoint(float& x, float& y) const {
        x = y = 0.0f;
        for (int i = 0; i < count_; ++i) { x += fingers_[i].x; y += fingers_[i].y; }
        x /= float(count_); y /= float(count_);
    }

    void measure(float& spread, float& angle) const {
        const float dx = fingers_[1].x - fingers_[0].x, dy = fingers_[1].y - fingers_[0].y;
        spread = std::sqrt(dx * dx + dy * dy);
        angle = std::atan2(dy, dx);
    }

    void down(long id, float x, float y) {
        if (count_ >= 3 || find(id) >= 0) return;
        fingers_[count_++] = {id, x, y};
        if (count_ == 1) {
            if (mode_ != Mode::None) return; // still lifting the fingers of a gesture
            sink_.cursor(x, y);
            if (!sink_.active() || sink_.overButton()) { mode_ = Mode::Press; sink_.button(0, 1); }
            else mode_ = Mode::Aim;
            return;
        }
        // A second or third finger: whatever one finger was doing is cancelled.
        if (mode_ == Mode::Press) sink_.button(0, 0);
        if (mode_ == Mode::Two) sink_.button(1, 0);
        if (!sink_.active() || mode_ == Mode::Done) { mode_ = Mode::Done; return; }
        float mx, my;
        midpoint(mx, my);
        sink_.cursor(mx, my);
        if (count_ == 2) {
            mode_ = Mode::Two;
            measure(spread_, angle_);
            spreadStart_ = spread_;
            angleMoved_ = 0.0f;
            pinching_ = twisting_ = false;
            sink_.button(1, 1);
        } else {
            mode_ = Mode::Three;
            sink_.button(2, 1);
        }
    }

    void moveFinger(long id, float x, float y) {
        int i = find(id);
        if (i >= 0) { fingers_[i].x = x; fingers_[i].y = y; }
    }

    void moved() {
        if (count_ == 0) return;
        if (mode_ == Mode::Aim || mode_ == Mode::Press) { sink_.cursor(fingers_[0].x, fingers_[0].y); return; }
        if (mode_ != Mode::Two && mode_ != Mode::Three) return;
        float mx, my;
        midpoint(mx, my);
        sink_.cursor(mx, my); // drives the orbit (right) or 4D turn (middle) drag
        if (mode_ != Mode::Two) return;
        float spread, angle;
        measure(spread, angle);
        float turn = angle - angle_;
        if (turn > 3.14159265f) turn -= 6.2831853f;
        if (turn < -3.14159265f) turn += 6.2831853f;
        // Each gesture starts only past a threshold, so a plain two-finger
        // drag does not also zoom or turn the slice.
        angleMoved_ += turn;
        if (!pinching_ && std::fabs(spread / std::max(spreadStart_, 1.0f) - 1.0f) > 0.12f) pinching_ = true;
        if (!twisting_ && std::fabs(angleMoved_) > 0.17f) twisting_ = true;
        // Zoom: one wheel notch moves the camera 1 unit; spreading the fingers
        // by a third comes in about 3 units.
        if (pinching_ && spread_ > 1.0f && spread > 1.0f) sink_.wheel(std::log(spread / spread_) * 10.0f, true);
        // Turn: one notch per 5 degrees (the slice follows the fingers), or per
        // 20 degrees for the drop's w in 4D eye.
        if (twisting_) sink_.wheel(-turn / (sink_.eyeView() ? 0.349f : 0.0873f), false);
        spread_ = spread;
        angle_ = angle;
    }

    void up(long id, bool cancelled) {
        int i = find(id);
        if (i < 0) return;
        for (int k = i; k + 1 < count_; ++k) fingers_[k] = fingers_[k + 1];
        --count_;
        switch (mode_) {
            case Mode::Aim:
                // Lifting the finger drops where it aimed (a click there).
                if (!cancelled) { sink_.button(0, 1); sink_.button(0, 0); }
                mode_ = Mode::None;
                return;
            case Mode::Press:
                sink_.button(0, 0);
                mode_ = Mode::None;
                return;
            case Mode::Two: sink_.button(1, 0); mode_ = Mode::Done; break;
            case Mode::Three: sink_.button(2, 0); mode_ = Mode::Done; break;
            default: break;
        }
        if (count_ == 0) mode_ = Mode::None;
    }
};
#endif
