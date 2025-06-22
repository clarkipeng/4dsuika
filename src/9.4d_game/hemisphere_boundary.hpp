#pragma once

#include "boundary.hpp"


class HemisphereBoundary : public Boundary {
public:
    glm::vec4 center;
    float radius;
    float margin;
    float cutoffAngle;  // radians

    HemisphereBoundary(glm::vec4 center, float radius,
                       float angleDegrees = 90.0f,
                       float margin = 0.1f)
      : center(center), radius(radius), margin(margin)
    {
        cutoffAngle = glm::radians(angleDegrees);
    }
    void checkSphere(PhysicsObject& obj) const override {
        glm::vec4 pos4 = obj.position;
        glm::vec4 prev_pos4 = obj.last_position;

        glm::vec4 offset4 = pos4 - center;
        float dist = glm::length(offset4);
        if (dist < 1e-6f) return;

        glm::vec4 normal4 = offset4 / dist;
        bool hit = false;

        // Define your 4D "down" direction - this example uses y-axis only
        glm::vec4 down4 = glm::vec4(0, -1, 0, 0);
        
        float angle = glm::acos(glm::clamp(glm::dot(normal4, down4), -1.0f, 1.0f));

        // 4D sphere boundaries - these are 4D radii, not projected 3D distances
        float outer_4d_radius = radius + margin + obj.radius;
        float inner_4d_radius = radius - margin - obj.radius;

        if (dist > inner_4d_radius && dist < outer_4d_radius && angle < cutoffAngle) {
            // Push to appropriate boundary based on which side object is on
            float target_dist = (dist < radius) ? inner_4d_radius : outer_4d_radius;
            offset4 = normal4 * target_dist;
            pos4 = center + offset4;
            hit = true;
        }

        if (hit) {
            // Keep all 4D components
            obj.position = pos4;

            glm::vec4 velocity = pos4 - prev_pos4;
            glm::vec4 tangent = velocity - glm::dot(velocity, normal4) * normal4;
            obj.last_position = pos4 - tangent;
        }
    }

    RayInter checkRay(float w, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const override {
        RayInter out;
        
        const float inner_radius = radius - margin;
        
        // Calculate effective 3D radius at this w-slice
        float dw = center.w - w;
        if (std::abs(dw) > inner_radius) return out;
        
        // Effective 3D radius at this w-slice (Pythagorean theorem in 4D)
        float effective_3d_radius_sq = inner_radius * inner_radius - dw * dw;
        if (effective_3d_radius_sq <= 0.0f) return out;
        
        float effective_3d_radius = std::sqrt(effective_3d_radius_sq);
        
        // Ray-sphere intersection in local 3D space
        glm::vec3 L = rayOrigin - glm::vec3(center);
        float a = glm::dot(rayDirection, rayDirection);
        float b = 2.0f * glm::dot(rayDirection, L);
        float c = glm::dot(L, L) - effective_3d_radius_sq;

        float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) return out; // No intersection

        float sqrtDisc = std::sqrt(disc);
        float t0 = (-b - sqrtDisc) / (2.0f * a);
        float t1 = (-b + sqrtDisc) / (2.0f * a);

        // Check both intersection points for validity against cutoff angle
        float minY = center.y - effective_3d_radius * std::cos(cutoffAngle);

        for (float t : {t0, t1}) {
            if (t <= 0.0f) continue;

            glm::vec3 P = rayOrigin + rayDirection * t;

            // Check if point is within the bowl cap
            if (P.y <= minY) {
                out.hit      = true;
                out.distance = t;
                out.point    = P;
                return out;
            }
        }

        return out; // No valid hit
    }
};
