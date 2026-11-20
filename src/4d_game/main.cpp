/*******************************************************************
** This code is part of game.
**
** game is free software: you can redistribute it and/or modify
** it under the terms of the CC BY 4.0 license as published by
** Creative Commons, either version 4 of the License, or (at your
** option) any later version.
******************************************************************/
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#ifndef GLFW_FOCUS_ON_SHOW
#define GLFW_FOCUS_ON_SHOW 0x0002000C
#endif

#ifndef __EMSCRIPTEN__
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>
#else
#include <emscripten/emscripten.h>
#endif

#include "game.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "camera_path.hpp"
#include "recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

const float TARGET_FPS = 60.0f;
const float FRAME_TIME = 1.0f / TARGET_FPS;
const float FIXED_TIMESTEP = 1.0f / 60.0f;

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void windowSizeCallback(GLFWwindow* window, int windowWidth, int windowHeight);

const unsigned int SCREEN_WIDTH = 1280;
const unsigned int SCREEN_HEIGHT = 800;

Game game(SCREEN_WIDTH, SCREEN_HEIGHT);

// Command line. With --screenshot or --bench the game runs headless: a hidden
// window that never takes focus, rendering into an offscreen framebuffer.
//   --headless            render hidden/offscreen, optionally without capture
//   --screenshot out.png   render, save a PNG, exit
//   --bench N              render N frames as fast as possible, print timings
//   --mute                do not initialize, load, or play audio
//   --scripted-playtest   headless demo sequence for screenshot/playtest jobs
//   --playtest-seconds s   run deterministic drops/rotation for s simulated seconds
//   --pose-at t            camera pose of the scripted playtest at time t (seconds)
//   --playtest-render dir  also render every 10th frame (timed) and save a frame
//                          to dir every 30 simulated seconds
//   --size WxH             offscreen size in pixels (default 1920x1080)
//   --state game|menu|pause|settings|controls|over   (menu = start screen)
//   --focus N              focused menu row (for screenshots of the focus state)
//   --turn xw:30,yw:0,zw:0,xy:0   4D turns in degrees, camera-local planes
//   --orbit yaw,pitch      degrees
//   --distance d           camera distance from the bowl (default 9.2; 34 frames the island)
//   --offset v             slice offset along its normal
//   --demo N               drop N fruit and let them settle first
//   --frames N             frames simulated before the capture (default 90, more with --demo)
//   --scale s              render scale (fraction of output resolution)
//   --facet f              low-poly facet strength (0 = smooth)
//   --quality q            low | medium | high | ultra (default high)
//   --adaptive             keep adaptive resolution on (otherwise fixed --scale)
//   --perf                 show the F3 performance overlay
//   --no-cache             march the analytic world SDF (no slice cache), to compare
//   --single-pass          scene in one pass (no deferred half-resolution lighting), to compare
//   --bench-turn deg       with --bench: turn xw by deg every frame (rebakes the cache)
//   --cost-map out.png     analytic SDF evaluations per pixel instead of colour (R static
//                          world, G moving world, B fruit) with statistics; deterministic,
//                          unlike timings on a shared GPU
//   --view mode            slice (default), thick or eye (docs/4D.md, "View modes")
//   --band b               thick slice: how far off the slice fruit still show (default 1)
//   --view-compare dir     every view and combination (slice, thick slice at bands 0.5,
//                          1, 2, 3, 4D eye; each alone and with the w inset) on the same
//                          scripted playtest: dir/view-<v>.png (early, mid, next merge),
//                          all in dir/compare.png, and a 10 s clip of play turning in 4D
//                          (frames in dir/view-<v>/; `make view-compare` adds the videos)
//   --danger v             hold the danger level at v (0..1), for review stills
//   --tutorial N           start the tutorial at step N (0 aim, 1 merge, 2 turn, 3 return), for stills
//   --no-fx                no merge bursts, combo or danger (Settings > Effects)
//   --no-glow              no same-kind glow (Settings > Merge glow)
//   --lane-snap            drops snap to whole w lanes (L in game)
//   --cursor x,y           mouse position in pixels (for the drop preview)
//   --fruit-sheet out.png  all 11 fruit in a labelled row, at tilt 0 and after
//                          --sheet-turn (default yw:35), stacked in one image
//   --record dir           dump every frame as dir/frame_000000.png at a fixed
//                          timestep (headless with --headless, else live; F9
//                          toggles recording in a live game, see docs/recording.md)
//   --fps n                recording frame rate (default 60)
//   --seconds s            recording length when headless (default: the camera
//                          path's duration, else --frames)
//   --camera-path file     scripted camera keyframes (see camera_path.hpp)
//   --play s               with --record: a scripted player drops the next fruit into
//                          the bowl every s seconds, through the cursor like a player
//   --warmup s             with --play: play s seconds off camera first, so the bowl
//                          is busy when the recording starts
//   --turntable dir        4D review turntable: each subject alone and centred while
//                          the slice turns 0 to 180 deg through xw, then yw, then zw;
//                          frames in dir/<subject>/ and dir/<subject>_sheet.png
//                          (see docs/recording.md; `make turntable` adds the videos)
//   --subject list         turntable subjects: all (default), fruit, bowl, or fruit
//                          names, comma separated
//   --sweep s              turntable seconds per 180 deg turn (default 4)
struct Options {
    std::string screenshot;
    int bench = 0;
    int width = 1920, height = 1080;
    std::string state = "game";
    int focus = -1;
    std::string turn;
    float yaw = 0.0f, pitch = glm::degrees(Camera4D::kDefaultPitch);
    float offset = 0.0f;
    float distance = Camera4D::kDefaultDistance;
    int demo = 0;
    int frames = 90;
    float scale = 1.0f;
    float facet = -1.0f;
    std::string quality = "high";
    bool adaptive = false;
    bool perf = false;
    bool laneSnap = false;
    bool noGlow = false;
    bool noFx = false;
    int tutorial = -1;
    float danger = -1.0f;
    bool noCache = false;
    bool singlePass = false;
    float benchTurn = 0.0f;
    std::string costMap;
    float cursorX = -1.0f, cursorY = -1.0f;
    std::string record;
    float playEvery = 0.0f, warmup = 0.0f;
    std::string cameraPath;
    int fps = 60;
    float seconds = 0.0f;
    float playtestSeconds = 0.0f;
    std::string playtestRender;
    float poseAt = -1.0f;
    bool forceHeadless = false;
    bool mute = false;
    std::string fruitSheet;
    std::string sheetTurn = "yw:35";
    std::string view = "slice";
    float band = 1.0f;
    std::string viewCompare;
    std::string turntable;
    std::string subject = "all";
    float sweep = 4.0f;
    bool headless() const {
        return forceHeadless || !screenshot.empty() || bench > 0 || !fruitSheet.empty() || !costMap.empty() ||
               playtestSeconds > 0.0f || !turntable.empty() || !viewCompare.empty();
    }
    bool muted() const { return mute || headless(); }
};

