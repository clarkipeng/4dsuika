#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "camera4d.hpp"
#include "globals.h"

#include "fruit.hpp"


class PhysicsObject
{
public:
    // ball state	
    glm::vec4 position;
    glm::vec4 last_position;
    glm::vec4 acceleration;

    float  target_radius;
    float  radius;
    bool   dynamic;
    bool   hidden;
    bool   growing; // used to prevent artificial velocity on first update
    float  flash;
    Fruit fruit;
    Rot4   orientation; // local fruit frame -> world frame
    std::array<float, 6> angular_velocity; // xy, xz, xw, yz, yw, zw radians/second
    // constructor(s)
    PhysicsObject()
        : position(0.0f),
          last_position(0.0f),
          acceleration(0.0f),
          target_radius(0.0f),
          radius(0.0f),
          dynamic(false),
          hidden(true),
          growing(false),
          flash(0.0f),
          fruit(Fruit::CHERRY),
          orientation(),
          angular_velocity{}
    {
    }
    PhysicsObject(glm::vec4 pos, Fruit f, bool dyn, bool hid)
        : position(pos),
          last_position(pos),
          acceleration(0.0f, 0.0f, 0.0f, 0.0f),
          target_radius(0.0f),
          radius(0.0f),
          dynamic(dyn),
          hidden(hid),
          growing(true),
          flash(0.0f),
          fruit(f),
          orientation(),
          angular_velocity{}
    {
        target_radius = FruitManager::getFruitProperties(fruit).radius;
        randomizeOrientation();
    }

    RayInter testRay(float w, const glm::vec3& rayOrigin, const glm::vec3& rayDirection){
        RayInter out;
        if (abs(position.w-w)>radius) return out;
        
        glm::vec3 oc = rayOrigin - glm::vec3(position);
        float a = glm::dot(rayDirection, rayDirection);
        float b = 2.0f * glm::dot(oc, rayDirection);
        float c = glm::dot(oc, oc) - (radius * radius - (position.w - w)*(position.w - w));
        
        float discriminant = b * b - 4 * a * c;

        
        if (discriminant < 0) {
            return out; // No intersection, hit remains false
        }
        
        // Calculate the two intersection points
        float sqrt_discriminant = sqrt(discriminant);
        float t1 = (-b - sqrt_discriminant) / (2.0f * a);
        float t2 = (-b + sqrt_discriminant) / (2.0f * a);
        
        // We want the closest positive intersection
        float t = -1.0f;
        if (t1 > 0 && t2 > 0) {
            t = std::min(t1, t2); // Both positive, take closest
        } else if (t1 > 0) {
            t = t1; // Only t1 is positive
        } else if (t2 > 0) {
            t = t2; // Only t2 is positive
        }
        
        if (t > 0) {
            out.hit = true;
            out.distance = t;
            out.point = rayOrigin + t * rayDirection;
        }
        
        return out;
    }

    void upgrade_fruit(){
        fruit = FruitManager::getNextFruit(fruit);
        // radius=0;
        target_radius = FruitManager::getFruitProperties(fruit).radius;
        growing=true;
    }
    
    void setPosition(glm::vec4 pos)
    {
        position      = pos;
        last_position = pos;
    }

    void update(float dt)
    {
        if (hidden) return;
        if (growing){
            radius += target_radius * dt * GROW_SPEED;
            if (radius>target_radius){
                radius=target_radius;
                growing=false;
            }
        }
        glm::vec4 last_update_move = position - last_position;

        glm::vec4 new_position = position + last_update_move + (acceleration - last_update_move * VELOCITY_DAMPING) * (dt * dt);
        last_position           = position;
        position                = new_position;
        acceleration = {0.0f, 0.0f, 0.0f, 0.0f};
        flash = std::max(0.0f, flash - dt * 2.4f);
        integrateSpin(dt);
    }

    void stop()
    {
        last_position = position;
    }

