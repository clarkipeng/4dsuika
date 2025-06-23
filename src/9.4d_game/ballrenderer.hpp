#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "learnopengl/shader.h"

#include "shape.hpp"


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
                glm::vec3 rotation = glm::vec3(0.0f), glm::vec3 color = glm::vec3(1.0f))
    {
        glm::mat4 model = glm::mat4(1.0f);
        glm::vec3 pos3d = glm::vec3(position.x, position.y, position.z);
        
        // Prevent division by zero
        float projected_scale = sqrt(scale*scale - (w_coord - position.w)*(w_coord - position.w));
        glm::vec3 scale3d = glm::vec3(abs(projected_scale));

        model = glm::translate(model, pos3d);
        
        // // Apply rotations if needed
        // if (rotation.x != 0.0f)
        //     model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        // if (rotation.y != 0.0f)
        //     model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        // if (rotation.z != 0.0f)
        //     model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            
        model = glm::scale(model, scale3d);

        // this->shader->setMat4("model", model);
        // this->shader->setVec3("objectColor", color);
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        this->shader->setMat4("model", model); // also needed!
        this->shader->setMat3("normalMatrix", normalMatrix);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(this->vao);
        glDrawElements(GL_TRIANGLES, indices_count, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void virtual Draw3d(unsigned int texture, glm::vec3 position, float scale, 
                glm::vec3 rotation = glm::vec3(0.0f), glm::vec3 color = glm::vec3(1.0f))
    {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        
        // // Apply rotations if needed
        // if (rotation.x != 0.0f)
        //     model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        // if (rotation.y != 0.0f)
        //     model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        // if (rotation.z != 0.0f)
        //     model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            
        model = glm::scale(model, glm::vec3(scale));

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        this->shader->setMat4("model", model); // also needed!
        this->shader->setMat3("normalMatrix", normalMatrix);
        // this->shader->setVec3("objectColor", color);

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