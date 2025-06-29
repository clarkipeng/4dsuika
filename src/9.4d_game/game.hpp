#pragma once
#include <vector>
#include <tuple>
#include <random>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "globals.h"
#include "stb_image.h"

#include "learnopengl/shader.h"
#include "learnopengl/filesystem.h"

#include "threadpool.hpp"

#include "hemisphere_renderer.hpp"
#include "ballrenderer.hpp"
#include "text_renderer.h"
#include "shape.hpp"

#include "hemisphere_boundary.hpp"
#include "physics_solver.hpp"

#include "render_helper.hpp"
#include "state_helper.hpp"
#include "fruit.hpp"

enum GameState {
    GAME_ACTIVE,
    GAME_MENU,
    GAME_OVER 
};

const float light_radius = 20.0f;
const float angle_in_sky = glm::radians(30.0f);
const float max_height = light_radius*sin(angle_in_sky); // height of the sun at its peak (noon)
const glm::vec3 light_color(300.0f);

float ShakeTime = 0.0f;
float                   angle=0;
float                   angle_speed=10;
float                   dim4=0;
bool place_object = false;

class BallRenderer;
class HemisphereRenderer;
class TextRenderer;
class PhysicSolver;
namespace tp { class ThreadPool; }
class Shader;
class HemisphereBoundary;
// enum Fruit : int;
// class FruitManager;
#include "fruit.hpp"
struct ViewState; // or just 'class ViewState;' if it's a class

class Game
{
public:
    // game state
    GameState               State;	
    bool                    Keys[1024];
    bool                    KeysProcessed[1024];
    unsigned int            Width, Height;

    unsigned int backgroundTexture;
    unsigned int defaultTexture;
    unsigned int bowlTexture;

    BallRenderer    *b_rend;
    HemisphereRenderer    *h_rend;
    TextRenderer    *t_rend;

    PhysicSolver *physics_solver;
    tp::ThreadPool *thread_pool;
    Shader *background_shader;
    Shader *ball_shader;
    Shader *text_shader;

    HemisphereBoundary boundary;   
    Fruit nextFruit;
    FruitManager fm;

    int total_points=0;
    int high_score = 0;
    ViewState state;

    // constructor/destructor
    Game(unsigned int width, unsigned int height) : boundary(glm::vec4(0.0f), 3, 90.0f, 0.1f) {
        this->State = GAME_MENU;
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
        total_points = 0;
    }
    ~Game(){
        delete b_rend;
        delete h_rend;
        delete t_rend;
        delete physics_solver;
        delete thread_pool;
    }
    void Reset(){
        // Clear all objects in the physics solver
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            if (physics_solver->has_obj[i]) {
                physics_solver->removeObject(i);
            }
        }
        // Reset physics solver internal state
        physics_solver->reset();  // Assuming you have or can add this method
        // Reset points
        total_points = 0;
        physics_solver->total_points = 0;

        // Reset the fruit manager state (if needed)
        fm.initializeFruits();
        nextFruit = fm.getRandomFruit();

        // Reset view state
        state.reset();  // Make sure ViewState has a Reset() method to restore initial camera & parameters

        // Reset any other relevant variables
        ShakeTime = 0.0f;
        angle = 0.0f;