static Options parseOptions(int argc, char* argv[]) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--headless") o.forceHeadless = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--scripted-playtest") {
            o.forceHeadless = true;
            o.state = "game";
            o.demo = std::max(o.demo, 8);
            o.frames = std::max(o.frames, 160);
        }
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--bench") o.bench = std::atoi(next().c_str());
        else if (a == "--size") sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--state") o.state = next();
        else if (a == "--focus") o.focus = std::atoi(next().c_str());
        else if (a == "--turn") o.turn = next();
        else if (a == "--orbit") sscanf(next().c_str(), "%f,%f", &o.yaw, &o.pitch);
        else if (a == "--distance") o.distance = std::atof(next().c_str());
        else if (a == "--offset") o.offset = std::atof(next().c_str());
        else if (a == "--demo") o.demo = std::atoi(next().c_str());
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--scale") o.scale = std::atof(next().c_str());
        else if (a == "--facet") o.facet = std::atof(next().c_str());
        else if (a == "--quality") o.quality = next();
        else if (a == "--adaptive") o.adaptive = true;
        else if (a == "--perf") o.perf = true;
        else if (a == "--lane-snap") o.laneSnap = true;
        else if (a == "--no-glow") o.noGlow = true;
        else if (a == "--no-fx") o.noFx = true;
        else if (a == "--tutorial") o.tutorial = std::atoi(next().c_str());
        else if (a == "--danger") o.danger = float(std::atof(next().c_str()));
        else if (a == "--no-cache") o.noCache = true;
        else if (a == "--single-pass") o.singlePass = true;
        else if (a == "--bench-turn") o.benchTurn = std::atof(next().c_str());
        else if (a == "--cost-map") o.costMap = next();
        else if (a == "--cursor") sscanf(next().c_str(), "%f,%f", &o.cursorX, &o.cursorY);
        else if (a == "--fruit-sheet") o.fruitSheet = next();
        else if (a == "--sheet-turn") o.sheetTurn = next();
        else if (a == "--record") o.record = next();
        else if (a == "--play") o.playEvery = std::atof(next().c_str());
        else if (a == "--warmup") o.warmup = std::atof(next().c_str());
        else if (a == "--turntable") o.turntable = next();
        else if (a == "--view-compare") o.viewCompare = next();
        else if (a == "--view") {
            o.view = next();
            if (o.view != "slice" && o.view != "thick" && o.view != "eye") {
                std::cerr << "--view: slice, thick or eye" << std::endl;
                std::exit(2);
            }
        }
        else if (a == "--band") o.band = float(std::atof(next().c_str()));
        else if (a == "--subject") o.subject = next();
        else if (a == "--sweep") o.sweep = std::max(0.1f, float(std::atof(next().c_str())));
        else if (a == "--fps") o.fps = std::max(1, std::atoi(next().c_str()));
        else if (a == "--seconds") o.seconds = std::atof(next().c_str());
        else if (a == "--camera-path") o.cameraPath = next();
        else if (a == "--playtest-render") o.playtestRender = next();
        else if (a == "--pose-at") o.poseAt = std::atof(next().c_str());
        else if (a == "--playtest-seconds") o.playtestSeconds = std::max(0.0f, float(std::atof(next().c_str())));
        else {
            // Fail loudly: a typo must never fall through to a visible, audible launch.
            std::cerr << "Unknown option " << a << std::endl;
            std::exit(2);
        }
    }
    return o;
}

static void applyTurns(const std::string& turns, Camera4D& cam);

static void applyCameraOptions(const Options& o, Camera4D& cam) {
    cam.yaw = glm::radians(o.yaw);
    cam.pitch = glm::radians(o.pitch);
    cam.offset = o.offset;
    cam.distance = o.distance;
    applyTurns(o.turn, cam);
}

static void applyTurns(const std::string& turns, Camera4D& cam) {
    std::stringstream ss(turns);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.size() < 4 || item[2] != ':') continue;
        auto axis = [](char c) { return c == 'x' ? AX_X : c == 'y' ? AX_Y : c == 'z' ? AX_Z : AX_W; };
        cam.turnLocal(axis(item[0]), axis(item[1]), glm::radians(float(std::atof(item.c_str() + 3))));
    }
}

// All 11 fruit in a row along x, high above the island against the sky. The
// row lies on the camera's x axis, so a camera-local yw or zw turn still cuts
// every fruit through its centre.
static void setupFruitSheet() {
    game.sheetMode = true;
    game.sheetFruit.clear();
    const float gap = 0.5f, y = 12.0f;
    float width = -gap;
    for (int t = 0; t <= int(WATERMELON); ++t) width += 2.0f * FruitManager::getFruitProperties(Fruit(t)).radius + gap;
    float x = -width * 0.5f;
    for (int t = 0; t <= int(WATERMELON); ++t) {
        float r = FruitManager::getFruitProperties(Fruit(t)).radius;
        game.sheetFruit.push_back({glm::vec4(x + r, y, 0.0f, 0.0f), r, t, 0.0f, t});
        x += 2.0f * r + gap;
    }
    Camera4D& cam = game.state.cam;
    cam = Camera4D{};
    cam.target = glm::vec4(0.0f, y, 0.0f, 0.0f);
    cam.pitch = 0.0f;
    cam.fovY = glm::radians(16.0f); // narrow lens: no stretching at the row's ends
    float aspect = float(game.FbWidth) / float(game.FbHeight);
    cam.distance = (width * 0.5f + 1.0f) / (std::tan(cam.fovY * 0.5f) * aspect) + 2.0f;
}

// Demo fruit scattered through 4D so screenshots show glomes crossing the
// slice; one is dropped every few frames, like a player would.
static void dropDemoFruit(int index) {
    uint32_t seed = 7u + 977u * uint32_t(index);
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / float(1 << 24); };
    glm::vec4 p(rnd() * 2.4f - 1.2f, 1.5f, rnd() * 2.4f - 1.2f, rnd() * 2.0f - 1.0f);
    game.physics_solver->addObject(PhysicsObject(p, Fruit(int(rnd() * 6.0f)), true, false));
}

