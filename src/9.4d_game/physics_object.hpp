// #pragma once

// #include <glad/glad.h>
// #include <glm/glm.hpp>

// #include "texture.h" // Assuming this provides texture-related functionality
// #include "globals.h" // Assuming this defines MAX_OBJECTS, GROW_SPEED, VELOCITY_DAMPING, etc.
// #include "fruit.hpp" // Assuming this defines Fruit and fm (FruitManager)

// // Remove VERLET_ACCEL_DAMPING - it's not needed here and can cause confusion.
// // Damping should be handled globally in the solver or via external forces.

// class PhysicsObject
// {
// public:
//     // ball state   
//     glm::vec4 position;
//     glm::vec4 last_position;
//     glm::vec4 acceleration;

//     float  target_radius;
//     float  radius;
//     bool   dynamic;
//     bool   hidden;
//     bool   growing; // used to prevent artificial velocity on first update
//     Fruit fruit;

//     // Default constructor - CRITICAL: Initialize all members
//     PhysicsObject() : 
//         position(0.0f, 0.0f, 0.0f, 0.0f),
//         last_position(0.0f, 0.0f, 0.0f, 0.0f),
//         acceleration(0.0f, 0.0f, 0.0f, 0.0f),
//         target_radius(0.0f),
//         radius(0.0f),
//         dynamic(false),
//         hidden(true),  // Start hidden by default
//         growing(false),
//         fruit(Fruit::CHERRY) // Default fruit type
//     {
//     }

//     // Parameterized constructor - keeps original initialization
//     PhysicsObject(glm::vec4 pos, Fruit f, bool dyn, bool hid) : 
//         position(pos),
//         last_position(pos), // last_position starts same as position for zero initial velocity
//         fruit(f), 
//         acceleration(0.0f, 0.0f, 0.0f, 0.0f),
//         dynamic(dyn), 
//         hidden(hid),
//         growing(true) // Original behavior: starts growing
//     {
//         radius = 0; // Starts with zero radius
//         target_radius = fm.getFruitProperties(fruit).radius;
//     }

//     // Copy constructor - as in your original
//     PhysicsObject(const PhysicsObject& other) = default; // Can use default if trivial

//     // Assignment operator - as in your original
//     PhysicsObject& operator=(const PhysicsObject& other) = default; // Can use default if trivial

//     // RayInter testRay(float w, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) - unchanged
//     RayInter testRay(float w, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) {
//         RayInter out;
//         if (hidden) return out; // Don't test rays against hidden objects
//         // The W component check for 3D sphere in a 4D space
//         if (glm::abs(position.w - w) > radius) return out; 

//         glm::vec3 oc = rayOrigin - glm::vec3(position);
//         float a = glm::dot(rayDirection, rayDirection);
//         float b = 2.0f * glm::dot(oc, rayDirection);
//         float c = glm::dot(oc, oc) - (radius * radius - (position.w - w)*(position.w - w));

//         float discriminant = b * b - 4 * a * c;

//         if (discriminant < 0) {
//             return out; // No intersection, hit remains false
//         }

//         // Calculate the two intersection points
//         float sqrt_discriminant = sqrt(discriminant);
//         float t1 = (-b - sqrt_discriminant) / (2.0f * a);
//         float t2 = (-b + sqrt_discriminant) / (2.0f * a);

//         // We want the closest positive intersection
//         float t = -1.0f;
//         if (t1 > 0 && t2 > 0) {
//             t = std::min(t1, t2); // Both positive, take closest
//         } else if (t1 > 0) {
//             t = t1; // Only t1 is positive
//         } else if (t2 > 0) {
//             t = t2; // Only t2 is positive
//         }

//         if (t > 0) {
//             out.hit = true;
//             out.distance = t;
//             out.point = rayOrigin + t * rayDirection;
//         }

//         return out;
//     }

//     void upgrade_fruit() {
//         fruit = fm.getNextFruit(fruit);
//         target_radius = fm.getFruitProperties(fruit).radius;
//         growing = true;
//     }

//     // setPosition - unchanged
//     void setPosition(glm::vec4 pos) {
//         position = pos;
//         last_position = pos;
//     }

