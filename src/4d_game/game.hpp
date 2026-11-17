#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>
#include <SDL_mixer.h>

#include "globals.h"
#include "learnopengl/shader.h"
#include "filesystem.h"
#include "threadpool.hpp"

#include "text_renderer.h"
#include "raymarch_renderer.hpp"
#include "camera4d.hpp"

#include "hemisphere_boundary.hpp"
#include "physics_solver.hpp"

#include "state_helper.hpp"
#include "fruit.hpp"

enum GameState {
    GAME_ACTIVE,
    GAME_MENU,
    GAME_OVER
};

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
// Held-key 4D turning. Each binding turns the camera in one of its own
// planes (0 right, 1 up, 2 back, 3 slice normal), so the keys act on what the
// player sees. Velocities ease in and out, and opposite keys undo each other.
struct TurnBinding {
    int keyPlus, keyMinus;
    Axis4 a, b;
    const char* help;
};
static const TurnBinding kTurnBindings[] = {
    {GLFW_KEY_D, GLFW_KEY_A, AX_X, AX_W, "A / D  turn left-right into 4D (xw)"},
    {GLFW_KEY_R, GLFW_KEY_F, AX_Y, AX_W, "R / F  turn up-down into 4D (yw)"},
    {GLFW_KEY_E, GLFW_KEY_Q, AX_Z, AX_W, "Q / E  turn depth into 4D (zw)"},
    {GLFW_KEY_C, GLFW_KEY_Z, AX_X, AX_Y, "Z / C  roll (xy)"},
};
constexpr int kTurnCount = sizeof(kTurnBindings) / sizeof(kTurnBindings[0]);

class Game
{
public:
    GameState               State;
    bool                    Keys[1024];
    bool                    KeysProcessed[1024];
    unsigned int            Width, Height;       // window size in screen points
    int                     FbWidth = 0, FbHeight = 0; // render target in pixels

    RaymarchRenderer renderer;
    TextRenderer    *t_rend = nullptr;
    Shader          *text_shader = nullptr;

    PhysicSolver *physics_solver;
    tp::ThreadPool *thread_pool;

    Mix_Chunk* mergeSound = nullptr;
    Mix_Chunk* loseSound = nullptr;
    Mix_Chunk* placeSound = nullptr;
    Mix_Music* themeMusic = nullptr;

    HemisphereBoundary boundary;
    Fruit nextFruit;
    FruitManager fm;

    int total_points=0;
    int high_score = 0;
    ViewState state;
    VolumeSettings vset;

    bool ballPlaced=false;
    bool homing = false;              // easing back to the default slice
    float turnVel[kTurnCount] = {};   // radians per second, per binding
    float offsetVel = 0.0f;
    float time = 0.0f;
    bool showPerf = false;            // F3: GPU time and render scale

