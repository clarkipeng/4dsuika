#pragma once
#include <limits>
#include <fstream>
#include <deque>
#include <functional>
#include <ctime>
#include <cstdlib>
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

// Display names by Fruit value.
inline constexpr const char* kFruitNames[] = {"cherry", "strawberry", "grape", "dekopon", "persimmon", "apple",
                                              "pear", "peach", "pineapple", "melon", "watermelon"};

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
    // Mouse-first controls (docs/controls.md). The wheel is per view mode: it turns
    // the slice (Slice), or sets the w of the drop (4D eye); Shift+wheel
    // always zooms. Both targets glide.
    Rot4 nextOrient;                   // orientation the next drop will have (the ghost shows it)
    bool nextOrientValid = false;
    Fruit nextOrientFor = CHERRY;
    float wheelTurn = 0.0f;           // wheel turn still to apply (rad, xw)
    float dropW = 0.0f, dropWShown = 0.0f; // drop's w offset: target, eased value
    static constexpr float kWheelStep = 0.09f;   // rad per notch (about 5 degrees)
    static constexpr float kDropWStep = 0.5f, kDropWMax = 2.0f;
    // Scripted mouse (reels): a visible cursor and a caption naming the input.
    bool showCursor = false;
    int heldButtons = 0;              // bit 0 left, 1 right, 2 middle
    std::string inputCaption;
    glm::vec2 orbitVel{0.0f};         // mouse-orbit glide after release (rad/s)
    float time = 0.0f;
    bool showPerf = false;            // F3: GPU time and render scale
    bool laneSnap = false;             // L: drops snap to whole w lanes
    static constexpr float kLaneSpacing = 1.0f;
    // Effects: merge bursts, the combo counter, the danger level (0..1).
    // Tutorial: starts in 3D (no w strip, rings or slice indicator; 4D keys locked),
    // then reveals 4D. Steps: 0 aim and drop, 1 merge, 2 turn the slice, 3 come back.
    static constexpr int kTutorialSteps = 4;
    bool tutorialActive = false;
    int tutorialStep = 0;
    bool tutorialSeen = true;          // interactive runs load this from disk
    float tutorialTime = 0.0f;         // seconds in this step
    int tutorialDrops = 0;             // drops in this step
    int tutorialPoints = 0;
    bool tutorialTurned = false;
    bool FourDUnlocked() const { return !tutorialActive || tutorialStep >= 2; }
    // Photo and replay mode (P): the game freezes and shows the last 10 s of the
    // world, scrubbable, the slice on the left and the 4D eye on the right, with
    // the camera still free to turn in 4D. F12 saves the view as a PNG.
    struct Snapshot { std::vector<FruitInstance> fruit; std::vector<BurstInstance> bursts; };
    static constexpr size_t kHistory = 600;
    std::deque<Snapshot> history;
    bool photo = false;
    int replayIndex = 0;
    bool replayPlaying = false;
    float scrubHeld = 0.0f;
    int photoAutoAfter = -1;           // --photo: enter photo mode once this many frames are recorded
    std::function<bool(const std::string&)> photoSaver; // set by main: writes the frame to a PNG
    bool photoSaveRequested = false;
    std::string photoToast;
    float photoToastTime = 0.0f;
    bool logEvents = false;            // --log-events: print merges and danger (to pick reel moments)
    bool hideUI = false;               // --no-ui: capture the scene only (no HUD, labels or overlay)
    bool effectsOn = true;             // Settings > Effects: bursts, combo, danger
    std::vector<BurstInstance> bursts;
    int combo = 0;                     // merges in a row, each within kComboWindow of the last
    float comboTimer = 0.0f, comboPop = 0.0f;
    float danger = 0.0f;
    float dangerOverride = -1.0f;      // --danger v: hold the level for review stills
    static constexpr float kComboWindow = 0.7f, kBurstSeconds = 1.0f, kRimY = 0.1f;
    bool showDepthStrip = true;        // V: bottom strip of depth along the slice normal

    // Fruit sheet (headless --fruit-sheet): fixed fruit instead of physics, labelled.
    bool sheetMode = false;
    std::vector<FruitInstance> sheetFruit;
    std::string sheetTitle;
    float sheetTitleScale = 0.9f;
    bool sheetNames = true;
    std::string caption;              // headless comparisons: centred at the top of the game view           // name under each fruit (off for the turntable)

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
        bursts.clear(); combo = 0; comboTimer = comboPop = danger = 0.0f;
        wheelTurn = dropW = dropWShown = 0.0f;
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
        const float unlocked = FourDUnlocked() ? 1.0f : 0.0f; // the 3D start of the tutorial: no turns into w
        for (int i = 0; i < kTurnCount; ++i) {
            const TurnBinding& b = kTurnBindings[i];
            float want = unlocked * turnSpeed * (float(keyDown(b.keyPlus)) - float(keyDown(b.keyMinus)));
            anyInput |= want != 0.0f;
            turnVel[i] += (want - turnVel[i]) * ease;
            if (std::abs(turnVel[i]) > 1e-5f) state.cam.turnLocal(b.a, b.b, turnVel[i] * dt);
        }
        // Mouse turning into 4D: arcball drag (applied at once) and the wheel (eased).
        if (state.pendingTurnX != 0.0f || state.pendingTurnY != 0.0f) {
            if (unlocked > 0.0f) {
                state.cam.turnLocal(AX_X, AX_W, state.pendingTurnX);
                state.cam.turnLocal(AX_Y, AX_W, state.pendingTurnY);
                anyInput = true;
            }
            state.pendingTurnX = state.pendingTurnY = 0.0f;
        }
        if (wheelTurn != 0.0f) {
            float step = wheelTurn * ease;
            if (std::abs(wheelTurn) < 1e-4f) step = wheelTurn;
            state.cam.turnLocal(AX_X, AX_W, step);
            wheelTurn -= step;
            anyInput = true;
        }
        {   // the drop's w follows its target smoothly, and rejoins the slice outside 4D eye
            const float target = renderer.viewMode == ViewMode::Eye && unlocked > 0.0f ? dropW : 0.0f;
            dropWShown += (target - dropWShown) * ease;
        }
        float wantOffset = unlocked * 1.5f * (float(keyDown(GLFW_KEY_W)) - float(keyDown(GLFW_KEY_S)));
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

    // Wheel by view mode. Before 4D is unlocked (3D start of the tutorial) it just zooms.
    void OnScroll(double dy, bool shift, bool ctrl) {
        const float d = float(dy);
        if (shift || !FourDUnlocked()) { state.cam.zoom(d); return; }
        switch (renderer.viewMode) {
            case ViewMode::Eye:
                dropW = glm::clamp(dropW + d * kDropWStep, -kDropWMax, kDropWMax);
                break;
            case ViewMode::Slice:
                wheelTurn += d * kWheelStep;
                break;
        }
    }

    void ProcessInput(float dt){
        if (State == GAME_ACTIVE) UpdateCamera(dt);
        else { state.pendingYaw = state.pendingPitch = 0.0f; state.pendingTurnX = state.pendingTurnY = 0.0f; }

        if (keyPressedOnce(GLFW_KEY_G)) {
            renderer.setQuality(Quality((int(renderer.quality()) + 1) % 4));
            ApplyRenderScale();
        }
        if (keyPressedOnce(GLFW_KEY_F3)) showPerf = !showPerf;
        if (keyPressedOnce(GLFW_KEY_V)) showDepthStrip = !showDepthStrip;
        if (keyPressedOnce(GLFW_KEY_L) && FourDUnlocked()) laneSnap = !laneSnap;
        if (keyPressedOnce(GLFW_KEY_F1) && tutorialActive) EndTutorial();
        if (keyPressedOnce(GLFW_KEY_M)) CycleViewMode(+1);

        if (keyPressedOnce(GLFW_KEY_P) && State == GAME_ACTIVE && page == Page::None) { if (photo) photo = false; else EnterPhoto(); }
        if (keyPressedOnce(GLFW_KEY_ESCAPE)) {
            if (photo) photo = false;
            else if (page != Page::None) ClosePage();
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
                if (photo) UpdatePhoto(dt);
                else {
                    UpdateGameActive(dt);
                    RecordSnapshot();
                    if (photoAutoAfter >= 0 && int(history.size()) >= photoAutoAfter) { photoAutoAfter = -1; EnterPhoto(); replayPlaying = true; replayIndex = 0; }
                }
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
                if (logEvents && State == GAME_ACTIVE) fprintf(stderr, "EV t=%.2f gameover\n", time);
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
        if (State != GAME_ACTIVE || photo) return;
        physics_solver->update(dt);
        total_points = physics_solver->total_points;
        UpdateEffects(dt);
        UpdateTutorial(dt);
    }
    // ---------------------------------------------------------------- photo and replay

    void RecordSnapshot() {
        history.push_back({CurrentFruit(), bursts});
        if (history.size() > kHistory) history.pop_front();
    }

    void EnterPhoto() {
        if (history.empty() || State != GAME_ACTIVE) return;
        photo = true;
        replayPlaying = false;
        replayIndex = int(history.size()) - 1;
    }

    void UpdatePhoto(float dt) {
        photoToastTime = std::max(0.0f, photoToastTime - dt);
        const int last = int(history.size()) - 1;
        if (keyPressedOnce(GLFW_KEY_ENTER)) replayPlaying = !replayPlaying;
        int dir = int(keyDown(GLFW_KEY_PERIOD)) - int(keyDown(GLFW_KEY_COMMA));
        if (dir != 0) {   // a tap steps one frame; holding speeds up
            scrubHeld += dt;
            replayPlaying = false;
            bool first = scrubHeld - dt <= 0.0f;
            int step = first ? 1 : (scrubHeld > 1.0f ? 6 : scrubHeld > 0.35f ? 2 : 0);
            replayIndex = std::clamp(replayIndex + dir * step, 0, last);
        } else scrubHeld = 0.0f;
        if (keyPressedOnce(GLFW_KEY_HOME)) replayIndex = 0;
        if (keyPressedOnce(GLFW_KEY_END)) replayIndex = last;
        if (keyPressedOnce(GLFW_KEY_F12)) photoSaveRequested = true;
        if (replayPlaying) replayIndex = replayIndex >= last ? 0 : replayIndex + 1;
    }

    static std::string PhotoPath() {
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        std::string dir = home ? std::string(home) : std::string(".");
        if (std::ifstream((dir + "/Pictures/.").c_str()).good()) dir += "/Pictures";
        char stamp[32];
        std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
        return dir + "/4dsuika-" + stamp + ".png";
    }

    // The slice (left) and the 4D eye (right) of the same replayed moment, from the same camera.
    void RenderPhoto() {
        const Snapshot& s = history[std::clamp(replayIndex, 0, int(history.size()) - 1)];
        renderer.setFruit(s.fruit);
        renderer.cues4D = FourDUnlocked();
        renderer.setEffects(s.bursts, 0.0f);
        renderer.setPreview(false, glm::vec4(0.0f), 0.0f, 0);
        const ViewMode saved = renderer.viewMode;
        const int half = FbWidth / 2;
        renderer.viewportX = 0;
        renderer.viewMode = ViewMode::Slice;
        renderer.draw(state.cam, half, FbHeight, time);
        renderer.viewportX = half;
        renderer.viewMode = ViewMode::Eye;
        renderer.draw(state.cam, half, FbHeight, time);
        renderer.viewportX = 0;
        renderer.viewMode = saved;
        glViewport(0, 0, FbWidth, FbHeight);
        if (photoSaveRequested) { // a clean frame, no panels
            photoSaveRequested = false;
            std::string path = PhotoPath();
            bool ok = photoSaver && photoSaver(path);
            photoToast = ok ? "saved " + path : "could not save the photo";
            photoToastTime = 4.0f;
            return;
        }
        RenderPhotoUI();
    }

    void RenderPhotoUI() {
        BeginText();
        const float mid = Width * 0.5f;
        FillRect(mid - 1.0f, 0.0f, 2.0f, float(Height), ui::kGold, 0.7f);
        auto tag = [&](const char* t, float x) {
            float w = t_rend->GetTextWidth(t, 0.6f) + 20.0f;
            FillRect(x, 14.0f, w, 30.0f, ui::kPanel, 0.88f);
            t_rend->RenderText(t, x + 10.0f, 21.0f, 0.6f, ui::kGold);
        };
        tag("SLICE", 14.0f);
        tag("4D EYE", mid + 14.0f);
        const std::string title = "PHOTO MODE";
        t_rend->RenderText(title, mid - t_rend->GetTextWidth(title, 0.7f) * 0.5f, 52.0f, 0.7f, ui::kCream);
        // Timeline over the last seconds, oldest on the left.
        const float bw = std::min(Width * 0.6f, 900.0f), bx = (Width - bw) * 0.5f, by = Height - 74.0f;
        FillRect(bx - 14.0f, by - 14.0f, bw + 28.0f, 64.0f, ui::kPanel, 0.9f);
        const int n = int(history.size());
        const float frac = n > 1 ? float(replayIndex) / float(n - 1) : 1.0f;
        FillRect(bx, by + 6.0f, bw, 4.0f, ui::kGold, 0.35f);
        FillRect(bx, by + 6.0f, bw * frac, 4.0f, ui::kGold, 0.95f);
        FillCircle({bx + bw * frac, by + 8.0f}, 7.0f, ui::kCream, 1.0f, 18);
        char buf[96];
        snprintf(buf, sizeof buf, "%.1f s ago", float(n - 1 - replayIndex) / 60.0f);
        t_rend->RenderText(buf, bx, by + 18.0f, 0.5f, ui::kCream);
        const std::string hint = "turn the slice as usual     , .  scrub     Enter  play     F12  save photo     P  back";
        t_rend->RenderText(hint, mid - t_rend->GetTextWidth(hint, 0.45f) * 0.5f, by + 32.0f, 0.45f, glm::vec3(0.8f, 0.75f, 0.7f));
        if (photoToastTime > 0.0f)
            t_rend->RenderText(photoToast, mid - t_rend->GetTextWidth(photoToast, 0.55f) * 0.5f, by - 44.0f, 0.55f, ui::kGold);
    }

    // ---------------------------------------------------------------- tutorial

    static std::string TutorialFile() {
#ifdef __EMSCRIPTEN__
        return "";
#else
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        return home ? std::string(home) + "/.4dsuika-tutorial-done" : "";
#endif
    }

    // Interactive runs only: the tutorial opens the first time the player presses Play.
    void LoadTutorialState() {
        std::string f = TutorialFile();
        std::ifstream in(f);
        tutorialSeen = f.empty() ? false : in.good();
    }

    void StartTutorial(int step) {
        tutorialActive = true;
        tutorialStep = step;
        tutorialTime = 0.0f;
        tutorialDrops = 0;
        tutorialPoints = total_points;
        tutorialTurned = false;
        if (step >= 2) { showDepthStrip = true; }
    }

    void EndTutorial() {
        tutorialActive = false;
        tutorialSeen = true;
        std::string f = TutorialFile();
        if (!f.empty()) std::ofstream(f) << "1\n";
    }

    void NextTutorialStep() {
        ++tutorialStep;
        tutorialTime = 0.0f;
        tutorialDrops = 0;
        tutorialPoints = total_points;
        tutorialTurned = false;
        if (tutorialStep >= kTutorialSteps) EndTutorial();
    }

    void UpdateTutorial(float dt) {
        if (!tutorialActive) return;
        tutorialTime += dt;
        float tilt = glm::degrees(state.cam.tilt());
        switch (tutorialStep) {
            case 0: if (tutorialDrops >= 1) NextTutorialStep(); break;
            case 1: if (total_points > tutorialPoints || tutorialDrops >= 6) NextTutorialStep(); break;
            case 2: if (tilt > 20.0f) NextTutorialStep(); break;
            case 3:
                if (tilt > 20.0f) tutorialTurned = true;
                if ((tutorialTurned && tilt < 5.0f) || tutorialTime > 20.0f) NextTutorialStep();
                break;
        }
    }

    void RenderTutorial() {
        if (!tutorialActive || sheetMode) return;
        static const char* titles[] = {"AIM AND DROP", "MERGE", "THE BOWL IS 4D", "FRUIT HIDE IN W"};
        static const char* lines[][2] = {
            {"Move the mouse over the bowl and click to drop a fruit.", "Right-drag turns the camera around the bowl."},
            {"Two of the same fruit merge into the next one.", "Drop a match next to one already in the bowl."},
            {"Everything here is a slice of a 4D world.", "Scroll the wheel, middle-drag, or hold A or D to turn the slice into 4D."},
            {"Fruit off the slice show as dashed rings; the bar below places every fruit in w.", "Press H or Space to come back to the default slice."},
        };
        const int s = std::min(tutorialStep, kTutorialSteps - 1);
        const float w = std::clamp(Width * 0.5f, 340.0f, 780.0f), h = 92.0f;
        const float bottom = (showDepthStrip && FourDUnlocked()) ? Height - DepthStripHeight() - 28.0f : Height - 24.0f;
        const float x = (Width - w) * 0.5f, y = bottom - h;
        FillRect(x, y, w, h, ui::kPanel, 0.92f);
        FillRect(x, y, 4.0f, h, ui::kGold, 0.95f);
        t_rend->RenderText(titles[s], x + 18.0f, y + 9.0f, 0.62f, ui::kGold);
        for (int i = 0; i < 2; ++i)
            t_rend->RenderText(lines[s][i], x + 18.0f, y + 33.0f + 20.0f * i, fitLabelScale(lines[s][i], w - 36.0f, 0.55f), ui::kCream);
        for (int i = 0; i < kTutorialSteps; ++i)
            FillCircle({x + w - 18.0f - 14.0f * (kTutorialSteps - 1 - i), y + 16.0f}, i == s ? 4.5f : 3.0f, ui::kGold, i <= s ? 0.95f : 0.3f, 12);
        t_rend->RenderText("F1 skip", x + w - 58.0f, y + h - 18.0f, 0.4f, glm::vec3(0.7f, 0.65f, 0.6f));
    }

    // Merge events become bursts; merges in quick succession count as a combo
    // (cosmetic: scoring is unchanged). Danger rises when settled fruit pile up
    // to the rim.
    void UpdateEffects(float dt) {
        if (!effectsOn) { physics_solver->takeMerges(); bursts.clear(); combo = 0; danger = 0.0f; return; }
        for (const auto& m : physics_solver->takeMerges()) {
            combo = comboTimer > 0.0f ? combo + 1 : 1;
            if (logEvents) fprintf(stderr, "EV t=%.2f merge fruit=%d combo=%d\n", time, int(m.fruit), combo);
            comboTimer = kComboWindow;
            comboPop = 1.0f;
            bursts.push_back({m.position, m.radius, m.fruit, 0.0f, std::min(1.0f + 0.4f * float(combo - 1), 2.6f)});
            if (bursts.size() > RaymarchRenderer::kMaxBursts) bursts.erase(bursts.begin());
        }
        for (auto& b : bursts) b.progress += dt / kBurstSeconds;
        bursts.erase(std::remove_if(bursts.begin(), bursts.end(), [](const BurstInstance& b) { return b.progress >= 1.0f; }),
                     bursts.end());
        comboTimer = std::max(0.0f, comboTimer - dt);
        if (comboTimer == 0.0f) combo = 0;
        comboPop = std::max(0.0f, comboPop - dt * 3.0f);

        float target = 0.0f;
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const auto& obj = physics_solver->objects[i];
            if (obj.flash > 0.02f) continue; // a merge zeroes the velocity: wait for it to settle
            if (glm::length(obj.getVelocity()) > 1.2f * dt) continue; // still falling or rolling (1.2 units/s)
            float y = obj.position.y; // the centre: big fruit have their top near the rim anyway
            if (y > kRimY + 0.2f) continue; // a fresh drop above the bowl, not the pile
            target = std::max(target, glm::clamp((y - (kRimY - 1.3f)) / 0.9f, 0.0f, 1.0f));
        }
        danger += (target - danger) * std::min(1.0f, dt * (target > danger ? 1.2f : 1.5f));
        if (dangerOverride >= 0.0f) danger = dangerOverride;
        if (logEvents && danger > 0.15f && int(time * 4) != int((time - dt) * 4)) fprintf(stderr, "EV t=%.2f danger=%.2f\n", time, danger);
    }

    int Sound(){
#ifndef __EMSCRIPTEN__
        if (!audioReady || audioMuted) return 0;
        if (physics_solver->just_merged > 0 && mergeSound != nullptr) Mix_PlayChannel(-1, mergeSound, 0);
        if (ballPlaced && placeSound != nullptr) Mix_PlayChannel(-1, placeSound, 0);
#endif
        return 0;
    }

    // Above what the cursor points at; with lane snap, moved along world w to
    // the nearest lane (every kLaneSpacing from the bowl's centre), or the
    // next lane towards the centre if that one would fall outside the bowl,
    // so a 4D aim is a choice of lane.
    glm::vec4 DropCenter(const Hit4& hit) const {
        glm::vec4 c = hit.point + glm::vec4(0.0f, 3.0f, 0.0f, 0.0f);
        const float reach = (boundary.radius - boundary.margin) * std::sin(boundary.cutoffAngle) - 0.3f;
        c.w += dropWShown;
        if (!laneSnap) {   // keep a shifted drop over the bowl
            if (dropWShown == 0.0f) return c;
            const float rr = std::hypot(c.x - boundary.center.x, c.z - boundary.center.z);
            const float room = std::sqrt(std::max(0.0f, reach * reach - rr * rr));
            c.w = boundary.center.w + glm::clamp(c.w - boundary.center.w, -room, room);
            return c;
        }
        float w = c.w - boundary.center.w;
        float lane = std::round(w / kLaneSpacing);
        for (; ; lane -= (lane > 0.0f) - (lane < 0.0f)) {
            glm::vec3 radial(c.x - boundary.center.x, c.z - boundary.center.z, lane * kLaneSpacing);
            if (glm::length(radial) <= reach || lane == 0.0f) break;
        }
        c.w = boundary.center.w + lane * kLaneSpacing;
        return c;
    }

    // The next drop's orientation is chosen up front, so the ghost in the air and
    // where it lands show the exact silhouette the fruit will have (a falling
    // fruit does not spin until it touches something).
    const Rot4& NextOrientation() {
        if (!nextOrientValid || nextOrientFor != nextFruit) {
            PhysicsObject tmp(glm::vec4(0.0f), nextFruit, true, true);
            nextOrient = tmp.orientation;
            nextOrientFor = nextFruit;
            nextOrientValid = true;
        }
        return nextOrient;
    }

    // Drop the next fruit above whatever the cursor points at.
    bool DropFruit() {
        if (photo) return false;
        Hit4 hit = getPlacementMouse(state, boundary, *physics_solver);
        if (hit.hit) {
            PhysicsObject dropped(DropCenter(hit), nextFruit, true, false);
            dropped.orientation = NextOrientation();
            physics_solver->addObject(dropped);
            nextOrientValid = false;
            nextFruit = FruitManager::getRandomFruit();
            ++tutorialDrops;
            ballPlaced = true;
            return true;
        }
        ballPlaced = false;
        return false;
    }

    // ------------------------------------------------------------ rendering

    std::vector<FruitInstance> CurrentFruit() const {
        std::vector<FruitInstance> fruit;
        for (int i = 0; i < MAX_OBJECTS; i++) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const PhysicsObject& obj = physics_solver->objects[i];
            fruit.push_back({obj.position, obj.radius, int(obj.fruit), obj.flash, i, obj.worldToLocalFrame()});
        }
        return fruit;
    }

    void RenderScene(const Hit4& aim, const Camera4D& cam) {
        std::vector<FruitInstance> fruit = CurrentFruit();
        renderer.setFruit(sheetMode ? sheetFruit : fruit);
        renderer.cues4D = FourDUnlocked();
        renderer.setEffects(sheetMode ? std::vector<BurstInstance>{} : bursts, sheetMode ? 0.0f : danger);
        {
            const float radius = FruitManager::getFruitProperties(nextFruit).radius;
            const glm::vec4 spawn = DropCenter(aim);
            glm::vec4 land;
            const bool landed = aim.hit && !sheetMode && LandingPoint(spawn, radius, land);
            renderer.setPreview(aim.hit, spawn, radius, int(nextFruit),
                                glm::transpose(NextOrientation().matrix()), landed, land);
        }
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
        float s = Width < 650 ? 0.36f : 0.50f;
        float size = std::max(28.0f, FbHeight * 0.045f) * float(Height) / float(std::max(FbHeight, 1));
        float clearance = showDepthStrip ? DepthStripHeight() + (homing ? 42.0f : 26.0f) : 0.0f;
        float y = Height - std::max(size * 2.5f + 8.0f, clearance);
        t_rend->RenderText(buf, 12.0f, y, s, glm::vec3(0.95f, 0.9f, 0.85f));
        if (homing) t_rend->RenderText("returning to default slice", 12.0f, y + 15.0f, s, glm::vec3(1.0f, 0.8f, 0.3f));
        // Always shown, so the player knows which view they are in and which key changes it.
        t_rend->RenderText("view: " + ViewLabel() + "   (M changes)", 12.0f, y - 18.0f, s, glm::vec3(0.75f, 0.9f, 1.0f));
    }

    static glm::vec3 FruitUiColor(int type) {
        static const glm::vec3 colors[] = {
            {0.82f, 0.08f, 0.12f}, {0.95f, 0.15f, 0.20f}, {0.38f, 0.18f, 0.78f}, {0.95f, 0.48f, 0.05f},
            {0.90f, 0.30f, 0.08f}, {0.86f, 0.18f, 0.08f}, {0.70f, 0.78f, 0.20f}, {0.95f, 0.42f, 0.42f},
            {0.95f, 0.68f, 0.12f}, {0.50f, 0.70f, 0.28f}, {0.16f, 0.48f, 0.16f},
        };
        return colors[std::clamp(type, 0, int(sizeof(colors) / sizeof(colors[0])) - 1)];
    }

    void FillRect(float x, float y, float w, float h, glm::vec3 color, float alpha) {
        t_rend->FillPoly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, color, alpha);
    }

    void DrawLine(glm::vec2 a, glm::vec2 b, float thickness, glm::vec3 color, float alpha) {
        glm::vec2 d = b - a;
        float len = glm::length(d);
        if (len < 1e-4f) return;
        glm::vec2 n(-d.y / len, d.x / len);
        t_rend->FillPoly({a + n * thickness, b + n * thickness, b - n * thickness, a - n * thickness}, color, alpha);
    }

    void FillCircle(glm::vec2 c, float r, glm::vec3 color, float alpha, int steps = 14) {
        std::vector<glm::vec2> pts;
        pts.reserve(steps);
        for (int i = 0; i < steps; ++i) {
            float a = glm::two_pi<float>() * float(i) / float(steps);
            pts.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
        }
        t_rend->FillPoly(pts, color, alpha);
    }

    void DrawCircle(glm::vec2 c, float r, float thickness, glm::vec3 color, float alpha, int steps = 32) {
        glm::vec2 prev = c + glm::vec2(r, 0.0f);
        for (int i = 1; i <= steps; ++i) {
            float a = glm::two_pi<float>() * float(i) / float(steps);
            glm::vec2 next = c + glm::vec2(std::cos(a), std::sin(a)) * r;
            DrawLine(prev, next, thickness, color, alpha);
            prev = next;
        }
    }

    static glm::vec3 DepthUiColor(float offset) {
        const glm::vec3 side = offset < 0.0f ? glm::vec3(1.0f, 0.50f, 0.40f)
                                             : glm::vec3(0.42f, 0.82f, 1.0f);
        return glm::mix(ui::kGold, side, glm::smoothstep(0.0f, 1.5f, std::abs(offset)));
    }

    // HUD scale: 1 up to 720 lines, then grows with the window so panels,
    // text and the w strip stay big and clean at 1080p and 4K.
    float UiScale() const { return std::max(1.0f, Height / 720.0f); }
    float DepthStripHeight() const { return std::clamp(Height * 0.125f, 54.0f, 104.0f * UiScale()); }

    void RenderDepthStrip(const Hit4& hit) {
        if (!showDepthStrip || !FourDUnlocked()) return;
        const glm::vec4 normal = state.cam.basis()[3];
        auto depth = [&](glm::vec4 p) { return glm::dot(p - boundary.center, normal); };
        const float slice = depth(state.cam.position());
        float span = std::max(boundary.radius, std::abs(slice) + 0.25f);
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const auto& obj = physics_solver->objects[i];
            span = std::max(span, std::abs(depth(obj.position)) + obj.radius);
        }
        if (hit.hit) span = std::max(span, std::abs(depth(DropCenter(hit))) + 0.25f);
        const float gizmoSpace = std::max(112.0f, Height * 0.173f);
        const float x = std::clamp(std::max(gizmoSpace, Width * 0.185f), 0.0f, Width * 0.42f);
        const float w = std::min(Width * 0.63f, Width - x - 12.0f);
        const float h = DepthStripHeight();
        const float y = Height - h - 12.0f;
        const float u = UiScale();
        const float pad = std::min(48.0f * u, w * 0.12f);
        auto screenX = [&](float d) { return x + w * 0.5f + d / span * (w * 0.5f - pad); };

        FillRect(x, y, w, h, ui::kPanel, 0.91f);
        const float rail = y + h - 12.0f * u;
        for (int i = 0; i < 32; ++i) {
            float left = glm::mix(-span, span, i / 32.0f);
            float right = glm::mix(-span, span, (i + 1) / 32.0f);
            DrawLine({screenX(left), rail}, {screenX(right), rail}, 1.6f * u,
                     DepthUiColor((left + right) * 0.5f - slice), 0.9f);
        }
        for (int i = -3; i <= 3; ++i) {
            float tick = screenX(span * i / 3.0f);
            DrawLine({tick, rail - 4.0f * u}, {tick, rail + 4.0f * u}, 0.6f * u, ui::kCream, 0.45f);
        }
        if (laneSnap) { // the world-w lanes, as they lie along the slice normal
            for (int k = -int(span / kLaneSpacing) - 1; k <= int(span / kLaneSpacing) + 1; ++k) {
                float lane = k * kLaneSpacing * normal.w;
                if (std::abs(lane) > span) continue;
                DrawLine({screenX(lane), y + 6.0f}, {screenX(lane), rail}, 0.7f, ui::kGold, 0.35f);
            }
        }
        const float marker = screenX(slice);
        DrawLine({marker, y + 4.0f}, {marker, y + h - 5.0f}, 1.8f * u, ui::kCream, 0.95f);
        t_rend->RenderText("-w", x + 7.0f * u, y + h * 0.40f, 0.55f * u, DepthUiColor(-span - slice));
        t_rend->RenderText("+w", x + w - t_rend->GetTextWidth("+w", 0.55f * u) - 7.0f * u,
                           y + h * 0.40f, 0.55f * u, DepthUiColor(span - slice));

        for (int i = 0; i < MAX_OBJECTS; ++i) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const auto& obj = physics_solver->objects[i];
            float d = depth(obj.position);
            float level = glm::clamp((obj.position.y - boundary.center.y + boundary.radius) /
                                     (boundary.radius + 1.0f), 0.0f, 1.0f);
            glm::vec2 c(screenX(d), glm::mix(rail - 10.0f * u, y + 10.0f * u, level));
            float r = std::clamp(obj.radius * h * 0.22f, 3.0f * u, h * 0.11f);
            float opacity = std::abs(d - slice) < obj.radius ? 1.0f : 0.55f;
            glm::vec3 fruit = FruitUiColor(int(obj.fruit));
            DrawLine({screenX(d - obj.radius), c.y}, {screenX(d + obj.radius), c.y}, 1.4f * u, fruit, 0.45f);
            FillCircle(c, r, fruit, opacity);
            DrawCircle(c, r, 1.2f * u, DepthUiColor(d - slice), opacity, 14);
        }
        if (hit.hit) {
            const float cursor = screenX(depth(DropCenter(hit)));
            t_rend->FillPoly({{cursor - 6.0f * u, y}, {cursor + 6.0f * u, y}, {cursor, y + 9.0f * u}},
                             FruitUiColor(int(nextFruit)), 1.0f);
        }
    }

    // First contact along world gravity with the current sphere colliders.
    // The pile may move before the dropped fruit reaches it.
    // Where a fruit of `radius` dropped from `spawn` comes to rest first (the bowl,
    // the rim or another fruit). False if nothing is below it.
    bool LandingPoint(const glm::vec4& spawn, float radius, glm::vec4& land) const {
        const glm::vec4 down(0.0f, -1.0f, 0.0f, 0.0f);
        float t0, t1;
        const float inner = boundary.radius - boundary.margin;
        glomeRoots(spawn, down, boundary.center, inner - radius, t0, t1);
        float travel = t1 > 0.0f ? t1 : std::numeric_limits<float>::infinity();
        // Near the edge the falling sphere can touch the rim above the cavity.
        glm::vec4 radial = spawn - boundary.center;
        radial.y = 0.0f;
        if (glm::length(radial) > 1e-4f) {
            glm::vec4 rim = boundary.center + glm::normalize(radial) * (inner * std::sin(boundary.cutoffAngle));
            rim.y -= inner * std::cos(boundary.cutoffAngle);
            glomeRoots(spawn, down, rim, radius, t0, t1);
            if (t1 > 0.0f) travel = std::min(travel, std::max(t0, 0.0f));
        }
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            if (!physics_solver->has_obj[i] || physics_solver->objects[i].hidden) continue;
            const auto& obj = physics_solver->objects[i];
            glomeRoots(spawn, down, obj.position, obj.radius + radius, t0, t1);
            if (t1 > 0.0f) travel = std::min(travel, std::max(t0, 0.0f));
        }
        if (!std::isfinite(travel)) return false;
        land = spawn + down * travel;
        return true;
    }

    void RenderDropGuide(const Hit4& hit) {
        if (!hit.hit || renderer.viewMode == ViewMode::Eye) return;
        const glm::vec4 spawn = DropCenter(hit);
        const float radius = FruitManager::getFruitProperties(nextFruit).radius;
        glm::vec4 land;
        if (!LandingPoint(spawn, radius, land)) return;
        auto project = [&](glm::vec4 p, glm::vec2& screen) {
            glm::vec2 ndc;
            if (!state.cam.project(p, float(Width) / std::max(Height, 1u), ndc)) return false;
            screen = (ndc * glm::vec2(0.5f, -0.5f) + 0.5f) * glm::vec2(Width, Height);
            return true;
        };
        glm::vec2 a, b, spawnEdge, landEdge;
        const glm::vec4 side = state.cam.basis()[0] * radius;
        if (!project(spawn, a) || !project(land, b) ||
            !project(spawn + side, spawnEdge) || !project(land + side, landEdge)) return;
        float r = glm::clamp(glm::length(landEdge - b), 3.0f, Height * 0.3f);
        glm::vec2 heading = b - a;
        float len = glm::length(heading);
        float stride = std::max(12.0f, len / 64.0f);
        for (float d = glm::length(spawnEdge - a); d + r < len; d += stride) {
            DrawLine(a + heading * (d / len), a + heading * (std::min(d + stride * 0.55f, len - r) / len),
                     0.9f, ui::kCream, 0.85f);
        }
        // The landing spot itself is drawn in the scene as the fruit's own silhouette (setPreview).
    }

    float fitLabelScale(const std::string& text, float width, float scale) {
        float tw = t_rend->GetTextWidth(text, scale);
        return tw > width && tw > 0.0f ? scale * width / tw : scale;
    }

    void RenderSheetLabels() {
        BeginText();
        t_rend->RenderText(sheetTitle, 25.0f, 25.0f, sheetTitleScale, glm::vec3(1.0f));
        if (!sheetNames) return;
        float aspect = float(Width) / float(std::max(Height, 1u));
        for (const FruitInstance& f : sheetFruit) {
            glm::vec2 ndc;
            if (!state.cam.project(f.center - glm::vec4(0, f.radius * 1.15f, 0, 0), aspect, ndc)) continue;
            float x = (ndc.x * 0.5f + 0.5f) * Width, y = (0.5f - ndc.y * 0.5f) * Height + 8.0f;
            std::string name = kFruitNames[f.type];
            t_rend->RenderText(name, x - t_rend->GetTextWidth(name, 0.6f) / 2.0f, y, 0.6f, glm::vec3(0.95f));
        }
    }

    // Combo counter near the top, popping on each merge; a red edge pulse when
    // the pile nears the rim (the rim itself pulses in the scene).
    void RenderEffectsUI() {
        if (danger > 0.02f) {
            float pulse = 0.5f + 0.5f * std::sin(time * 7.0f);
            float a = danger * (0.10f + 0.22f * pulse);
            const glm::vec3 red(1.0f, 0.12f, 0.06f);
            float e = std::max(10.0f, Height * 0.012f) * (1.0f + 2.0f * danger);
            FillRect(0.0f, 0.0f, float(Width), e, red, a);
            FillRect(0.0f, Height - e, float(Width), e, red, a);
            FillRect(0.0f, e, e, Height - 2.0f * e, red, a);
            FillRect(Width - e, e, e, Height - 2.0f * e, red, a);
            if (danger > 0.5f) {
                const std::string warn = "FULL BOWL";
                float sc = 0.9f;
                t_rend->RenderText(warn, (Width - t_rend->GetTextWidth(warn, sc)) * 0.5f, Height * 0.065f, sc,
                                   glm::mix(glm::vec3(1.0f, 0.55f, 0.4f), glm::vec3(1.0f, 0.2f, 0.12f), pulse));
            }
        }
        if (combo >= 2 && comboTimer > 0.0f) {
            std::string text = "COMBO x" + std::to_string(combo);
            float sc = (0.9f + 0.25f * std::min(combo, 6)) * (1.0f + 0.35f * comboPop);
            float fade = std::min(1.0f, comboTimer / 0.5f);
            glm::vec3 col = glm::mix(ui::kGold, glm::vec3(1.0f, 0.95f, 0.8f), comboPop) * fade;
            t_rend->RenderText(text, Width * 0.02f, 14.0f * UiScale() + std::clamp(Height * 0.12f, 56.0f, 104.0f * UiScale()) + 22.0f * UiScale(), sc * std::min(UiScale(), 1.3f), col); // left, under the score: centre is where the drop preview hangs
        }
    }

    void RenderGameUI(const Hit4& aim)
    {
        if (sheetMode) { RenderSheetLabels(); return; }
        BeginText();
        RenderDropGuide(aim);
        const float u = UiScale();
        const float panelWidth = std::clamp(Width * 0.20f, 100.0f, 185.0f * u);
        const float panelHeight = std::clamp(Height * 0.12f, 56.0f, 104.0f * u);
        const float margin = 14.0f * u;
        const float nextX = Width - panelWidth - margin;
        FillRect(margin, margin, panelWidth, panelHeight, ui::kPanel, 0.88f);
        FillRect(nextX, margin, panelWidth, panelHeight, ui::kPanel, 0.88f);
        t_rend->RenderText("SCORE", margin + 10.0f * u, margin + 7.0f * u, 0.50f * u, ui::kGold);
        std::string score = std::to_string(int(physics_solver->total_points));
        t_rend->RenderText(score, margin + 10.0f * u, margin + panelHeight * 0.34f,
                           fitLabelScale(score, panelWidth - 20.0f * u, panelHeight / 49.0f), ui::kCream);
        t_rend->RenderText("NEXT DROP", nextX + 10.0f * u, margin + 7.0f * u, 0.50f * u, ui::kGold);
        {   // The fruit at its true size against the whole ladder, so a merge is easy to plan.
            std::string next = kFruitNames[int(nextFruit)];
            const float rMin = FruitManager::getFruitProperties(CHERRY).radius;
            const float rMax = FruitManager::getFruitProperties(WATERMELON).radius;
            float t = (FruitManager::getFruitProperties(nextFruit).radius - rMin) / std::max(rMax - rMin, 1e-3f);
            float r = glm::mix(6.0f * u, panelHeight * 0.25f, t);
            float cy = margin + panelHeight * 0.50f;
            FillCircle({nextX + 12.0f * u + panelHeight * 0.25f, cy}, r, FruitUiColor(int(nextFruit)), 1.0f, 24);
            float tx = nextX + 22.0f * u + panelHeight * 0.5f;
            t_rend->RenderText(next, tx, cy - 9.0f * u, fitLabelScale(next, nextX + panelWidth - tx - 8.0f * u, 0.75f * u), ui::kCream);
            const float ly = margin + panelHeight - 10.0f * u;
            const float step = (panelWidth - 28.0f * u) / 10.0f;
            for (int k = 0; k <= int(WATERMELON); ++k) {
                glm::vec2 c(nextX + 14.0f * u + step * k, ly);
                bool now = k == int(nextFruit);
                FillCircle(c, (now ? 4.5f : 2.6f) * u, FruitUiColor(k), now ? 1.0f : 0.55f, 12);
                if (now) DrawCircle(c, 6.5f * u, 1.0f * u, ui::kCream, 0.9f, 16);
            }
        }
        if (laneSnap) t_rend->RenderText("W-LANE SNAP  (L)", nextX + 10.0f * u, margin + panelHeight + 8.0f * u, 0.5f * u, ui::kGold);
        RenderEffectsUI();
        if (FourDUnlocked()) RenderSliceLabel();
        RenderDepthStrip(aim);
        RenderTutorial();
        if (!caption.empty())
            t_rend->RenderText(caption, (Width - t_rend->GetTextWidth(caption, 0.9f)) * 0.5f, 25.0f, 0.9f, glm::vec3(1.0f, 0.92f, 0.75f));
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
        if (photo) { RenderPhoto(); return; }
        Hit4 aim;
        if (!sheetMode) aim = getPlacementMouse(state, boundary, *physics_solver);
        RenderScene(aim, state.cam);
        if (hideUI) return;
        RenderGameUI(aim);
        RenderInputOverlay();
    }

    // Reels recorded with a scripted mouse: the cursor (filled while a button is
    // held) and a caption naming the input. Off in normal play.
    void RenderInputOverlay() {
        BeginText();
        if (showCursor) {
            const glm::vec2 c(state.m_xpos, state.m_ypos);
            const float u = std::clamp(Height / 540.0f, 1.0f, 2.2f);
            auto tri = [&](glm::vec2 o, float k, glm::vec3 col) {
                t_rend->FillPoly({o, o + glm::vec2(0.0f, 18.0f) * u * k, o + glm::vec2(12.5f, 12.5f) * u * k}, col, 1.0f);
            };
            DrawLine(c + glm::vec2(5.5f, 12.0f) * u, c + glm::vec2(10.5f, 19.5f) * u, 3.2f * u, glm::vec3(0.06f, 0.05f, 0.05f), 1.0f);
            tri(c + glm::vec2(-1.5f, -2.0f) * u, 1.3f, glm::vec3(0.06f, 0.05f, 0.05f));
            DrawLine(c + glm::vec2(5.5f, 12.0f) * u, c + glm::vec2(10.5f, 19.5f) * u, 1.6f * u, ui::kCream, 1.0f);
            tri(c, 1.0f, ui::kCream);
            if (heldButtons) {
                static const glm::vec3 col[3] = {glm::vec3(1.0f, 0.85f, 0.35f), glm::vec3(0.55f, 0.85f, 1.0f), glm::vec3(0.75f, 1.0f, 0.6f)};
                const int b = heldButtons & 1 ? 0 : heldButtons & 2 ? 1 : 2;
                FillCircle(c, 15.0f * u, col[b], 0.35f, 24);
            }
        }
        if (!inputCaption.empty()) {
            const float sc = std::clamp(Height / 540.0f, 1.0f, 2.0f) * 0.85f;
            const float w = t_rend->GetTextWidth(inputCaption, sc), h = 30.0f * sc + 14.0f;
            const float x = (Width - w) * 0.5f, y = Height * 0.045f;
            FillRect(x - 20.0f, y, w + 40.0f, h, ui::kPanel, 0.9f);
            FillRect(x - 20.0f, y, 4.0f, h, ui::kGold, 0.95f);
            t_rend->RenderText(inputCaption, x, y + 8.0f, sc, ui::kCream);
        }
    }

    // ---------------------------------------------------------------- menus

    static std::string percent(float v) { return std::to_string(int(v * 100.0f + 0.5f)) + "%"; }

    static void CycleIndex(int& i, int dir, int count) { i = (i + dir + count) % count; }

    // M or Settings > View switches between Slice (the game, the default) and
    // 4D eye. The choice is saved for the next run.
    static constexpr int kViewCount = 2;
    static ViewMode ViewAt(int i) { return i == 1 ? ViewMode::Eye : ViewMode::Slice; }
    int ViewIndex() const { return renderer.viewMode == ViewMode::Eye ? 1 : 0; }
    std::string ViewLabel() const { return viewModeName(renderer.viewMode); }
    void SetView(ViewMode m) { renderer.viewMode = m; }
    static std::string ViewFile() {
#ifdef __EMSCRIPTEN__
        return "";
#else
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        return home ? std::string(home) + "/.4dsuika-view" : "";
#endif
    }
    void SaveView() const {
        std::string f = ViewFile();
        if (f.empty()) return;
        std::ofstream(f) << (renderer.viewMode == ViewMode::Eye ? "eye" : "slice") << "\n";
    }
    // Interactive runs only: Slice unless the player chose 4D eye last time
    // (an old "thick" choice falls back to Slice).
    void LoadView() {
        std::ifstream in(ViewFile());
        std::string name;
        if (!(in >> name)) return;
        SetView(name == "eye" ? ViewMode::Eye : ViewMode::Slice);
    }
    void CycleViewMode(int dir) {
        SetView(ViewAt((ViewIndex() + dir + kViewCount) % kViewCount));
        SaveView();
    }

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
            MenuItem::button("Play", [this] { Reset(); if (!tutorialSeen) StartTutorial(0); }),
            MenuItem::button("Tutorial", [this] { Reset(); StartTutorial(0); }),
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
        items.push_back(MenuItem::choice("View", [this] { return ViewLabel(); },
            [this](int d) { CycleViewMode(d); }));
        items.push_back(MenuItem::slider("Facets", [this] { return renderer.facet; }, [this](float v) { renderer.facet = v; }));
        items.push_back(MenuItem::choice("Effects", [this] { return std::string(effectsOn ? "On" : "Off"); },
            [this](int) { effectsOn = !effectsOn; }));
        items.push_back(MenuItem::choice("Merge glow", [this] { return std::string(renderer.sameKindGlow ? "On" : "Off"); },
            [this](int) { renderer.sameKindGlow = !renderer.sameKindGlow; }));
        items.push_back(MenuItem::choice("W-lane snap", [this] { return std::string(laneSnap ? "On" : "Off"); },
            [this](int) { laneSnap = !laneSnap; }));
        items.push_back(MenuItem::choice("W-depth strip", [this] { return std::string(showDepthStrip ? "On" : "Off"); },
            [this](int) { showDepthStrip = !showDepthStrip; }));
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
            MenuItem::info("Right drag / arrows", "orbit the camera"),
            MenuItem::info("Middle drag", "turn into 4D (or Shift + right drag)"),
            MenuItem::info("Wheel (slice)", "turn the slice through w"),
            MenuItem::info("Wheel (4D eye)", "set the w of the drop"),
            MenuItem::info("Shift + wheel", "zoom"),
        };
        for (const TurnBinding& b : kTurnBindings) {
            std::string h = b.help;
            size_t gap = h.find("  ");
            items.push_back(MenuItem::info(h.substr(0, gap), h.substr(gap + 2)));
        }
        items.push_back(MenuItem::info("W / S", "move the slice along the 4th axis"));
        items.push_back(MenuItem::info("H / Space", "back to the default slice"));
        items.push_back(MenuItem::info("V", "toggle the bottom w strip"));
        items.push_back(MenuItem::info("L", "snap drops to whole w lanes"));
        items.push_back(MenuItem::info("P", "photo mode: replay the last 10 s, slice and 4D eye side by side"));
        items.push_back(MenuItem::info("G / F3", "cycle quality / show timing"));
        items.push_back(MenuItem::info("M", "change view: slice, 4D eye"));
        items.push_back(MenuItem::info("Esc", "pause"));
        items.push_back(MenuItem::button("Back", [this] { ClosePage(); }));
        menu.set(std::move(items), std::move(st));
        menu.focus = int(menu.itemCount()) - 1;
    }

    void RenderMenuScreen(const Camera4D& cam, float dim) {
        EnsureMenu();
        renderer.dim = dim;
        RenderScene({}, cam);
        renderer.dim = 0.0f;
        BeginText();
        menu.draw(*t_rend, float(Width), float(Height));
    }
};
