#include "game.h"
#include "globals.h"
#include "threadpool.hpp"
#include "physics_solver.hpp"
// #include "resource_manager.h"
#include "ballrenderer.hpp"
#include "stb_image.h"

#include "learnopengl/shader.h"
#include "learnopengl/filesystem.h"

#include "hemisphere_boundary.hpp"
#include "hemisphere_renderer.hpp"
#include "shape.hpp"
#include "helper.hpp"
#include "fruit.hpp"

#include <random>

//debug
// #include <glm/gtx/string_cast.hpp>

// #include "learnopengl/camera.h"

// Game-related State data
BallRenderer    *b_rend;
HemisphereRenderer    *h_rend;
PhysicSolver *physics_solver;
tp::ThreadPool *thread_pool;
Shader *background_shader;
Shader *ballshader;

const float light_radius = 20.0f;
const float angle_in_sky = glm::radians(30.0f);
const float max_height = light_radius*sin(angle_in_sky); // height of the sun at its peak (noon)
const glm::vec3 light_color(300.0f);

HemisphereBoundary boundary(glm::vec4(0.0f), 3, 90.0f, 0.1f);
// GameObject        *Player;
// BallObject        *Ball;
// ParticleGenerator *Particles;
// PostProcessor     *Effects;
// ISoundEngine      *SoundEngine = createIrrKlangDevice();
// TextRenderer      *Text;

float ShakeTime = 0.0f;
float                   angle=0;
float                   angle_speed=10;
float                   dim4=0;
bool place_object = false;
Fruit nextFruit;

// enum FRUIT{
//     CHER
// }

struct ViewState {
    float w=0.0f;
    const float w_min=-5.0f, w_max=5.0f;

    glm::vec3 sphereCenter = glm::vec3(0.0f, 0.0f, 0.0f);
    float sphereRadius = 5.0f;

    glm::vec3 orbitTarget = sphereCenter; //+ glm::vec3(0, sphereRadius, 0);
    float radius = 8.0f;

    float yaw = glm::radians(90.0f);    // looking along +X
    float pitch = glm::radians(20.0f);  // slight downward

    const float pitchMin = glm::radians(-89.0f);
    const float pitchMax = glm::radians(89.0f);

    bool rotating = false;
    double lastX = 0.0, lastY = 0.0;

    int windowWidth, windowHeight;
    float m_xpos, m_ypos;

    bool initialized=false;
    bool mouseMoved=false;
    void Init(GLFWwindow *window){
        glfwGetWindowSize(window,&windowWidth,&windowHeight);
        initialized=true;
    }
    // Update camera position based on current yaw, pitch, radius
    glm::vec3 getCameraPosition() const {
        float x = radius * cos(pitch) * cos(yaw);
        float y = radius * sin(pitch);
        float z = radius * cos(pitch) * sin(yaw);

        return orbitTarget + glm::vec3(x, y, z);
    }
    glm::mat4 getViewMatrix() const {
        glm::vec3 dir = {
            cosf(pitch) * cosf(yaw),
            sinf(pitch),
            cosf(pitch) * sinf(yaw)
        };
        glm::vec3 eye = orbitTarget - dir * radius;
        return glm::lookAt(eye, orbitTarget, glm::vec3(0, 1, 0));
    }

    // Call on mouse button press/release
    void onMouseButton(int button, int action, double xpos, double ypos) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                rotating = true;
                lastX = xpos;
                lastY = ypos;
            } else if (action == GLFW_RELEASE) {
                rotating = false;
            }
        }
    }

    // Call on mouse move
    void onCursorPos(double xpos, double ypos) {
        m_xpos = xpos;
        m_ypos = ypos;
        mouseMoved=true;

        if (!rotating) return;

        float sensitivity = 0.005f;
        float dx = float(xpos - lastX);
        float dy = float(ypos - lastY);

        yaw   += dx * sensitivity;
        pitch -= dy * sensitivity;

        if (pitch > pitchMax) pitch = pitchMax;
        if (pitch < pitchMin) pitch = pitchMin;

        lastX = xpos;
        lastY = ypos;
    }

    // Call on scroll
    void onScroll(double yoffset) {
        float zoomSpeed = 1.0f;
        radius -= float(yoffset) * zoomSpeed;
        if (radius < sphereRadius + 1.0f) radius = sphereRadius + 1.0f;
        if (radius > sphereRadius + 50.0f) radius = sphereRadius + 50.0f;
    }
    
} state;

