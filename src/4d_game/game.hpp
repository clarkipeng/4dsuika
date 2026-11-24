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
#include "menu.hpp"
#include "fruit.hpp"
#include "synth.hpp"

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
    Shader          *shape_shader = nullptr;

    PhysicSolver *physics_solver;
    tp::ThreadPool *thread_pool;

    Mix_Chunk* mergeSound = nullptr;
    Mix_Chunk* loseSound = nullptr;
    Mix_Chunk* placeSound = nullptr;
    Mix_Music* themeMusic = nullptr;
    Mix_Chunk* uiSounds[3] = {};      // synth.hpp: focus, press, value
    Mix_Chunk* mergeChimes[12] = {};  // one marimba note per fruit
    Mix_Chunk* bestSound = nullptr;
    float bestSoundIn = -1.0f;        // seconds until the new-best arpeggio (after the lose sound)
    bool sdlReady = false;
    bool audioReady = false;
    bool audioMuted = false;

    HemisphereBoundary boundary;
    Fruit nextFruit;
    FruitManager fm;

    int total_points=0;
    int high_score = 0;               // best score, saved between runs (interactive)
    int bestAtStart = 0;              // the best before this game: game over shows a new best against it
    // This game's stats for the game-over card.
    int statMerges = 0, statBestCombo = 0, statBiggest = -1;
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
    // Hints name touch gestures instead of mouse buttons (touch_input.hpp):
    // set by the first touch, cleared by the mouse wheel.
    bool touchUI = false;
    void SetTouchUI(bool on) { if (touchUI != on) { touchUI = on; menuBuiltFor = -1; } }
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
        if (mergeSound) Mix_FreeChunk(mergeSound);
        if (loseSound) Mix_FreeChunk(loseSound);
        if (placeSound) Mix_FreeChunk(placeSound);
        if (themeMusic) Mix_FreeMusic(themeMusic);
        for (Mix_Chunk* c : uiSounds) if (c) Mix_FreeChunk(c);
        for (Mix_Chunk* c : mergeChimes) if (c) Mix_FreeChunk(c);
        if (bestSound) Mix_FreeChunk(bestSound);
        if (audioReady) Mix_CloseAudio();
        if (sdlReady) SDL_Quit();
        delete t_rend;
        delete text_shader;
        delete shape_shader;
        delete physics_solver;
        delete thread_pool;
    }
    void SetAudioState(bool sdlInitialized, bool mixerInitialized, bool muted) {
        sdlReady = sdlInitialized;
        audioReady = mixerInitialized;
        audioMuted = muted;
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
        statMerges = statBestCombo = 0; statBiggest = -1;
        bestAtStart = high_score;
        playClock = 0.0f; scoreShown = 0.0f; scorePop = 0.0f; lastCombo = 0;
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
        shape_shader = new Shader(FileSystem::getPath("resources/shaders/ui_shape.vs").c_str(), FileSystem::getPath("resources/shaders/ui_shape.fs").c_str());
        t_rend = new TextRenderer(text_shader);
        t_rend->SetShapeShader(shape_shader);
        try {
            // Chakra Petch (OFL): its cut-corner letters match the chamfered panels and the
            // faceted scene. Semi-bold for body text, bold for titles, numbers and buttons.
            // Baked large and mipmapped so the HUD stays crisp from 720p to 4K.
            t_rend->LoadFace(TextRenderer::Body, FileSystem::getPath("resources/fonts/ChakraPetch-SemiBold.ttf"), 64);
            t_rend->LoadFace(TextRenderer::Display, FileSystem::getPath("resources/fonts/ChakraPetch-Bold.ttf"), 128);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load font: " << e.what() << std::endl;
        }
        menu.onSound = [this](int k) { UiSound(k); };

        fm.initializeFruits();
        nextFruit = fm.getRandomFruit();
        state.Init(win);

        if (audioReady && !audioMuted) {
            mergeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/drop.wav").c_str());
            if (!mergeSound) std::cerr << "Warning: Could not load drop.wav: " << Mix_GetError() << std::endl;
            loseSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/game_over.wav").c_str());
            if (!loseSound) std::cerr << "Warning: Could not load game_over.wav: " << Mix_GetError() << std::endl;
            placeSound = Mix_LoadWAV(FileSystem::getPath("resources/audio/click.wav").c_str());
            if (!placeSound) std::cerr << "Warning: Could not load click.wav: " << Mix_GetError() << std::endl;
            themeMusic = Mix_LoadMUS(FileSystem::getPath("resources/audio/theme.ogg").c_str());
            if (!themeMusic) std::cerr << "Mix_LoadMUS error: " << Mix_GetError() << std::endl;
            Mix_AllocateChannels(24);
            for (int i = 0; i < 3; ++i) uiSounds[i] = synth::uiSound(i);
            for (int i = 0; i < 12; ++i) mergeChimes[i] = synth::mergeChime(i);
            bestSound = synth::newBest();
            if (!uiSounds[1] || !mergeChimes[0] || !bestSound) std::cerr << "Warning: synth sounds failed: " << Mix_GetError() << std::endl;
            else if (logEvents) std::cerr << "EV synth sounds ready\n";
            ApplyVolumeSettings();
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (audioReady && !audioMuted && themeMusic) Mix_PlayMusic(themeMusic, -1);
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
        if (State == GAME_OVER && page == Page::None && keyPressedOnce(GLFW_KEY_R)) { UiSound(1); Reset(); }
        if (State != GAME_ACTIVE && page != Page::Controls && keyPressedOnce(GLFW_KEY_SLASH)) { UiSound(1); Open(Page::Controls); }
        else if (State == GAME_ACTIVE && !photo && keyPressedOnce(GLFW_KEY_SLASH)) { UiSound(1); OpenControlsFromGame(); }
        if (State != GAME_ACTIVE) UpdateMenuInput(dt);
    }

    // Best score file (interactive runs only, so headless stills stay reproducible).
    bool bestFileOn = false;
    void LoadBest() {
        if (!bestFileOn) return;
        std::ifstream in(FileSystem::userFile("best"));
        int b = 0;
        if (in >> b) high_score = std::max(high_score, b);
        bestAtStart = high_score;
    }
    void SaveBest() const {
        if (!bestFileOn) return;
        FileSystem::writeUserFile("best", std::to_string(high_score) + "\n");
    }

    void PlaySfx(Mix_Chunk* c) {
        if (audioReady && !audioMuted && soundOn && c) Mix_PlayChannel(-1, c, 0);
    }
    // UI feedback sounds: 0 focus moved, 1 pressed, 2 value changed.
    void UiSound(int k) {
        if (k >= 0 && k < 3) PlaySfx(uiSounds[k]);
    }
    // One chime per frame for the biggest fruit merged; a running combo
    // climbs the ladder a step per merge (up to three), so chains rise.
    void MergeChime(int fruit, int comboCount) {
        int i = std::clamp(fruit - std::min(std::max(comboCount - 1, 0), 3), 0, 11);
        PlaySfx(mergeChimes[i]);
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
        in.mouseDown = (heldButtons & 1) || (window && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);  // heldButtons: also the scripted mouse
        in.mousePressed = in.mouseDown && !mouseWasDown;
        mouseWasDown = in.mouseDown;
        menu.update(*t_rend, in, dt, float(Width), float(Height));
        EnsureMenu();  // an item may have changed the screen
    }

    void ApplyVolumeSettings()
    {
        if (!audioReady || audioMuted) return;
        float master = soundOn ? 1.0f : 0.0f;
        Mix_VolumeMusic(static_cast<int>(master * musicVolume * MIX_MAX_VOLUME));
        int sfx_volume_sdl = static_cast<int>(master * sfxVolume * MIX_MAX_VOLUME);
        if (mergeSound) Mix_VolumeChunk(mergeSound, sfx_volume_sdl);
        if (loseSound)  Mix_VolumeChunk(loseSound, sfx_volume_sdl);
        if (placeSound) Mix_VolumeChunk(placeSound, sfx_volume_sdl);
        for (Mix_Chunk* c : uiSounds) if (c) Mix_VolumeChunk(c, sfx_volume_sdl);
        for (Mix_Chunk* c : mergeChimes) if (c) Mix_VolumeChunk(c, sfx_volume_sdl);
        if (bestSound) Mix_VolumeChunk(bestSound, sfx_volume_sdl);
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
        if (bestSoundIn > 0.0f && (bestSoundIn -= dt) <= 0.0f) {
            PlaySfx(bestSound);
        }

        switch (State)
        {
            case GAME_ACTIVE:
                if (audioReady && !audioMuted && Mix_PausedMusic() == 1) Mix_ResumeMusic();
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
                if (audioReady && !audioMuted && State == GAME_OVER) Mix_PauseMusic();
                break;
            case GAME_MENU:
            case GAME_PAUSED:
                if (audioReady && !audioMuted && Mix_PausedMusic() == 1) Mix_ResumeMusic();
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
                if (total_points > high_score) { high_score = total_points; SaveBest(); }
                if (State == GAME_ACTIVE) PlaySfx(loseSound);
                if (State == GAME_ACTIVE && total_points > bestAtStart && total_points > 0) bestSoundIn = 1.1f;
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

    // Interactive runs only: the tutorial opens the first time the player presses Play.
    void LoadTutorialState() {
        std::string f = FileSystem::userFile("tutorial-done");
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
        FileSystem::writeUserFile("tutorial-done", "1\n");
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


    // Merge events become bursts; merges in quick succession count as a combo
    // (cosmetic: scoring is unchanged). Danger rises when settled fruit pile up
    // to the rim.
    void UpdateEffects(float dt) {
        if (!effectsOn) {
            int biggest = -1;
            for (const auto& m : physics_solver->takeMerges()) { ++statMerges; statBiggest = std::max(statBiggest, int(m.fruit)); biggest = std::max(biggest, int(m.fruit)); }
            if (biggest >= 0) MergeChime(biggest, 1);
            bursts.clear(); combo = 0; danger = 0.0f;
            return;
        }
        int chime = -1;
        for (const auto& m : physics_solver->takeMerges()) {
            combo = comboTimer > 0.0f ? combo + 1 : 1;
            chime = std::max(chime, int(m.fruit));
            ++statMerges;
            statBestCombo = std::max(statBestCombo, combo);
            statBiggest = std::max(statBiggest, int(m.fruit));
            if (logEvents) fprintf(stderr, "EV t=%.2f merge fruit=%d combo=%d\n", time, int(m.fruit), combo);
            comboTimer = kComboWindow;
            comboPop = 1.0f;
            bursts.push_back({m.position, m.radius, m.fruit, 0.0f, std::min(1.0f + 0.4f * float(combo - 1), 2.6f)});
            if (bursts.size() > RaymarchRenderer::kMaxBursts) bursts.erase(bursts.begin());
        }
        if (chime >= 0) MergeChime(chime, combo);
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
        if (!audioReady || audioMuted) return 0;
        if (physics_solver->just_merged > 0 && mergeSound != nullptr) Mix_PlayChannel(-1, mergeSound, 0);
        if (ballPlaced && placeSound != nullptr) Mix_PlayChannel(-1, placeSound, 0);
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
        renderer.showGizmo = showPerf && !hideUI;  // the axis gizmo is a debug read-out (F3)
        renderer.draw(cam, FbWidth, FbHeight, time);
    }

    void BeginText() { t_rend->Begin(float(Width), float(Height)); }


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
    // A narrow (portrait phone) window scales by width instead, so the HUD
    // keeps about 400 units across.
    float UiScale() const { return std::max(1.0f, std::min(Height / 720.0f, Width / 400.0f)); }
    float DepthStripHeight() const { return std::clamp(Height * 0.125f, 54.0f, 104.0f * UiScale()); }


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


    // ------------------------------------------------------------------ HUD
    // Glazed-ceramic HUD (docs/ui.md): score and next-drop cards at the top,
    // icon buttons under the next card, the slice dial and w track along the
    // bottom. Debug read-outs and the axis gizmo only with F3.
    enum HudId { HudNone = -1, HudView = 0, HudHelp, HudPause, HudHome, HudSkip, kHudButtons };
    ui::Rect hudRect[kHudButtons];
    bool hudVisible[kHudButtons] = {};
    float hudHover[kHudButtons] = {}, hudPress[kHudButtons] = {};
    float scoreShown = 0.0f, scorePop = 0.0f, comboShown = 0.0f, homeShown = 0.0f;
    float playClock = 0.0f;           // seconds of play since Reset (starter chips fade out)
    float hudLastTime = -1.0f;
    bool controlsFromGame = false;    // ? in play opened Controls; Back returns to the game

    int HudButtonAt(glm::vec2 p) const {
        for (int i = 0; i < kHudButtons; ++i)
            if (hudVisible[i] && hudRect[i].has(p)) return i;
        return HudNone;
    }
    // A left click on a HUD button runs it and does not drop a fruit.
    bool HudClick(glm::vec2 p) {
        if (State != GAME_ACTIVE || photo || hideUI) return false;
        int b = HudButtonAt(p);
        if (b == HudNone) return false;
        hudPress[b] = 1.0f;
        UiSound(1);
        switch (b) {
            case HudView: CycleViewMode(+1); break;
            case HudHelp: OpenControlsFromGame(); break;
            case HudPause: State = GAME_PAUSED; break;
            case HudHome: homing = true; break;
            case HudSkip: EndTutorial(); break;
        }
        return true;
    }
    void OpenControlsFromGame() {
        controlsFromGame = true;
        State = GAME_PAUSED;
        page = Page::None;
        Open(Page::Controls);
    }

    float HudMargin() const { return 18.0f * UiScale(); }
    float TopCardHeight() const { return 100.0f * UiScale(); }

    void RenderGameUI(const Hit4& aim)
    {
        if (sheetMode) { RenderSheetLabels(); return; }
        BeginText();
        const float dt = hudLastTime < 0.0f ? 0.0f : std::clamp(time - hudLastTime, 0.0f, 0.1f);
        hudLastTime = time;
        playClock += dt;
        RenderDropGuide(aim);
        RenderEffectsUI(dt);
        RenderScoreCard(dt);
        RenderNextCard();
        RenderHudButtons(dt);
        RenderDepthStrip(aim);
        RenderSliceDial(dt);
        RenderStarterChips();
        hudVisible[HudSkip] = false;   // RenderTutorial shows it while the coach is up
        RenderTutorial();
        if (!caption.empty()) {
            const float u = UiScale();
            FaceScope f(*t_rend, TextRenderer::Display);
            float sc = 0.8f * u;
            ui::textOnScene(*t_rend, caption, (Width - t_rend->GetTextWidth(caption, sc)) * 0.5f, HudMargin() + 8.0f * u, sc, ui::kCream);
        }
        if (showPerf) RenderDebugInfo();
    }

    void RenderScoreCard(float dt) {
        TextRenderer& t = *t_rend;
        const float u = UiScale(), m = HudMargin();
        const int pts = int(physics_solver->total_points);
        if (float(pts) > scoreShown + 0.5f && scoreShown >= 0.0f) scorePop = 1.0f;
        scoreShown += (float(pts) - scoreShown) * std::min(1.0f, dt * 10.0f);
        if (std::abs(float(pts) - scoreShown) < 0.5f) scoreShown = float(pts);
        scorePop = std::max(0.0f, scorePop - dt * 3.5f);
        ui::Rect r{m, m, 220.0f * u, TopCardHeight()};
        ui::glazePanel(t, r, u, 1.0f, 22.0f * u);
        FaceScope f(t, TextRenderer::Display);
        const float ls = 0.5f * u;
        t.RenderText("SCORE", r.x + 20.0f * u, r.y + 16.0f * u, ls, ui::kTerracotta);
        const int best = std::max(high_score, pts);
        if (best > 0) {
            FaceScope fb(t, TextRenderer::Body);
            std::string b = "best " + ui::thousands(best);
            float bs = 0.48f * u;
            t.RenderText(b, r.x + r.w - 20.0f * u - t.GetTextWidth(b, bs), r.y + 16.0f * u, bs,
                         pts > 0 && pts >= high_score && high_score > 0 ? ui::kGold : ui::kClayDeep);
        }
        std::string s = ui::thousands((long long)std::lround(scoreShown));
        float vs = ui::fit(t, s, 1.75f * u, r.w - 40.0f * u) * (1.0f + 0.12f * ui::easeOutCubic(scorePop));
        float cap = t.CapHeight(vs);
        t.RenderText(s, r.x + 20.0f * u, r.y + r.h - 18.0f * u - cap, vs, glm::mix(ui::kInk, ui::kGold, scorePop * 0.8f));
    }

    void RenderNextCard() {
        TextRenderer& t = *t_rend;
        const float u = UiScale(), m = HudMargin();
        ui::Rect r{Width - m - 250.0f * u, m, 250.0f * u, TopCardHeight()};
        ui::glazePanel(t, r, u, 1.0f, 22.0f * u);
        FaceScope f(t, TextRenderer::Display);
        t.RenderText("NEXT", r.x + 20.0f * u, r.y + 16.0f * u, 0.5f * u, ui::kTerracotta);
        // The fruit at its size on the ladder (cherry small, watermelon big), its name, and the ladder.
        const float rMin = FruitManager::getFruitProperties(CHERRY).radius;
        const float rMax = FruitManager::getFruitProperties(WATERMELON).radius;
        float k = (FruitManager::getFruitProperties(nextFruit).radius - rMin) / std::max(rMax - rMin, 1e-3f);
        const float slot = 30.0f * u;
        float fr = glm::mix(9.0f * u, slot, std::sqrt(k));
        glm::vec2 c(r.x + 20.0f * u + slot, r.y + r.h * 0.55f);
        DrawFruitBall(c, fr, int(nextFruit), 1.0f);
        std::string name = kFruitNames[int(nextFruit)];
        if (!name.empty()) name[0] = char(std::toupper((unsigned char)name[0]));
        float tx = c.x + slot + 14.0f * u;
        float ns = ui::fit(t, name, 0.85f * u, r.x + r.w - tx - 16.0f * u);
        t.RenderText(name, tx, c.y - t.CapHeight(ns) * 0.5f - 8.0f * u, ns, ui::kInk);
        // Ladder: every fruit as a dot, the next one marked.
        const float ly = c.y + 18.0f * u, lx0 = tx + 2.0f * u, lx1 = r.x + r.w - 20.0f * u;
        const float step = (lx1 - lx0) / float(WATERMELON);
        for (int i = 0; i <= int(WATERMELON); ++i) {
            glm::vec2 d(lx0 + step * float(i), ly);
            bool now = i == int(nextFruit);
            if (now) t.Shape(UiShape::circle(d, 6.5f * u).ring(1.6f * u).fill(ui::kInk, 0.9f));
            t.Shape(UiShape::circle(d, (now ? 4.0f : 2.8f) * u).fill(FruitUiColor(i), now ? 1.0f : 0.6f));
        }
        if (laneSnap) {
            FaceScope fb(t, TextRenderer::Body);
            const std::string tag = "w-lane snap  L";
            float s = 0.46f * u, h = 26.0f * u, w = t.GetTextWidth(tag, s) + 24.0f * u;
            ui::Rect g{r.x, r.y + r.h + 12.0f * u, w, h};
            t.Shape(UiShape::rect(g.x, g.y, g.w, g.h, h * 0.5f).grad(ui::kGoldTop, ui::kGoldBottom));
            t.RenderText(tag, g.x + 12.0f * u, g.y + (h - t.CapHeight(s)) * 0.5f, s, ui::kOnGold);
        }
    }

    // A shaded fruit dot for the HUD: soft shadow, colour gradient, highlight.
    void DrawFruitBall(glm::vec2 c, float r, int type, float alpha) {
        TextRenderer& t = *t_rend;
        glm::vec3 col = FruitUiColor(type);
        t.Shape(UiShape::circle(c + glm::vec2(0.0f, r * 0.12f), r).fill(ui::kShadow, 0.25f * alpha).soft(std::max(2.0f, r * 0.3f)));
        t.Shape(UiShape::circle(c, r).grad(glm::mix(col, glm::vec3(1.0f), 0.28f), col * 0.85f, alpha));
        t.Shape(UiShape::circle(c + glm::vec2(-0.32f, -0.36f) * r, r * 0.26f).fill(glm::vec3(1.0f), 0.5f * alpha));
    }

    // View, help and pause under the next card. The view button names the view and its key.
    void RenderHudButtons(float dt) {
        TextRenderer& t = *t_rend;
        const float u = UiScale(), m = HudMargin();
        const glm::vec2 mouse(state.m_xpos, state.m_ypos);
        const int hover = HudButtonAt(mouse);
        for (int i = 0; i < kHudButtons; ++i) {
            ui::approach(hudHover[i], i == hover ? 1.0f : 0.0f, dt, 18.0f);
            hudPress[i] = std::max(0.0f, hudPress[i] - dt * 6.0f);
        }
        const float br = 22.0f * u, y = m + TopCardHeight() + 16.0f * u + (laneSnap ? 38.0f * u : 0.0f);
        float x = Width - m;
        hudRect[HudPause] = {x - 2.0f * br, y, 2.0f * br, 2.0f * br};
        x -= 2.0f * br + 10.0f * u;
        hudRect[HudHelp] = {x - 2.0f * br, y, 2.0f * br, 2.0f * br};
        x -= 2.0f * br + 10.0f * u;
        const bool eye = renderer.viewMode == ViewMode::Eye;
        const std::string label = eye ? "4D eye" : "Slice";
        float vw;
        {
            FaceScope f(t, TextRenderer::Display);
            // The key letters are left out for touch.
            vw = 2.0f * br * 0.9f + t.GetTextWidth(label, 0.6f * u) + 16.0f * u +
                 (touchUI ? 6.0f * u : 10.0f * u + ui::keycapWidth(t, "M", 22.0f * u));
        }
        hudRect[HudView] = {x - vw, y, vw, 2.0f * br};
        hudVisible[HudView] = hudVisible[HudHelp] = hudVisible[HudPause] = true;

        ui::iconButton(t, hudRect[HudPause].centre(), br, ui::Icon::Pause, u, hudHover[HudPause], hudPress[HudPause]);
        ui::iconButton(t, hudRect[HudHelp].centre(), br, ui::Icon::Help, u, hudHover[HudHelp], hudPress[HudHelp]);
        {
            const ui::Rect& v = hudRect[HudView];
            ui::pill(t, v, u, ui::Tone::Secondary, hudHover[HudView], hudPress[HudView]);
            float sink = 4.0f * u * hudPress[HudView];
            ui::icon(t, eye ? ui::Icon::Eye : ui::Icon::Slice, {v.x + br * 0.95f, v.y + br + sink}, br * 1.1f, ui::kInk);
            FaceScope f(t, TextRenderer::Display);
            float s = 0.6f * u;
            float lx = v.x + 2.0f * br * 0.9f;
            t.RenderText(label, lx, v.y + (v.h - t.CapHeight(s)) * 0.5f + sink, s, ui::kInk);
            if (!touchUI) ui::keycap(t, "M", lx + t.GetTextWidth(label, s) + 10.0f * u, v.y + (v.h - 22.0f * u) * 0.5f - 1.0f * u + sink, 22.0f * u);
        }
        // Tooltip for the round buttons.
        if (hover == HudHelp || hover == HudPause) {
            const char* tip = hover == HudHelp ? "Controls  ?" : "Pause  Esc";
            FaceScope f(t, TextRenderer::Body);
            float s = 0.46f * u, w = t.GetTextWidth(tip, s) + 20.0f * u, h = 26.0f * u;
            const ui::Rect& b = hudRect[hover];
            float tx = std::min(b.centre().x - w * 0.5f, Width - m - w);
            float a = hudHover[hover];
            t.Shape(UiShape::rect(tx, b.y + b.h + 10.0f * u, w, h, h * 0.5f).fill(ui::kShadow, 0.8f * a));
            t.RenderText(tip, tx + 10.0f * u, b.y + b.h + 10.0f * u + (h - t.CapHeight(s)) * 0.5f, s, ui::kCream, a);
        }
    }

    // First seconds of a game (after the tutorial): the three inputs that matter, fading out.
    void RenderStarterChips() {
        if (tutorialActive || playClock > 10.0f) return;
        float a = ui::clamp01((playClock - 0.3f) / 0.5f) * ui::clamp01((10.0f - playClock) / 1.5f);
        if (a <= 0.0f) return;
        const float u = UiScale();
        float h = 34.0f * u;
        float bottom = (showDepthStrip && FourDUnlocked()) ? Height - DepthStripHeight() - HudMargin() - 16.0f * u : Height - HudMargin();
        ui::keyChipRow(*t_rend, StarterChips(), Width * 0.5f, bottom - h, h, a, Width - 2.0f * HudMargin());
    }

    // Bottom-left: how far the slice is turned and moved, the view, and a home button.
    void RenderSliceDial(float dt) {
        hudVisible[HudHome] = false;
        if (!FourDUnlocked()) return;
        TextRenderer& t = *t_rend;
        const float u = UiScale(), m = HudMargin();
        const float size = DepthStripHeight();
        ui::Rect r{m, Height - m - size, size, size};
        t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, 20.0f * u).fill(ui::kGlaze2, 0.72f));
        t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, 20.0f * u).ring(std::max(1.0f, 1.2f * u)).fill(ui::kEdge, 0.35f));
        const float tilt = glm::degrees(state.cam.tilt());
        const float off = state.cam.offset;
        glm::vec2 c(r.x + r.w * 0.5f, r.y + r.h * 0.46f);
        float rad = r.w * 0.30f, th = 5.0f * u;
        t.Shape(UiShape::arcRing(c, rad, th, 0.0f, 6.2832f).fill(ui::kCream, 0.18f));
        float sweep = glm::radians(std::clamp(tilt, 0.0f, 180.0f)) * 2.0f;
        if (sweep > 0.01f) t.Shape(UiShape::arcRing(c, rad, th, -1.5708f, sweep).fill(DepthUiColor(tilt / 60.0f), 1.0f));
        {
            FaceScope f(t, TextRenderer::Display);
            std::string deg = std::to_string(int(std::lround(tilt))) + "\xC2\xB0";
            float s = ui::fit(t, deg, 0.66f * u, rad * 1.6f);
            ui::textCentered(t, deg, c.x, c.y - t.CapHeight(s) * 0.5f, s, ui::kCream);
        }
        {
            FaceScope f(t, TextRenderer::Body);
            char buf[32];
            if (std::abs(off) >= 0.005f) snprintf(buf, sizeof buf, "w %+.1f", off);
            else snprintf(buf, sizeof buf, "slice turn");
            float s = ui::fit(t, buf, 0.5f * u, r.w - 10.0f * u);
            ui::textCentered(t, buf, c.x, r.y + r.h - 7.0f * u - t.CapHeight(s), s, ui::kCream, 0.8f);
        }
        const bool away = tilt > 1.0f || std::abs(off) > 0.01f;
        ui::approach(homeShown, away ? 1.0f : 0.0f, dt, 10.0f);
        if (homeShown > 0.02f) {
            float br = 20.0f * u;
            glm::vec2 bc(r.x + r.w + 14.0f * u + br, r.y + br + 4.0f * u);
            hudRect[HudHome] = {bc.x - br, bc.y - br, 2.0f * br, 2.0f * br};
            hudVisible[HudHome] = homeShown > 0.5f;
            float pop = ui::easeOutBack(homeShown);
            ui::iconButton(t, bc, br * pop, ui::Icon::Home, u, hudHover[HudHome], hudPress[HudHome], homeShown, homing);
            if (!touchUI) ui::keycap(t, "H", bc.x - 11.0f * u, bc.y + br + 10.0f * u, 22.0f * u, homeShown);
        }
    }

    // F3: frame time, render size, slice numbers and the axis gizmo.
    void RenderDebugInfo() {
        TextRenderer& t = *t_rend;
        const float u = UiScale();
        FaceScope f(t, TextRenderer::Body);
        char buf[160];
        int rw = int(FbWidth * renderer.renderScale), rh = int(FbHeight * renderer.renderScale);
        snprintf(buf, sizeof buf, "%s  scene %.1f ms  %dx%d (%.0f%%)  view %s  tilt %.1f deg  offset %+.2f",
                 qualityName(renderer.quality()), renderer.gpuMs(), rw, rh, renderer.renderScale * 100.0f,
                 ViewLabel().c_str(), glm::degrees(state.cam.tilt()), state.cam.offset);
        float s = 0.46f * u, w = t.GetTextWidth(buf, s) + 24.0f * u, h = 30.0f * u;
        float x = (Width - w) * 0.5f, y = HudMargin() + TopCardHeight() + 14.0f * u;
        t.Shape(UiShape::rect(x, y, w, h, 8.0f * u).fill(ui::kShadow, 0.8f));
        t.RenderText(buf, x + 12.0f * u, y + (h - t.CapHeight(s)) * 0.5f, s, ui::kCream);
    }

    // Hand cursor over anything clickable in play (main.cpp sets it).
    bool WantsHandCursor() const {
        if (State != GAME_ACTIVE) return menu.hovering();
        if (photo || hideUI) return false;
        return HudButtonAt(glm::vec2(state.m_xpos, state.m_ypos)) != HudNone;
    }

    // Coach panel for the onboarding steps, above the w track.
    void RenderTutorial() {
        if (!tutorialActive || sheetMode) return;
        TextRenderer& t = *t_rend;
        static const char* titles[] = {"Aim and drop", "Merge", "The bowl is 4D", "Fruit hide in w"};
        static const char* mouseLines[][2] = {
            {"Move the mouse over the bowl and click to drop a fruit.", "Right-drag turns the camera around the bowl."},
            {"Two of the same fruit merge into the next one.", "Drop a match next to one already in the bowl."},
            {"Everything here is a slice of a 4D world.", "Scroll the wheel, middle-drag, or hold A or D to turn the slice."},
            {"Fruit just off the slice show as dotted rings; the track below places every fruit in w.", "Press H or Space to come back to the default slice."},
        };
        static const char* touchLines[][2] = {
            {"Drag over the bowl and lift your finger to drop a fruit.", "Drag with two fingers to turn the camera around the bowl."},
            {"Two of the same fruit merge into the next one.", "Drop a match next to one already in the bowl."},
            {"Everything here is a slice of a 4D world.", "Twist two fingers to turn the slice."},
            {"Fruit just off the slice show as dotted rings; the track below places every fruit in w.", "Twist back, or tap the home button, to return to the default slice."},
        };
        const auto& lines = touchUI ? touchLines : mouseLines;
        const int s = std::min(tutorialStep, kTutorialSteps - 1);
        const float u = UiScale();
        const float w = std::min(Width - 2.0f * HudMargin(), 640.0f * u);
        const float pad = 24.0f * u;
        // Body lines wrap on narrow screens; the card grows to fit them and,
        // if the last line runs under it, a row for Skip.
        FaceScope body(t, TextRenderer::Body);
        const float ls = 0.62f * u;
        struct Line { std::string text; float y; int para; };
        std::vector<Line> body_;
        float ly = 58.0f * u;
        for (int i = 0; i < 2; ++i) {
            for (auto& l : ui::wrap(t, lines[s][i], ls, w - 2.0f * pad)) { body_.push_back({l, ly, i}); ly += 24.0f * u; }
            ly += 4.0f * u;
        }
        const float kh = 24.0f * u, ks = 0.5f * u;
        const char* skipLabel = touchUI ? "Skip" : "skip";
        const float lw = t.GetTextWidth(skipLabel, ks);
        const float skipW = lw + (touchUI ? 0.0f : 8.0f * u + ui::keycapWidth(t, "F1", kh));
        float h = ly + 14.0f * u;
        if (body_.size() > 2 && t.GetTextWidth(body_.back().text, ls) + skipW + 24.0f * u > w - 2.0f * pad) h += 30.0f * u;
        const float bottom = (showDepthStrip && FourDUnlocked()) ? Height - DepthStripHeight() - HudMargin() - 18.0f * u
                                                                 : Height - HudMargin() - 8.0f * u;
        ui::Rect r{(Width - w) * 0.5f, bottom - h, w, h};
        ui::glazePanel(t, r, u, 1.0f, 22.0f * u);
        {
            FaceScope f(t, TextRenderer::Display);
            float ts = 0.8f * u;
            t.RenderText(titles[s], r.x + pad, r.y + 20.0f * u, ts, ui::kTerracotta);
        }
        for (int i = 0; i < kTutorialSteps; ++i) {
            glm::vec2 d(r.x + r.w - pad - 18.0f * u * float(kTutorialSteps - 1 - i), r.y + 30.0f * u);
            if (i == s) t.Shape(UiShape::capsule(d - glm::vec2(5.0f * u, 0.0f), d + glm::vec2(5.0f * u, 0.0f), 5.0f * u).fill(ui::kGoldBottom));
            else t.Shape(UiShape::circle(d, 4.5f * u).fill(i < s ? ui::kGoldBottom : ui::kClay, i < s ? 1.0f : 0.5f));
        }
        {
            for (auto& l : body_)
                t.RenderText(l.text, r.x + pad, r.y + l.y, ls, l.para == 0 ? ui::kInk : ui::kClayDeep);
            // "F1 skip" (touch: a "Skip" pill) is also a button.
            float kx = r.x + r.w - pad - skipW;
            float ky = r.y + r.h - kh - 14.0f * u;
            hudRect[HudSkip] = {kx - 10.0f * u, ky - 7.0f * u, r.x + r.w - pad + 10.0f * u - (kx - 10.0f * u), kh + 14.0f * u};
            hudVisible[HudSkip] = true;
            const float hv = std::max(hudHover[HudSkip], touchUI ? 0.6f : 0.0f);
            if (hv > 0.01f) t.Shape(UiShape::rect(hudRect[HudSkip].x, hudRect[HudSkip].y, hudRect[HudSkip].w, hudRect[HudSkip].h, hudRect[HudSkip].h * 0.5f).fill(ui::kClay, 0.25f * hv));
            if (!touchUI) ui::keycap(t, "F1", kx, ky, kh);
            t.RenderText(skipLabel, r.x + r.w - pad - lw, ky + kh * 0.5f - t.CapHeight(ks) * 0.5f, ks, glm::mix(ui::kClayDeep, ui::kInk, hv));
        }
    }

    // The w track: every fruit placed along the slice normal (world w at tilt 0),
    // stacked by height. The cream bar is the current slice; fruit it cuts are
    // bright, the rest faded. A triangle marks where the held fruit will drop.
    void RenderDepthStrip(const Hit4& hit) {
        if (!showDepthStrip || !FourDUnlocked()) return;
        TextRenderer& t = *t_rend;
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
        const float u = UiScale(), m = HudMargin();
        const float h = DepthStripHeight();
        const float x = m + h + 14.0f * u + 40.0f * u + 14.0f * u;   // after the slice dial and its home button
        const float w = std::min(Width * 0.62f, Width - x - m);
        const float y = Height - h - m;
        const float pad = std::min(54.0f * u, w * 0.12f);
        auto screenX = [&](float d) { return x + w * 0.5f + d / span * (w * 0.5f - pad); };

        t.Shape(UiShape::rect(x, y, w, h, 20.0f * u).fill(ui::kGlaze2, 0.72f));
        t.Shape(UiShape::rect(x, y, w, h, 20.0f * u).ring(std::max(1.0f, 1.2f * u)).fill(ui::kEdge, 0.35f));
        const float top = y + 30.0f * u, rail = y + h - 16.0f * u;
        {   // legend
            FaceScope f(t, TextRenderer::Body);
            float s = 0.5f * u;
            // A narrow track (phone) keeps the short forms that fit.
            const std::string r = "faded = off your slice";
            const float room = w - 32.0f * u, rw = t.GetTextWidth(r, s) + 24.0f * u;
            std::string l = "w track: where each fruit sits in the 4th dimension";
            if (t.GetTextWidth(l, s) + rw > room) l = "w track";
            t.RenderText(l, x + 16.0f * u, y + 9.0f * u, s, ui::kCream, 0.85f);
            if (t.GetTextWidth(l, s) + rw <= room)
                t.RenderText(r, x + w - 16.0f * u - t.GetTextWidth(r, s), y + 9.0f * u, s, ui::kCream, 0.6f);
        }
        // Rail, coloured from coral (-w) through gold (the slice) to sky (+w).
        const int seg = 40;
        for (int i = 0; i < seg; ++i) {
            float a = glm::mix(-span, span, i / float(seg)), b = glm::mix(-span, span, (i + 1) / float(seg));
            t.Shape(UiShape::capsule({screenX(a), rail}, {screenX(b), rail}, 2.2f * u).fill(DepthUiColor((a + b) * 0.5f - slice), 0.95f));
        }
        {
            FaceScope f(t, TextRenderer::Display);
            float s = 0.56f * u;
            float cy = rail - t.CapHeight(s) * 0.5f;
            t.RenderText("\xE2\x88\x92w", x + 14.0f * u, cy, s, DepthUiColor(-span - slice));
            t.RenderText("+w", x + w - 14.0f * u - t.GetTextWidth("+w", s), cy, s, DepthUiColor(span - slice));
        }
        if (laneSnap) {   // the world-w lanes, as they lie along the slice normal
            for (int k = -int(span / kLaneSpacing) - 1; k <= int(span / kLaneSpacing) + 1; ++k) {
                float lane = k * kLaneSpacing * normal.w;
                if (std::abs(lane) > span) continue;
                t.Shape(UiShape::capsule({screenX(lane), top}, {screenX(lane), rail}, 0.8f * u).fill(ui::kGold, 0.3f));
            }
        }
        // The slice: a cream bar with a soft glow.
        const float sx = screenX(slice);
        t.Shape(UiShape::capsule({sx, top - 4.0f * u}, {sx, rail + 6.0f * u}, 6.0f * u).fill(ui::kCream, 0.18f).soft(6.0f * u));
        t.Shape(UiShape::capsule({sx, top - 4.0f * u}, {sx, rail + 6.0f * u}, 1.8f * u).fill(ui::kCream, 0.95f));
        // Fruit, far ones first so the cut ones sit on top.
        std::vector<int> order;
        for (int i = 0; i < MAX_OBJECTS; ++i)
            if (physics_solver->has_obj[i] && !physics_solver->objects[i].hidden) order.push_back(i);
        auto cut = [&](int i) { const auto& o = physics_solver->objects[i]; return std::abs(depth(o.position) - slice) < o.radius; };
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return !cut(a) && cut(b); });
        for (int i : order) {
            const auto& obj = physics_solver->objects[i];
            float d = depth(obj.position);
            float level = glm::clamp((obj.position.y - boundary.center.y + boundary.radius) / (boundary.radius + 1.0f), 0.0f, 1.0f);
            float r = std::clamp(obj.radius / span * (w * 0.5f - pad), 3.5f * u, (rail - top) * 0.32f);
            glm::vec2 c(screenX(d), glm::mix(rail - 6.0f * u - r, top + r, level));
            const bool on = cut(i);
            float a = on ? 1.0f : 0.42f;
            t.Shape(UiShape::capsule({screenX(d - obj.radius), c.y}, {screenX(d + obj.radius), c.y}, 1.2f * u)
                        .fill(FruitUiColor(int(obj.fruit)), 0.35f * a));
            DrawFruitBall(c, r, int(obj.fruit), a);
            if (on) t.Shape(UiShape::circle(c, r + 1.5f * u).ring(1.6f * u).fill(ui::kCream, 0.9f));
        }
        if (hit.hit) {
            const float cx = screenX(depth(DropCenter(hit)));
            const float th = 12.0f * u;
            t.Shape(UiShape::tri({cx - th, y - 2.0f * u}, {cx + th, y - 2.0f * u}, {cx, y + th}, 2.0f * u).fill(ui::kCream));
            t.Shape(UiShape::tri({cx - th * 0.62f, y + 0.5f * u}, {cx + th * 0.62f, y + 0.5f * u}, {cx, y + th * 0.72f}, 1.0f * u)
                        .fill(FruitUiColor(int(nextFruit))));
        }
    }

    // Combo badge under the score card, popping on each merge; a soft red
    // vignette and a warning pill when the pile nears the rim.
    void RenderEffectsUI(float dt) {
        TextRenderer& t = *t_rend;
        const float u = UiScale(), m = HudMargin();
        if (danger > 0.02f) {
            float pulse = 0.5f + 0.5f * std::sin(time * 7.0f);
            float a = danger * (0.35f + 0.35f * pulse);
            float e = std::max(24.0f, Height * 0.05f) * (0.6f + danger);
            t.Shape(UiShape::rect(-e, -e, Width + 2.0f * e, Height + 2.0f * e, e).ring(e * 1.4f).soft(e * 1.2f).fill(ui::kDanger, a));
            if (danger > 0.5f) {
                FaceScope f(t, TextRenderer::Display);
                const std::string warn = "Bowl almost full!";
                float s = 0.62f * u, h = 40.0f * u, w = t.GetTextWidth(warn, s) + 40.0f * u;
                float k = 1.0f + 0.04f * pulse;
                ui::Rect r{(Width - w * k) * 0.5f, m + 4.0f * u, w * k, h * k};
                t.Shape(UiShape::rect(r.x, r.y + 4.0f * u, r.w, r.h, r.h * 0.5f).fill(ui::kFoot));
                t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, r.h * 0.5f).grad(glm::mix(ui::kDanger, glm::vec3(1.0f), 0.2f), ui::kDanger));
                ui::textCentered(t, warn, Width * 0.5f, r.y + (r.h - t.CapHeight(s * k)) * 0.5f, s * k, ui::kCream);
            }
        }
        const bool showCombo = combo >= 2 && comboTimer > 0.0f;
        ui::approach(comboShown, showCombo ? std::min(1.0f, comboTimer / 0.4f) : 0.0f, dt, 12.0f);
        if (comboShown > 0.02f && lastCombo >= 2) {
            if (showCombo) lastCombo = combo;
            FaceScope f(t, TextRenderer::Display);
            std::string text = "Combo \xC3\x97" + std::to_string(lastCombo);
            float pop = 1.0f + 0.25f * ui::easeOutBack(comboPop);
            float s = (0.72f + 0.05f * std::min(lastCombo, 6)) * u * pop;
            float h = t.CapHeight(s) + 22.0f * u * pop, w = t.GetTextWidth(text, s) + 32.0f * u * pop;
            ui::Rect r{m, m + TopCardHeight() + 16.0f * u, w, h};
            float a = comboShown;
            t.Shape(UiShape::rect(r.x, r.y + 4.0f * u, r.w, r.h, r.h * 0.5f).fill(ui::kGoldFoot, a));
            t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, r.h * 0.5f).grad(ui::kGoldTop, ui::kGoldBottom, a));
            t.Shape(UiShape::tri({r.x + r.h * 0.25f, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h * 0.5f}).fill(glm::vec3(1.0f), 0.14f * a));
            t.RenderText(text, r.x + 16.0f * u * pop, r.y + (r.h - t.CapHeight(s)) * 0.5f, s, ui::kOnGold, a);
        } else if (showCombo) {
            lastCombo = combo;
        }
    }
    int lastCombo = 0;                // the combo the fading badge shows


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
        dimShown = 0.0f; lastMenuTime = time;  // menus fade the backdrop in from the game
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
    void SaveView() const {
        FileSystem::writeUserFile("view", renderer.viewMode == ViewMode::Eye ? "eye\n" : "slice\n");
    }
    // Interactive runs only: Slice unless the player chose 4D eye last time
    // (an old "thick" choice falls back to Slice).
    void LoadView() {
        std::ifstream in(FileSystem::userFile("view"));
        std::string name;
        if (!(in >> name)) return;
        SetView(name == "eye" ? ViewMode::Eye : ViewMode::Slice);
    }
    void CycleViewMode(int dir) {
        SetView(ViewAt((ViewIndex() + dir + kViewCount) % kViewCount));
        SaveView();
    }

    // A menu's key hints; on touch there is no keyboard to show.
    using Hints = std::vector<std::pair<std::string, std::string>>;
    Hints KeyHints(Hints keys) const { return touchUI ? Hints{} : keys; }

    // Rebuilds the menu when the screen it shows changes.
    void EnsureMenu() {
        int key = (int(State) * 4 + int(page)) * 2 + int(Width < Height);
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
    void ClosePage() {
        page = returnPage; returnPage = Page::None;
        if (controlsFromGame && page == Page::None) { controlsFromGame = false; State = GAME_ACTIVE; }
    }

    // Key chips at the bottom of the title screen: the three inputs that matter, and where the rest are.
    std::vector<std::pair<std::string, std::string>> StarterChips() const {
        if (touchUI) return {{"Lift", "drop"}, {"2 fingers", "orbit"}, {"Twist", "turn into 4D"}, {"?", "all controls"}};
        return {{"Click", "drop"}, {"Right drag", "orbit"}, {"Wheel", "turn into 4D"}, {"?", "all controls"}};
    }

    void BuildTitleMenu() {
        MenuStyle st;
        st.title = "Suika";
        st.badge = "4D";
        st.subtitle = "Merge fruit in four dimensions";
        st.hero = true;
        st.width = 360.0f;
        st.keys = {};
        if (high_score > 0) st.note = [this] { return "Best  " + ui::thousands(high_score); };
        st.sceneChips = StarterChips();
        std::vector<MenuItem> items = {
            MenuItem::button("Play", [this] { Reset(); if (!tutorialSeen) StartTutorial(0); }).withIcon(ui::Icon::Play).asPrimary(),
            MenuItem::button("Tutorial", [this] { Reset(); StartTutorial(0); }).withIcon(ui::Icon::Book),
            MenuItem::button("Controls", [this] { Open(Page::Controls); }).withIcon(ui::Icon::Keys).sameRow(),
            MenuItem::button("Settings", [this] { Open(Page::Settings); }).withIcon(ui::Icon::Gear),
        };
#ifndef __EMSCRIPTEN__
        items.push_back(MenuItem::button("Quit", [this] { if (window) glfwSetWindowShouldClose(window, GLFW_TRUE); }).withIcon(ui::Icon::Quit).sameRow());
#endif
        menu.set(std::move(items), std::move(st));
    }

    // Score and best as two big numbers, for the pause and game-over cards.
    void DrawScorePair(TextRenderer& t, const ui::Rect& r, float u, float alpha, bool newBest) {
        FaceScope f(t, TextRenderer::Display);
        const float half = r.w * 0.5f;
        auto block = [&](float cx, const char* label, int value, glm::vec3 col) {
            float ls = 0.52f * u;
            ui::textCentered(t, label, cx, r.y, ls, ui::kClayDeep, alpha);
            float vs = ui::fit(t, ui::thousands(value), 2.3f * u, half - 16.0f * u);
            ui::textCentered(t, ui::thousands(value), cx, r.y + t.CapHeight(ls) + 12.0f * u, vs, col, alpha);
        };
        block(r.x + half * 0.5f, "SCORE", total_points, ui::kInk);
        block(r.x + half * 1.5f, "BEST", std::max(high_score, total_points), newBest ? ui::kGold : ui::kClayDeep);
        t.Shape(UiShape::capsule({r.x + half, r.y + 4.0f * u}, {r.x + half, r.y + r.h - 6.0f * u}, 0.8f * u).fill(ui::kClay, 0.6f * alpha));
    }

    void BuildPauseMenu() {
        MenuStyle st;
        st.title = "Paused";
        st.width = 400.0f;
        st.keys = KeyHints({{"Esc", "resume"}, {"Enter", "select"}});
        st.headerHeight = 86.0f;
        st.header = [this](TextRenderer& t, const ui::Rect& r, float u, float a) { DrawScorePair(t, r, u, a, false); };
        menu.set({
            MenuItem::button("Resume", [this] { State = GAME_ACTIVE; }).withIcon(ui::Icon::Play).asPrimary(),
            MenuItem::button("Restart", [this] { Reset(); }).withIcon(ui::Icon::Restart),
            MenuItem::button("Controls", [this] { Open(Page::Controls); }).withIcon(ui::Icon::Keys).sameRow(),
            MenuItem::button("Settings", [this] { Open(Page::Settings); }).withIcon(ui::Icon::Gear),
            MenuItem::button("Menu", [this] { State = GAME_MENU; }).withIcon(ui::Icon::Home).sameRow(),
        }, std::move(st));
    }

    // Game over: the score, a new-best badge, and three facts about the game.
    void DrawGameOverHeader(TextRenderer& t, const ui::Rect& r, float u, float alpha) {
        FaceScope f(t, TextRenderer::Display);
        const bool newBest = total_points > bestAtStart && total_points > 0;
        const float cx = r.x + r.w * 0.5f;
        float y = r.y;
        ui::textCentered(t, "SCORE", cx, y, 0.52f * u, ui::kClayDeep, alpha);
        y += t.CapHeight(0.52f * u) + 12.0f * u;
        // The number counts up as the card opens.
        float count = ui::easeOutCubic(alpha);
        std::string score = ui::thousands((long long)std::lround(total_points * count));
        float ss = 3.0f * u;
        ui::textCentered(t, score, cx, y, ss, ui::kInk, alpha);
        y += t.CapHeight(ss) + 14.0f * u;
        {   // new best badge, or the best to beat
            std::string b = newBest ? "New best!" : high_score > 0 ? "Best  " + ui::thousands(high_score) : "First game";
            float bs = 0.56f * u, bh = 30.0f * u;
            float iconW = newBest ? bh * 0.8f : 0.0f;
            float bw = t.GetTextWidth(b, bs) + iconW + 28.0f * u;
            float pop = newBest ? ui::easeOutBack((alpha - 0.5f) * 2.0f) : 1.0f;
            ui::Rect br{cx - bw * 0.5f * pop, y, bw * pop, bh};
            if (newBest) {
                t.Shape(UiShape::rect(br.x, br.y + 3.0f * u, br.w, br.h, bh * 0.5f).fill(ui::kGoldFoot, alpha));
                t.Shape(UiShape::rect(br.x, br.y, br.w, br.h, bh * 0.5f).grad(ui::kGoldTop, ui::kGoldBottom, alpha));
                ui::icon(t, ui::Icon::Star, {br.x + 14.0f * u + iconW * 0.4f, br.y + bh * 0.5f}, bh * 0.75f, ui::kOnGold, alpha * pop);
            } else {
                t.Shape(UiShape::rect(br.x, br.y, br.w, br.h, bh * 0.5f).fill(ui::kGlaze2, alpha));
                t.Shape(UiShape::rect(br.x, br.y, br.w, br.h, bh * 0.5f).ring(std::max(1.0f, 1.2f * u)).fill(ui::kEdge, 0.4f * alpha));
            }
            t.RenderText(b, br.x + 14.0f * u + iconW, br.y + (bh - t.CapHeight(bs)) * 0.5f, bs, newBest ? ui::kOnGold : ui::kInk, alpha * pop);
            y += bh + 22.0f * u;
        }
        // Stats: biggest fruit (drawn), merges, best combo.
        const float col = r.w / 3.0f;
        const float vs = 1.1f * u, ls = 0.5f * u;
        auto label = [&](int i, const std::string& l) {
            FaceScope fb(t, TextRenderer::Body);
            ui::textCentered(t, l, r.x + col * (i + 0.5f), y + 6.0f * u + t.CapHeight(vs) + 16.0f * u, ls, ui::kClayDeep, alpha);
        };
        float vy = y + 6.0f * u;
        {
            glm::vec2 c(r.x + col * 0.5f, vy + t.CapHeight(vs) * 0.5f);
            if (statBiggest >= 0) {
                float fr = t.CapHeight(vs) * 0.75f;
                t.Shape(UiShape::circle(c + glm::vec2(0.0f, 2.0f * u), fr).fill(ui::kShadow, 0.25f * alpha).soft(4.0f * u));
                t.Shape(UiShape::circle(c, fr).grad(glm::mix(FruitUiColor(statBiggest), glm::vec3(1.0f), 0.25f), FruitUiColor(statBiggest), alpha));
                t.Shape(UiShape::circle(c + glm::vec2(-0.3f, -0.35f) * fr, fr * 0.28f).fill(glm::vec3(1.0f), 0.55f * alpha));
            } else {
                ui::textCentered(t, "-", c.x, vy, vs, ui::kInk, alpha);
            }
            label(0, statBiggest >= 0 ? kFruitNames[statBiggest] : "biggest");
        }
        ui::textCentered(t, std::to_string(statMerges), r.x + col * 1.5f, vy, vs, ui::kInk, alpha);
        label(1, "merges");
        ui::textCentered(t, statBestCombo >= 2 ? "x" + std::to_string(statBestCombo) : "-", r.x + col * 2.5f, vy, vs, ui::kInk, alpha);
        label(2, "best combo");
    }

    void BuildGameOverMenu() {
        MenuStyle st;
        st.title = "Bowl overflowed!";
        st.width = 420.0f;
        st.keys = KeyHints({{"R", "play again"}, {"Enter", "select"}});
        st.headerHeight = 214.0f;
        st.header = [this](TextRenderer& t, const ui::Rect& r, float u, float a) { DrawGameOverHeader(t, r, u, a); };
        menu.set({
            MenuItem::button("Play again", [this] { Reset(); }).withIcon(ui::Icon::Restart).asPrimary().withKey("R"),
            MenuItem::button("Menu", [this] { State = GAME_MENU; }).withIcon(ui::Icon::Home),
            MenuItem::button("Settings", [this] { Open(Page::Settings); }).withIcon(ui::Icon::Gear).sameRow(),
        }, std::move(st));
    }

    std::string ResolutionLabel() const {
        float s = std::min(renderer.renderScale, renderer.maxScale(FbWidth, FbHeight));
        int h = std::max(1, int(FbHeight * s + 0.5f));
        std::string mode = scaleChoice == 0 ? std::string("Auto") : percent(kScaleChoices[scaleChoice]);
        return mode + "  " + std::to_string(h) + "p";
    }

    void BuildSettingsMenu() {
        MenuStyle st;
        st.title = "Settings";
        st.width = 600.0f;
        st.keys = KeyHints({{"Esc", "back"}, {"Enter", "change"}});
        std::vector<MenuItem> items;
        auto onOff = [](bool v) { return std::string(v ? "On" : "Off"); };
        items.push_back(MenuItem::header("Picture"));
        items.push_back(MenuItem::choice("Quality", [this] { return std::string(qualityName(renderer.quality())); },
            [this](int d) { renderer.setQuality(Quality((int(renderer.quality()) + d + 4) % 4)); ApplyRenderScale(); }));
        items.push_back(MenuItem::choice("Resolution", [this] { return ResolutionLabel(); },
            [this](int d) { CycleIndex(scaleChoice, d, kScaleCount); ApplyRenderScale(); }));
        items.push_back(MenuItem::choice("Merge effects", [=] { return onOff(effectsOn); }, [this](int) { effectsOn = !effectsOn; }));
        items.push_back(MenuItem::choice("Same-fruit glow", [=] { return onOff(renderer.sameKindGlow); },
            [this](int) { renderer.sameKindGlow = !renderer.sameKindGlow; }));
        items.push_back(MenuItem::header("4D"));
        items.push_back(MenuItem::choice("View", [this] { return ViewLabel(); }, [this](int d) { CycleViewMode(d); }));
        items.push_back(MenuItem::choice("W track", [=] { return onOff(showDepthStrip); }, [this](int) { showDepthStrip = !showDepthStrip; }));
        items.push_back(MenuItem::choice("Snap drops to w lanes", [=] { return onOff(laneSnap); }, [this](int) { laneSnap = !laneSnap; }));
        items.push_back(MenuItem::header("Sound"));
        items.push_back(MenuItem::choice("Sound", [this] { return std::string(soundOn ? "On" : "Muted"); },
            [this](int) { soundOn = !soundOn; ApplyVolumeSettings(); }));
        items.push_back(MenuItem::slider("Music", [this] { return musicVolume; }, [this](float v) { musicVolume = v; ApplyVolumeSettings(); }));
        items.push_back(MenuItem::slider("Sound effects", [this] { return sfxVolume; }, [this](float v) { sfxVolume = v; ApplyVolumeSettings(); }));
        items.push_back(MenuItem::button("Back", [this] { ClosePage(); }).withIcon(ui::Icon::Back));
        items.push_back(MenuItem::button("Controls", [this] { Open(Page::Controls); }).withIcon(ui::Icon::Keys).sameRow());
        menu.set(std::move(items), std::move(st));
    }

    // Every binding, grouped, keys drawn as keycaps, two per line.
    std::vector<MenuItem> ControlsItems() const {
        std::vector<MenuItem> touch = {
            MenuItem::header("Touch"),
            MenuItem::info("Drag", "aim the drop"),
            MenuItem::info("Lift", "drop the fruit").sameRow(),
            MenuItem::info("2 fingers", "drag to orbit the camera"),
            MenuItem::info("Pinch", "zoom in and out").sameRow(),
            MenuItem::info("Twist", "turn the slice through w"),
            MenuItem::info("3 fingers", "drag to turn freely into 4D").sameRow(),
        };
        std::vector<MenuItem> keys = {
            MenuItem::header("Play"),
            MenuItem::info("Click", "drop the fruit"),
            MenuItem::info("Right drag", "orbit the camera").sameRow(),
            MenuItem::info("Shift / Wheel", "zoom"),
            MenuItem::info("Esc", "pause").sameRow(),
            MenuItem::header("Turn into 4D"),
            MenuItem::info("Wheel", "turn the slice through w"),
            MenuItem::info("Middle drag", "turn freely into 4D").sameRow(),
            MenuItem::info("A / D", "turn left-right into w"),
            MenuItem::info("R / F", "turn up-down into w").sameRow(),
            MenuItem::info("Q / E", "turn depth into w"),
            MenuItem::info("W / S", "slide along w").sameRow(),
            MenuItem::info("Z / C", "roll"),
            MenuItem::info("H / Space", "back to the start slice").sameRow(),
            MenuItem::header("View and extras"),
            MenuItem::info("M", "Slice or 4D eye view"),
            MenuItem::info("V", "show or hide the w track").sameRow(),
            MenuItem::info("L", "snap drops to w lanes"),
            MenuItem::info("P", "photo mode and replay").sameRow(),
            MenuItem::info("G", "cycle quality"),
            MenuItem::info("F3", "debug info").sameRow(),
            MenuItem::header("What you see"),
            MenuItem::info("", "solid outline: your next fruit").withSwatch(1),
            MenuItem::info("", "dots: a fruit just off your slice").withSwatch(2).sameRow(),
            MenuItem::info("", "band: a fruit your slice cuts").withSwatch(3),
            MenuItem::info("", "band colour: -w, on slice, +w").withSwatch(4).sameRow(),
        };
        if (!touchUI) return keys;
        // A portrait phone has room for one column and no keyboard: show only
        // the touch gestures and the legend.
        if (Width < Height) keys.erase(keys.begin(), keys.end() - 5);
        keys.insert(keys.begin(), touch.begin(), touch.end());
        return keys;
    }

    void BuildControlsMenu() {
        MenuStyle st;
        st.title = "Controls";
        st.width = 860.0f;
        st.keyColumn = 172.0f;
        st.keys = KeyHints({{"Esc", "back"}});
        std::vector<MenuItem> items = ControlsItems();
        if (Width < Height) { // portrait: one column, so the text stays readable
            st.width = 380.0f;
            st.keyColumn = 130.0f;
            for (auto& m : items) m.joinRow = false;
        }
        items.push_back(MenuItem::button("Back", [this] { ClosePage(); }).withIcon(ui::Icon::Back));
        menu.set(std::move(items), std::move(st));
        menu.focus = int(menu.itemCount()) - 1;
    }

    float dimShown = 0.0f;            // the menu backdrop dim, eased
    float lastMenuTime = -1.0f;
    void RenderMenuScreen(const Camera4D& cam, float dim) {
        EnsureMenu();
        float dt = lastMenuTime < 0.0f ? 1.0f : std::clamp(time - lastMenuTime, 0.0f, 0.1f);
        lastMenuTime = time;
        ui::approach(dimShown, dim, dt, 6.0f);
        renderer.dim = dimShown;
        RenderScene({}, cam);
        renderer.dim = 0.0f;
        if (hideUI) return;
        BeginText();
        menu.draw(*t_rend, float(Width), float(Height), dt);
    }
};
