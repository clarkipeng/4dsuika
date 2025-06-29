#pragma once

#include <vector>
#include <string>
#include <random>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>
#include "hemisphere_boundary.hpp"
#include "physics_solver.hpp"

#include "learnopengl/shader.h"
#include "learnopengl/filesystem.h"



struct ViewState {
    float w=0.0f;
    const float w_min=-5.0f, w_max=5.0f;

    glm::vec3 sphereCenter = glm::vec3(0.0f, 0.0f, 0.0f);
    float sphereRadius = 5.0f;

    glm::vec3 orbitTarget = sphereCenter; //+ glm::vec3(0, sphereRadius, 0);
    float radius = 8.0f;

    float yaw = glm::radians(00.0f);    // looking along +X
    float pitch = glm::radians(-20.0f);  // slight downward

    const float pitchMin = glm::radians(-89.0f);
    const float pitchMax = glm::radians(89.0f);

    bool rotating = false;
    double lastX = 0.0, lastY = 0.0;

    int windowWidth, windowHeight;
    float m_xpos, m_ypos;

    bool initialized=false;
    bool mouseMoved=false;
    void Init(GLFWwindow *window){
        glfwGetWindowSize(window,&windowWidth,&windowHeight);
        initialized=true;
    }

    void reset() {
        w = 0.0f;
        sphereCenter = glm::vec3(0.0f, 0.0f, 0.0f);
        sphereRadius = 5.0f;
        orbitTarget = sphereCenter;
        radius = 8.0f;
        yaw = glm::radians(0.0f);
        pitch = glm::radians(-20.0f);
        rotating = false;
        lastX = 0.0;
        lastY = 0.0;
        m_xpos = 0.0f;
        m_ypos = 0.0f;
        mouseMoved = false;
    }
    
    // Update camera position based on current yaw, pitch, radius
    glm::vec3 getCameraPosition() const {
        float x = radius * cos(pitch) * cos(yaw);
        float y = radius * sin(pitch);
        float z = radius * cos(pitch) * sin(yaw);

        return orbitTarget + glm::vec3(x, y, z);
    }
    glm::mat4 getViewMatrix() const {
        glm::vec3 dir = {
            cosf(pitch) * cosf(yaw),
            sinf(pitch),
            cosf(pitch) * sinf(yaw)
        };
        glm::vec3 eye = orbitTarget - dir * radius;
        return glm::lookAt(eye, orbitTarget, glm::vec3(0, 1, 0));
    }

    // Call on mouse button press/release
    void onMouseButton(int button, int action, double xpos, double ypos) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                rotating = true;
                lastX = xpos;
                lastY = ypos;
            } else if (action == GLFW_RELEASE) {
                rotating = false;
            }
        }
    }

    // Call on mouse move
    void onCursorPos(double xpos, double ypos) {
        m_xpos = xpos;
        m_ypos = ypos;
        mouseMoved=true;

        if (!rotating) return;

        float sensitivity = 0.005f;
        float dx = float(xpos - lastX);
        float dy = float(ypos - lastY);

        yaw   += dx * sensitivity;
        pitch -= dy * sensitivity;

        if (pitch > pitchMax) pitch = pitchMax;
        if (pitch < pitchMin) pitch = pitchMin;

        lastX = xpos;
        lastY = ypos;
    }

    // Call on scroll
    void onScroll(double yoffset) {
        float zoomSpeed = 1.0f;
        radius -= float(yoffset) * zoomSpeed;
        if (radius < sphereRadius + 1.0f) radius = sphereRadius + 1.0f;
        if (radius > sphereRadius + 50.0f) radius = sphereRadius + 50.0f;
    }
    
};

inline RayInter getPlacementMouse(ViewState *state, HemisphereBoundary *boundary, PhysicSolver *physics_solver){
    float winX = state->m_xpos;
    float winY = state->windowHeight - state->m_ypos;
    glm::mat4 view_matrix = state->getViewMatrix();
    glm::mat4 projection_matrix = glm::perspective(
        glm::radians(45.0f),
        float(state->windowWidth) / float(state->windowHeight),
        0.1f, 
        100.0f
    );

    glm::vec4 viewport(0, 0, state->windowWidth, state->windowHeight);

    // 3) Unproject near & far
    glm::vec3 nearPoint = glm::unProject(
        glm::vec3(winX, winY, 0.0f),
        view_matrix,
        projection_matrix,
        viewport
    );
    glm::vec3 farPoint = glm::unProject(
        glm::vec3(winX, winY, 1.0f),
        view_matrix,
        projection_matrix,
        viewport
    );

    // 4) Build ray
    glm::vec3 ray_origin    = nearPoint;
    glm::vec3 ray_direction = glm::normalize(farPoint - nearPoint);

    // 5) Intersection test
    RayInter closestResult = boundary->checkRay(state->w,ray_origin,ray_direction);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (!physics_solver->has_obj[i]) continue;
        auto result = physics_solver->objects[i].testRay(state->w, ray_origin, ray_direction);
        if (result.hit && result.distance < closestResult.distance) {
            closestResult = result;
        }
    }

    return closestResult;
}