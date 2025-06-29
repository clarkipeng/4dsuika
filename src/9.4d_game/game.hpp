#pragma once
#include <vector>
#include <tuple>
#include <random>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>
#include <SDL_mixer.h>

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

    Mix_Chunk* mergeSound;
    Mix_Chunk* loseSound;
    Mix_Chunk* placeSound;

    HemisphereBoundary boundary;   
    Fruit nextFruit;
    FruitManager fm;

    int total_points=0;
    int high_score = 0;
    ViewState state;

    bool mouseClicked=false;

    // constructor/destructor
    Game(unsigned int width, unsigned int height) : boundary(glm::vec4(0.0f), 3, 90.0f, 0.1f) {
        State = GAME_MENU;
        Width = width;
        Height = height;

        // Initialize keys to false
        for (int i = 0; i < 1024; i++) {
            Keys[i] = false;
            KeysProcessed[i] = false;
        }

        // Initialize the physics solver and thread pool
        thread_pool = new tp::ThreadPool(4); // Adjust the number of threads as needed

        // glm::vec3 center, float radius, float angleDegrees = 90.0f, float margin = 0.01f
        physics_solver = new PhysicSolver(*thread_pool, &boundary);
        // physics_solver = new PhysicSolver(*thread_pool);
        total_points = 0;

    }
    ~Game(){
        Mix_FreeChunk(mergeSound);
        Mix_CloseAudio();
        SDL_Quit();

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

        mergeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/drop.wav").c_str());
        if (!mergeSound) {
            std::cerr << "Mix_LoadWAV error: " << Mix_GetError() << std::endl;
        }
        loseSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/game_over.wav").c_str());
        if (!loseSound) {
            std::cerr << "Mix_LoadWAV error: " << Mix_GetError() << std::endl;
        }
        placeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/click.wav").c_str());
        if (!placeSound) {
            std::cerr << "Mix_LoadWAV error: " << Mix_GetError() << std::endl;
        }

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
        if (State == GAME_ACTIVE)
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
        if (State == GAME_MENU)
        {
            if (Keys[GLFW_KEY_ENTER] && !KeysProcessed[GLFW_KEY_ENTER]){
                KeysProcessed[GLFW_KEY_ENTER] = true;
                State = GAME_ACTIVE;
            }
            if (mouseClicked){
                State = GAME_ACTIVE;
            }
        }

        if (State == GAME_OVER) {
            if (Keys[GLFW_KEY_ENTER] && !KeysProcessed[GLFW_KEY_ENTER]){
                KeysProcessed[GLFW_KEY_ENTER] = true;
                State = GAME_ACTIVE;
                Reset();
            }
            if (mouseClicked){
                State = GAME_ACTIVE;
                Reset();
            }
        }
        
        // Toggle menu with Escape key
        if (Keys[GLFW_KEY_ESCAPE] && !KeysProcessed[GLFW_KEY_ESCAPE]){
            if (State == GAME_ACTIVE || State == GAME_OVER) {
                State = GAME_MENU;
            } else {
                State = GAME_ACTIVE;
            }
            KeysProcessed[GLFW_KEY_ESCAPE] = true;
        }
    }

    void FixedUpdate(float dt){
        if (State != GAME_ACTIVE) return;
        physics_solver->update(dt);
        total_points = physics_solver->total_points;
    }
    void Update(float dt){
        Sound();
        if (State == GAME_OVER){
            // Smooth pan (rotate around bowl)
            // state.yaw += dt * 0.5f; // Adjust speed as needed
            float yawVelocity = (state.yaw - state.lastYaw);
            float pitchVelocity = (state.pitch - state.lastPitch);
            state.updateCameraPos(yawVelocity,pitchVelocity);

            // Smooth zoom: Interpolate radius toward target
            float targetRadius = 15.0f; // Smaller = zoom in
            float zoomSpeed = 1.5f;
            state.radius += (targetRadius - state.radius) * dt * zoomSpeed;

            // Animate w in a loop between w_min and w_max
            static float w_timer = 0.0f;
            w_timer += dt;

            float amplitude = (state.w_max - state.w_min) / 2.0f;
            float midpoint = (state.w_max + state.w_min) / 2.0f;
            float frequency = 0.1f; // Adjust how fast w oscillates

            state.w = midpoint + amplitude * sin(w_timer * frequency * 2.0f * 3.14159f);
        }
        mouseClicked=false;
    }
    int Sound(){
        if (physics_solver->just_merged > 0) {
            Mix_PlayChannel(-1, mergeSound, 0);
        }
        if (mouseClicked) {
            Mix_PlayChannel(-1, placeSound, 0);
        }
    }
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
            (float)Width / (float)Height,
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

        bool losing=false;
        for (int i=0;i<MAX_OBJECTS;i++) {
            if (!physics_solver->has_obj[i])continue;
            PhysicsObject &obj = physics_solver->objects[i];
            if (obj.hidden) continue;
            
            // if (obj.position.y < -10){
            //     if (total_points > high_score) high_score = total_points;
            //     State = GAME_OVER;
            //     continue;
            // }
            if (obj.position.y < -10){
                if (total_points > high_score) high_score = total_points;
                float yawVelocity = 0.0001;
                float pitchVelocity = 0;
                state.updateCameraPos(yawVelocity,pitchVelocity);
                State = GAME_OVER;
                losing=true;
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
            
            // Render the object
            b_rend->Draw4d(state.w, obj.fruit, obj.position, glm::vec3(0.0f));
            renderedCount++;
        }
        if (losing) Mix_PlayChannel(-1, loseSound, 0);
        
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
        glm::mat4 projection_3d = glm::perspective(glm::radians(45.0f), (float)Width / (float)Height, 0.1f, 100.0f);
        
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
        glm::mat4 projection_2d = glm::ortho(0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f);
        t_rend->shader->use();
        t_rend->shader->setMat4("projection", projection_2d);
        t_rend->shader->setInt("text", 0);

        // Render menu text
        // Note: Y-coordinate is from the top of the screen. Centering text requires knowing its rendered width, so we approximate with offsets.
        std::string title = "Suika 4D";
        std::string subtitle = "Click or Press ENTER to Play";
        std::string hint = "Press ESC to Pause";

        float centerX = Width / 2.0f;
        float centerY = Height / 2.0f;

        float titleScale = 2.0f;
        float subtitleScale = 1.5f;
        float hintScale = 1.0f;

        t_rend->RenderText(title, centerX -  t_rend->GetTextWidth(title, titleScale) / 2.0f,centerY - 100.0f,titleScale,glm::vec3(0.9f, 0.9f, 1.0f));
        t_rend->RenderText(subtitle,centerX -  t_rend->GetTextWidth(subtitle, subtitleScale) / 2.0f,centerY,subtitleScale,glm::vec3(0.8f, 0.8f, 0.8f));
        t_rend->RenderText(hint,25.0f,25.0f,hintScale,glm::vec3(1.0f));
    }
    void RenderEndScreen() {
        
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
            (float)Width / (float)Height,
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
        for (int i=0;i<MAX_OBJECTS;i++) {
            if (!physics_solver->has_obj[i])continue;
            PhysicsObject &obj = physics_solver->objects[i];
            if (obj.hidden) continue;
            
            float w_diff = state.w - obj.position.w;
            float projected_scale = sqrt(obj.radius*obj.radius - w_diff*w_diff);
            b_rend->Draw4d(state.w, obj.fruit, obj.position, glm::vec3(0.0f));
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

        glDepthFunc(GL_LEQUAL); // Change depth function so skybox renders behind everything
        glDepthMask(GL_FALSE);
        background_shader->use();

        view = glm::mat4(glm::mat3(state.getViewMatrix()));
        projection = glm::perspective(glm::radians(45.0f),
                                                (float)Width / (float)Height,
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

        std::string msg1 = "GAME OVER";
        std::string msg2 = "Score: " + std::to_string(total_points);
        std::string msg3 = "High Score: " + std::to_string(high_score);
        std::string msg4 = "Click or Press ENTER to Restart";

        float scale1 = 1.5f;
        float scale2 = 1.0f;

        float centerX = state.windowWidth / 2.0f;

        t_rend->RenderText(msg1, centerX - t_rend->GetTextWidth(msg1, scale1) / 2.0f, 150.0f, scale1, glm::vec3(1.0f, 0.2f, 0.2f));
        t_rend->RenderText(msg2, centerX - t_rend->GetTextWidth(msg2, scale2) / 2.0f, 250.0f, scale2, glm::vec3(1.0f));
        t_rend->RenderText(msg3, centerX - t_rend->GetTextWidth(msg3, scale2) / 2.0f, 300.0f, scale2, glm::vec3(0.8f, 0.8f, 0.2f));
        t_rend->RenderText(msg4, centerX - t_rend->GetTextWidth(msg4, scale2) / 2.0f, 400.0f, scale2, glm::vec3(0.8f));
    }
    // void DoCollisions();
    // reset
    // void ResetLevel();
    // void ResetPlayer();
    // powerups
    // void SpawnPowerUps(GameObject &block);
    // void UpdatePowerUps(float dt);
};
