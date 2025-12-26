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

#include "globals.h"
#include "render_helper.hpp"
#include "state_helper.hpp"
#include "physics_solver.hpp"

// #include "resource_manager.h"

#include <iostream>

#include <thread>  // for std::this_thread::sleep_for
#include <chrono>  // for std::chrono::duration

// At the top of your file
const float TARGET_FPS = 60.0f;
const float FRAME_TIME = 1.0f / TARGET_FPS;

// For fixed timestep
const float FIXED_TIMESTEP = 1.0f / 60.0f; // 60 updates per second
float fixedUpdateAccumulator = 0.0f;

// GLFW function declarations
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void windowSizeCallback(GLFWwindow* window, int windowWidth, int windowHeight);

// The Width of the screen
const unsigned int SCREEN_WIDTH = 800;
// The height of the screen
const unsigned int SCREEN_HEIGHT = 600;

Game game(SCREEN_WIDTH, SCREEN_HEIGHT);

int main(int argc, char* argv[])
{
    // std::cout << "GL_VERSION: " << glGetString(GL_VERSION) << "\n";
    // std::cout << "GL_RENDERER: " << glGetString(GL_RENDERER) << "\n";
    // std::cout << "GLSL_VERSION: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";
    std::cout << "Starting 4D Game initialization..." << std::endl;
    
    try {
        std::cout << "Initializing GLFW..." << std::endl;
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        glfwWindowHint(GLFW_RESIZABLE, true);

        std::cout << "Creating GLFW window..." << std::endl;
        GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "game", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return -1;
        }
        glfwMakeContextCurrent(window);

        std::cout << "Initializing SDL audio..." << std::endl;
        // Initialize SDL audio with error handling for web build
        #ifdef __EMSCRIPTEN__
            // For web build, try to initialize SDL but don't fail if it doesn't work
            if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                std::cerr << "Warning: SDL_Init error: " << SDL_GetError() << std::endl;
                // Don't return, continue without audio
            } else {
                // Disable preload for web build
                if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
                    std::cerr << "Warning: Mix_OpenAudio error: " << Mix_GetError() << std::endl;
                    // Don't return, continue without audio
                }
            }
        #else
            // For native builds, initialize SDL normally
            if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
                return 1;
            }
            if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
                std::cerr << "Mix_OpenAudio error: " << Mix_GetError() << std::endl;
                return 1;
            }
        #endif
        
        std::cout << "Loading OpenGL function pointers with GLAD..." << std::endl;
        // glad: load all OpenGL function pointers
        // ---------------------------------------
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            std::cout << "Failed to initialize GLAD" << std::endl;
            return -1;
        }

        std::cout << "Setting up GLFW callbacks..." << std::endl;
        glfwSetKeyCallback(window, key_callback);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetWindowSizeCallback(window, windowSizeCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);

        std::cout << "Configuring OpenGL..." << std::endl;
        // OpenGL configuration
        // --------------------
        // glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        std::cout << "Initializing game..." << std::endl;
        // initialize game
        // ---------------
        game.Init(window);
        std::cout << "Game initialization completed successfully!" << std::endl;

        // deltaTime variables
        // -------------------
        float deltaTime = 0.0f;
        float lastFrame = 0.0f;

        while (!glfwWindowShouldClose(window))
        {
            // Calculate delta time
            float currentFrame = glfwGetTime();
            float deltaTime = currentFrame - lastFrame;
            if (deltaTime > 0.25f) deltaTime = 0.25f; // clamp delta to prevent spiral of death
            lastFrame = currentFrame;

            glfwPollEvents();

            game.ProcessInput(deltaTime);

            // Fixed update loop
            fixedUpdateAccumulator += deltaTime;
            while (fixedUpdateAccumulator >= FIXED_TIMESTEP)
            {
                game.FixedUpdate(FIXED_TIMESTEP);  // You'll need to add this to your Game class
                fixedUpdateAccumulator -= FIXED_TIMESTEP;
            }
            game.Update(deltaTime);  // You'll need to add this to your Game class

            // Render
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            game.Render();

            glfwSwapBuffers(window);

            // Frame rate cap
            float frameEnd = glfwGetTime();
            float frameDuration = frameEnd - currentFrame;
            if (frameDuration < FRAME_TIME)
            {
                std::this_thread::sleep_for(std::chrono::duration<float>(FRAME_TIME - frameDuration));
            }
            
        }


        // delete all resources as loaded using the resource manager
        // ---------------------------------------------------------
        // ResourceManager::Clear();

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
#endif

// This is the entry point that the linker is demanding.
#if _WIN32
int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    // Call your standard main function.
    return main(__argc, __argv);
}
#endif


void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    // // when a user presses the escape key, we set the WindowShouldClose property to true, closing the application
    // if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    //     glfwSetWindowShouldClose(window, true);
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
    if (game.State == GAME_MENU && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS){
        // game.ballPlaced=true;
    }
    if (game.State == GAME_OVER && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS){
        // game.ballPlaced=true;
    }
    if (game.State == GAME_ACTIVE && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        RayInter closestResult = getPlacementMouse(&game.state, &game.boundary, game.physics_solver);

        if (closestResult.hit) {
            // std::cout << "HIT\n";
            game.physics_solver->addObject(
                PhysicsObject(glm::vec4(closestResult.point, game.state.w)+ glm::vec4(0,3.0f,0,0),
                game.nextFruit, true, false)
            );
            game.nextFruit = FruitManager::getRandomFruit();
        }
        game.ballPlaced=true;
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