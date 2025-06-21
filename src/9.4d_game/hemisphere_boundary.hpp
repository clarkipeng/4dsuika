#pragma once

#include "boundary.hpp"

class HemisphereBoundary : public Boundary {
public:
    glm::vec4 center;
    float radius;
    float margin;
    float cutoffAngle; // In radians

    HemisphereBoundary(glm::vec4 center, float radius, float angleDegrees = 90.0f, float margin = 1.0f)
        : center(center), radius(radius), margin(margin)
    {
        cutoffAngle = glm::radians(angleDegrees); // convert to radians
    }

    // void enforce(PhysicsObject& obj) const override {

    //     glm::vec3 pos = glm::vec3(obj.position);
    //     glm::vec3 offset = pos - center;
    //     float dist = glm::length(offset);

    //     // Early out: object is outside sphere or above the angle cone
    //     if (dist > radius) {
    //         offset = glm::normalize(offset) * (radius - margin);
    //         pos = center + offset;
    //     }

    //     // Compute angle between offset and vertical (negative Z or positive Y depending on your up-axis)
    //     float angle = glm::acos(glm::dot(glm::normalize(offset), glm::vec3(0, 0, -1)));

    //     if (angle > cutoffAngle) {
    //         // Project point back to angle surface
    //         float height = -radius * cos(cutoffAngle);  // negative Z height
    //         float radial = radius * sin(cutoffAngle);
    //         glm::vec3 horizontal = glm::normalize(glm::vec3(offset.x, 0.0f, offset.y)) * radial;
    //         pos = center + horizontal + glm::vec3(0, height, 0); // bowl shape in -Z
    //     }

    //     obj.position = glm::vec4(pos, obj.position.w);
    // }

    // void enforce(PhysicsObject& obj) const override {
    //     glm::vec3 pos = glm::vec3(obj.position);
    //     glm::vec3 offset = pos - center;
    //     float dist = glm::length(offset);

    //     if (dist < 1e-6f) return; // avoid divide-by-zero in normalize

    //     glm::vec3 normal = offset / dist;

    //     bool hit = false;

    //     // Enforce max radius constraint (keep inside bowl)
    //     if (dist > radius) {
    //         offset = normal * (radius - margin);
    //         pos = center + offset;
    //         hit = true;
    //     }

    //     // Clamp to hemisphere angle
    //     float angle = glm::acos(glm::clamp(glm::dot(normal, glm::vec3(0, -1, 0)), -1.0f, 1.0f));
    //     if (angle > cutoffAngle) {
    //         glm::vec3 axis = glm::cross(normal, glm::vec3(0, -1, 0));
    //         if (glm::length(axis) > 1e-6f) {
    //             axis = glm::normalize(axis);
    //             glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle - cutoffAngle, axis);
    //             glm::vec3 clamped = glm::vec3(rot * glm::vec4(normal * (radius - margin), 1.0f));
    //             pos = center + clamped;
    //             hit = true;
    //         }
    //     }

    //     if (hit) {
    //         obj.position = glm::vec4(pos, obj.position.w);
    //         obj.last_position = obj.position; // zero out Verlet velocity
    //     }
    // }
    // void enforce(PhysicsObject& obj) const override {
    //     glm::vec3 pos = glm::vec3(obj.position);
    //     glm::vec3 prev_pos = glm::vec3(obj.last_position);
    //     glm::vec3 offset = pos - center;
    //     float dist = glm::length(offset);

    //     if (dist < 1e-6f) return;

    //     glm::vec3 normal = offset / dist;
    //     bool hit = false;

    //     // Clamp to outer radius
    //     float angle = glm::acos(glm::clamp(glm::dot(normal, glm::vec3(0, -1,0)), -1.0f, 1.0f));
    //     if (dist > radius && angle < cutoffAngle ) {
    //         offset = normal * (radius - margin);
    //         pos = center + offset;
    //         hit = true;
    //     }

    //     // Clamp to hemisphere angle
    //     if (angle > cutoffAngle && dist < radius) {
    //         glm::vec3 axis = glm::cross(normal, glm::vec3(0, -1, 0));
    //         if (glm::length(axis) > 1e-6f) {
    //             axis = glm::normalize(axis);
    //             glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle - cutoffAngle, axis);
    //             glm::vec3 clamped = glm::vec3(rot * glm::vec4(normal * (radius - margin), 1.0f));
    //             pos = center + clamped;
    //             hit = true;
    //         }
    //     }

    //     if (hit) {
    //         glm::vec3 velocity = pos - prev_pos;

    //         // Project velocity onto the tangent plane to retain sliding
    //         glm::vec3 tangent = velocity - glm::dot(velocity, normal) * normal;
    //         obj.last_position = glm::vec4(pos - tangent, obj.last_position.w); // Verlet-preserving projection
    //         obj.position = glm::vec4(pos, obj.position.w);
    //     }
    // }
    
    
    void enforce(PhysicsObject& obj) const override {
        glm::vec4 pos4 = obj.position;
        glm::vec4 prev_pos4 = obj.last_position;

        glm::vec4 offset4 = pos4 - center;
        float dist = glm::length(offset4);
        if (dist < 1e-6f) return;

        glm::vec4 normal4 = offset4 / dist;
        bool hit = false;

        glm::vec4 down4 = glm::vec4(0, -1, 0, 0); // Define your "up/down" direction

        float angle = glm::acos(glm::clamp(glm::dot(normal4, down4), -1.0f, 1.0f));

        float outer_limit = radius + margin + obj.radius;
        float inner_limit = radius - margin - obj.radius;

        // -- Case 1: Clamp to outer shell if inside wall thickness and within bowl opening
        if (dist > inner_limit && dist < outer_limit && angle < cutoffAngle) {
            offset4 = normal4 * inner_limit;
            pos4 = center + offset4;
            hit = true;
        }

        // // -- Case 2: Clamp to angle if beyond cutoff angle and within inner limit
        // if (angle > cutoffAngle && dist < inner_limit) {
        //     // Construct a 4D rotation to clamp angle back to cutoff
        //     glm::vec4 projected = normal4 - glm::dot(normal4, down4) * down4;
        //     float proj_len = glm::length(projected);
        //     if (proj_len > 1e-6f) {
        //         glm::vec4 side = projected / proj_len;

        //         // Compute new direction at cutoff angle
        //         glm::vec4 newDir = glm::cos(cutoffAngle) * down4 + glm::sin(cutoffAngle) * side;
        //         pos4 = center + newDir * inner_limit;
        //         normal4 = newDir;
        //         hit = true;
        //     }
        // }

        if (hit) {
            obj.position = glm::vec4(pos4.x, pos4.y, pos4.z, obj.position.w);

            glm::vec4 velocity = pos4 - prev_pos4;
            glm::vec4 tangent = velocity - glm::dot(velocity, normal4) * normal4;
            obj.last_position = glm::vec4(pos4 - tangent);
        }
    }





};
