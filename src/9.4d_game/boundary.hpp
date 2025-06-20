#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include "physics_object.hpp"

class Boundary {
public:
    virtual ~Boundary() = default;

    // Called per object during boundary update
    virtual void enforce(PhysicsObject& obj, float dt, const glm::vec3& gravity) const = 0;
};