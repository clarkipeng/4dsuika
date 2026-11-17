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

#ifndef __EMSCRIPTEN__
#include <SDL.h>
#include <SDL_mixer.h>
#endif

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
#include "menu.hpp"
#include "fruit.hpp"

enum GameState {
    GAME_ACTIVE,
    GAME_MENU,    // start screen
    GAME_PAUSED,
    GAME_OVER
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

#ifndef __EMSCRIPTEN__
    Mix_Chunk* mergeSound = nullptr;
    Mix_Chunk* loseSound = nullptr;
    Mix_Chunk* placeSound = nullptr;
    Mix_Music* themeMusic = nullptr;
    bool sdlReady = false;
    bool audioReady = false;
    bool audioMuted = false;
#endif

    HemisphereBoundary boundary;
    Fruit nextFruit;
    FruitManager fm;

    int total_points=0;
    int high_score = 0;
    ViewState state;
    Menu menu;
    enum class Page { None, Settings, Controls };
    Page page = Page::None;           // settings or controls opened over the current screen
    int menuBuiltFor = -1;
    GLFWwindow* window = nullptr;
    bool mouseWasDown = false;
    float heldKeyTime = 0.0f;
    float musicVolume = 0.8f, sfxVolume = 1.0f;
    bool soundOn = true;
    int scaleChoice = 0;              // index into kScaleChoices; 0 = adaptive

    bool ballPlaced=false;
    bool homing = false;              // easing back to the default slice
    float turnVel[kTurnCount] = {};   // radians per second, per binding
    float offsetVel = 0.0f;
    glm::vec2 orbitVel{0.0f};         // mouse-orbit glide after release (rad/s)
    float time = 0.0f;
    bool showPerf = false;            // F3: GPU time and render scale

