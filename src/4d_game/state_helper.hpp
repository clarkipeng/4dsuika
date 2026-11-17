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
        mouseMoved = false;
    }

    void onMouseButton(int button, int action, double xpos, double ypos) {
        if (button != GLFW_MOUSE_BUTTON_RIGHT) return;
        rotating = (action == GLFW_PRESS);
        lastX = xpos;
        lastY = ypos;
    }

    // Right-drag: ordinary turning inside the slice (yaw and pitch).
    void onCursorPos(double xpos, double ypos) {
        m_xpos = float(xpos);
        m_ypos = float(ypos);
        mouseMoved = true;
        if (!rotating) return;
        const float sensitivity = 0.005f;
        pendingYaw -= float(xpos - lastX) * sensitivity;
        pendingPitch -= float(ypos - lastY) * sensitivity;
        lastX = xpos;
        lastY = ypos;
    }

    void onScroll(double yoffset) { cam.zoom(float(yoffset)); }

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
    return best;
}