// Run physics in 1/60 s steps for a frame of length dt (any frame rate).
static void advanceSim(float dt, float& acc) {
    acc += dt;
    while (acc >= FIXED_TIMESTEP - 1e-6f) {
        game.FixedUpdate(FIXED_TIMESTEP);
        acc -= FIXED_TIMESTEP;
    }
}

static int activeObjectCount() {
    int n = 0;
    for (bool has : game.physics_solver->has_obj) {
        if (has) ++n;
    }
    return n;
}

static float highestObjectY() {
    float y = -1000.0f;
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (game.physics_solver->has_obj[i]) y = std::max(y, game.physics_solver->objects[i].position.y);
    }
    return y;
}

static void setScriptedPlaytestPose(float t) {
    Camera4D cam;
    cam.yaw = 0.45f * std::sin(t * 0.19f) + t * 0.10f;
    cam.pitch = Camera4D::kDefaultPitch + 0.18f * std::sin(t * 0.13f);
    cam.distance = 12.0f + 2.0f * std::sin(t * 0.07f);
    cam.offset = 1.4f * std::sin(t * 0.11f);
    cam.turnLocal(AX_X, AX_W, 0.95f * std::sin(t * 0.17f));
    cam.turnLocal(AX_Y, AX_W, 0.65f * std::sin(t * 0.09f + 0.7f));
    cam.turnLocal(AX_Z, AX_W, 0.75f * std::sin(t * 0.12f + 1.3f));
    game.state.cam = cam;
}

// Point the cursor at a spot inside the bowl, in the current slice, the way a
// player aims: within 1.3 units of the bowl's axis, just below its rim.
static void aimIntoBowl(int dropIndex, int w, int h) {
    uint32_t seed = 11u + 7919u * uint32_t(dropIndex);
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / float(1 << 24); };
    const Camera4D& cam = game.state.cam;
    glm::mat4 R = cam.basis();
    float a = rnd() * 6.2831853f, r = 1.3f * std::sqrt(rnd());
    glm::vec4 p = cam.target + cam.offset * R[3] + r * (std::cos(a) * R[0] + std::sin(a) * R[2]);
    p.y = -0.3f;   // just under the rim, so the near rim never hides the spot
    glm::vec2 ndc(0.0f, -0.2f);
    game.state.cam.project(p, float(w) / float(std::max(h, 1)), ndc);
    game.state.m_xpos = glm::clamp(ndc.x * 0.5f + 0.5f, 0.05f, 0.95f) * float(w);
    game.state.m_ypos = glm::clamp(0.5f - ndc.y * 0.5f, 0.05f, 0.95f) * float(h);
}

static void setScriptedPlaytestCursor(int dropIndex, int w, int h) {
    // Aim from the visible bowl interior, then sweep locally. Fixed screen
    // coordinates can point at sky once the 4D slice rotates.
    float x = 0.50f, y = 0.55f;
    glm::vec2 ndc;
    float aspect = float(w) / float(std::max(h, 1));
    if (game.state.cam.project(glm::vec4(0.0f, -2.4f, 0.0f, 0.0f), aspect, ndc)) {
        x = ndc.x * 0.5f + 0.5f;
        y = 0.5f - ndc.y * 0.5f;
    }
    x += 0.12f * std::sin(float(dropIndex) * 2.399f);
    y += 0.08f * std::cos(float(dropIndex) * 1.731f);
    game.state.m_xpos = glm::clamp(x, 0.12f, 0.88f) * float(w);
    game.state.m_ypos = glm::clamp(y, 0.18f, 0.86f) * float(h);
}

static bool savePng(const std::string& path, int w, int h);

static int runScriptedPlaytest(const Options& o, GLuint fbo) {
    const bool render = !o.playtestRender.empty();
    std::vector<float> renderMs;
    std::vector<int> renderObjects;
    std::FILE* times = nullptr; // per rendered frame: time, ms, objects, tilt, bake ms
    std::srand(20261117);
    game.State = GAME_ACTIVE;
    game.page = Game::Page::None;
    game.menuBuiltFor = -1;
    game.nextFruit = FruitManager::getRandomFruit();

    const int totalFrames = std::max(1, int(std::ceil(o.playtestSeconds / FIXED_TIMESTEP)));
    const int dropEvery = 45; // 0.75 s at 60 Hz: faster than a cautious player, slower than spam.
    int attempts = 0, hit = 0, placed = 0, full = 0, losses = 0, merges = 0, maxObjects = 0;
    float maxY = -1000.0f, maxTilt = 0.0f;

    for (int frame = 0; frame < totalFrames; ++frame) {
        float t = frame * FIXED_TIMESTEP;
        setScriptedPlaytestPose(t);

        if (frame % dropEvery == 0) {
            setScriptedPlaytestCursor(attempts, o.width, o.height);
            int before = activeObjectCount();
            bool didHit = game.DropFruit();
            int after = activeObjectCount();
            ++attempts;
            if (didHit) ++hit;
            if (after > before) ++placed;
            else if (didHit) ++full;
        }

        game.FixedUpdate(FIXED_TIMESTEP);
        merges += game.physics_solver->just_merged.load();
        game.Update(FIXED_TIMESTEP);

        maxObjects = std::max(maxObjects, activeObjectCount());
        maxY = std::max(maxY, highestObjectY());
        maxTilt = std::max(maxTilt, game.state.cam.tilt());

        if (render && frame % 10 == 0) {
            auto t0 = std::chrono::steady_clock::now();
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, o.width, o.height);
            game.Render();
            glFinish();
            renderMs.push_back(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
            renderObjects.push_back(activeObjectCount());
            if (!times) times = std::fopen((o.playtestRender + "/times.csv").c_str(), "w");
            if (times) std::fprintf(times, "%.2f,%.2f,%d,%.1f,%.2f\n", t, renderMs.back(), renderObjects.back(),
                                    glm::degrees(game.state.cam.tilt()), game.renderer.bakeMs());
            if (frame % 1800 == 0) {
                char name[64];
                std::snprintf(name, sizeof name, "/t%04d.png", int(t + 0.5f));
                savePng(o.playtestRender + name, o.width, o.height);
            }
        }

        if (game.State == GAME_OVER) {
            ++losses;
            game.Reset();
        }
    }
    if (times) std::fclose(times);
    if (render && !renderMs.empty()) {
        std::vector<float> sorted = renderMs;
        std::sort(sorted.begin(), sorted.end());
        float worst = sorted.back();
        int worstObjects = renderObjects[std::max_element(renderMs.begin(), renderMs.end()) - renderMs.begin()];
        printf("playtest render %dx%d: %zu frames, median %.2f ms, p95 %.2f ms, p99 %.2f ms, max %.2f ms (%d objects)\n",
               o.width, o.height, sorted.size(), sorted[sorted.size() / 2], sorted[sorted.size() * 95 / 100],
               sorted[sorted.size() * 99 / 100], worst, worstObjects);
    }

    printf("scripted playtest %.1fs: frames %d, attempts %d, hit %d, placed %d, missed %d, full %d, merges %d, losses %d, max objects %d, score %d, high score %d, max y %.2f, max tilt %.1f deg\n",
           o.playtestSeconds, totalFrames, attempts, hit, placed, attempts - hit, full, merges, losses,
           maxObjects, game.total_points, game.high_score, maxY, glm::degrees(maxTilt));
    return placed > 0 ? 0 : 1;
}