    // Fruit sheet (headless --fruit-sheet): fixed fruit instead of physics, labelled.
    bool sheetMode = false;
    std::vector<FruitInstance> sheetFruit;
    std::string sheetTitle;

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
#ifndef __EMSCRIPTEN__
        if (mergeSound) Mix_FreeChunk(mergeSound);
        if (loseSound) Mix_FreeChunk(loseSound);
        if (placeSound) Mix_FreeChunk(placeSound);
        if (themeMusic) Mix_FreeMusic(themeMusic);
        if (audioReady) Mix_CloseAudio();
        if (sdlReady) SDL_Quit();
#endif
        delete t_rend;
        delete text_shader;
        delete physics_solver;
        delete thread_pool;
    }
    void SetAudioState(bool sdlInitialized, bool mixerInitialized, bool muted) {
#ifndef __EMSCRIPTEN__
        sdlReady = sdlInitialized;
        audioReady = mixerInitialized;
        audioMuted = muted;
#else
        (void)sdlInitialized;
        (void)mixerInitialized;
        (void)muted;
#endif
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

    void Init(GLFWwindow* win){
        window = win;
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
        state.Init(win);

#ifndef __EMSCRIPTEN__
        if (audioReady && !audioMuted) {
            mergeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/drop.wav").c_str());
            if (!mergeSound) std::cerr << "Warning: Could not load drop.wav: " << Mix_GetError() << std::endl;
            loseSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/game_over.wav").c_str());
            if (!loseSound) std::cerr << "Warning: Could not load game_over.wav: " << Mix_GetError() << std::endl;
            placeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/click.wav").c_str());
            if (!placeSound) std::cerr << "Warning: Could not load click.wav: " << Mix_GetError() << std::endl;
            themeMusic = Mix_LoadMUS(FileSystem::getPath("resources/audio/theme.wav").c_str());
            if (!themeMusic) std::cerr << "Mix_LoadMUS error: " << Mix_GetError() << std::endl;
        }
#endif

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifndef __EMSCRIPTEN__
        if (audioReady && !audioMuted && themeMusic) Mix_PlayMusic(themeMusic, -1);
#endif
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

        // Mouse turning: applied at once while dragging, then glides out.
        if (state.rotating) {
            if (dt > 0.0f) {
                orbitVel += (glm::vec2(state.pendingYaw, state.pendingPitch) / dt - orbitVel) * ease;
            }
            state.cam.orbitBy(state.pendingYaw, state.pendingPitch);
        } else {
            state.cam.orbitBy(orbitVel.x * dt, orbitVel.y * dt);
            orbitVel *= std::exp(-dt * 4.0f);
        }
        state.pendingYaw = state.pendingPitch = 0.0f;

        float orbitYaw = float(keyDown(GLFW_KEY_LEFT)) - float(keyDown(GLFW_KEY_RIGHT));
        float orbitPitch = float(keyDown(GLFW_KEY_UP)) - float(keyDown(GLFW_KEY_DOWN));
        state.cam.orbitBy(orbitYaw * 1.5f * dt, orbitPitch * 1.0f * dt);

        if (keyPressedOnce(GLFW_KEY_H) || keyPressedOnce(GLFW_KEY_SPACE)) homing = true;
        if (anyInput || state.rotating) homing = false;  // the player takes over
        if (homing) orbitVel = glm::vec2(0.0f);
        if (homing && state.cam.easeHome(dt)) homing = false;
    }

    void ProcessInput(float dt){
        if (State == GAME_ACTIVE) UpdateCamera(dt);
        else state.pendingYaw = state.pendingPitch = 0.0f;

        if (keyPressedOnce(GLFW_KEY_G)) {
            renderer.setQuality(Quality((int(renderer.quality()) + 1) % 4));
            ApplyRenderScale();
        }
        if (keyPressedOnce(GLFW_KEY_F3)) showPerf = !showPerf;

        if (keyPressedOnce(GLFW_KEY_ESCAPE)) {
            if (page != Page::None) ClosePage();
            else if (State == GAME_ACTIVE) State = GAME_PAUSED;
            else if (State == GAME_PAUSED) State = GAME_ACTIVE;
        }
        if (State != GAME_ACTIVE) UpdateMenuInput(dt);
    }

    void UpdateMenuInput(float dt) {
        EnsureMenu();
        MenuInput in;
        in.up = keyPressedOnce(GLFW_KEY_UP) || keyPressedOnce(GLFW_KEY_W);
        in.down = keyPressedOnce(GLFW_KEY_DOWN) || keyPressedOnce(GLFW_KEY_S);
        in.select = keyPressedOnce(GLFW_KEY_ENTER) || keyPressedOnce(GLFW_KEY_KP_ENTER) || keyPressedOnce(GLFW_KEY_SPACE);
        bool left = keyDown(GLFW_KEY_LEFT) || keyDown(GLFW_KEY_A), right = keyDown(GLFW_KEY_RIGHT) || keyDown(GLFW_KEY_D);
        in.horizontalHeld = int(right) - int(left);
        if (keyPressedOnce(GLFW_KEY_LEFT) || keyPressedOnce(GLFW_KEY_A)) in.horizontal = -1;
        if (keyPressedOnce(GLFW_KEY_RIGHT) || keyPressedOnce(GLFW_KEY_D)) in.horizontal = +1;
        in.mouse = glm::vec2(state.m_xpos, state.m_ypos);
        in.mouseDown = window && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        in.mousePressed = in.mouseDown && !mouseWasDown;
        mouseWasDown = in.mouseDown;
        menu.update(*t_rend, in, dt, float(Width), float(Height));
        EnsureMenu();  // an item may have changed the screen
    }

    void ApplyVolumeSettings()
    {
#ifndef __EMSCRIPTEN__
        if (!audioReady || audioMuted) return;
        float master = soundOn ? 1.0f : 0.0f;
        Mix_VolumeMusic(static_cast<int>(master * musicVolume * MIX_MAX_VOLUME));
        int sfx_volume_sdl = static_cast<int>(master * sfxVolume * MIX_MAX_VOLUME);
        if (mergeSound) Mix_VolumeChunk(mergeSound, sfx_volume_sdl);
        if (loseSound)  Mix_VolumeChunk(loseSound, sfx_volume_sdl);
        if (placeSound) Mix_VolumeChunk(placeSound, sfx_volume_sdl);
#endif
    }

    // Render scale choices: 0 = adaptive, otherwise a fixed fraction of the window
    // (still capped by the quality preset's pixel budget).
    static constexpr float kScaleChoices[] = {0.0f, 1.0f, 0.75f, 0.5f, 0.33f};
    static constexpr int kScaleCount = 5;
    void ApplyRenderScale() {
        float s = kScaleChoices[scaleChoice];
        renderer.adaptive = s == 0.0f;
        if (s > 0.0f) renderer.renderScale = s;
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
#ifndef __EMSCRIPTEN__
                if (audioReady && !audioMuted && Mix_PausedMusic() == 1) Mix_ResumeMusic();
#endif
                UpdateGameActive(dt);
                break;
            case GAME_OVER:
                UpdateGameOver(dt);
                ApplyVolumeSettings();
#ifndef __EMSCRIPTEN__
                if (audioReady && !audioMuted && State == GAME_OVER) Mix_PauseMusic();
#endif
                break;
            case GAME_MENU:
            case GAME_PAUSED:
#ifndef __EMSCRIPTEN__
                if (audioReady && !audioMuted && Mix_PausedMusic() == 1) Mix_ResumeMusic();
#endif
                ApplyVolumeSettings();
                break;
        }
        ballPlaced = false;
    }

    void UpdateGameActive(float dt)
    {
        menuBuiltFor = -1;  // pause and game-over menus show fresh scores
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i]) continue;
            if (physics_solver->objects[i].position.y < -3) {
                if (total_points > high_score) high_score = total_points;
#ifndef __EMSCRIPTEN__
                if (State == GAME_ACTIVE && loseSound != nullptr) Mix_PlayChannel(-1, loseSound, 0);
#endif
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
#ifndef __EMSCRIPTEN__
        if (!audioReady || audioMuted) return 0;
        if (physics_solver->just_merged > 0 && mergeSound != nullptr) Mix_PlayChannel(-1, mergeSound, 0);
        if (ballPlaced && placeSound != nullptr) Mix_PlayChannel(-1, placeSound, 0);
#endif
        return 0;
    }

    // Drop the next fruit above whatever the cursor points at.
    bool DropFruit() {
        Hit4 hit = getPlacementMouse(state, boundary, *physics_solver);
        if (hit.hit) {
            physics_solver->addObject(PhysicsObject(hit.point + glm::vec4(0, 3.0f, 0, 0), nextFruit, true, false));
            nextFruit = FruitManager::getRandomFruit();
            ballPlaced = true;
            return true;
        }
        ballPlaced = false;
        return false;
    }

    // ------------------------------------------------------------ rendering

    void RenderScene(bool showPreview) { RenderScene(showPreview, state.cam); }
    void RenderScene(bool showPreview, const Camera4D& cam) {
        std::vector<FruitInstance> fruit;
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const PhysicsObject& obj = physics_solver->objects[i];
            fruit.push_back({obj.position, obj.radius, int(obj.fruit), obj.flash, i, obj.worldToLocalFrame()});
        }
        renderer.setFruit(sheetMode ? sheetFruit : fruit);
        Hit4 hit;
        if (showPreview) hit = getPlacementMouse(state, boundary, *physics_solver);
        renderer.setPreview(hit.hit, hit.point + glm::vec4(0, 3.0f, 0, 0),
                            FruitManager::getFruitProperties(nextFruit).radius, int(nextFruit));
        renderer.draw(cam, FbWidth, FbHeight, time);
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

    void RenderSheetLabels() {
        static const char* names[] = {"cherry", "strawberry", "grape", "dekopon", "persimmon", "apple",
                                      "pear", "peach", "pineapple", "melon", "watermelon"};
        BeginText();
        t_rend->RenderText(sheetTitle, 25.0f, 25.0f, 0.9f, glm::vec3(1.0f));
        float aspect = float(Width) / float(std::max(Height, 1u));
        for (const FruitInstance& f : sheetFruit) {
            glm::vec2 ndc;
            if (!state.cam.project(f.center - glm::vec4(0, f.radius * 1.15f, 0, 0), aspect, ndc)) continue;
            float x = (ndc.x * 0.5f + 0.5f) * Width, y = (0.5f - ndc.y * 0.5f) * Height + 8.0f;
            std::string name = names[f.type];
            t_rend->RenderText(name, x - t_rend->GetTextWidth(name, 0.6f) / 2.0f, y, 0.6f, glm::vec3(0.95f));
        }
    }

    void RenderGameUI()
    {
        if (sheetMode) { RenderSheetLabels(); return; }
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
            case GAME_MENU:   RenderMenuScreen(Camera4D::showcase(time), 0.35f); break;
            case GAME_PAUSED:
            case GAME_OVER:   RenderMenuScreen(state.cam, 0.5f); break;
        }
    }

    void RenderGame() {
        RenderScene(true);
        RenderGameUI();
    }

    // ---------------------------------------------------------------- menus

    static std::string percent(float v) { return std::to_string(int(v * 100.0f + 0.5f)) + "%"; }

    static void CycleIndex(int& i, int dir, int count) { i = (i + dir + count) % count; }

    // Rebuilds the menu when the screen it shows changes.
    void EnsureMenu() {
        int key = int(State) * 4 + int(page);
        if (key == menuBuiltFor) return;
        menuBuiltFor = key;
        if (page == Page::Settings) BuildSettingsMenu();
        else if (page == Page::Controls) BuildControlsMenu();
        else if (State == GAME_MENU) BuildTitleMenu();
        else if (State == GAME_PAUSED) BuildPauseMenu();
        else BuildGameOverMenu();
    }

    Page returnPage = Page::None;     // where Back from a page goes
    void Open(Page p) { returnPage = page; page = p; EnsureMenu(); }
    void ClosePage() { page = returnPage; returnPage = Page::None; }

    void BuildTitleMenu() {
        MenuStyle st;
        st.title = "SUIKA 4D";
        st.subtitle = "Merge fruit in four dimensions";
        st.hero = true;
        st.width = 360.0f;
        std::vector<MenuItem> items = {
            MenuItem::button("Play", [this] { Reset(); }),
            MenuItem::button("Settings", [this] { Open(Page::Settings); }),
            MenuItem::button("Controls", [this] { Open(Page::Controls); }),
        };
#ifndef __EMSCRIPTEN__
        items.push_back(MenuItem::button("Quit", [this] { if (window) glfwSetWindowShouldClose(window, GLFW_TRUE); }));
#endif
        st.hint = "Up / Down  choose     Enter  select";
        menu.set(std::move(items), std::move(st));
    }

    void BuildPauseMenu() {
        MenuStyle st;
        st.title = "PAUSED";
        st.subtitle = "Score " + std::to_string(total_points);
        st.width = 380.0f;
        st.hint = "Up / Down  choose     Enter  select     Esc  resume";
        menu.set({
            MenuItem::button("Resume", [this] { State = GAME_ACTIVE; }),
            MenuItem::button("Restart", [this] { Reset(); }),
            MenuItem::button("Settings", [this] { Open(Page::Settings); }),
            MenuItem::button("Controls", [this] { Open(Page::Controls); }),
            MenuItem::button("Main menu", [this] { State = GAME_MENU; }),
        }, std::move(st));
    }

    void BuildGameOverMenu() {
        MenuStyle st;
        st.title = "GAME OVER";
        st.titleColor = ui::kCoral;
        st.subtitle = "Score " + std::to_string(total_points) + "     Best " + std::to_string(high_score);
        st.width = 380.0f;
        st.hint = "Up / Down  choose     Enter  select";
        menu.set({
            MenuItem::button("Play again", [this] { Reset(); }),
            MenuItem::button("Settings", [this] { Open(Page::Settings); }),
            MenuItem::button("Main menu", [this] { State = GAME_MENU; }),
        }, std::move(st));
    }

    void BuildSettingsMenu() {
        MenuStyle st;
        st.title = "SETTINGS";
        st.width = 580.0f;
        st.note = [this] {
            int w = std::max(1, int(FbWidth * std::min(renderer.renderScale, renderer.maxScale(FbWidth, FbHeight)) + 0.5f));
            int h = std::max(1, int(FbHeight * std::min(renderer.renderScale, renderer.maxScale(FbWidth, FbHeight)) + 0.5f));
            return "Scene rendered at " + std::to_string(w) + " x " + std::to_string(h);
        };
        std::vector<MenuItem> items;
        items.push_back(MenuItem::choice("Quality", [this] { return std::string(qualityName(renderer.quality())); },
            [this](int d) { renderer.setQuality(Quality((int(renderer.quality()) + d + 4) % 4)); ApplyRenderScale(); }));
        items.push_back(MenuItem::choice("Render scale", [this] {
                return scaleChoice == 0 ? std::string("Auto") : percent(kScaleChoices[scaleChoice]);
            }, [this](int d) { CycleIndex(scaleChoice, d, kScaleCount); ApplyRenderScale(); }));
        items.push_back(MenuItem::slider("Facets", [this] { return renderer.facet; }, [this](float v) { renderer.facet = v; }));
#ifndef __EMSCRIPTEN__
        items.push_back(MenuItem::choice("Sound", [this] { return std::string(soundOn ? "On" : "Muted"); },
            [this](int) { soundOn = !soundOn; ApplyVolumeSettings(); }));
        items.push_back(MenuItem::slider("Music", [this] { return musicVolume; }, [this](float v) { musicVolume = v; ApplyVolumeSettings(); }));
        items.push_back(MenuItem::slider("Effects", [this] { return sfxVolume; }, [this](float v) { sfxVolume = v; ApplyVolumeSettings(); }));
#endif
        items.push_back(MenuItem::button("Controls", [this] { Open(Page::Controls); }));
        items.push_back(MenuItem::button("Back", [this] { ClosePage(); }));
        menu.set(std::move(items), std::move(st));
    }

    void BuildControlsMenu() {
        MenuStyle st;
        st.title = "CONTROLS";
        st.width = 680.0f;
        st.keyColumn = 230.0f;
        st.hint = "Enter or Esc  back";
        std::vector<MenuItem> items = {
            MenuItem::info("Left click", "drop the fruit"),
            MenuItem::info("Right drag / arrows", "turn the view"),
            MenuItem::info("Mouse wheel", "zoom"),
        };
        for (const TurnBinding& b : kTurnBindings) {
            std::string h = b.help;
            size_t gap = h.find("  ");
            items.push_back(MenuItem::info(h.substr(0, gap), h.substr(gap + 2)));
        }
        items.push_back(MenuItem::info("W / S", "move the slice along the 4th axis"));
        items.push_back(MenuItem::info("H / Space", "back to the default slice"));
        items.push_back(MenuItem::info("G / F3", "cycle quality / show timing"));
        items.push_back(MenuItem::info("Esc", "pause"));
        items.push_back(MenuItem::button("Back", [this] { ClosePage(); }));
        menu.set(std::move(items), std::move(st));
        menu.focus = int(menu.itemCount()) - 1;
    }

    void RenderMenuScreen(const Camera4D& cam, float dim) {
        EnsureMenu();
        renderer.dim = dim;
        RenderScene(false, cam);
        renderer.dim = 0.0f;
        BeginText();
        menu.draw(*t_rend, float(Width), float(Height));
    }
};
