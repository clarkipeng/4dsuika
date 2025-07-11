#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cassert>
#include <iostream>

#include "learnopengl/shader.h"
#include "ballrenderer.hpp"
#include "shape.hpp"

// Renders a hemisphere shell with inner and outer walls using CPU-side geometry filtering
class HemisphereRenderer : public BallRenderer {
private:
    float sigma;       // Clip angle in radians
    float innerRadius;
    float outerRadius;

public:
    HemisphereRenderer(Shader* shader,
                       ShapeType shape,
                       int fidelity,
                       float angleDegrees = 90.0f,
                       float radius = 1.0f,
                       float margin = 0.1f)
        : BallRenderer(shader, shape, fidelity),
          sigma(glm::radians(angleDegrees)),
          innerRadius(radius - margin),
          outerRadius(radius + margin)
    {
        initRenderData();
    }

protected:
    virtual void initRenderData() override {
        std::vector<float> vertices;
        std::vector<unsigned int> indices;

        generateBowlMesh(fidelity*12, fidelity*12, sigma, innerRadius, outerRadius, vertices, indices);
        // generateBowl(shape_type, fidelity, sigma, innerRadius, outerRadius, vertices, indices);

        std::cout << "Hemisphere shell: "
                  << vertices.size() / 8 << " verts, "
                  << indices.size() / 3 << " tris\n";

        assert(!vertices.empty() && !indices.empty());

        indices_count = static_cast<int>(indices.size());
        vertices_count = static_cast<int>(vertices.size());

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0); // Position
        glEnableVertexAttribArray(1); // Normal
        glEnableVertexAttribArray(2); // TexCoords

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

        glBindVertexArray(0);
    }
};
