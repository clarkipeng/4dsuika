
#ifndef GLOBALS_H
#define GLOBALS_H

const int MAX_OBJECTS = 100;
const float VELOCITY_DAMPING = 1000.0f;
// const float RESPONSE_COEF = 1.0f;
// const float VELOCITY_DAMPING = 10000.0f;
// const float RESPONSE_COEF = 1.0f;
const float RESPONSE_COEF = 0.1f;
const float GROW_SPEED = 5.0f;
const float EPS           = 0.0001f;

// Result structure for ray intersection
struct RayInter{
    bool hit = false;
    float distance = FLT_MAX;
    glm::vec3 point = glm::vec3(0.0f);
    
    RayInter() = default;
    
    RayInter(bool h, float d, glm::vec3 p)
        : hit(h), distance(d), point(p) {}
};
#endif