// Writes the bound read framebuffer (w x h, RGBA8) as a top-down PNG.
static bool savePng(const std::string& path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    size_t row = size_t(w) * 4;
    for (int y = 0; y < h / 2; ++y)
        std::swap_ranges(px.begin() + y * row, px.begin() + (y + 1) * row, px.begin() + (h - 1 - y) * row);
    return stbi_write_png(path.c_str(), w, h, 4, px.data(), int(row)) != 0;
}

#ifndef __EMSCRIPTEN__
// Review turntable (docs/recording.md): one subject alone and centred, the
// slice turning 0 to 180 deg through xw, then yw, then zw (planes of the
// default slice's axes), pivoting on the subject's 4D centre so every slice
// cuts it through the middle. Writes the frames of a video per subject and a
// contact sheet: one row per plane, a column every 30 deg.
static int runTurntable(const Options& o, GLuint fbo) {
    std::vector<int> subjects; // fruit type, or -1 for the bowl
    std::stringstream ss(o.subject);
    for (std::string item; std::getline(ss, item, ',');) {
        if (item == "all" || item == "fruit")
            for (int t = 0; t <= int(WATERMELON); ++t) subjects.push_back(t);
        if (item == "all" || item == "bowl") subjects.push_back(-1);
        for (int t = 0; t <= int(WATERMELON); ++t)
            if (item == kFruitNames[t]) subjects.push_back(t);
        if (item != "all" && item != "fruit" && item != "bowl" &&
            std::none_of(std::begin(kFruitNames), std::end(kFruitNames), [&](const char* n) { return item == n; })) {
            std::cerr << "--subject: unknown subject " << item << std::endl;
            return 2;
        }
    }
    const Axis4 planes[] = {AX_X, AX_Y, AX_Z};
    const char* planeNames[] = {"xw", "yw", "zw"};
    const int holdFrames = int(std::lround(0.5f * o.fps));
    const int sweepFrames = std::max(1, int(std::lround(o.sweep * o.fps)));
    const int columns = 7, down = 3; // sheet: 0, 30, ..., 180 deg; tiles at a third of the frame size
    const int tw = o.width / down, th = o.height / down;
    const float aspect = float(o.width) / float(o.height);

    game.State = GAME_ACTIVE;
    game.sheetMode = true;
    game.sheetNames = false;
    game.sheetTitleScale = 1.1f * float(o.height) / 720.0f; // readable on the sheet at a third of the size
    int mode = -1;
    for (int subject : subjects) {
        const bool bowl = subject < 0;
        const std::string name = bowl ? "bowl" : kFruitNames[subject];
        if (mode != int(bowl)) { // the shaders keep only the subject
            mode = int(bowl);
            game.renderer.extraDefines = bowl ? "#define TURNTABLE 2\n" : "#define TURNTABLE 1\n";
            game.renderer.setQuality(game.renderer.quality());
            game.renderer.renderScale = o.scale * game.renderer.maxScale(o.width, o.height);
        }
        // The fruit floats where the sky is empty; the bowl turns about its
        // glome's centre (the origin, at the rim) and is framed 1.5 lower.
        glm::vec4 centre = bowl ? glm::vec4(0.0f) : glm::vec4(0.0f, 12.0f, 0.0f, 0.0f);
        float frameDown = bowl ? 1.5f : 0.0f;
        float extent = bowl ? 3.3f : FruitManager::getFruitProperties(Fruit(subject)).radius * 1.3f;
        game.sheetFruit.clear();
        if (!bowl) game.sheetFruit.push_back({centre, FruitManager::getFruitProperties(Fruit(subject)).radius, subject, 0.0f, 1 + subject});
        Camera4D base;
        base.yaw = glm::radians(25.0f);
        base.pitch = glm::radians(bowl ? -28.0f : -18.0f);
        base.fovY = glm::radians(30.0f);
        base.distance = extent / (0.62f * std::tan(base.fovY * 0.5f) * std::min(1.0f, aspect));

        auto pose = [&](int plane, float deg) {
            Camera4D& cam = game.state.cam;
            cam = base;
            cam.slice = Rot4::plane(planes[plane], AX_W, glm::radians(deg));
            cam.target = centre + cam.slice.apply(glm::vec4(0.0f, -frameDown, 0.0f, 0.0f)); // stays in the slice
            char buf[96];
            snprintf(buf, sizeof buf, "%s    slice %s %3.0f deg", name.c_str(), planeNames[plane], deg);
            game.sheetTitle = buf;
        };
        auto render = [&]() {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, o.width, o.height);
            glClearColor(0, 0, 0, 1);
            game.Render();
        };

        Recorder rec;
        rec.fps = o.fps;
        if (!rec.start(o.turntable + "/" + name)) return 1;
        for (int plane = 0; plane < 3; ++plane) {
            int total = holdFrames + sweepFrames + holdFrames;
            for (int i = 0; i <= total; ++i) {
                float k = glm::clamp(float(i - holdFrames) / float(sweepFrames), 0.0f, 1.0f);
                pose(plane, 180.0f * k);
                render();
                rec.capture(o.width, o.height);
            }
        }
        rec.stop();

        // Contact sheet, top-down rows; each tile a box-filtered frame.
        std::vector<unsigned char> sheet(size_t(tw) * columns * th * 3 * 4), px(size_t(o.width) * o.height * 4);
        for (int plane = 0; plane < 3; ++plane) {
            for (int c = 0; c < columns; ++c) {
                pose(plane, 180.0f * c / (columns - 1));
                render();
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, o.width, o.height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                for (int y = 0; y < th; ++y) {
                    for (int x = 0; x < tw; ++x) {
                        int sum[4] = {};
                        for (int dy = 0; dy < down; ++dy)
                            for (int dx = 0; dx < down; ++dx) {
                                const unsigned char* s = &px[(size_t(o.height - 1 - (y * down + dy)) * o.width + x * down + dx) * 4];
                                for (int ch = 0; ch < 4; ++ch) sum[ch] += s[ch];
                            }
                        unsigned char* d = &sheet[(size_t(plane * th + y) * tw * columns + c * tw + x) * 4];
                        for (int ch = 0; ch < 4; ++ch) d[ch] = (unsigned char)(sum[ch] / (down * down));
                    }
                }
            }
        }
        std::string sheetPath = o.turntable + "/" + name + "_sheet.png";
        if (!stbi_write_png(sheetPath.c_str(), tw * columns, th * 3, 4, sheet.data(), tw * columns * 4)) {
            std::cerr << "Could not write " << sheetPath << std::endl;
            return 1;
        }
        std::cout << "wrote " << sheetPath << std::endl;
    }
    return 0;
}

