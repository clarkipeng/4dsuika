#pragma once

#include <algorithm>
#include <cmath>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "camera4d.hpp"
#include "hemisphere_boundary.hpp"
#include "physics_solver.hpp"

// Window, mouse, and the 4D camera.
struct ViewState {
    Camera4D cam;

    bool rotating = false;
    double lastX = 0.0, lastY = 0.0;
    float pendingYaw = 0.0f, pendingPitch = 0.0f; // mouse turn since last frame
    // 4D arcball: middle-drag, or Shift+right-drag for mice without a middle
    // button. Horizontal drag turns xw, vertical yw.
    bool turning4 = false;
    float pendingTurnX = 0.0f, pendingTurnY = 0.0f;

    int windowWidth = 0, windowHeight = 0;
    float m_xpos = 0.0f, m_ypos = 0.0f;

    bool initialized = false;
    bool mouseMoved = false;

    void Init(GLFWwindow* window) {
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        initialized = true;
    }

    void reset() {
        cam = Camera4D{};
        rotating = false;
        turning4 = false;
        mouseMoved = false;
    }

    void onMouseButton(int button, int action, double xpos, double ypos, int mods = 0) {
        const bool press = action == GLFW_PRESS;
        if (button == GLFW_MOUSE_BUTTON_MIDDLE ||
            (button == GLFW_MOUSE_BUTTON_RIGHT && press && (mods & GLFW_MOD_SHIFT))) {
            turning4 = press;
            if (press) rotating = false;
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            turning4 = false;   // releasing ends a Shift+right drag whatever Shift is doing
            rotating = press;
        } else {
            return;
        }
        lastX = xpos;
        lastY = ypos;
    }

    // Right-drag: ordinary turning inside the slice (yaw and pitch).
    void onCursorPos(double xpos, double ypos) {
        m_xpos = float(xpos);
        m_ypos = float(ypos);
        mouseMoved = true;
        if (turning4) {
            const float k = 0.006f;
            pendingTurnX += float(xpos - lastX) * k;
            pendingTurnY += float(ypos - lastY) * k;
            lastX = xpos;
            lastY = ypos;
            return;
        }
        if (!rotating) return;
        const float sensitivity = 0.005f;
        pendingYaw -= float(xpos - lastX) * sensitivity;
        pendingPitch -= float(ypos - lastY) * sensitivity;
        lastX = xpos;
        lastY = ypos;
    }


    // World-space ray under the mouse cursor.
    void mouseRay(glm::vec4& origin, glm::vec4& dir) const {
        glm::vec2 ndc(2.0f * m_xpos / std::max(windowWidth, 1) - 1.0f,
                      1.0f - 2.0f * m_ypos / std::max(windowHeight, 1));
        cam.worldRay(ndc, float(windowWidth) / float(std::max(windowHeight, 1)), origin, dir);
    }
};

struct Hit4 {
    bool hit = false;
    float t = 1e30f;
    glm::vec4 point{0.0f};
    bool clamped = false;   // the cursor missed the bowl; point is the nearest spot inside the rim
};

// Nearest positive hit of a 4D ray with the glome |p - c| = r (both roots).
inline void glomeRoots(const glm::vec4& o, const glm::vec4& d, const glm::vec4& c, float r,
                       float& t0, float& t1) {
    glm::vec4 oc = o - c;
    float b = glm::dot(oc, d);
    float h = b * b - (glm::dot(oc, oc) - r * r);
    if (h < 0.0f) { t0 = t1 = -1.0f; return; }
    h = std::sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
}

// What the cursor points at in 4D: the inside of the bowl or a fruit.
// The ray lies in the camera's slice, so this works under any rotation.
inline Hit4 getPlacementMouse(const ViewState& state, const HemisphereBoundary& bowl,
                              const PhysicSolver& physics) {
    glm::vec4 o, d;
    state.mouseRay(o, d);
    Hit4 best;
    float t0, t1;
    glomeRoots(o, d, bowl.center, bowl.radius - bowl.margin, t0, t1);
    for (float t : {t0, t1}) {
        if (t <= 0.0f || t >= best.t) continue;
        glm::vec4 p = o + d * t;
        if (p.y <= bowl.center.y) best = {true, t, p};
    }
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (!physics.has_obj[i] || physics.objects[i].hidden) continue;
        const PhysicsObject& obj = physics.objects[i];
        glomeRoots(o, d, obj.position, obj.radius, t0, t1);
        if (t0 > 0.0f && t0 < best.t) best = {true, t0, o + d * t0};
    }
    if (best.hit) return best;
    // Off the bowl: aim at the rim plane (or, looking above the horizon, the
    // ray's closest approach to the bowl), pulled inside the rim. The drop then
    // follows the cursor round the edge instead of vanishing.
    const float rimY = bowl.center.y - (bowl.radius - bowl.margin) * std::cos(bowl.cutoffAngle);
    float t = d.y < -1e-3f ? (rimY - o.y) / d.y : glm::dot(bowl.center - o, d);
    if (t <= 0.0f) t = glm::dot(bowl.center - o, d);
    if (t <= 0.0f) return best;
    glm::vec4 p = o + d * t;
    p.y = rimY;
    const float reach = (bowl.radius - bowl.margin) * std::sin(bowl.cutoffAngle) - 0.3f;
    glm::vec4 off = p - bowl.center;
    off.y = 0.0f;
    float len = glm::length(off);
    if (len > reach) p = bowl.center + off * (reach / len) + glm::vec4(0.0f, rimY - bowl.center.y, 0.0f, 0.0f);
    best.hit = true;
    best.t = t;
    best.point = p;
    best.clamped = true;
    return best;
}
