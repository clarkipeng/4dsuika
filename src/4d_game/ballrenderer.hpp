#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "learnopengl/shader.h"

#include "shape.hpp"
#include "fruit.hpp"

class BallRenderer
{
protected:
    ShapeType shape_type;
    int fidelity = 2;
    int vertices_count, indices_count;
    unsigned int vao, vbo, ebo;

public:
    Shader *shader;
    
    BallRenderer(Shader *shader, ShapeType shape_type = ICOSPHERE, int fidelity_ = 1)
        : shader(shader), shape_type(shape_type), fidelity(fidelity_)
    {
        this->initRenderData();
    }

    ~BallRenderer()
    {
        glDeleteVertexArrays(1, &this->vao);
        glDeleteBuffers(1, &this->vbo);
        glDeleteBuffers(1, &this->ebo);
    }

    void virtual Draw4d(float w_coord, unsigned int texture, glm::vec4 position, float scale, 
                glm::vec3 rotation = glm::vec3(0.0f), float alpha_mult = 1.0f,
                glm::vec3 spatial_offset = glm::vec3(0.0f))
    {
        glm::mat4 model = glm::mat4(1.0f);
        float wDelta = position.w - w_coord;
        glm::vec3 pos3d = glm::vec3(position.x, position.y, position.z) + spatial_offset;
        
        // Prevent division by zero
        float projected_scale = sqrt(max(0.0f, scale*scale - wDelta*wDelta));
        glm::vec3 scale3d = glm::vec3(abs(projected_scale));

        model = glm::translate(model, pos3d);
        model = glm::scale(model, scale3d);
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        this->shader->setMat4("model", model); // also needed!
        this->shader->setMat3("normalMatrix", normalMatrix);
        this->shader->setFloat("wDelta", wDelta);
        this->shader->setFloat("alpha_mult", alpha_mult);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(this->vao);
        glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void virtual Draw3d(glm::vec3 position, unsigned int texture, float scale, 
                glm::vec3 rotation = glm::vec3(0.0f),float alpha_mult = 1.0f)
    {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        
        model = glm::scale(model, glm::vec3(scale));

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        this->shader->setMat4("model", model); // also needed!
        this->shader->setMat3("normalMatrix", normalMatrix);
        this->shader->setFloat("wDelta", 0.0f);
        this->shader->setFloat("alpha_mult", alpha_mult);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(this->vao);
        glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void virtual Draw4d(float w_coord, Fruit fruit, glm::vec4 position, glm::vec3 rotation = glm::vec3(0.0f), float alpha_mult = 1.0f, glm::vec3 spatial_offset = glm::vec3(0.0f))
    {
        const float scale = FruitManager::getFruitProperties(fruit).radius;
        const unsigned int texture = FruitManager::getFruitProperties(fruit).texture;

        glm::mat4 model = glm::mat4(1.0f);
        float wDelta = position.w - w_coord;
        glm::vec3 pos3d = glm::vec3(position.x, position.y, position.z) + spatial_offset;
        
        // Prevent division by zero
        float projected_scale = sqrt(max(0.0f, scale*scale - wDelta*wDelta));
        glm::vec3 scale3d = glm::vec3(abs(projected_scale));

        model = glm::translate(model, pos3d);
        
        model = glm::scale(model, scale3d);
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        this->shader->setMat4("model", model); // also needed!
        this->shader->setMat3("normalMatrix", normalMatrix);
        this->shader->setFloat("wDelta", wDelta);
        this->shader->setFloat("alpha_mult", alpha_mult);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(this->vao);
        glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void virtual Draw3d(Fruit fruit, glm::vec3 position,
                glm::vec3 rotation = glm::vec3(0.0f), float alpha_mult = 1.0f)
    {
        const float scale = FruitManager::getFruitProperties(fruit).radius;
        const unsigned int texture = FruitManager::getFruitProperties(fruit).texture;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        
        model = glm::scale(model, glm::vec3(scale));

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        this->shader->setMat4("model", model); // also needed!
        this->shader->setMat3("normalMatrix", normalMatrix);
        this->shader->setFloat("wDelta", 0.0f);
        this->shader->setFloat("alpha_mult", alpha_mult);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(this->vao);
        glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

protected:
    virtual void initRenderData()
    {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        
        // Make sure generateBall function exists and is accessible
        generateBall(shape_type, fidelity, vertices, indices);

        std::cout 
        << "Ball mesh: " 
        << vertices.size()/5 << " verts, " 
        << indices.size()/3 << " tris\n";
        assert(vertices.size() > 0 && indices.size() > 0);
        
        indices_count = indices.size();
        vertices_count = vertices.size();

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);

        // Upload vertex data
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        // Upload index data
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        // Vertex attributes (position: 3 floats, texcoords: 2 floats)
        glEnableVertexAttribArray(0); // position
        glEnableVertexAttribArray(1); // normal
        glEnableVertexAttribArray(2); // texcoords
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);           // pos
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); // normal
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); // tex

        glBindVertexArray(0);
    }
};