// View-mode comparison (docs/4D.md, "View modes"): every view and every useful
// combination with game's w-depth strip, on the same scripted-playtest run.
// One simulation drives all views at once, so each view sees exactly the same
// game: a still per view of three moments (early, mid, the next merge), all of
// them in compare.png, and a 10 s clip per view of real play around the mid
// moment with the slice swinging through xw and yw on top of the scripted pose.
static int runViewCompare(const Options& o, GLuint fbo) {
    struct View { std::string file, name; ViewMode mode; float band; bool inset; };
    std::vector<View> views;
    for (bool inset : {false, true}) {
        const char* plus = inset ? "+inset" : "";
        const char* with = inset ? " + w inset" : "";
        views.push_back({std::string("slice") + plus, std::string("slice") + with, ViewMode::Slice, 1.0f, inset});
        for (float b : {0.5f, 1.0f, 2.0f, 3.0f}) {
            char file[32], name[64];
            snprintf(file, sizeof file, "thick-b%g%s", b, plus);
            snprintf(name, sizeof name, "thick slice, band +-%g%s", b, with);
            views.push_back({file, name, ViewMode::Thick, b, inset});
        }
        views.push_back({std::string("eye") + plus, std::string("4D eye") + with, ViewMode::Eye, 1.0f, inset});
    }
    const int nViews = int(views.size()), down = 2, nMoments = 3;
    const int tw = o.width / down, th = o.height / down;
    std::error_code ec;
    std::filesystem::create_directories(o.viewCompare, ec);

    std::srand(20261117);
    game.State = GAME_ACTIVE;
    game.page = Game::Page::None;
    game.menuBuiltFor = -1;
    game.nextFruit = FruitManager::getRandomFruit();

    auto render = [&](const View& v, const std::string& caption) {
        game.renderer.viewMode = v.mode;
        game.renderer.band = v.band;
        game.showDepthStrip = v.inset;
        game.caption = caption;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, o.width, o.height);
        glClearColor(0, 0, 0, 1);
        game.Render();
    };
    // compare.png: a row per view, a column per moment; view-<file>.png: one row.
    std::vector<unsigned char> sheet(size_t(tw) * nMoments * th * nViews * 4), px(size_t(o.width) * o.height * 4);
    auto tile = [&](int row, int col) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, o.width, o.height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                int sum[4] = {};
                for (int dy = 0; dy < down; ++dy)
                    for (int dx = 0; dx < down; ++dx)
                        for (int ch = 0; ch < 4; ++ch)
                            sum[ch] += px[(size_t(o.height - 1 - (y * down + dy)) * o.width + x * down + dx) * 4 + ch];
                unsigned char* d = &sheet[(size_t(row * th + y) * tw * nMoments + col * tw + x) * 4];
                for (int ch = 0; ch < 4; ++ch) d[ch] = (unsigned char)(sum[ch] / (down * down));
            }
    };

    std::vector<std::unique_ptr<Recorder>> recs;
    for (const View& v : views) {
        recs.push_back(std::make_unique<Recorder>());
        recs.back()->fps = o.fps;
        if (!recs.back()->start(o.viewCompare + "/view-" + v.file)) return 1;
    }

    // Moments: early (15 s), mid (90 s), and the first merge after that, a
    // tenth of a second later, with the slice moved through the new fruit.
    // The clips run from 88 s to 98 s, so they show the mid moment and,
    // usually, the merge.
    const char* momentNames[] = {"early", "mid", "merge"};
    const int dropEvery = 45, clipStart = 88 * 60, clipFrames = 10 * 60;
    const int stepsPerFrame = std::max(1, 60 / std::max(o.fps, 1));
    int attempts = 0, moment = 0, mergeFrame = -1;
    for (int frame = 0; (moment < nMoments || frame < clipStart + clipFrames) && frame < 60 * 600; ++frame) {
        float t = frame * FIXED_TIMESTEP;
        setScriptedPlaytestPose(t);
        if (frame % dropEvery == 0) {
            setScriptedPlaytestCursor(attempts++, o.width, o.height);
            game.DropFruit();
        }
        game.FixedUpdate(FIXED_TIMESTEP);
        if (moment == 2 && mergeFrame < 0 && game.physics_solver->just_merged.load() > 0) mergeFrame = frame;
        game.Update(FIXED_TIMESTEP);
        if (game.State == GAME_OVER) game.Reset();

        int c = frame - clipStart;
        if (c >= 0 && c < clipFrames && c % stepsPerFrame == 0) {
            const Camera4D pose = game.state.cam;
            float u = float(c) / float(clipFrames);
            Camera4D& cam = game.state.cam;
            cam.turnLocal(AX_X, AX_W, glm::radians(80.0f) * std::sin(6.2831853f * u));
            cam.turnLocal(AX_Y, AX_W, glm::radians(35.0f) * std::sin(6.2831853f * 2.0f * u));
            cam.yaw += 0.6f * std::sin(6.2831853f * u);
            for (int v = 0; v < nViews; ++v) {
                render(views[v], views[v].name);
                recs[v]->capture(o.width, o.height);
            }
            game.state.cam = pose;
        }

        bool shoot = (moment == 0 && frame == 15 * 60) || (moment == 1 && frame == 90 * 60) ||
                     (moment == 2 && mergeFrame >= 0 && frame == mergeFrame + 6);
        if (!shoot) continue;
        const Camera4D pose = game.state.cam;
        if (moment == 2) { // the slice through the brightest (newest) merged fruit
            int best = -1;
            for (int i = 0; i < MAX_OBJECTS; ++i)
                if (game.physics_solver->has_obj[i] &&
                    (best < 0 || game.physics_solver->objects[i].flash > game.physics_solver->objects[best].flash))
                    best = i;
            Camera4D& cam = game.state.cam;
            if (best >= 0)
                cam.offset = glm::clamp(glm::dot(game.physics_solver->objects[best].position - cam.target, cam.normal()),
                                        -Camera4D::kOffsetMax, Camera4D::kOffsetMax);
        }
        for (int v = 0; v < nViews; ++v) {
            char cap[128];
            snprintf(cap, sizeof cap, "%s   (%s, %.1f s)", views[v].name.c_str(), momentNames[moment], t);
            render(views[v], cap);
            tile(v, moment);
        }
        game.state.cam = pose;
        printf("moment %s at %.2f s: %d fruit\n", momentNames[moment], t, activeObjectCount());
        ++moment;
    }
    for (auto& r : recs) r->stop();
    game.caption.clear();
    if (moment < nMoments) { std::cerr << "view compare: no merge after the mid moment" << std::endl; return 1; }
    auto write = [&](const std::string& path, const unsigned char* data, int rows) {
        if (!stbi_write_png(path.c_str(), tw * nMoments, th * rows, 4, data, tw * nMoments * 4)) {
            std::cerr << "Could not write " << path << std::endl;
            return false;
        }
        std::cout << "wrote " << path << std::endl;
        return true;
    };
    for (int v = 0; v < nViews; ++v)
        if (!write(o.viewCompare + "/view-" + views[v].file + ".png", &sheet[size_t(v) * th * tw * nMoments * 4], 1))
            return 1;
    return write(o.viewCompare + "/compare.png", sheet.data(), nViews) ? 0 : 1;
}
#endif