//     // Crucial change: Revert update to its original, simpler Verlet form
//     void update(float dt) {
//         if (hidden) return; // Don't update hidden objects

//         if (growing) {
//             radius += target_radius * dt * GROW_SPEED;
//             if (radius > target_radius) {
//                 radius = target_radius;
//                 growing = false;
//             }
//         }
        
//         // Original Verlet Integration step with VELOCITY_DAMPING (from globals.h)
//         glm::vec4 last_update_move = position - last_position;
//         glm::vec4 new_position = position + last_update_move + (acceleration - last_update_move * VELOCITY_DAMPING) * (dt * dt);
//         last_position = position;
//         position = new_position;
//         acceleration = {0.0f, 0.0f, 0.0f, 0.0f}; // Reset acceleration for next frame
//     }

//     // stop - unchanged
//     void stop() {
//         last_position = position;
//     }

//     // slowdown - unchanged
//     void slowdown(float ratio) {
//         last_position = last_position + ratio * (position - last_position);
//     }

//     // getSpeed - unchanged
//     float getSpeed() const {
//         return glm::length(position - last_position); // Using glm::length
//     }

//     // getVelocity - unchanged
//     glm::vec4 getVelocity() const {
//         return position - last_position;
//     }

//     // addVelocity - unchanged
//     void addVelocity(glm::vec4 v) {
//         last_position -= v;
//     }

//     // setPositionSameSpeed - unchanged
//     void setPositionSameSpeed(glm::vec4 new_position) {
//         const glm::vec4 to_last = last_position - position;
//         position = new_position;
//         last_position = position + to_last;
//     }

//     // move - unchanged
//     void move(glm::vec4 v) {
//         position += v;
//         // Your original move didn't move last_position. This is important.
//         // If you move position but not last_position, you implicitly add velocity.
//         // To preserve current velocity, last_position should also move.
//         // If you want to ADD to velocity, use addVelocity.
//         // Let's keep it as your original for "move" which just shifts the current position.
//         // If it was intended to shift without changing velocity, last_position should also move.
//         // For simple positional shifts, your original `move` is fine.
//     }

//     // disable - reverted to original
//     void disable() {
//         hidden = true;
//         // Your original didn't explicitly set dynamic=false or reset pos/vel here.
//         // If an object is disabled, it shouldn't be processed by the solver.
//         // Setting dynamic=false and resetting is a good practice for disabled objects,
//         // but if your `if (hidden)` checks prevent processing, it's less critical here.
//         // Let's stick to your original for now, for minimal change.
//     }
    
//     // enable - reverted to original
//     void enable() {
//         hidden = false;
//         // Your original didn't explicitly set dynamic=true here.
//         // Ensure that when you 'enable' an object, it also becomes 'dynamic' if it should participate in physics.
//         // dynamic = true; // Consider adding this if dynamic objects need to be explicitly enabled.
//     }

//     // isActive and reset were additions, let's keep them if they are used elsewhere.
//     // They are utility functions and don't directly impact physics explosions.
//     // Helper method to check if object is active
//     bool isActive() const {
//         return !hidden;
//     }

//     // Helper method to reset object to default state
//     void reset() {
//         position = glm::vec4(0.0f);
//         last_position = glm::vec4(0.0f);
//         acceleration = glm::vec4(0.0f);
//         radius = 0.0f;
//         target_radius = 0.0f;
//         dynamic = false; // Reset to false is safer
//         hidden = true;   // Reset to true is safer
//         growing = false;
//         fruit = Fruit::CHERRY;
//     }
// };
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "texture.h"
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
    Fruit fruit;
    // constructor(s)
    PhysicsObject(){

    }
    PhysicsObject(glm::vec4 pos, Fruit f, bool dyn, bool hid): position(pos),last_position(pos), fruit(f), acceleration(0.0f, 0.0f, 0.0f, 0.0f),  dynamic(dyn), hidden(hid)
    {
        radius = 0;
        target_radius = fm.getFruitProperties(fruit).radius;
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
    
    void upgrade_fruit(){
        fruit = fm.getNextFruit(fruit);
        // radius=0;
        target_radius = fm.getFruitProperties(fruit).radius;
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