RayInter getPlacementMouse(){
    float winX = state.m_xpos;
    float winY = state.windowHeight - state.m_ypos;
    glm::mat4 view_matrix = state.getViewMatrix();
    glm::mat4 projection_matrix = glm::perspective(
        glm::radians(45.0f),
        float(state.windowWidth) / float(state.windowHeight),
        0.1f, 
        100.0f
    );

    glm::vec4 viewport(0, 0, state.windowWidth, state.windowHeight);

    // 3) Unproject near & far
    glm::vec3 nearPoint = glm::unProject(
        glm::vec3(winX, winY, 0.0f),
        view_matrix,
        projection_matrix,
        viewport
    );
    glm::vec3 farPoint = glm::unProject(
        glm::vec3(winX, winY, 1.0f),
        view_matrix,
        projection_matrix,
        viewport
    );

    // 4) Build ray
    glm::vec3 ray_origin    = nearPoint;
    glm::vec3 ray_direction = glm::normalize(farPoint - nearPoint);

    // 5) Intersection test
    RayInter closestResult = boundary.checkRay(state.w,ray_origin,ray_direction);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (!physics_solver->has_obj[i]) continue;
        auto result = physics_solver->objects[i].testRay(state.w, ray_origin, ray_direction);
        if (result.hit && result.distance < closestResult.distance) {
            closestResult = result;
        }
    }

    return closestResult;
}
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // double xpos_d, ypos_d;
    // glfwGetCursorPos(window,&xpos_d,&ypos_d);
    // state.m_xpos = static_cast<float>(xpos_d);
    // state.m_ypos = static_cast<float>(ypos_d);
    
    state.onMouseButton(button, action, state.m_xpos, state.m_ypos);

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        RayInter closestResult = getPlacementMouse();

        if (closestResult.hit) {
            // std::cout << "HIT\n";
            physics_solver->addObject(
                PhysicsObject(glm::vec4(closestResult.point, state.w)+ glm::vec4(0,3.0f,0,0),
                nextFruit, true, false)
            );
            nextFruit = fm.getRandomFruit();
        } else {
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!state.initialized) state.Init(window);
    state.onCursorPos(xpos, ypos);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (!state.initialized) state.Init(window);
    state.onScroll(yoffset);
}
void windowSizeCallback(GLFWwindow* window, int windowWidth, int windowHeight){
    if (!state.initialized) state.Init(window);
    state.windowHeight = windowHeight;
    state.windowWidth = windowWidth;
}