static int runHeadless(GLFWwindow* window, const Options& o) {
    GLuint fbo, color;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, o.width, o.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    game.state.windowWidth = game.Width = o.width;
    game.state.windowHeight = game.Height = o.height;
    game.FbWidth = o.width;
    game.FbHeight = o.height;
    game.state.m_xpos = o.cursorX >= 0 ? o.cursorX : o.width * 0.5f;
    game.state.m_ypos = o.cursorY >= 0 ? o.cursorY : o.height * 0.55f;
    Quality q = o.quality == "low" ? Quality::Low : o.quality == "medium" ? Quality::Medium
              : o.quality == "ultra" ? Quality::Ultra : Quality::High;
    if (!o.costMap.empty()) game.renderer.extraDefines = "#define COST_MAP\n";
    if (q != game.renderer.quality() || !o.costMap.empty()) game.renderer.setQuality(q);
    // Headless runs render at a fixed scale (of the preset's cap) so results
    // are reproducible; adaptive resolution is for live play.
    game.renderer.adaptive = o.adaptive;
    game.showPerf = o.perf;
    game.laneSnap = o.laneSnap;
    game.renderer.sameKindGlow = !o.noGlow;
    game.effectsOn = !o.noFx;
    game.dangerOverride = o.danger;
    if (o.tutorial >= 0) game.StartTutorial(o.tutorial);
    game.renderer.renderScale = o.scale * game.renderer.maxScale(o.width, o.height);
    if (o.facet >= 0.0f) game.renderer.facet = o.facet;
    game.renderer.useCache = !o.noCache;
    game.renderer.useDeferred = !o.singlePass;
    game.renderer.viewMode = o.view == "thick" ? ViewMode::Thick : o.view == "eye" ? ViewMode::Eye : ViewMode::Slice;
    game.renderer.band = std::clamp(o.band, RaymarchRenderer::kBandMin, RaymarchRenderer::kBandMax);
    auto showMenuState = [&](const std::string& st) {
        game.State = st == "menu" || st == "settings" || st == "controls" ? GAME_MENU
                   : st == "pause" ? GAME_PAUSED : st == "over" ? GAME_OVER : GAME_ACTIVE;
        game.page = st == "settings" ? Game::Page::Settings : st == "controls" ? Game::Page::Controls : Game::Page::None;
        game.menuBuiltFor = -1;
        game.EnsureMenu();
        if (o.focus >= 0) game.menu.focus = o.focus;
    };
    bool late = o.state == "over" || o.state == "pause";  // play first, then show the menu
    showMenuState(late ? "game" : o.state);
    applyCameraOptions(o, game.state.cam);
    if (o.poseAt >= 0.0f) setScriptedPlaytestPose(o.poseAt);
    if (o.playtestSeconds > 0.0f) return runScriptedPlaytest(o, fbo);
    if (!o.turntable.empty()) {
#ifdef __EMSCRIPTEN__
        std::cerr << "--turntable is only available in the desktop build" << std::endl;
        return 1;
#else
        return runTurntable(o, fbo);
#endif
    }
    if (!o.viewCompare.empty()) {
#ifdef __EMSCRIPTEN__
        std::cerr << "--view-compare is only available in the desktop build" << std::endl;
        return 1;
#else
        return runViewCompare(o, fbo);
#endif
    }

    CameraPath path;
    bool hasPath = false;
    if (!o.cameraPath.empty()) {
        std::string err;
        if (!path.load(o.cameraPath, err)) { std::cerr << "camera path: " << err << std::endl; return 1; }
        hasPath = true;
    }
    float pathTime = 0.0f;
    float simAcc = 0.0f;

    auto frame = [&](float dt) {
        game.FixedUpdate(dt);
        game.Update(dt);
        if (hasPath) path.apply(pathTime, game.state.cam);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, o.width, o.height);
        glClearColor(0, 0, 0, 1);
        game.Render();
    };
    if (!o.record.empty()) {
#ifdef __EMSCRIPTEN__
        std::cerr << "--record is only available in the desktop build" << std::endl;
        return 1;
#else
        // Fixed-timestep capture of every frame from the first one.
        Recorder rec;
        rec.fps = o.fps;
        if (!rec.start(o.record)) return 1;
        float seconds = o.seconds > 0 ? o.seconds : hasPath ? path.duration() : o.frames / float(o.fps);
        int total = std::max(1, int(std::lround(seconds * o.fps)));
        // Gameplay: the scripted player drops through the real cursor path (so
        // the drop preview shows), aiming into the bowl in the current slice.
        int drops = 0, resets = 0;
        auto play = [&](float t, float dt) {
            if (o.playEvery <= 0.0f) return;
            if (std::floor((t + dt) / o.playEvery) > std::floor(t / o.playEvery)) {
                aimIntoBowl(drops++, o.width, o.height);
                game.DropFruit();
            } else if (drops > 0) {
                aimIntoBowl(drops, o.width, o.height);   // the cursor moves to the next spot
            }
            if (game.State == GAME_OVER) { ++resets; game.Reset(); }
        };
        if (o.playEvery > 0.0f) {
            std::srand(20261118);
            game.nextFruit = FruitManager::getRandomFruit();
            if (hasPath) path.apply(0.0f, game.state.cam);
            for (float t = 0.0f; t < o.warmup; t += FIXED_TIMESTEP) {
                play(t, FIXED_TIMESTEP);
                game.FixedUpdate(FIXED_TIMESTEP);
                game.Update(FIXED_TIMESTEP);
            }
            printf("warm-up: %d drops, %d fruit in play, %d game overs, state %d\n", drops, activeObjectCount(), resets, int(game.State));
        }
        for (int i = 0; i < total; ++i) {
            if (i % 12 == 0 && i / 12 < o.demo) dropDemoFruit(i / 12);
            pathTime = i * rec.dt();
            if (hasPath) path.apply(pathTime, game.state.cam);
            play(o.warmup + pathTime, rec.dt());
            game.ProcessInput(rec.dt());
            advanceSim(rec.dt(), simAcc);
            game.Update(rec.dt());
            if (hasPath) path.apply(pathTime, game.state.cam);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glViewport(0, 0, o.width, o.height);
            glClearColor(0, 0, 0, 1);
            game.Render();
            rec.capture(o.width, o.height);
            if (i % o.fps == 0) { printf("\rframe %d / %d", i, total); fflush(stdout); }
        }
        printf("\n");
        if (o.playEvery > 0.0f) printf("played: %d drops, %d fruit in play, %d game overs\n", drops, activeObjectCount(), resets);
        rec.stop();
        return 0;
#endif
    }
    const int dropEvery = 12;
    int frames = std::max(o.frames, o.demo * dropEvery + 60);
    for (int i = 0; i < frames; ++i) {
        if (i % dropEvery == 0 && i / dropEvery < o.demo) dropDemoFruit(i / dropEvery);
        frame(FIXED_TIMESTEP);
    }
    if (late) showMenuState(o.state);

    if (o.bench > 0) {
        // Per frame: wall time with glFinish (the whole frame, robust when
        // other work shares the GPU) and the scene pass's GPU timer. The
        // fastest quartile is the least disturbed by other GPU users.
        game.renderer.syncTiming = true;
        frame(FIXED_TIMESTEP);
        glFinish();
        std::vector<float> wall, gpu, bake;
        int bakes0 = game.renderer.bakeCount();
        for (int i = 0; i < o.bench; ++i) {
            if (o.benchTurn != 0.0f) game.state.cam.turnLocal(AX_X, AX_W, glm::radians(o.benchTurn));
            auto t0 = std::chrono::steady_clock::now();
            frame(0.0f);
            glFinish();
            wall.push_back(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
            gpu.push_back(game.renderer.lastGpuMs());
            bake.push_back(game.renderer.bakeMs());
        }
        auto pct = [](std::vector<float> v, float q) {
            std::sort(v.begin(), v.end());
            return v[std::min(v.size() - 1, size_t(q * v.size()))];
        };
        printf("bench %dx%d %s scale %.2f: frame wall median %.2f ms, p25 %.2f ms (%.0f fps); scene GPU median %.2f ms\n",
               o.width, o.height, qualityName(game.renderer.quality()), game.renderer.renderScale,
               pct(wall, 0.5f), pct(wall, 0.25f), 1000.0f / pct(wall, 0.5f), pct(gpu, 0.5f));
        printf("slice cache %s: %d bakes, bake GPU median %.2f ms\n",
               o.noCache ? "off" : "on", game.renderer.bakeCount() - bakes0, pct(bake, 0.5f));
    }
    // Read the frame, flipped to top-down rows, appended to px.
    auto capture = [&](std::vector<unsigned char>& px) {
        frame(0.0f);
        size_t row = size_t(o.width) * 4, at = px.size();
        px.resize(at + row * o.height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, o.width, o.height, GL_RGBA, GL_UNSIGNED_BYTE, px.data() + at);
        for (int y = 0; y < o.height / 2; ++y)
            std::swap_ranges(px.begin() + at + y * row, px.begin() + at + (y + 1) * row,
                             px.begin() + at + (o.height - 1 - y) * row);
    };
    auto save = [&](const std::string& path, const std::vector<unsigned char>& px, int h) {
        if (!stbi_write_png(path.c_str(), o.width, h, 4, px.data(), o.width * 4)) {
            std::cerr << "Could not write " << path << std::endl;
            return false;
        }
        std::cout << "wrote " << path << std::endl;
        return true;
    };
    if (!o.screenshot.empty()) {
        std::vector<unsigned char> px;
        capture(px);
        if (!save(o.screenshot, px, o.height)) return 1;
    }
    if (!o.costMap.empty()) {
        std::vector<unsigned char> px;
        capture(px);
        const char* names[] = {"static world", "moving world", "fruit"};
        for (int ch = 0; ch < 3; ++ch) {
            std::vector<int> cost;
            for (size_t i = ch; i < px.size(); i += 4) cost.push_back(px[i]);
            std::sort(cost.begin(), cost.end());
            double sum = 0.0;
            for (int c : cost) sum += c;
            printf("cost map, %s SDF evaluations per pixel: mean %.1f, median %d, p95 %d, max %d\n", names[ch],
                   sum / cost.size(), cost[cost.size() / 2], cost[cost.size() * 95 / 100], cost.back());
        }
        if (!save(o.costMap, px, o.height)) return 1;
    }
    if (!o.fruitSheet.empty()) {
        std::vector<unsigned char> px;
        setupFruitSheet();
        game.sheetTitle = "slice tilt 0";
        capture(px);
        applyTurns(o.sheetTurn, game.state.cam);
        game.sheetTitle = "after a camera-local " + o.sheetTurn + " deg turn";
        capture(px);
        if (!save(o.fruitSheet, px, o.height * 2)) return 1;
    }
    return 0;
}

