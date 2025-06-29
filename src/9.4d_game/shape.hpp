#pragma once

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

inline void generateSphereMesh(int stacks, int slices, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
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

inline void generateIcosphere(int subdivisions, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
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

inline void generateBall(
    ShapeType shape,
    int fidelity,
    std::vector<float>& vertices,
    std::vector<unsigned int>& indices
    ){
    if (shape == ICOSPHERE) {
        generateIcosphere(fidelity, vertices, indices);
    } else {
        generateSphereMesh(fidelity*12, fidelity*12, vertices, indices);
    }
}
inline void generateBowlMesh(
    int stacks,
    int slices,
    float sigma,              // Clipping angle from top, in radians (0 = tip, PI = hemisphere)
    float innerRadius,
    float outerRadius,
    std::vector<float>& vertices,
    std::vector<unsigned int>& indices
) {
    vertices.clear();
    indices.clear();

    int bowlStacks = std::max(1, int((sigma / glm::pi<float>()) * stacks));

    // Outer & inner shell
    for (int shell = 0; shell < 2; ++shell) {
        float radius = (shell == 0) ? outerRadius : innerRadius;
        bool flip = (shell == 1);

        for (int i = 0; i <= bowlStacks; ++i) {
            float v = (float)i / bowlStacks;
            float phi = sigma * v;

            for (int j = 0; j <= slices; ++j) {
                float u = (float)j / slices;
                float theta = 2.0f * glm::pi<float>() * u;

                float x = sin(phi) * cos(theta);
                float y = -cos(phi);
                float z = sin(phi) * sin(theta);

                glm::vec3 pos = radius * glm::vec3(x, y, z);
                glm::vec3 norm = glm::normalize(glm::vec3(x, y, z));
                if (flip) norm = -norm;

                vertices.push_back(pos.x);
                vertices.push_back(pos.y);
                vertices.push_back(pos.z);
                vertices.push_back(norm.x);
                vertices.push_back(norm.y);
                vertices.push_back(norm.z);
                vertices.push_back(u);       // tex.u
                vertices.push_back(v);       // tex.v
            }
        }
    }

    int ringVerts = slices + 1;
    int outerOffset = 0;
    int innerOffset = (bowlStacks + 1) * ringVerts;

    // Outer shell indices
    for (int i = 0; i < bowlStacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int a = outerOffset + i * ringVerts + j;
            int b = a + ringVerts;
            int c = a + 1;
            int d = b + 1;

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);

            indices.push_back(b);
            indices.push_back(d);
            indices.push_back(c);
        }
    }

    // Inner shell indices (reverse winding)
    for (int i = 0; i < bowlStacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int a = innerOffset + i * ringVerts + j;
            int b = a + ringVerts;
            int c = a + 1;
            int d = b + 1;

            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(a);

            indices.push_back(c);
            indices.push_back(d);
            indices.push_back(b);
        }
    }

    // Rim stitching
    for (int j = 0; j < slices; ++j) {
        int o0 = outerOffset + bowlStacks * ringVerts + j;
        int o1 = o0 + 1;
        int i0 = innerOffset + bowlStacks * ringVerts + j;
        int i1 = i0 + 1;

        // Outer to inner rim
        indices.push_back(o0);
        indices.push_back(o1);
        indices.push_back(i1);

        indices.push_back(i1);
        indices.push_back(i0);
        indices.push_back(o0);
    }
}


// void generateBowl(
//     ShapeType shape,
//     int fidelity,
//     float sigma,         // clipping angle from top in radians, 0 = tip, PI = full sphere
//     float innerRadius,
//     float outerRadius,
//     std::vector<float>& vertices,
//     std::vector<unsigned int>& indices
// ) {
//     std::vector<float> fullVerts;
//     std::vector<unsigned int> fullIndices;

//     if (shape == ICOSPHERE) {
//         generateIcosphere(fidelity, fullVerts, fullIndices);
//     } else {
//         generateSphereMesh(fidelity * 12, fidelity * 12, fullVerts, fullIndices);
//     }

