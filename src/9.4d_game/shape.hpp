#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


enum ShapeType {
    ICOSPHERE,
    SPHERE
};

void generateSphereMesh(int stacks, int slices, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    for (int i = 0; i <= stacks; ++i) {
        float v = (float)i / stacks;
        float phi = glm::pi<float>() * v;

        for (int j = 0; j <= slices; ++j) {
            float u = (float)j / slices;
            float theta = 2.0f * glm::pi<float>() * u;

            float x = sin(phi) * cos(theta);
            float y = sin(phi) * sin(theta);
            float z = cos(phi);

            vertices.push_back(x);     // pos.x
            vertices.push_back(y);     // pos.y
            vertices.push_back(z);     // pos.z
            
            vertices.push_back(x);     // pos.x
            vertices.push_back(y);     // pos.y
            vertices.push_back(z);     // pos.z

            vertices.push_back(u);     // tex.u
            vertices.push_back(v);     // tex.v
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int first = i * (slices + 1) + j;
            int second = first + slices + 1;

            // two triangles per quad
            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }
}

inline unsigned long long cantorPair(unsigned int a, unsigned int b) {
    unsigned int x = std::min(a, b);
    unsigned int y = std::max(a, b);
    return (unsigned long long)(x + y) * (x + y + 1) / 2 + y;
}

void generateIcosphere(int subdivisions, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    // Initial 12 vertices of icosahedron
    vertices = {
        -1,  t,  0,  1,  t,  0, -1, -t,  0,  1, -t,  0,
         0, -1,  t,  0,  1,  t,  0, -1, -t,  0,  1, -t,
         t,  0, -1,  t,  0,  1, -t,  0, -1, -t,  0,  1
    };

    // Normalize initial vertices
    for (size_t i = 0; i < vertices.size(); i += 3) {
        float x = vertices[i], y = vertices[i + 1], z = vertices[i + 2];
        float len = std::sqrt(x * x + y * y + z * z);
        vertices[i] /= len;
        vertices[i + 1] /= len;
        vertices[i + 2] /= len;
    }

    // Initial 20 triangles of icosahedron
    indices = {
        0, 11, 5,  0, 5, 1,   0, 1, 7,    0, 7, 10,  0, 10, 11,
        11, 10, 2,  5, 11, 4,  1, 5, 9,    7, 1, 8,  10, 7, 6,
        3, 9, 4,   3, 4, 2,   3, 2, 6,    3, 6, 8,   3, 8, 9,
        4, 9, 5,   2, 4, 11,  6, 2, 10,   8, 6, 7,   9, 8, 1
    };

    std::unordered_map<unsigned long long, unsigned int> midpointCache;

    auto addMidpoint = [&](unsigned int a, unsigned int b) -> unsigned int {
        unsigned long long key = cantorPair(a, b);
        auto it = midpointCache.find(key);
        if (it != midpointCache.end()) return it->second;

        float ax = vertices[3 * a], ay = vertices[3 * a + 1], az = vertices[3 * a + 2];
        float bx = vertices[3 * b], by = vertices[3 * b + 1], bz = vertices[3 * b + 2];
        float mx = (ax + bx) * 0.5f;
        float my = (ay + by) * 0.5f;
        float mz = (az + bz) * 0.5f;
        float len = std::sqrt(mx * mx + my * my + mz * mz);
        mx /= len;
        my /= len;
        mz /= len;

        unsigned int index = static_cast<unsigned int>(vertices.size() / 3);
        vertices.push_back(mx);
        vertices.push_back(my);
        vertices.push_back(mz);

        midpointCache[key] = index;
        return index;
    };

    for (int i = 0; i < subdivisions; ++i) {
        std::vector<unsigned int> newIndices;
        for (size_t j = 0; j < indices.size(); j += 3) {
            unsigned int v1 = indices[j];
            unsigned int v2 = indices[j + 1];
            unsigned int v3 = indices[j + 2];

            unsigned int a = addMidpoint(v1, v2);
            unsigned int b = addMidpoint(v2, v3);
            unsigned int c = addMidpoint(v3, v1);

            newIndices.insert(newIndices.end(), {
                v1, a, c,
                v2, b, a,
                v3, c, b,
                a, b, c
            });
        }
        indices = std::move(newIndices);
    }
}

void generateBall(
    ShapeType shape,
    int fidelity,
    std::vector<float>& vertices,
    std::vector<unsigned int>& indices
    ){
    if (shape == ICOSPHERE) {
        generateIcosphere(fidelity*4, vertices, indices);
    } else {
        generateSphereMesh(fidelity*12, fidelity*12, vertices, indices);
    }
}
void generateBowl(
    ShapeType shape,
    int fidelity,
    float sigma,     // clipping angle from top in radians, 0 = tip, PI = full sphere
    float innerRadius,
    float outerRadius,
    std::vector<float>& vertices,
    std::vector<unsigned int>& indices
) {
    std::vector<float> fullVerts;
    std::vector<unsigned int> fullIndices;

    // Generate full sphere mesh first
    if (shape == ICOSPHERE) {
        generateIcosphere(fidelity, fullVerts, fullIndices);
    } else {
        generateSphereMesh(fidelity*12, fidelity*12, fullVerts, fullIndices);
    }

    // Maps old vertex index to new vertex index for outer and inner surfaces
    std::vector<unsigned int> outerMap(fullVerts.size() / 3, std::numeric_limits<unsigned int>::max());
    std::vector<unsigned int> innerMap(fullVerts.size() / 3, std::numeric_limits<unsigned int>::max());
    std::vector<float> bowlVerts;
    std::vector<unsigned int> bowlIndices;

    // Precompute cosine of sigma for clipping by angle from top (y-axis)
    float cosSigma = std::cos(sigma);

    // Keep vertices only where angle between vertex pos and +Y axis is >= sigma
    // Equivalently, keep vertices where y <= cosSigma (since normalized vertices)
    for (size_t i = 0; i < fullVerts.size(); i += 3) {
        glm::vec3 pos(fullVerts[i], fullVerts[i + 1], fullVerts[i + 2]);
        float y = pos.y;

        if (y <= cosSigma) {
            glm::vec3 norm = glm::normalize(pos);

            // Outer vertex
            unsigned int outerIdx = static_cast<unsigned int>(bowlVerts.size() / 3);
            outerMap[i / 3] = outerIdx;
            glm::vec3 outerPos = norm * outerRadius;
            bowlVerts.push_back(outerPos.x);
            bowlVerts.push_back(outerPos.y);
            bowlVerts.push_back(outerPos.z);

            // Inner vertex (norm reversed)
            unsigned int innerIdx = static_cast<unsigned int>(bowlVerts.size() / 3);
            innerMap[i / 3] = innerIdx;
            glm::vec3 innerPos = norm * innerRadius;
            bowlVerts.push_back(innerPos.x);
            bowlVerts.push_back(innerPos.y);
            bowlVerts.push_back(innerPos.z);
        }
    }

    // Outer surface triangles + inner surface triangles (reversed winding)
    for (size_t i = 0; i < fullIndices.size(); i += 3) {
        unsigned int a = fullIndices[i];
        unsigned int b = fullIndices[i + 1];
        unsigned int c = fullIndices[i + 2];

        if (outerMap[a] != std::numeric_limits<unsigned int>::max() &&
            outerMap[b] != std::numeric_limits<unsigned int>::max() &&
            outerMap[c] != std::numeric_limits<unsigned int>::max()) {
            bowlIndices.push_back(outerMap[a]);
            bowlIndices.push_back(outerMap[b]);
            bowlIndices.push_back(outerMap[c]);
        }

        if (innerMap[a] != std::numeric_limits<unsigned int>::max() &&
            innerMap[b] != std::numeric_limits<unsigned int>::max() &&
            innerMap[c] != std::numeric_limits<unsigned int>::max()) {
            // reversed winding order for inner surface
            bowlIndices.push_back(innerMap[c]);
            bowlIndices.push_back(innerMap[b]);
            bowlIndices.push_back(innerMap[a]);
        }
    }

    // Side wall stitching along the cutoff rim (where vertex.y approx equals cosSigma)
    const float epsilon = 0.01f;
    for (size_t i = 0; i < fullVerts.size(); i += 3) {
        float y = fullVerts[i + 1];
        if (std::abs(y - cosSigma) <= epsilon) {
            unsigned int outerIdx = outerMap[i / 3];
            unsigned int innerIdx = innerMap[i / 3];
            if (outerIdx == std::numeric_limits<unsigned int>::max() || innerIdx == std::numeric_limits<unsigned int>::max())
                continue;

            // Try to find a neighbor vertex near rim to create quad
            // For simplicity, loop through all vertices again, connect closest in rim neighborhood
            for (size_t j = i + 3; j < fullVerts.size(); j += 3) {
                float y2 = fullVerts[j + 1];
                if (std::abs(y2 - cosSigma) <= epsilon) {
                    unsigned int outerNext = outerMap[j / 3];
                    unsigned int innerNext = innerMap[j / 3];
                    if (outerNext == std::numeric_limits<unsigned int>::max() || innerNext == std::numeric_limits<unsigned int>::max())
                        continue;

                    // Build two triangles for side wall quad: outerIdx, outerNext, innerNext, innerIdx
                    bowlIndices.push_back(outerIdx);
                    bowlIndices.push_back(outerNext);
                    bowlIndices.push_back(innerNext);

                    bowlIndices.push_back(innerNext);
                    bowlIndices.push_back(innerIdx);
                    bowlIndices.push_back(outerIdx);
                    break;
                }
            }
        }
    }

    vertices = std::move(bowlVerts);
    indices = std::move(bowlIndices);
}
