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

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>

#include "game.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

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
//   --screenshot out.png   render, save a PNG, exit
//   --bench N              render N frames as fast as possible, print timings
//   --size WxH             offscreen size in pixels (default 1920x1080)
//   --state game|menu|over
//   --turn xw:30,yw:0,zw:0,xy:0   4D turns in degrees, camera-local planes
//   --orbit yaw,pitch      degrees
//   --offset v             slice offset along its normal
//   --demo N               drop N fruit and let them settle first
//   --frames N             frames simulated before the capture (default 90, more with --demo)
//   --scale s              render scale (fraction of output resolution)
//   --cursor x,y           mouse position in pixels (for the drop preview)
struct Options {
    std::string screenshot;
    int bench = 0;
    int width = 1920, height = 1080;
    std::string state = "game";
    std::string turn;
    float yaw = 0.0f, pitch = glm::degrees(Camera4D::kDefaultPitch);
    float offset = 0.0f;
    int demo = 0;
    int frames = 90;
    float scale = 1.0f;
    float cursorX = -1.0f, cursorY = -1.0f;
    bool headless() const { return !screenshot.empty() || bench > 0; }
};

static Options parseOptions(int argc, char* argv[]) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--screenshot") o.screenshot = next();
        else if (a == "--bench") o.bench = std::atoi(next().c_str());
        else if (a == "--size") sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--state") o.state = next();
        else if (a == "--turn") o.turn = next();
        else if (a == "--orbit") sscanf(next().c_str(), "%f,%f", &o.yaw, &o.pitch);
        else if (a == "--offset") o.offset = std::atof(next().c_str());
        else if (a == "--demo") o.demo = std::atoi(next().c_str());
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--scale") o.scale = std::atof(next().c_str());
        else if (a == "--cursor") sscanf(next().c_str(), "%f,%f", &o.cursorX, &o.cursorY);
        else std::cerr << "Unknown option " << a << std::endl;
    }
    return o;
}

static void applyCameraOptions(const Options& o, Camera4D& cam) {
    cam.yaw = glm::radians(o.yaw);
    cam.pitch = glm::radians(o.pitch);
    cam.offset = o.offset;
    std::stringstream ss(o.turn);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.size() < 4 || item[2] != ':') continue;
        auto axis = [](char c) { return c == 'x' ? AX_X : c == 'y' ? AX_Y : c == 'z' ? AX_Z : AX_W; };
        cam.turnLocal(axis(item[0]), axis(item[1]), glm::radians(float(std::atof(item.c_str() + 3))));
    }
}

// Demo fruit scattered through 4D so screenshots show glomes crossing the
// slice; one is dropped every few frames, like a player would.
static void dropDemoFruit(int index) {
    uint32_t seed = 7u + 977u * uint32_t(index);
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / float(1 << 24); };
    glm::vec4 p(rnd() * 2.4f - 1.2f, 1.5f, rnd() * 2.4f - 1.2f, rnd() * 2.0f - 1.0f);
    game.physics_solver->addObject(PhysicsObject(p, Fruit(int(rnd() * 6.0f)), true, false));
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
    game.renderer.renderScale = o.scale;
    game.State = o.state == "menu" ? GAME_MENU : GAME_ACTIVE;
    applyCameraOptions(o, game.state.cam);

    auto frame = [&](float dt) {
        game.FixedUpdate(dt);
        game.Update(dt);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, o.width, o.height);
        glClearColor(0, 0, 0, 1);
        game.Render();
    };
    const int dropEvery = 12;
    int frames = std::max(o.frames, o.demo * dropEvery + 60);
    for (int i = 0; i < frames; ++i) {
        if (i % dropEvery == 0 && i / dropEvery < o.demo) dropDemoFruit(i / dropEvery);
        frame(FIXED_TIMESTEP);
    }
    if (o.state == "over") game.State = GAME_OVER;

    if (o.bench > 0) {
        frame(FIXED_TIMESTEP);
        glFinish();
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < o.bench; ++i) frame(0.0f);
        glFinish();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / o.bench;
        printf("bench %dx%d scale %.2f: %.2f ms/frame (%.1f fps)\n", o.width, o.height, o.scale, ms, 1000.0 / ms);
    }
    if (!o.screenshot.empty()) {
        frame(0.0f);
        std::vector<unsigned char> px(size_t(o.width) * o.height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, o.width, o.height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        stbi_flip_vertically_on_write(1);
        if (!stbi_write_png(o.screenshot.c_str(), o.width, o.height, 4, px.data(), o.width * 4)) {
            std::cerr << "Could not write " << o.screenshot << std::endl;
            return 1;
        }
        std::cout << "wrote " << o.screenshot << std::endl;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    Options opt = parseOptions(argc, argv);
    try {
        if (opt.headless()) {
            // Never show a window, take focus, or join the Dock.
            glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
            SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        }
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
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

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            std::cerr << "Warning: SDL_Init error: " << SDL_GetError() << std::endl;
        } else if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
            std::cerr << "Warning: Mix_OpenAudio error: " << Mix_GetError() << std::endl;
        }

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            return -1;
        }

        glfwSetKeyCallback(window, key_callback);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetWindowSizeCallback(window, windowSizeCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);

        game.Init(window);

        if (opt.headless()) {
            int rc = runHeadless(window, opt);
            glfwTerminate();
            return rc;
        }

        float fixedUpdateAccumulator = 0.0f;
        float lastFrame = glfwGetTime();
        while (!glfwWindowShouldClose(window))
        {
            float currentFrame = glfwGetTime();
            float deltaTime = currentFrame - lastFrame;
            if (deltaTime > 0.25f) deltaTime = 0.25f; // clamp delta to prevent spiral of death
            lastFrame = currentFrame;

            glfwPollEvents();
            game.ProcessInput(deltaTime);

            fixedUpdateAccumulator += deltaTime;
            while (fixedUpdateAccumulator >= FIXED_TIMESTEP)
            {
                game.FixedUpdate(FIXED_TIMESTEP);
                fixedUpdateAccumulator -= FIXED_TIMESTEP;
            }
            game.Update(deltaTime);

            glfwGetFramebufferSize(window, &game.FbWidth, &game.FbHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, game.FbWidth, game.FbHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            game.Render();

            glfwSwapBuffers(window);

            float frameDuration = glfwGetTime() - currentFrame;
            if (frameDuration < FRAME_TIME)
            {
                std::this_thread::sleep_for(std::chrono::duration<float>(FRAME_TIME - frameDuration));
            }
        }

        glfwTerminate();
        return 0;
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
    game.state.onScroll(yoffset);
}

void windowSizeCallback(GLFWwindow* window, int windowWidth, int windowHeight){
    if (!game.state.initialized) game.state.Init(window);
    game.state.windowHeight = windowHeight;
    game.state.windowWidth = windowWidth;
}
