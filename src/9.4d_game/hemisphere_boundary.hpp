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
        const float inner_radius = radius - margin;
        const float outer_radius = radius + margin;
        const float required_dist = obj.radius;

        glm::vec4 pos = obj.position;
        glm::vec4 prev = obj.last_position;

        glm::vec4 offset = pos - center;
        float dist = glm::length(offset);
        if (dist < 1e-6f) return;

        glm::vec4 normal = offset / dist;
        glm::vec4 down = glm::vec4(0, -1, 0, 0);
        float cos_theta = glm::clamp(glm::dot(normal, down), -1.0f, 1.0f);
        float angle = glm::acos(cos_theta);

        // Clamp normal to cutoffAngle cone (if above it)
        if (angle > cutoffAngle) {
            float clamped_cos = glm::cos(cutoffAngle);
            glm::vec4 perp = normal - cos_theta * down;
            float perp_len = glm::length(perp);
            if (perp_len > 1e-6f) {
                perp /= perp_len;
                normal = clamped_cos * down + glm::sqrt(1.0f - clamped_cos * clamped_cos) * perp;
            } else {
                normal = glm::vec4(glm::sin(cutoffAngle), -glm::cos(cutoffAngle), 0.0f, 0.0f);
            }
            normal = glm::normalize(normal);
        }

        // Points on inner/outer shell
        glm::vec4 inner_point = center + normal * inner_radius;
        glm::vec4 outer_point = center + normal * outer_radius;

        // Rim edge point
        float rim_y = center.y - inner_radius * glm::cos(cutoffAngle);
        float rim_rad = inner_radius * glm::sin(cutoffAngle);
        glm::vec4 rim_offset = offset;
        rim_offset.y = 0;  // Flatten to horizontal rim plane

        float rim_len = glm::length(rim_offset);
        if (rim_len > 1e-6f) rim_offset = rim_offset / rim_len * rim_rad;
        else rim_offset = glm::vec4(rim_rad, 0, 0, 0); // Default rim direction

        glm::vec4 rim_point = glm::vec4(center.x + rim_offset.x, rim_y, center.z + rim_offset.z, center.w + rim_offset.w);

        // Find closest surface point
        float d_inner = glm::length(pos - inner_point);
        float d_outer = glm::length(pos - outer_point);
        float d_rim   = glm::length(pos - rim_point);

        glm::vec4 closest = inner_point;
        float min_dist = d_inner;

        if (d_outer < min_dist) {
            min_dist = d_outer;
            closest = outer_point;
        }
        if (d_rim < min_dist) {
            min_dist = d_rim;
            closest = rim_point;
        }

        if (min_dist < required_dist) {
            glm::vec4 push_dir = pos - closest;
            float len = glm::length(push_dir);
            push_dir = (len > 1e-6f) ? push_dir / len : glm::vec4(0, 1, 0, 0);
            pos = closest + push_dir * required_dist;

            obj.position = pos;

            glm::vec4 velocity = pos - prev;
            glm::vec4 tangent = velocity - glm::dot(velocity, push_dir) * push_dir;
            obj.last_position = pos - tangent;
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