        // Set game state back to active or menu as desired
        State = GAME_ACTIVE;

    }
    // initialize game state (load all shaders/textures/levels)
    void Init(GLFWwindow* window){
        ball_shader = new Shader("2.2.2.pbr.vs", "2.2.2.pbr.fs");
        background_shader = new Shader("2.2.2.background.vs", "2.2.2.background.fs");
        text_shader = new Shader("text_2d.vs", "text_2d.fs");

        GLint success;
        char infoLog[512];
        glGetShaderiv(ball_shader->ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(ball_shader->ID, 512, NULL, infoLog);
            std::cerr << "[ball_shader] Link error:\n" << infoLog << std::endl;
        }

        b_rend = new BallRenderer(ball_shader, SPHERE, 5);
        h_rend = new HemisphereRenderer(ball_shader, SPHERE, 5, 90.0f, 1.0f, boundary.margin/boundary.radius);
    
        t_rend = new TextRenderer(text_shader);
        t_rend->Load(FileSystem::getPath("resources/fonts/OCRAEXT.TTF").c_str(), 24);

        fm.initializeFruits();
        nextFruit = fm.getRandomFruit();

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

        ball_shader->use();
        ball_shader->setInt("albedoMap", 0);

        // glActiveTexture(GL_TEXTURE1);
        // glBindTexture(GL_TEXTURE_2D, metallicTex);

        // glActiveTexture(GL_TEXTURE2);
        // glBindTexture(GL_TEXTURE_2D, roughnessTex);

        // glActiveTexture(GL_TEXTURE3);
        // glBindTexture(GL_TEXTURE_2D, aoTex);

        // glActiveTexture(GL_TEXTURE4);
        // glBindTexture(GL_TEXTURE_2D, alphaTex);

        // ball_shader->use();
        // ball_shader->setInt("albedoMap", 0);
        // ball_shader->setInt("metallicMap", 1);
        // ball_shader->setInt("roughnessMap", 2);
        // ball_shader->setInt("aoMap", 3);
        // ball_shader->setInt("alphaMap", 4);

        // GLuint albedoTex, metallicTex, roughnessTex, aoTex, alphaTex;
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    // game loop
    void ProcessInput(float dt){
        if (this->State == GAME_ACTIVE)
        {
            // Handle input for camera movement using continuous key check
            if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_W) == GLFW_PRESS) {
                state.w += 1.0f * dt; // Move up
            }
            if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_S) == GLFW_PRESS) {
                state.w -= 1.0f * dt; // Move down
            }
        }

        // Handle single key presses for state changes
        // From menu to game
        if (this->State == GAME_MENU)
        {
            if (this->Keys[GLFW_KEY_ENTER] && !this->KeysProcessed[GLFW_KEY_ENTER]){
                this->State = GAME_ACTIVE;
                State = GAME_ACTIVE; // Update global state
                this->KeysProcessed[GLFW_KEY_ENTER] = true;
            }
        }

        if (this->State == GAME_OVER) {
            if (this->Keys[GLFW_KEY_ENTER] && !this->KeysProcessed[GLFW_KEY_ENTER]){
                Reset(); // Reset game
                this->State = GAME_ACTIVE;
                this->KeysProcessed[GLFW_KEY_ENTER] = true;
            }
        }
        
        // Toggle menu with Escape key
        if (this->Keys[GLFW_KEY_ESCAPE] && !this->KeysProcessed[GLFW_KEY_ESCAPE]){
            if (this->State == GAME_ACTIVE) {
                this->State = GAME_MENU;
                State = GAME_MENU; // Update global state
            } else {
                this->State = GAME_ACTIVE;
                State = GAME_ACTIVE; // Update global state
            }
            this->KeysProcessed[GLFW_KEY_ESCAPE] = true;
        }
    }

    void FixedUpdate(float dt){
        if (State != GAME_ACTIVE) return;
        physics_solver->update(dt);
        total_points = physics_solver->total_points;
    }
    void Update(float dt){}

    void Render(){
        switch(State){
            case GAME_ACTIVE:
                RenderGame();
                break;
            case GAME_MENU:
                RenderMenu();
                break;
            case GAME_OVER:
                RenderEndScreen();
                break;
                
        }
    }
    void RenderGame(){
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
            std::cout << "Objects to render: " << MAX_OBJECTS - physics_solver->no_obj.size() << std::endl;
        }

        // Render all physics objects
        int renderedCount = 0;
        // for (auto &obj : physics_solver->objects) {
        for (int i=0;i<MAX_OBJECTS;i++) {
            if (!physics_solver->has_obj[i])continue;
            PhysicsObject &obj = physics_solver->objects[i];
            if (obj.hidden) continue;
            
            if (obj.position.y < -10){
                physics_solver->removeObject(i);
                if (total_points > high_score) high_score = total_points;
                this->State = GAME_OVER;
                continue;
            }
            
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

        // projection = glm::ortho(0.0f, static_cast<float>(state.windowWidth), 0.0f, static_cast<float>(state.windowHeight));
        // text_shader->use();
        // glUniformMatrix4fv(glGetUniformLocation(text_shader->ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        
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

        RayInter mousePos = getPlacementMouse(&state, &boundary, physics_solver);
        if (mousePos.hit){
            b_rend->Draw3d(nextFruit, mousePos.point+glm::vec3(0,3,0),glm::vec3(0.0f));
        }
        state.mouseMoved=false;

        projection = glm::ortho(0.0f, static_cast<float>(state.windowWidth), static_cast<float>(state.windowHeight), 0.0f);
        t_rend->shader->use();
        t_rend->shader->setMat4("projection", projection);
        t_rend->shader->setInt("text", 0);
        t_rend->RenderText("Score " + std::to_string((int)physics_solver->total_points), 25.0f, 25.0f, 1.0f, glm::vec3(1.0f));

        // glDepthMask(GL_FALSE);   // Don't write to depth buffer
        // glDepthMask(GL_TRUE);
    }
    void RenderMenu(){
        // Clear buffers
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Render skybox background
        glDepthFunc(GL_LEQUAL); // Change depth function so skybox renders behind everything
        glDepthMask(GL_FALSE);
        background_shader->use();
        
        // Create view/projection matrices for the skybox
        glm::mat4 view = glm::mat4(glm::mat3(state.getViewMatrix())); // Remove translation
        glm::mat4 projection_3d = glm::perspective(glm::radians(45.0f), (float)this->Width / (float)this->Height, 0.1f, 100.0f);
        
        background_shader->setMat4("view", view);
        background_shader->setMat4("projection", projection_3d);
        background_shader->setInt("environmentMap", 0);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, backgroundTexture);
        renderCube();
        
        // Restore default depth function
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);

        // Setup for 2D text rendering with top-left origin
        glm::mat4 projection_2d = glm::ortho(0.0f, static_cast<float>(this->Width), static_cast<float>(this->Height), 0.0f);
        t_rend->shader->use();
        t_rend->shader->setMat4("projection", projection_2d);
        t_rend->shader->setInt("text", 0);

        // Render menu text
        // Note: Y-coordinate is from the top of the screen. Centering text requires knowing its rendered width, so we approximate with offsets.
        t_rend->RenderText("Suika 4D", this->Width / 2.0f - 150.0f, this->Height / 2.0f - 100.0f, 2.0f, glm::vec3(0.9f, 0.9f, 1.0f));
        t_rend->RenderText("Press ENTER to Play", this->Width / 2.0f - 220.0f, this->Height / 2.0f, 1.5f, glm::vec3(0.8f, 0.8f, 0.8f));
        t_rend->RenderText("Press ESC to Pause", 25.0f, 25.0f, 1.0f, glm::vec3(1.0f));
    }
    void RenderEndScreen() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Render background
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        background_shader->use();

        glm::mat4 view = glm::mat4(glm::mat3(state.getViewMatrix()));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
                                                (float)this->Width / (float)this->Height,
                                                0.1f, 100.0f);
        background_shader->setMat4("view", view);
        background_shader->setMat4("projection", projection);
        background_shader->setInt("environmentMap", 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, backgroundTexture);
        renderCube();
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);

        // Render text
        projection = glm::ortho(0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f);
        t_rend->shader->use();
        t_rend->shader->setMat4("projection", projection);
        t_rend->shader->setInt("text", 0);

        float centerX = Width / 2.0f;
        t_rend->RenderText("GAME OVER", centerX - 100, 150.0f, 1.5f, glm::vec3(1.0f, 0.2f, 0.2f));
        t_rend->RenderText("Score: " + std::to_string(total_points), centerX - 80, 250.0f, 1.0f, glm::vec3(1.0f));
        t_rend->RenderText("High Score: " + std::to_string(high_score), centerX - 100, 300.0f, 1.0f, glm::vec3(0.8f, 0.8f, 0.2f));
        t_rend->RenderText("Press ENTER to Retry", centerX - 120, 400.0f, 1.0f, glm::vec3(0.8f));
    }
    // void DoCollisions();
    // reset
    // void ResetLevel();
    // void ResetPlayer();
    // powerups
    // void SpawnPowerUps(GameObject &block);
    // void UpdatePowerUps(float dt);
};