Game::Game(unsigned int width, unsigned int height) {
    this->State = GAME_ACTIVE;
    this->Width = width;
    this->Height = height;

    // Initialize keys to false
    for (int i = 0; i < 1024; i++) {
        this->Keys[i] = false;
        this->KeysProcessed[i] = false;
    }

    // Initialize the physics solver and thread pool
    thread_pool = new tp::ThreadPool(4); // Adjust the number of threads as needed

    // glm::vec3 center, float radius, float angleDegrees = 90.0f, float margin = 0.01f
    physics_solver = new PhysicSolver(*thread_pool, &boundary);
    // physics_solver = new PhysicSolver(*thread_pool);
}
Game::~Game(){
    delete b_rend;
    delete h_rend;
    delete physics_solver;
    delete thread_pool;
}
void Game::Init(GLFWwindow* window){
    ballshader = new Shader("2.2.2.pbr.vs", "2.2.2.pbr.fs");
    background_shader = new Shader("2.2.2.background.vs", "2.2.2.background.fs");

    GLint success;
    char infoLog[512];
    glGetShaderiv(ballshader->ID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(ballshader->ID, 512, NULL, infoLog);
        std::cerr << "[ballshader] Link error:\n" << infoLog << std::endl;
    }
    

    b_rend = new BallRenderer(ballshader, SPHERE, 5);
    h_rend = new HemisphereRenderer(ballshader, SPHERE, 5, 90.0f, 1.0f, boundary.margin/boundary.radius);

    fm.initializeFruits();
    nextFruit = fm.getRandomFruit();

    glfwSetWindowSizeCallback(window, windowSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    state.Init(window);

    std::vector<std::string> faces
    {
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // right
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // left
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // top
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // bottom
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // front
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // back
    };
    backgroundTexture = loadCubemap(faces);

    defaultTexture = loadTexture(FileSystem::getPath("resources/textures/pbr/gold/albedo.png").c_str());
    bowlTexture = loadTexture(FileSystem::getPath("resources/textures/pbr/gold/albedo.png").c_str());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, defaultTexture);

    ballshader->use();
    ballshader->setInt("albedoMap", 0);

    // glActiveTexture(GL_TEXTURE1);
    // glBindTexture(GL_TEXTURE_2D, metallicTex);

    // glActiveTexture(GL_TEXTURE2);
    // glBindTexture(GL_TEXTURE_2D, roughnessTex);

    // glActiveTexture(GL_TEXTURE3);
    // glBindTexture(GL_TEXTURE_2D, aoTex);

    // glActiveTexture(GL_TEXTURE4);
    // glBindTexture(GL_TEXTURE_2D, alphaTex);

    // ballshader->use();
    // ballshader->setInt("albedoMap", 0);
    // ballshader->setInt("metallicMap", 1);
    // ballshader->setInt("roughnessMap", 2);
    // ballshader->setInt("aoMap", 3);
    // ballshader->setInt("alphaMap", 4);

    // GLuint albedoTex, metallicTex, roughnessTex, aoTex, alphaTex;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
void Game::ProcessInput(float dt){
    // // Handle input for camera movement
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_W) == GLFW_PRESS) {
        state.w += 1.0f * dt; // Move up
    }
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_S) == GLFW_PRESS) {
        state.w -= 1.0f * dt; // Move down
    }
}
void Game::Update(float dt){
    physics_solver->update(dt);
}