#ifndef __EMSCRIPTEN__
// Live recording (F9 or --record dir): a fixed timestep per rendered frame, so
// the capture is exact however slowly it renders.
struct LiveRecording {
    Recorder rec;
    std::string dir;          // --record dir; empty means a timestamped folder
    CameraPath path;
    bool hasPath = false;
    bool toggle = false;      // set by the F9 key callback
    bool savedAdaptive = true;
    int frames = 0;

    void begin(GLFWwindow* window) {
        std::string d = dir;
        if (d.empty()) {
            char buf[48];
            time_t now = time(nullptr);
            strftime(buf, sizeof buf, "recordings/%Y%m%d-%H%M%S", localtime(&now));
            d = buf;
        }
        if (!rec.start(d)) return;
        savedAdaptive = game.renderer.adaptive;
        game.renderer.adaptive = false;   // a steady resolution, not a frame-time-driven one
        frames = 0;
        glfwSetWindowTitle(window, "Suika 4D  [REC]");
    }
    void end(GLFWwindow* window) {
        rec.stop();
        game.renderer.adaptive = savedAdaptive;
        glfwSetWindowTitle(window, "Suika 4D");
    }
};
static LiveRecording live;
#endif

static void runInteractiveFrame(GLFWwindow* window, float& fixedUpdateAccumulator, float& lastFrame, bool throttle) {
    float currentFrame = glfwGetTime();
    float deltaTime = currentFrame - lastFrame;
    if (deltaTime > 0.25f) deltaTime = 0.25f;
    lastFrame = currentFrame;
#ifndef __EMSCRIPTEN__
    if (live.toggle) {
        live.toggle = false;
        if (live.rec.active()) live.end(window); else live.begin(window);
    }
    const bool recording = live.rec.active();
    if (recording) { deltaTime = live.rec.dt(); throttle = false; }
#else
    const bool recording = false;
#endif

    glfwPollEvents();
    game.ProcessInput(deltaTime);

    fixedUpdateAccumulator += deltaTime;
    while (fixedUpdateAccumulator >= FIXED_TIMESTEP)
    {
        game.FixedUpdate(FIXED_TIMESTEP);
        fixedUpdateAccumulator -= FIXED_TIMESTEP;
    }
    game.Update(deltaTime);
    if (!recording) game.renderer.reportFrameTime(deltaTime * 1000.0f);

#ifndef __EMSCRIPTEN__
    if (recording && live.hasPath) live.path.apply(live.frames * live.rec.dt(), game.state.cam);
#endif
    glfwGetFramebufferSize(window, &game.FbWidth, &game.FbHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, game.FbWidth, game.FbHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    game.Render();

#ifndef __EMSCRIPTEN__
    if (recording) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        live.rec.capture(game.FbWidth, game.FbHeight);
        ++live.frames;
    }
#endif
    glfwSwapBuffers(window);

#ifndef __EMSCRIPTEN__
    if (throttle) {
        float frameDuration = glfwGetTime() - currentFrame;
        if (frameDuration < FRAME_TIME)
        {
            std::this_thread::sleep_for(std::chrono::duration<float>(FRAME_TIME - frameDuration));
        }
    }
#else
    (void)throttle;
#endif
}