    void slowdown(float ratio)
    {
        last_position = last_position + ratio * (position - last_position);
    }

    float getSpeed() const
    {
        return glm::length(position - last_position);
    }

    glm::vec4 getVelocity() const
    {
        return position - last_position;
    }

    glm::mat4 worldToLocalFrame() const
    {
        return glm::transpose(orientation.matrix());
    }

    void randomizeOrientation()
    {
        orientation = Rot4{};
        const Axis4 planes[6][2] = {
            {AX_X, AX_Y}, {AX_X, AX_Z}, {AX_X, AX_W},
            {AX_Y, AX_Z}, {AX_Y, AX_W}, {AX_Z, AX_W}
        };
        for (int i = 0; i < 6; ++i) {
            float u = float(std::rand()) / float(RAND_MAX);
            float angle = (u * 2.0f - 1.0f) * 3.14159265359f;
            orientation = Rot4::plane(planes[i][0], planes[i][1], angle) * orientation;
        }
        orientation.normalize();
    }

    void addRollingSpin(glm::vec4 contact_normal, glm::vec4 tangent_velocity, float strength = 1.0f)
    {
        if (hidden || radius <= EPS) return;
        float n_len = glm::length(contact_normal);
        if (n_len <= EPS) return;
        contact_normal /= n_len;
        tangent_velocity -= contact_normal * glm::dot(tangent_velocity, contact_normal);
        if (glm::dot(tangent_velocity, tangent_velocity) <= EPS * EPS) return;

        const float k = strength / std::max(radius, 0.08f);
        angular_velocity[0] += (contact_normal.x * tangent_velocity.y - contact_normal.y * tangent_velocity.x) * k;
        angular_velocity[1] += (contact_normal.x * tangent_velocity.z - contact_normal.z * tangent_velocity.x) * k;
        angular_velocity[2] += (contact_normal.x * tangent_velocity.w - contact_normal.w * tangent_velocity.x) * k;
        angular_velocity[3] += (contact_normal.y * tangent_velocity.z - contact_normal.z * tangent_velocity.y) * k;
        angular_velocity[4] += (contact_normal.y * tangent_velocity.w - contact_normal.w * tangent_velocity.y) * k;
        angular_velocity[5] += (contact_normal.z * tangent_velocity.w - contact_normal.w * tangent_velocity.z) * k;
        clampSpin();
    }

    void integrateSpin(float dt)
    {
        const Axis4 planes[6][2] = {
            {AX_X, AX_Y}, {AX_X, AX_Z}, {AX_X, AX_W},
            {AX_Y, AX_Z}, {AX_Y, AX_W}, {AX_Z, AX_W}
        };
        bool changed = false;
        for (int i = 0; i < 6; ++i) {
            float angle = angular_velocity[i] * dt;
            if (std::abs(angle) > 1e-5f) {
                orientation = Rot4::plane(planes[i][0], planes[i][1], angle) * orientation;
                changed = true;
            }
        }
        if (changed) orientation.normalize();

        const float damping = std::exp(-1.35f * dt);
        for (float& w : angular_velocity) w *= damping;
    }

    void clampSpin()
    {
        float mag2 = 0.0f;
        for (float w : angular_velocity) mag2 += w * w;
        const float max_spin = 18.0f;
        if (mag2 > max_spin * max_spin) {
            float s = max_spin / std::sqrt(mag2);
            for (float& w : angular_velocity) w *= s;
        }
    }

    void addVelocity(glm::vec4 v)
    {
        last_position -= v;
    }

    void setPositionSameSpeed(glm::vec4 new_position)
    {
        const glm::vec4 to_last = last_position - position;
        position           = new_position;
        last_position      = position + to_last;
    }

    void move(glm::vec4 v)
    {
        position += v;
    }

    void disable()
    {
        hidden=true;
        dynamic=false;
        flash=0.0f;
    }
    void enable()
    {
        hidden=false;
    }
};