void Game::Render(){
    // Clear buffers
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Setup shader and matrices
    glm::mat4 view = state.getViewMatrix();
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        (float)this->Width / (float)this->Height,
        0.1f,
        100.0f
    );
    glm::vec3 cameraPos = state.getCameraPosition();
    
    b_rend->shader->use();
    b_rend->shader->setMat4("view", view);
    b_rend->shader->setMat4("projection", projection);
    b_rend->shader->setVec3("camPos", cameraPos);
    
    b_rend->shader->setFloat("metallic", 0.0f);
    b_rend->shader->setFloat("roughness", 0.5f);
    b_rend->shader->setFloat("ao", 1.0f);
    b_rend->shader->setFloat("alpha", 1.0f);
    // b_rend->shader->setVec3("albedo", glm::vec3(1.0f, 0.5f, 0.5f)); // Set albedo color

    float theta = 3.1415*(state.w-state.w_min) / (state.w_max-state.w_min); // or use glfwGetTime(), animating from 0 to 2π
    float x = light_radius * cos(theta);
    float y = max_height * sin(theta);
    b_rend->shader->setVec3("lightPositions[" + std::to_string(0) + "]", glm::vec3(x,y,light_radius * cos(angle_in_sky)));
    b_rend->shader->setVec3("lightColors[" + std::to_string(0) + "]", light_color);

    // Debug output (only occasionally to avoid spam)
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) { // Print every 60 frames (once per second at 60fps)
        std::cout << "=== FRAME " << frameCount/60 << " ===" << std::endl;
        std::cout << "Camera pos: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << std::endl;
        std::cout << "W coordinate: " << state.w << std::endl;
        std::cout << "Objects to render: " << physics_solver->objects.size() << std::endl;
    }

    // Render all physics objects
    int renderedCount = 0;
    // for (auto &obj : physics_solver->objects) {
    for (int i=0;i<MAX_OBJECTS;i++) {
        if (!physics_solver->has_obj[i])continue;
        PhysicsObject &obj = physics_solver->objects[i];
        if (obj.hidden) continue;
        
        // Debug 4D projection calculation
        float w_diff = state.w - obj.position.w;
        float projected_scale = sqrt(obj.radius*obj.radius - w_diff*w_diff);
        
        if (frameCount % 60 == 0) {
            std::cout << "Object " << renderedCount << ": pos(" 
                      << obj.position.x << ", " << obj.position.y << ", " 
                      << obj.position.z << ", " << obj.position.w << ")" << std::endl;
            std::cout << "  w_diff=" << w_diff << ", projected_scale=" << projected_scale << std::endl;
        }
        
        // Skip objects that would be behind the 4D viewing plane or have invalid scale
        // if (abs(w_diff) < 0.001f) {
        //     if (frameCount % 60 == 0) {
        //         std::cout << "  SKIPPED: Object too close to 4D viewing plane" << std::endl;
        //     }
        //     continue;
        // }
        
        // if (abs(projected_scale) > 100.0f) {
        //     if (frameCount % 60 == 0) {
        //         std::cout << "  SKIPPED: Projected scale too large (" << projected_scale << ")" << std::endl;
        //     }
        //     continue;
        // }
        
        // Render the object
        b_rend->Draw4d(state.w, obj.fruit, obj.position, glm::vec3(0.0f));
        // GLenum err;
        // while ((err = glGetError()) != GL_NO_ERROR) {
        //     std::cerr << "[Draw4d] GL ERROR: " << err << std::endl;
        // }
        renderedCount++;
        
    }
    
    if (frameCount % 60 == 0) {
        std::cout << "Actually rendered: " << renderedCount << " objects" << std::endl;
    }

    h_rend->shader->use();
    h_rend->shader->setMat4("view", view);
    h_rend->shader->setMat4("projection", projection);
    h_rend->shader->setVec3("camPos", cameraPos);
    h_rend->shader->setFloat("metallic", 0.0f);
    h_rend->shader->setFloat("roughness", 0.5f);
    h_rend->shader->setFloat("ao", 1.0f);
    h_rend->shader->setFloat("alpha", 1.0f);

    h_rend->Draw4d(state.w, bowlTexture, glm::vec4(0.0f), boundary.radius, glm::vec3(0.0f));


    // glDepthFunc(GL_LEQUAL);  // Same as skybox, allows rendering at same depth
    // glDepthMask(GL_FALSE);   // Don't write to depth buffer
    // Re-enable depth testing and writing
    // glDepthFunc(GL_LESS);    // Reset to normal
    // glDepthMask(GL_TRUE);

    // Render skybox LAST (after all objects)
    glDepthFunc(GL_LEQUAL); // Change depth function so skybox renders behind everything
    glDepthMask(GL_FALSE);
    background_shader->use();
    // Remove translation from view matrix for skybox
    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    background_shader->setMat4("view", skyboxView);
    background_shader->setMat4("projection", projection);
    background_shader->setInt("environmentMap", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, backgroundTexture);
    renderCube();
    // Restore default depth function
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    
    // Set up shader for transparent object
    b_rend->shader->use();  // Make sure we're using the right shader
    b_rend->shader->setMat4("view", view);           // Reset view matrix
    b_rend->shader->setMat4("projection", projection); // Reset projection matrix  
    b_rend->shader->setVec3("camPos", cameraPos);    // Reset camera position
    b_rend->shader->setFloat("metallic", 0.0f);
    b_rend->shader->setFloat("roughness", 0.5f);
    b_rend->shader->setFloat("ao", 1.0f);
    b_rend->shader->setFloat("alpha", 0.5f);

    RayInter mousePos = getPlacementMouse();
    if (mousePos.hit){
        b_rend->Draw3d(nextFruit, mousePos.point+glm::vec3(0,3,0),glm::vec3(0.0f));
    }
    state.mouseMoved=false;
    // glDepthMask(GL_FALSE);   // Don't write to depth buffer
    // glDepthMask(GL_TRUE);
}
// void Game::ResetLevel(){

// }
// void Game::ResetPlayer(){

// }