int main(int argc, char* argv[])
{
    Options opt = parseOptions(argc, argv);
    try {
#ifndef __EMSCRIPTEN__
        if (opt.headless()) {
            // Never show a window, take focus, or join the Dock.
#ifdef __APPLE__
            glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
#endif
        }
        if (opt.muted()) {
            SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        }
#endif
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            return -1;
        }
#ifdef __EMSCRIPTEN__
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif
        glfwWindowHint(GLFW_RESIZABLE, true);
        if (opt.headless()) {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        }

        GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Suika 4D", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return -1;
        }
        glfwMakeContextCurrent(window);
        if (opt.bench > 0) glfwSwapInterval(0);

        bool sdlReady = false;
        bool audioReady = false;
#ifndef __EMSCRIPTEN__
        if (!opt.muted()) {
            if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                std::cerr << "Warning: SDL_Init error: " << SDL_GetError() << std::endl;
            } else {
                sdlReady = true;
                if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
                    std::cerr << "Warning: Mix_OpenAudio error: " << Mix_GetError() << std::endl;
                } else {
                    audioReady = true;
                }
            }
        }
#endif

#ifndef __EMSCRIPTEN__
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            return -1;
        }
#endif

        glfwSetKeyCallback(window, key_callback);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetWindowSizeCallback(window, windowSizeCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);

        game.SetAudioState(sdlReady, audioReady, opt.muted());
        game.Init(window);
        game.LoadTutorialState();

#ifndef __EMSCRIPTEN__
        live.rec.fps = opt.fps;
        live.dir = opt.record;
        if (!opt.cameraPath.empty()) {
            std::string err;
            if (live.path.load(opt.cameraPath, err)) live.hasPath = true;
            else std::cerr << "camera path: " << err << std::endl;
        }
        stbi_write_png_compression_level = 3;  // fast encodes; frames become video anyway
#endif
        if (opt.headless()) {
            int rc = runHeadless(window, opt);
            glfwTerminate();
            return rc;
        }

#ifdef __EMSCRIPTEN__
        struct LoopContext {
            GLFWwindow* window = nullptr;
            float fixedUpdateAccumulator = 0.0f;
            float lastFrame = 0.0f;
        };
        static LoopContext loop;
        loop.window = window;
        loop.lastFrame = glfwGetTime();
        emscripten_set_main_loop_arg([](void* arg) {
            auto* ctx = static_cast<LoopContext*>(arg);
            if (glfwWindowShouldClose(ctx->window)) {
                emscripten_cancel_main_loop();
                return;
            }
            runInteractiveFrame(ctx->window, ctx->fixedUpdateAccumulator, ctx->lastFrame, false);
        }, &loop, 0, 1);
        return 0;
#else
        float fixedUpdateAccumulator = 0.0f;
        float lastFrame = glfwGetTime();
        if (!opt.record.empty()) live.toggle = true;  // --record starts at once
        while (!glfwWindowShouldClose(window))
        {
            runInteractiveFrame(window, fixedUpdateAccumulator, lastFrame, true);
        }
        live.rec.stop();

        glfwTerminate();
        return 0;
#endif
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        glfwTerminate();
        return -1;
    }
}

#if _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}
#endif

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
#ifndef __EMSCRIPTEN__
    if (key == GLFW_KEY_F9 && action == GLFW_PRESS) live.toggle = true;
#endif
    if (key >= 0 && key < 1024)
    {
        if (action == GLFW_PRESS)
            game.Keys[key] = true;
        else if (action == GLFW_RELEASE)
        {
            game.Keys[key] = false;
            game.KeysProcessed[key] = false;
        }
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    game.state.onMouseButton(button, action, game.state.m_xpos, game.state.m_ypos);
    if (game.State == GAME_ACTIVE && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        game.DropFruit();
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!game.state.initialized) game.state.Init(window);
    game.state.onCursorPos(xpos, ypos);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (!game.state.initialized) game.state.Init(window);
    if (game.State == GAME_ACTIVE) game.state.onScroll(yoffset);
}

void windowSizeCallback(GLFWwindow* window, int windowWidth, int windowHeight){
    if (!game.state.initialized) game.state.Init(window);
    game.state.windowHeight = windowHeight;
    game.state.windowWidth = windowWidth;
}
