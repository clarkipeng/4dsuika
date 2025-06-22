#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "texture.h"
#include "globals.h"


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
    // constructor(s)
    PhysicsObject(){

    }
    PhysicsObject(glm::vec4 pos, float rad, bool dyn, bool hid): position(pos),last_position(pos), acceleration(0.0f, 0.0f, 0.0f, 0.0f),  dynamic(dyn), hidden(hid)
    {
        radius = 0;
        target_radius = rad;
        growing=true;
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
    
    
    void setPosition(glm::vec4 pos)
    {
        position      = pos;
        last_position = pos;
    }

    void update(float dt)
    {
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
        return (position - last_position).length();
    }

    glm::vec4 getVelocity() const
    {
        return position - last_position;
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
    }
    void enable()
    {
        hidden=false;
    }
};