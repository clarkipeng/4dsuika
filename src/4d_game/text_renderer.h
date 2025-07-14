#pragma once

#include <map>
#include <string>
#include <iostream>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "learnopengl/shader.h"

struct Character {
    unsigned int TextureID;
    glm::ivec2 Size;
    glm::ivec2 Bearing;
    unsigned int Advance;
};

class TextRenderer {
private:
    unsigned int vao, vbo;
    std::map<char, Character> Characters;

    void initRenderData();

public:
    Shader* shader;

    TextRenderer(Shader* shader)
        : shader(shader)
    {
        // glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
        // this->shader->use();
        // this->shader->setMat4("projection", projection);
        // this->shader->setInt("text", 0);
        initRenderData();
    }

    ~TextRenderer()
    {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
    }

    void Load(std::string fontPath, unsigned int fontSize);

    void RenderText(const std::string& text, float x, float y, float scale, glm::vec3 color);
    void RenderTextScale(const std::string& text, float x, float y, float targetWidth, float targetHeight, glm::vec3 color);
    float GetTextWidth(const std::string& text, float scale);
};