//     float cosSigma = std::cos(sigma);
//     size_t vertCount = fullVerts.size() / 8;

//     std::vector<unsigned int> outerMap(vertCount, UINT_MAX);
//     std::vector<unsigned int> innerMap(vertCount, UINT_MAX);
    
//     std::vector<unsigned int> outerRim, innerRim;

//     for (size_t i = 0; i < vertCount; ++i) {
//         glm::vec3 pos(fullVerts[8 * i], fullVerts[8 * i + 1], fullVerts[8 * i + 2]);
//         if (pos.y <= cosSigma) {
//             glm::vec3 n = glm::normalize(pos);

//             glm::vec3 op = n * outerRadius;
//             unsigned int outerIndex = vertices.size() / 8;
//             outerMap[i] = outerIndex;
//             vertices.insert(vertices.end(), {op.x, op.y, op.z, n.x, n.y, n.z, (n.x + 1) * 0.5f, (n.y + 1) * 0.5f});

//             glm::vec3 ip = n * innerRadius;
//             unsigned int innerIndex = vertices.size() / 8;
//             innerMap[i] = innerIndex;
//             vertices.insert(vertices.end(), {ip.x, ip.y, ip.z, -n.x, -n.y, -n.z, (n.x + 1) * 0.5f, (n.y + 1) * 0.5f});
//         }
//         if (std::abs(pos.y - cosSigma) < 0.01f) {
//             glm::vec2 dir = glm::normalize(glm::vec2(pos.x, pos.z)); // radial direction
//             float y = cosSigma;
//             glm::vec3 op(dir.x * outerRadius, y, dir.y * outerRadius);
//             glm::vec3 on = glm::normalize(glm::vec3(dir.x, 0, dir.y));
//             unsigned int oIdx = vertices.size() / 8;
//             vertices.insert(vertices.end(), {
//                 op.x, op.y, op.z,
//                 on.x, on.y, on.z,
//                 (on.x + 1) * 0.5f, (on.z + 1) * 0.5f
//             });
//             outerRim.push_back(oIdx);

//             // Inner rim vertex
//             glm::vec3 ip(dir.x * innerRadius, y, dir.y * innerRadius);
//             glm::vec3 in = -on;
//             unsigned int iIdx = vertices.size() / 8;
//             vertices.insert(vertices.end(), {
//                 ip.x, ip.y, ip.z,
//                 in.x, in.y, in.z,
//                 (in.x + 1) * 0.5f, (in.z + 1) * 0.5f
//             });
//             innerRim.push_back(iIdx);
//         }
//     }

//     // Shell triangles
//     for (size_t i = 0; i < fullIndices.size(); i += 3) {
//         unsigned int a = fullIndices[i];
//         unsigned int b = fullIndices[i + 1];
//         unsigned int c = fullIndices[i + 2];

//         if (outerMap[a] != UINT_MAX && outerMap[b] != UINT_MAX && outerMap[c] != UINT_MAX) {
//             indices.insert(indices.end(), {outerMap[a], outerMap[b], outerMap[c]});
//         }
//         if (innerMap[a] != UINT_MAX && innerMap[b] != UINT_MAX && innerMap[c] != UINT_MAX) {
//             indices.insert(indices.end(), {innerMap[c], innerMap[b], innerMap[a]});
//         }
//     }
//     size_t count = outerRim.size();
//     for (size_t i = 0; i < count; ++i) {
//         size_t j = (i + 1) % count;

//         // Outer shell to rim
//         indices.insert(indices.end(), {
//             outerRim[i], outerRim[j], outerMap[i],
//             outerMap[i], outerMap[j], outerRim[j]
//         });

//         // Inner shell to rim (reverse winding)
//         indices.insert(indices.end(), {
//             innerMap[j], innerMap[i], innerRim[i],
//             innerRim[i], innerRim[j], innerMap[j]
//         });
//     }
// }