    Game(unsigned int width, unsigned int height) : boundary(glm::vec4(0.0f), 3, 90.0f, 0.1f) {
        State = GAME_MENU;
        Width = width;
        Height = height;
        for (int i = 0; i < 1024; i++) {
            Keys[i] = false;
            KeysProcessed[i] = false;
        }

        unsigned int thread_count = 1;
        #ifndef __EMSCRIPTEN__
            thread_count = std::thread::hardware_concurrency();
            if (thread_count == 0) thread_count = 1;
            if (thread_count > 4) thread_count = 4;
        #endif
        thread_pool = new tp::ThreadPool(thread_count);
        physics_solver = new PhysicSolver(*thread_pool, &boundary);
        total_points = 0;
    }
    ~Game(){
        Mix_FreeChunk(mergeSound);
        Mix_CloseAudio();
        SDL_Quit();
        delete t_rend;
        delete physics_solver;
        delete thread_pool;
    }
    void Reset(){
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            if (physics_solver->has_obj[i]) {
                physics_solver->removeObject(i);
            }
        }
        physics_solver->reset();
        total_points = 0;
        physics_solver->total_points = 0;
        fm.initializeFruits();
        nextFruit = fm.getRandomFruit();
        state.reset();
        homing = false;
        State = GAME_ACTIVE;
    }

    void Init(GLFWwindow* window){
        if (!renderer.init()) std::cerr << "Raymarch renderer failed to initialise" << std::endl;
        text_shader = new Shader(FileSystem::getPath("resources/shaders/text_2d.vs").c_str(), FileSystem::getPath("resources/shaders/text_2d.fs").c_str());
        t_rend = new TextRenderer(text_shader);
        try {
            t_rend->Load(FileSystem::getPath("resources/fonts/OCRAEXT.TTF").c_str(), 24);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load font: " << e.what() << std::endl;
        }

        fm.initializeFruits();
        nextFruit = fm.getRandomFruit();
        state.Init(window);

        mergeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/drop.wav").c_str());
        if (!mergeSound) std::cerr << "Warning: Could not load drop.wav: " << Mix_GetError() << std::endl;
        loseSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/game_over.wav").c_str());
        if (!loseSound) std::cerr << "Warning: Could not load game_over.wav: " << Mix_GetError() << std::endl;
        placeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/click.wav").c_str());
        if (!placeSound) std::cerr << "Warning: Could not load click.wav: " << Mix_GetError() << std::endl;
        themeMusic = Mix_LoadMUS(FileSystem::getPath("resources/audio/theme.wav").c_str());
        if (!themeMusic) std::cerr << "Mix_LoadMUS error: " << Mix_GetError() << std::endl;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (themeMusic) Mix_PlayMusic(themeMusic, -1);
    }

    bool keyDown(int key) const { return key >= 0 && key < 1024 && Keys[key]; }
    bool keyPressedOnce(int key) {
        if (!keyDown(key) || KeysProcessed[key]) return false;
        KeysProcessed[key] = true;
        return true;
    }

    // Camera controls: smooth 4D turning, slice offset, keyboard orbit, home.
    void UpdateCamera(float dt) {
        const float turnSpeed = 1.1f;  // rad/s at full tilt
        const float ease = 1.0f - std::exp(-dt * 10.0f);
        bool anyInput = false;
        for (int i = 0; i < kTurnCount; ++i) {
            const TurnBinding& b = kTurnBindings[i];
            float want = turnSpeed * (float(keyDown(b.keyPlus)) - float(keyDown(b.keyMinus)));
            anyInput |= want != 0.0f;
            turnVel[i] += (want - turnVel[i]) * ease;
            if (std::abs(turnVel[i]) > 1e-5f) state.cam.turnLocal(b.a, b.b, turnVel[i] * dt);
        }
        float wantOffset = 1.5f * (float(keyDown(GLFW_KEY_W)) - float(keyDown(GLFW_KEY_S)));
        anyInput |= wantOffset != 0.0f;
        offsetVel += (wantOffset - offsetVel) * ease;
        state.cam.moveAlongNormal(offsetVel * dt);

        float orbitYaw = float(keyDown(GLFW_KEY_LEFT)) - float(keyDown(GLFW_KEY_RIGHT));
        float orbitPitch = float(keyDown(GLFW_KEY_UP)) - float(keyDown(GLFW_KEY_DOWN));
        state.cam.orbitBy(orbitYaw * 1.5f * dt, orbitPitch * 1.0f * dt);

        if (keyPressedOnce(GLFW_KEY_H) || keyPressedOnce(GLFW_KEY_SPACE)) homing = true;
        if (anyInput || state.rotating) homing = false;  // the player takes over
        if (homing && state.cam.easeHome(dt)) homing = false;
    }

    void ProcessInput(float dt){
        if (State == GAME_ACTIVE) UpdateCamera(dt);

        if (State == GAME_MENU && keyPressedOnce(GLFW_KEY_ENTER)) State = GAME_ACTIVE;
        if (State == GAME_OVER && keyPressedOnce(GLFW_KEY_ENTER)) Reset();

        if (keyPressedOnce(GLFW_KEY_G)) {
            renderer.setQuality(Quality((int(renderer.quality()) + 1) % 4));
        }
        if (keyPressedOnce(GLFW_KEY_F3)) showPerf = !showPerf;

        if (keyPressedOnce(GLFW_KEY_ESCAPE)) {
            State = (State == GAME_ACTIVE || State == GAME_OVER) ? GAME_MENU : GAME_ACTIVE;
        }
    }

    void ApplyVolumeSettings()
    {
        Mix_VolumeMusic(static_cast<int>(vset.musicVolume * MIX_MAX_VOLUME));
        int sfx_volume_sdl = static_cast<int>(vset.sfxVolume * MIX_MAX_VOLUME);
        if (mergeSound) Mix_VolumeChunk(mergeSound, sfx_volume_sdl);
        if (loseSound)  Mix_VolumeChunk(loseSound, sfx_volume_sdl);
        if (placeSound) Mix_VolumeChunk(placeSound, sfx_volume_sdl);
    }

    void Update(float dt)
    {
        time += dt;
        Width = state.windowWidth;
        Height = state.windowHeight;
        Sound();

        switch (State)
        {
            case GAME_ACTIVE:
                if (Mix_PausedMusic() == 1) Mix_ResumeMusic();
                UpdateGameActive(dt);
                break;
            case GAME_OVER:
                UpdateGameOver(dt);
                if (State == GAME_OVER) Mix_PauseMusic();
                break;
            case GAME_MENU:
                if (Mix_PausedMusic() == 1) Mix_ResumeMusic();
                vset.UpdateLayout(Width, Height);
                vset.UpdateControls();
                ApplyVolumeSettings();
                break;
        }
        ballPlaced = false;
    }

    void UpdateGameActive(float dt)
    {
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i]) continue;
            if (physics_solver->objects[i].position.y < -3) {
                if (total_points > high_score) high_score = total_points;
                if (State == GAME_ACTIVE && loseSound != nullptr) Mix_PlayChannel(-1, loseSound, 0);
                State = GAME_OVER;
                return;
            }
        }
    }

    // Game over: drift around the bowl and slowly turn the slice through 4D.
    void UpdateGameOver(float dt)
    {
        state.cam.orbitBy(0.15f * dt, 0.0f);
        state.cam.turnLocal(AX_X, AX_W, 0.25f * dt);
        state.cam.distance += (15.0f - state.cam.distance) * dt * 1.5f;
    }

    void FixedUpdate(float dt)
    {
        if (State != GAME_ACTIVE) return;
        physics_solver->update(dt);
        total_points = physics_solver->total_points;
    }
    int Sound(){
        if (physics_solver->just_merged > 0 && mergeSound != nullptr) Mix_PlayChannel(-1, mergeSound, 0);
        if (ballPlaced && placeSound != nullptr) Mix_PlayChannel(-1, placeSound, 0);
        return 0;
    }

    // Drop the next fruit above whatever the cursor points at.
    void DropFruit() {
        Hit4 hit = getPlacementMouse(state, boundary, *physics_solver);
        if (hit.hit) {
            physics_solver->addObject(PhysicsObject(hit.point + glm::vec4(0, 3.0f, 0, 0), nextFruit, true, false));
            nextFruit = FruitManager::getRandomFruit();
        }
        ballPlaced = true;
    }

    // ------------------------------------------------------------ rendering

    void RenderScene(bool showPreview) {
        std::vector<FruitInstance> fruit;
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const PhysicsObject& obj = physics_solver->objects[i];
            fruit.push_back({obj.position, obj.radius, int(obj.fruit)});
        }
        renderer.setFruit(fruit);
        Hit4 hit;
        if (showPreview) hit = getPlacementMouse(state, boundary, *physics_solver);
        renderer.setPreview(hit.hit, hit.point + glm::vec4(0, 3.0f, 0, 0),
                            FruitManager::getFruitProperties(nextFruit).radius, int(nextFruit));
        renderer.draw(state.cam, FbWidth, FbHeight, time);
    }

    void BeginText() {
        glm::mat4 projection = glm::ortho(0.0f, float(Width), float(Height), 0.0f);
        text_shader->use();
        text_shader->setMat4("projection", projection);
        text_shader->setInt("text", 0);
    }

    // Slice read-out next to the orientation indicator (bottom-left).
    void RenderSliceLabel() {
        char buf[96];
        float tiltDeg = glm::degrees(state.cam.tilt());
        snprintf(buf, sizeof buf, "slice tilt %2.0f deg   offset %+.2f", tiltDeg, state.cam.offset);
        float s = 0.6f;
        float size = std::max(28.0f, FbHeight * 0.045f) * float(Height) / float(std::max(FbHeight, 1));
        t_rend->RenderText(buf, size * 3.9f, Height - size * 1.9f - 8.0f, s, glm::vec3(0.95f, 0.9f, 0.85f));
        if (homing) t_rend->RenderText("returning to default slice", size * 3.9f, Height - size * 1.9f + 14.0f, s, glm::vec3(1.0f, 0.8f, 0.3f));
    }

    void RenderGameUI()
    {
        BeginText();
        t_rend->RenderText("Score " + std::to_string((int)physics_solver->total_points), 25.0f, 25.0f, 1.0f, glm::vec3(1.0f));
        RenderSliceLabel();
        if (showPerf) {
            char buf[128];
            int rw = int(FbWidth * renderer.renderScale), rh = int(FbHeight * renderer.renderScale);
            snprintf(buf, sizeof buf, "%s  scene %.1f ms  %dx%d (%.0f%%)", qualityName(renderer.quality()),
                     renderer.gpuMs(), rw, rh, renderer.renderScale * 100.0f);
            t_rend->RenderText(buf, 25.0f, 60.0f, 0.6f, glm::vec3(0.9f, 0.95f, 0.8f));
        }
    }

    void Render() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        switch (State) {
            case GAME_ACTIVE: RenderGame(); break;
            case GAME_MENU:   RenderMenu(); break;
            case GAME_OVER:   RenderEndScreen(); break;
        }
    }

    void RenderGame() {
        RenderScene(true);
        RenderGameUI();
    }

    void RenderSlider(const Slider& slider)
    {
        float scale = 0.75f;
        glm::vec3 color = glm::vec3(0.9f);
        std::string bar = "[";
        int barLength = 20;
        for (int i = 0; i < barLength; ++i) {
            bar += (i < (*slider.value) * barLength) ? "=" : "-";
        }
        bar += "]";
        t_rend->RenderText(slider.label,  slider.rect.x, slider.rect.y - 25.0f, scale, color);
        t_rend->RenderTextScale(bar,  slider.rect.x, slider.rect.y,slider.rect.z,slider.rect.w, color);
    }

    void RenderMenu() {
        renderer.dim = 0.55f;
        RenderScene(false);
        renderer.dim = 0.0f;
        BeginText();

        float centerX = Width / 2.0f;
        float centerY = Height / 2.0f;
        std::string title = "Suika 4D";
        t_rend->RenderText(title, centerX - t_rend->GetTextWidth(title, 2.0f) / 2.0f, centerY - 150.0f, 2.0f, glm::vec3(1.0f, 0.92f, 0.85f));
        std::string subtitle = "Press ESC or ENTER to Play";
        t_rend->RenderText(subtitle, centerX - t_rend->GetTextWidth(subtitle, 1.0f) / 2.0f, centerY - 90.0f, 1.0f, glm::vec3(0.9f));

        float instructionScale = 0.7f;
        glm::vec3 instructionColor = glm::vec3(0.88f);
        float startY = centerY - 30.0f;
        float lineSpacing = 22.0f;
        std::vector<std::string> lines = {
            "Left Click - Drop fruit",
            "Right-Click + Drag / Arrows - Turn the view",
            "Mouse Wheel - Zoom",
        };
        for (const TurnBinding& b : kTurnBindings) lines.push_back(b.help);
        lines.push_back("W / S  move the slice along the 4th axis");
        lines.push_back("H or Space - Back to the default slice");
        lines.push_back(std::string("G - Graphics quality: ") + qualityName(renderer.quality()) + "   F3 - Performance");
        lines.push_back("ESC - Pause / Menu");
        for (size_t i = 0; i < lines.size(); i++) {
            t_rend->RenderText(lines[i], centerX - t_rend->GetTextWidth(lines[i], instructionScale) / 2.0f, startY + lineSpacing * i, instructionScale, instructionColor);
        }
        RenderSlider(vset.musicSlider);
        RenderSlider(vset.sfxSlider);
    }

    void RenderEndScreen() {
        RenderScene(false);
        BeginText();

        std::string msg1 = "GAME OVER";
        std::string msg2 = "Score: " + std::to_string(total_points);
        std::string msg3 = "High Score: " + std::to_string(high_score);
        std::string msg4 = "Press ENTER to Restart";

        float scale1 = 1.5f;
        float scale2 = 1.0f;
        float centerX = Width / 2.0f;
        t_rend->RenderText(msg1, centerX - t_rend->GetTextWidth(msg1, scale1) / 2.0f, 150.0f, scale1, glm::vec3(1.0f, 0.3f, 0.25f));
        t_rend->RenderText(msg2, centerX - t_rend->GetTextWidth(msg2, scale2) / 2.0f, 250.0f, scale2, glm::vec3(1.0f));
        t_rend->RenderText(msg3, centerX - t_rend->GetTextWidth(msg3, scale2) / 2.0f, 300.0f, scale2, glm::vec3(1.0f, 0.85f, 0.3f));
        t_rend->RenderText(msg4, centerX - t_rend->GetTextWidth(msg4, scale2) / 2.0f, 400.0f, scale2, glm::vec3(0.9f));
    }
};
