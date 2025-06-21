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
    
    
    void setPosition(glm::vec4 pos)
    {
        position      = pos;
        last_position = pos;
    }

    void update(float dt)
    {
        if (growing){
            radius += target_radius * dt;
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