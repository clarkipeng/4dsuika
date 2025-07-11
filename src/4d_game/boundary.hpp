#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include "globals.h"
#include "physics_object.hpp"

class Boundary {
public:
    virtual ~Boundary() = default;

    // Called per object during boundary update
    virtual void checkSphere(PhysicsObject& obj) const = 0;

    virtual RayInter checkRay(float w, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const = 0;
};