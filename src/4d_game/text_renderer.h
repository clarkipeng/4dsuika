#pragma once
// 2D UI drawing: text from a mipmapped glyph atlas (two faces, UTF-8) and
// antialiased signed-distance shapes (rounded rects, capsules, arcs,
// triangles) with gradients and soft shadows. All sizes are in screen points.
// Text `scale` is relative to a nominal 24 px font, whatever size the atlas
// was baked at, so callers keep one meaning of scale.

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "learnopengl/shader.h"

struct Character {
    glm::vec2 Size;     // in nominal (24 px) units
    glm::vec2 Bearing;
    float Advance;
    glm::vec4 uv;       // u0, v0, u1, v1 in the atlas
};

// A shape for TextRenderer::Shape. Build one with the helpers below.
struct UiShape {
    enum Mode { RoundRect = 0, Capsule = 1, Arc = 2, Triangle = 3 };
    int mode = RoundRect;
    glm::vec2 a{0.0f}, b{0.0f}, c{0.0f};  // rect: centre, half size; capsule: ends; arc: centre; triangle: corners
    float radius = 0.0f;                  // corner radius / capsule radius / arc radius / triangle rounding
    float border = 0.0f;                  // > 0: only a ring this wide inside the edge (rect, arc thickness)
    glm::vec2 arc{0.0f, 6.2832f};         // arc: start angle, sweep (radians, y down, clockwise)
    float feather = 1.0f;                 // edge softness in points (1 = antialiased edge; large = shadow)
    glm::vec3 top{1.0f}, bottom{1.0f};    // vertical gradient over the shape's bounds
    float alpha = 1.0f;

    static UiShape rect(float x, float y, float w, float h, float r) {
        UiShape s; s.mode = RoundRect; s.a = {x + w * 0.5f, y + h * 0.5f}; s.b = {w * 0.5f, h * 0.5f};
        s.radius = std::min(r, std::min(w, h) * 0.5f); return s;
    }
    static UiShape circle(glm::vec2 c, float r) { return rect(c.x - r, c.y - r, 2.0f * r, 2.0f * r, r); }
    static UiShape capsule(glm::vec2 p, glm::vec2 q, float r) { UiShape s; s.mode = Capsule; s.a = p; s.b = q; s.radius = r; return s; }
    static UiShape arcRing(glm::vec2 c, float r, float thick, float start, float sweep) {
        UiShape s; s.mode = Arc; s.a = c; s.radius = r; s.border = thick; s.arc = {start, sweep}; return s;
    }
    static UiShape tri(glm::vec2 p, glm::vec2 q, glm::vec2 r, float round = 0.0f) {
        UiShape s; s.mode = Triangle; s.a = p; s.b = q; s.c = r; s.radius = round; return s;
    }
    UiShape& fill(glm::vec3 col, float a = 1.0f) { top = bottom = col; alpha = a; return *this; }
    UiShape& grad(glm::vec3 t, glm::vec3 btm, float a = 1.0f) { top = t; bottom = btm; alpha = a; return *this; }
    UiShape& soft(float f) { feather = f; return *this; }
    UiShape& ring(float w) { border = w; return *this; }
};

class TextRenderer {
public:
    enum Face { Body = 0, Display = 1, kFaces = 2 };

private:
    unsigned int vao, vbo, whiteTex = 0;
    unsigned int atlasTex[kFaces] = {0, 0};
    size_t vboBytes = 0;
    std::map<uint32_t, Character> Characters[kFaces];
    int face_ = Body;
    Shader* shapeShader_ = nullptr;

    void initRenderData();
    void stream(const std::vector<float>& v);
    const Character* glyph(uint32_t code) const;

public:
    Shader* shader;

    explicit TextRenderer(Shader* shader) : shader(shader) { initRenderData(); }
    ~TextRenderer();

    // Bakes `fontPath` into the atlas for `face` at `pixelSize`. Load() without a
    // face fills both (the old single-font API).
    void LoadFace(int face, const std::string& fontPath, unsigned int pixelSize);
    void Load(std::string fontPath, unsigned int fontSize) { LoadFace(Body, fontPath, fontSize); LoadFace(Display, fontPath, fontSize); }
    void SetShapeShader(Shader* s) { shapeShader_ = s; }
    // Ortho projection for both shaders (top-left origin, points).
    void Begin(float width, float height);

    void UseFace(int f) { face_ = (f >= 0 && f < kFaces) ? f : Body; }
    int CurrentFace() const { return face_; }

    void RenderText(const std::string& text, float x, float y, float scale, glm::vec3 color, float alpha = 1.0f);
    void RenderTextScale(const std::string& text, float x, float y, float targetWidth, float targetHeight, glm::vec3 color);
    float GetTextWidth(const std::string& text, float scale);
    // Height of a capital letter at this scale (RenderText's y is the top of capitals).
    float CapHeight(float scale);
    // Flat-coloured convex polygon (triangle fan), for hard-edged details.
    void FillPoly(const std::vector<glm::vec2>& points, glm::vec3 color, float alpha = 1.0f);
    // Antialiased SDF shape.
    void Shape(const UiShape& s);
};

// Restores the previous face at the end of a scope.
struct FaceScope {
    TextRenderer& t; int prev;
    FaceScope(TextRenderer& tr, int f) : t(tr), prev(tr.CurrentFace()) { t.UseFace(f); }
    ~FaceScope() { t.UseFace(prev); }
};
