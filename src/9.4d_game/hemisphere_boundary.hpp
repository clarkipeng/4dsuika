#pragma once

#include "boundary.hpp"

class HemisphereBoundary : public Boundary {
public:
    glm::vec3 center;
    float radius;
    float margin;
    float cutoffAngle; // In radians

    HemisphereBoundary(glm::vec3 center, float radius, float angleDegrees = 90.0f, float margin = 0.01f)
        : center(center), radius(radius), margin(margin)
    {
        cutoffAngle = glm::radians(angleDegrees); // convert to radians
    }

    void enforce(PhysicsObject& obj, float dt, const glm::vec3& gravity) const override {
        obj.acceleration += gravity;
        obj.update(dt);

        glm::vec3 pos = glm::vec3(obj.position);
        glm::vec3 offset = pos - center;
        float dist = glm::length(offset);

        // Early out: object is outside sphere or above the angle cone
        if (dist > radius) {
            offset = glm::normalize(offset) * (radius - margin);
            pos = center + offset;
        }

        // Compute angle between offset and vertical (negative Z or positive Y depending on your up-axis)
        float angle = glm::acos(glm::dot(glm::normalize(offset), glm::vec3(0, 0, -1)));

        if (angle > cutoffAngle) {
            // Project point back to angle surface
            float height = -radius * cos(cutoffAngle);  // negative Z height
            float radial = radius * sin(cutoffAngle);
            glm::vec3 horizontal = glm::normalize(glm::vec3(offset.x, offset.y, 0.0f)) * radial;
            pos = center + horizontal + glm::vec3(0, 0, height); // bowl shape in -Z
        }

        obj.position = glm::vec4(pos, obj.position.w);
    }
    
};
