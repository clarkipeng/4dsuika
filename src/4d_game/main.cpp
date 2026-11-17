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
//   --size WxH             offscreen size in pixels (default 1920x1080)
//   --state game|menu|pause|settings|controls|over   (menu = start screen)
//   --focus N              focused menu row (for screenshots of the focus state)
//   --turn xw:30,yw:0,zw:0,xy:0   4D turns in degrees, camera-local planes
//   --orbit yaw,pitch      degrees
//   --distance d           camera distance from the bowl (default 13; 34 frames the island)
//   --offset v             slice offset along its normal
//   --demo N               drop N fruit and let them settle first
//   --frames N             frames simulated before the capture (default 90, more with --demo)
//   --scale s              render scale (fraction of output resolution)
//   --facet f              low-poly facet strength (0 = smooth)
//   --quality q            low | medium | high | ultra (default high)
//   --adaptive             keep adaptive resolution on (otherwise fixed --scale)
//   --perf                 show the F3 performance overlay
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
    float cursorX = -1.0f, cursorY = -1.0f;
    std::string record;
    std::string cameraPath;
    int fps = 60;
    float seconds = 0.0f;
    bool forceHeadless = false;
    bool mute = false;
    std::string fruitSheet;
    std::string sheetTurn = "yw:35";
    bool headless() const { return forceHeadless || !screenshot.empty() || bench > 0 || !fruitSheet.empty(); }
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
        else if (a == "--cursor") sscanf(next().c_str(), "%f,%f", &o.cursorX, &o.cursorY);
        else if (a == "--fruit-sheet") o.fruitSheet = next();
        else if (a == "--sheet-turn") o.sheetTurn = next();
        else if (a == "--record") o.record = next();
        else if (a == "--fps") o.fps = std::max(1, std::atoi(next().c_str()));
        else if (a == "--seconds") o.seconds = std::atof(next().c_str());
        else if (a == "--camera-path") o.cameraPath = next();
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
    if (q != game.renderer.quality()) game.renderer.setQuality(q);
    // Headless runs render at a fixed scale (of the preset's cap) so results
    // are reproducible; adaptive resolution is for live play.
    game.renderer.adaptive = o.adaptive;
    game.showPerf = o.perf;
    game.renderer.renderScale = o.scale * game.renderer.maxScale(o.width, o.height);
    if (o.facet >= 0.0f) game.renderer.facet = o.facet;
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
        for (int i = 0; i < total; ++i) {
            if (i % 12 == 0 && i / 12 < o.demo) dropDemoFruit(i / 12);
            pathTime = i * rec.dt();
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
        // GPU time of the scene pass from timer queries (the number that
        // decides 60 fps), plus wall time per whole frame for reference.
        game.renderer.syncTiming = true;
        frame(FIXED_TIMESTEP);
        glFinish();
        std::vector<float> gpu;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < o.bench; ++i) {
            frame(0.0f);
            gpu.push_back(game.renderer.lastGpuMs());
        }
        glFinish();
        double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / o.bench;
        std::sort(gpu.begin(), gpu.end());
        float median = gpu[gpu.size() / 2], p95 = gpu[std::min(gpu.size() - 1, gpu.size() * 95 / 100)];
        printf("bench %dx%d %s scale %.2f: scene GPU median %.2f ms, p95 %.2f ms; frame wall %.2f ms (%.0f fps)\n",
               o.width, o.height, qualityName(game.renderer.quality()), game.renderer.renderScale,
               median, p95, wall, 1000.0 / wall);
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
