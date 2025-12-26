#pragma once
#include <vector>
#include <tuple>
#include <random>
#include <thread>
#include <algorithm>

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
#include "learnopengl/model.h"
#include "filesystem.h"

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

const float light_radius = 1000.0f;
const float angle_in_sky = glm::radians(10.0f);
const float max_height = light_radius*sin(angle_in_sky); // height of the sun at its peak (noon)

float ShakeTime = 0.0f;
float                   angle=0;
float                   angle_speed=10;
float                   dim4=0;
bool place_object = false;


struct Slider {
    // The slider's position and size on the screen
    glm::vec4 rect; // x, y, width, height
    std::string label;
    
    // A pointer to the actual game variable this slider controls (e.g., musicVolume)
    float* value; 
    
    // State to track if the user is currently dragging this slider
    bool isDragging;
    void Update(){
        double mouseX, mouseY;
        glfwGetCursorPos(glfwGetCurrentContext(), &mouseX, &mouseY);
        
        bool isMousePressed = glfwGetMouseButton(glfwGetCurrentContext(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        // Check if the mouse is over the slider's rectangle
        bool isMouseOver = (mouseX >= rect.x && mouseX <= rect.x + rect.z &&
                        mouseY >= rect.y && mouseY <= rect.y + rect.w);

        if (isMousePressed && isMouseOver) {
            isDragging = true;
        }
        
        if (!isMousePressed) {
            isDragging = false;
        }

        if (isDragging) {
            // Update the slider's value based on mouse position
            float newValue = (mouseX - rect.x) / rect.z;
            // Clamp the value between 0.0 and 1.0
            *value = glm::clamp(newValue, 0.0f, 1.0f);
        }
    }
};
struct VolumeSettings {
    float musicVolume;
    float sfxVolume;
    
    Slider musicSlider;
    Slider sfxSlider;

    // The constructor sets up the internal logic
    VolumeSettings() {
        musicVolume = 0.8f;
        sfxVolume = 1.0f;
        musicSlider.label = "Music Volume";
        musicSlider.value = &musicVolume; 
        musicSlider.isDragging = false;
        sfxSlider.label = "SFX Volume";
        sfxSlider.value = &sfxVolume; 
        sfxSlider.isDragging = false;
    }
    
    void UpdateLayout(unsigned int Width, unsigned int Height) {
        float sliderX = Width * 0.75f; // Position in right quadrant
        float sliderWidth = Width/4;
        float sliderHeight = 10.0f;
        // float startY = Height * 0.6f; // Adjust start Y position
        float startY = Height * 0.1f; // Adjust start Y position
        float sliderSpacing = 60.0f * (Height / 800.0f);

        musicSlider.rect = {sliderX, startY, sliderWidth, sliderHeight};
        sfxSlider.rect = {sliderX, startY + sliderSpacing, sliderWidth, sliderHeight};
    }
    // This function handles the mouse input
    void UpdateControls() {
        musicSlider.Update();
        sfxSlider.Update();
    }
};
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
    Shader *model_shader;
    Model* tableModel;

    Mix_Chunk* mergeSound;
    Mix_Chunk* loseSound;
    Mix_Chunk* placeSound;
    Mix_Music* themeMusic;

    HemisphereBoundary boundary;   
    Fruit nextFruit;
    FruitManager fm;

    int total_points=0;
    int high_score = 0;
    ViewState state;
    VolumeSettings vset;

    bool ballPlaced=false;

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

        unsigned int thread_count = 1;
        
        #ifdef __EMSCRIPTEN__
            thread_count = 1;
        #else
            thread_count = std::thread::hardware_concurrency();
            if (thread_count == 0) thread_count = 1; // Fallback to single thread
            if (thread_count > 4) thread_count = 4; // Cap at 4 threads
        #endif
        
        thread_pool = new tp::ThreadPool(thread_count);

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
        ball_shader = new Shader(FileSystem::getPath("resources/shaders/pbr.vs").c_str(), FileSystem::getPath("resources/shaders/pbr.fs").c_str());
        background_shader = new Shader(FileSystem::getPath("resources/shaders/background.vs").c_str(), FileSystem::getPath("resources/shaders/background.fs").c_str());
        text_shader = new Shader(FileSystem::getPath("resources/shaders/text_2d.vs").c_str(), FileSystem::getPath("resources/shaders/text_2d.fs").c_str());
        model_shader = ball_shader;
        // new Shader(FileSystem::getPath("resources/shaders/table.vs").c_str(), FileSystem::getPath("resources/shaders/table.fs").c_str());

        tableModel = new Model(FileSystem::getPath("resources/models/flyingIsland/flyingIsland.obj").c_str()); 
        // tableModel = new Model(FileSystem::getPath("resources/models/skyisland/skyisland.obj").c_str()); 
        // Load shaders with error handling
        // try {
        // ball_shader = new Shader("2.2.2.pbr.vs", "2.2.2.pbr.fs");
        // background_shader = new Shader("2.2.2.background.vs", "2.2.2.background.fs");
        // text_shader = new Shader("text_2d.vs", "text_2d.fs");
        // #ifdef __EMSCRIPTEN__
        //     // #pragma message("__EMSCRIPTEN__ is defined")
        //     std::cout<<"start"<<std::endl;
        //     ball_shader = new Shader(FileSystem::getPath("resources/shaders/pbr_web.vs").c_str(), FileSystem::getPath("resources/shaders/pbr_web.fs").c_str());
        //     background_shader = new Shader(FileSystem::getPath("resources/shaders/background_web.vs").c_str(), FileSystem::getPath("resources/shaders/background_web.fs").c_str());
        //     text_shader = new Shader(FileSystem::getPath("resources/shaders/text_2d_web.vs").c_str(), FileSystem::getPath("resources/shaders/text_2d_web.fs").c_str());
        //     std::cout<<"end"<<std::endl;
        // #else
        //     std::cout<<"NOT"<<std::endl;
        //     ball_shader = new Shader(FileSystem::getPath("resources/shaders/ball.vs").c_str(), FileSystem::getPath("resources/shaders/ball.fs").c_str());
        //     background_shader = new Shader(FileSystem::getPath("resources/shaders/background.vs").c_str(), FileSystem::getPath("resources/shaders/background.fs").c_str());
        //     text_shader = new Shader(FileSystem::getPath("resources/shaders/text_2d.vs").c_str(), FileSystem::getPath("resources/shaders/text_2d.fs").c_str());
        // #endif
        // } catch (const std::exception& e) {
        //     std::cerr << "Error loading shaders: " << e.what() << std::endl;
        //     throw; // Re-throw to prevent continuing with invalid shaders
        // }

        // GLint success;
        // char infoLog[512];
        // glGetShaderiv(ball_shader->ID, GL_LINK_STATUS, &success);
        // if (!success) {
        //     glGetShaderInfoLog(ball_shader->ID, 512, NULL, infoLog);
        //     std::cerr << "[ball_shader] Link error:\n" << infoLog << std::endl;
        // }

        b_rend = new BallRenderer(ball_shader, SPHERE, 5);
        h_rend = new HemisphereRenderer(ball_shader, SPHERE, 5, 90.0f, 1.0f, boundary.margin/boundary.radius);
    
        t_rend = new TextRenderer(text_shader);
        try {
            t_rend->Load(FileSystem::getPath("resources/fonts/OCRAEXT.TTF").c_str(), 24);
            std::cout << "Font loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load font: " << e.what() << std::endl;
            // Continue without font - text rendering will be disabled
        }

        fm.initializeFruits();
        nextFruit = fm.getRandomFruit();

        state.Init(window);

        // Load audio files with error handling
        mergeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/drop.wav").c_str());
        if (!mergeSound) {
            std::cerr << "Warning: Could not load drop.wav: " << Mix_GetError() << std::endl;
            mergeSound = nullptr;
        }
        loseSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/game_over.wav").c_str());
        if (!loseSound) {
            std::cerr << "Warning: Could not load game_over.wav: " << Mix_GetError() << std::endl;
            loseSound = nullptr;
        }
        placeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/click.wav").c_str());
        if (!placeSound) {
            std::cerr << "Warning: Could not load click.wav: " << Mix_GetError() << std::endl;
            placeSound = nullptr;
        }
        themeMusic = Mix_LoadMUS(FileSystem::getPath("resources/audio/theme.wav").c_str());
        if (!themeMusic) {
            std::cerr << "Mix_LoadMUS error: " << Mix_GetError() << std::endl;
        }

        std::vector<std::string> faces
        {
            FileSystem::getPath("resources/textures/sky/right.jpg"),  // right
            FileSystem::getPath("resources/textures/sky/left.jpg"),  // left
            FileSystem::getPath("resources/textures/sky/top.jpg"),  // top
            FileSystem::getPath("resources/textures/sky/bottom.jpg"),  // bottom
            FileSystem::getPath("resources/textures/sky/front.jpg"),  // front
            FileSystem::getPath("resources/textures/sky/back.jpg"),  // back
        };
        
        try {
            backgroundTexture = loadCubemap(faces);
            std::cout << "Background texture loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load background texture: " << e.what() << std::endl;
            backgroundTexture = 0; // Use 0 as fallback
        }

        try {
            defaultTexture = loadTexture(FileSystem::getPath("resources/textures/gold/albedo.png").c_str());
            bowlTexture = loadTexture(FileSystem::getPath("resources/textures/gold/albedo.png").c_str());
            std::cout << "Default and bowl textures loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load default/bowl textures: " << e.what() << std::endl;
            defaultTexture = 0;
            bowlTexture = 0;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, defaultTexture);

        ball_shader->use();
        ball_shader->setInt("texture_diffuse1", 0);

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

        if (themeMusic) {
            Mix_PlayMusic(themeMusic, -1);
        }
        std::cout<<"INIT DONE"<<std::endl;
    }
    // game loop
    void ProcessInput(float dt){
        if (State == GAME_ACTIVE)
        {
            // Handle input for camera movement using continuous key check
            if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_W) == GLFW_PRESS) {
                state.w += 1.0f * dt; // Move up
                state.w = min(state.w,state.w_max);
            }
            if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_S) == GLFW_PRESS) {
                state.w -= 1.0f * dt; // Move down
                state.w = max(state.w,state.w_min);
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
            // if (ballPlaced){
            //     State = GAME_ACTIVE;
            // }
        }

        if (State == GAME_OVER) {
            if (Keys[GLFW_KEY_ENTER] && !KeysProcessed[GLFW_KEY_ENTER]){
                KeysProcessed[GLFW_KEY_ENTER] = true;
                State = GAME_ACTIVE;
                Reset();
            }
            // if (ballPlaced){
            //     State = GAME_ACTIVE;
            //     Reset();
            // }
        }
        
        // Toggle menu with Escape key
        if (Keys[GLFW_KEY_ESCAPE] && !KeysProcessed[GLFW_KEY_ESCAPE]){
            if (State == GAME_ACTIVE || State == GAME_OVER) {
                State = GAME_MENU;
            } 
            else {
                State = GAME_ACTIVE;
            }
            KeysProcessed[GLFW_KEY_ESCAPE] = true;
        }
    }

    void ApplyVolumeSettings()
    {
        // SDL_mixer uses a volume range of 0 to 128 (MIX_MAX_VOLUME)
        
        // Set the music volume
        Mix_VolumeMusic(static_cast<int>(vset.musicVolume * MIX_MAX_VOLUME));

        // Set the volume for all sound effect chunks
        int sfx_volume_sdl = static_cast<int>(vset.sfxVolume * MIX_MAX_VOLUME);
        if (mergeSound) Mix_VolumeChunk(mergeSound, sfx_volume_sdl);
        if (loseSound)  Mix_VolumeChunk(loseSound, sfx_volume_sdl);
        if (placeSound) Mix_VolumeChunk(placeSound, sfx_volume_sdl);
    }

    void Update(float dt)
    {
        Width = state.windowWidth;
        Height = state.windowHeight;
        // Logic that should run every frame, regardless of state
        Sound(); // Manages ongoing sounds

        // Call the appropriate update function for the current state
        switch (State)
        {
            case GAME_ACTIVE:
                if (Mix_PausedMusic() == 1) {
                    Mix_ResumeMusic();
                }
                UpdateGameActive(dt);
                break;
            case GAME_OVER:
                UpdateGameOver(dt);
                if (State == GAME_OVER) {
                    Mix_PauseMusic();
                }
                break;
            case GAME_MENU:
                if (Mix_PausedMusic() == 1) {
                    Mix_ResumeMusic();
                }
                vset.UpdateLayout(Width, Height);
                vset.UpdateControls();
                ApplyVolumeSettings();
                break;
        }
        ballPlaced = false;
    }

    /**
     * Handles all logic that should run only when the game is active.
     */
    void UpdateGameActive(float dt)
    {
        // Check for the game-over condition
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i]) continue;

            if (physics_solver->objects[i].position.y < -3) {
                if (total_points > high_score) {
                    high_score = total_points;
                }
                // Play the lose sound once when the state changes
                if (State == GAME_ACTIVE && loseSound != nullptr) {
                    Mix_PlayChannel(-1, loseSound, 0);
                }

                State = GAME_OVER;
                return; // Exit immediately, game is over
            }
        }
    }

    /**
     * Handles the camera and other animations for the game-over screen.
     */
    void UpdateGameOver(float dt)
    {
        // Smooth pan (rotate around bowl)
        float yawVelocity = (state.yaw - state.lastYaw);
        float pitchVelocity = (state.pitch - state.lastPitch);
        state.updateCameraPos(yawVelocity, pitchVelocity);

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

    /**
     * Handles physics updates at a fixed interval. This function is already correct.
     */
    void FixedUpdate(float dt)
    {
        if (State != GAME_ACTIVE) return;
        physics_solver->update(dt);
        total_points = physics_solver->total_points;
    }
    int Sound(){
        if (physics_solver->just_merged > 0 && mergeSound != nullptr) {
            Mix_PlayChannel(-1, mergeSound, 0);
        }
        if (ballPlaced && placeSound != nullptr) {
            Mix_PlayChannel(-1, placeSound, 0);
        }
        return 0; // Add return statement to prevent undefined behavior
    }

    void SetupModelShader(const glm::mat4& projection, const glm::mat4& view, 
                        const glm::vec3& camPos, const glm::vec3& lightPos, 
                        const glm::vec3& lightColor) {
        model_shader->use();
        model_shader->setMat4("view", view);
        model_shader->setMat4("projection", projection);
        model_shader->setVec3("camPos", camPos);
        model_shader->setVec3("lightPositions[0]", lightPos);
        model_shader->setVec3("lightColors[0]", lightColor);
        model_shader->setFloat("metallic", 0.0f);
        model_shader->setFloat("roughness", 0.5f);
        model_shader->setFloat("ao", 1.0f);
        model_shader->setFloat("alpha", 1.0f);

        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_CUBE_MAP, backgroundTexture);
        model_shader->setInt("environmentMap", 10);
    }
    void SetupBallShader(const glm::mat4& projection, const glm::mat4& view,
                    const glm::vec3& camPos, const glm::vec3& lightPos,
                    const glm::vec3& lightColor) {
        ball_shader->use();
        ball_shader->setMat4("view", view);
        ball_shader->setMat4("projection", projection);
        ball_shader->setVec3("camPos", camPos);
        ball_shader->setVec3("lightPositions[0]", lightPos);
        ball_shader->setVec3("lightColors[0]", lightColor);
        ball_shader->setFloat("metallic", 0.0f);
        ball_shader->setFloat("roughness", 0.5f);
        ball_shader->setFloat("ao", 1.0f);

        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_CUBE_MAP, backgroundTexture);
        ball_shader->setInt("environmentMap", 10);
    }

    /**
     * Renders the skybox.
     */
    void RenderSkybox(const glm::mat4& projection, const glm::mat4& view)
    {
        glDepthFunc(GL_LEQUAL); // Render behind everything else
        glDepthMask(GL_FALSE);  // Don't write to the depth buffer

        background_shader->use();
        glm::mat4 skyboxView = glm::mat4(glm::mat3(view)); // Remove translation
        background_shader->setMat4("view", skyboxView);
        background_shader->setMat4("projection", projection);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, backgroundTexture);
        renderCube();
        
        glDepthMask(GL_TRUE);   // Restore depth writing
        glDepthFunc(GL_LESS);   // Restore default depth function
    }
    
    void RenderTable() {
        // model_shader should already be active when this is called
        model_shader->use(); 
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, glm::vec3(-2.0f, -13.65f, -2.0f));
        modelMatrix = glm::scale(modelMatrix, glm::vec3(2.f, 2.f, 2.f));
        model_shader->setMat4("model", modelMatrix);
        tableModel->Draw(*model_shader);
    }

    void RenderPhysicsObjects() {
        // ball_shader should already be active when this is called
        ball_shader->setFloat("alpha", 1.0f);
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            
            PhysicsObject &obj = physics_solver->objects[i];
            b_rend->Draw4d(state.w, obj.fruit, obj.position, glm::vec3(0.0f));
        }
    }

    void RenderPreviewObject() {
        RayInter mousePos = getPlacementMouse(&state, &boundary, physics_solver);
        if (mousePos.hit) {
            // ball_shader should already be active when this is called
            ball_shader->setFloat("alpha", 0.5f);
            b_rend->Draw3d(nextFruit, mousePos.point + glm::vec3(0, 3, 0), glm::vec3(0.0f));
        }
    }

    /**
     * Renders the score text for the game UI.
     */
    void RenderGameUI()
    {
        glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(state.windowWidth), static_cast<float>(state.windowHeight), 0.0f);
        text_shader->use();
        text_shader->setMat4("projection", projection);
        t_rend->RenderText("Score " + std::to_string((int)physics_solver->total_points), 25.0f, 25.0f, 1.0f, glm::vec3(1.0f));
    }

    void Render() {
        // Clear the screen and set initial OpenGL state for all frames
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glDepthFunc(GL_LESS);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Call the correct render function based on the current game state
        switch (State) {
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

    void RenderGame() {
        // 1. Set up the 3D camera matrices
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)Width / (float)Height, 0.1f, 100.0f);
        glm::mat4 view = state.getViewMatrix();
        glm::vec3 camPos = state.getCameraPosition();

        // 2. Calculate dynamic lighting once
        float t = (state.w - state.w_min) / (state.w_max - state.w_min);
        glm::vec3 light_color = calculateSunsetColor(t);
        float theta = 3.14159f * (0.8*t+0.1);
        float x = light_radius * cos(theta);
        float y = max_height * sin(theta);
        float z = light_radius * sin(theta); // Fixed Z coordinate
        glm::vec3 light_position = glm::vec3(x, y, z);

        // 3. Render the skybox first (it uses its own shader)
        RenderSkybox(projection, view);

        // 4. Render the Table with model shader
        SetupModelShader(projection,view,camPos,light_position,light_color);
        RenderTable();

        // 5. Render Physics Objects (Fruits) with ball shader
        SetupBallShader(projection,view,camPos,light_position,light_color);
        RenderPhysicsObjects();

        // 6. Draw the Bowl (assuming it uses ball_shader)
        h_rend->Draw4d(state.w, bowlTexture, glm::vec4(0.0f), boundary.radius, glm::vec3(0.0f));

        // 7. Draw the transparent Preview Object
        RenderPreviewObject();

        // 8. Finally, render the 2D UI on top
        RenderGameUI();
    }

    void RenderSlider(const Slider& slider)
    {
        float scale = 0.75f;
        glm::vec3 color = glm::vec3(0.9f);
        
        // Create the visual bar based on the slider's current value
        std::string bar = "[";
        int barLength = 20;
        for (int i = 0; i < barLength; ++i) {
            bar += (i < (*slider.value) * barLength) ? "=" : "-";
        }
        bar += "]";

        // Render the label and the bar
        t_rend->RenderText(slider.label,  slider.rect.x, slider.rect.y - 25.0f, scale, color);
        t_rend->RenderTextScale(bar,  slider.rect.x, slider.rect.y,slider.rect.z,slider.rect.w, color);
    }
    void RenderMenu() {
        // 1. Setup 3D view for the background skybox
        glm::mat4 projection3D = glm::perspective(glm::radians(45.0f), (float)Width / (float)Height, 0.1f, 100.0f);
        glm::mat4 view = state.getViewMatrix();
        RenderSkybox(projection3D, view);

        // 2. Setup 2D projection for all text and UI rendering
        glm::mat4 projection2D = glm::ortho(0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f);
        t_rend->shader->use();
        t_rend->shader->setMat4("projection", projection2D);

        // --- Render All Menu Text ---
        float centerX = Width / 2.0f;
        float centerY = Height / 2.0f;
        
        // Title and Subtitle
        std::string title = "Suika 4D";
        t_rend->RenderText(title, centerX - t_rend->GetTextWidth(title, 2.0f) / 2.0f, centerY - 150.0f, 2.0f, glm::vec3(0.9f, 0.9f, 1.0f));
        std::string subtitle = "Press ESC or ENTER to Play";
        t_rend->RenderText(subtitle, centerX - t_rend->GetTextWidth(subtitle, 1.0f) / 2.0f, centerY - 50.0f, 1.0f, glm::vec3(0.8f, 0.8f, 0.8f));

        // Instructions Block
        float instructionScale = 0.75f;
        glm::vec3 instructionColor = glm::vec3(0.7f);


        float startY = Height*0.6f;
        // float startY = centerY + 50.0f;
        float lineSpacing = 20.0f;
        
        vector<std::string> lines = {"W / S - Move in the 4th Dimension","Left Click - Place Fruit","Right-Click + Drag - Rotate Camera","Mouse Wheel - Zoom In/Out","ESC - Pause / Return to Menu"};
        for (int i =0;i<lines.size();i++){
            t_rend->RenderText(lines[i], centerX - t_rend->GetTextWidth(lines[i], instructionScale) / 2.0f, startY + lineSpacing*i, instructionScale, instructionColor);
        }

        // float sliderYOffset = startY;// + (lineSpacing * 5) + 40.0f; // Position sliders below instructions
        // Music Slider

        RenderSlider(vset.musicSlider);
        RenderSlider(vset.sfxSlider);
    }
    void RenderEndScreen() {
        // 1. Set up the 3D camera matrices
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)Width / (float)Height, 0.1f, 100.0f);
        glm::mat4 view = state.getViewMatrix();
        glm::vec3 camPos = state.getCameraPosition();

        // 2. Calculate dynamic lighting once
        float t = (state.w - state.w_min) / (state.w_max - state.w_min);
        glm::vec3 light_color = calculateSunsetColor(t);
        float theta = 3.14159f * (0.8*t+0.1);
        float x = light_radius * cos(theta);
        float y = max_height * sin(theta);
        float z = light_radius * sin(theta); // Fixed Z coordinate
        glm::vec3 light_position = glm::vec3(x, y, z);

        // 3. Render the skybox first (it uses its own shader)
        RenderSkybox(projection, view);

        // 4. Render the Table with model shader
        SetupModelShader(projection,view,camPos,light_position,light_color);
        RenderTable();

        // 5. Render Physics Objects (Fruits) with ball shader
        SetupBallShader(projection,view,camPos,light_position,light_color);
        RenderPhysicsObjects();

        // 6. Draw the Bowl (assuming it uses ball_shader)
        h_rend->Draw4d(state.w, bowlTexture, glm::vec4(0.0f), boundary.radius, glm::vec3(0.0f));

        glm::mat4 projection_2d = glm::ortho(0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f);
        text_shader->use();
        text_shader->setMat4("projection", projection_2d);
        text_shader->setInt("text", 0);

        std::string msg1 = "GAME OVER";
        std::string msg2 = "Score: " + std::to_string(total_points);
        std::string msg3 = "High Score: " + std::to_string(high_score);
        std::string msg4 = "Press ESC or ENTER to Restart";

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
