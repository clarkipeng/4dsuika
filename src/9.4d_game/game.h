/*******************************************************************
** This code is part of Breakout.
**
** Breakout is free software: you can redistribute it and/or modify
** it under the terms of the CC BY 4.0 license as published by
** Creative Commons, either version 4 of the License, or (at your
** option) any later version.
******************************************************************/
#ifndef GAME_H
#define GAME_H
#include <vector>
#include <tuple>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// #include "learnopengl/shader.h"
// #include "learnopengl/filesystem.h"

enum GameState {
    GAME_ACTIVE,
    GAME_MENU,
    GAME_WIN
};

// typedef std::tuple<bool, Direction, glm::vec2> Collision;
// const glm::vec2 PLAYER_SIZE(100.0f, 20.0f);
// const float PLAYER_VELOCITY(500.0f);
// const glm::vec2 INITIAL_BALL_VELOCITY(100.0f, -350.0f);
// const float BALL_RADIUS = 12.5f;

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


    // constructor/destructor
    Game(unsigned int width, unsigned int height);
    ~Game();
    // initialize game state (load all shaders/textures/levels)
    void Init(GLFWwindow* window);
    // game loop
    void ProcessInput(float dt);
    void FixedUpdate(float dt);
    void Update(float dt);
    void Render();
    // void DoCollisions();
    // reset
    // void ResetLevel();
    // void ResetPlayer();
    // powerups
    // void SpawnPowerUps(GameObject &block);
    // void UpdatePowerUps(float dt);
};

#endif