#pragma once
// The one renderer: raymarches the 4D SDF scene (resources/shaders/raymarch4d.fs
// with world4d.glsl pasted in) into an offscreen target at `renderScale` of
// the window, then a present pass upscales it and draws the slice indicator.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "camera4d.hpp"
#include "filesystem.h"

struct FruitInstance {
    glm::vec4 center;
    float radius;
    int type;
    float flash = 0.0f; // 0..1 highlight, e.g. just merged
};

class RaymarchRenderer {
public:
    static constexpr int kMaxFruit = 64; // uniform budget fits WebGL2's 224 vec4 minimum

    float renderScale = 1.0f; // fraction of window resolution the scene is marched at
    float dim = 0.0f;         // darken the scene behind menus, 0..1
    float facet = 0.85f;      // low-poly facet strength, 0 = smooth
    glm::vec4 sunDir = glm::normalize(glm::vec4(-0.55f, 0.32f, -0.75f, 0.18f));

    bool init() {
        scene_ = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/raymarch4d.fs");
        present_ = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/present4d.fs");
        glGenVertexArrays(1, &vao_);
        return scene_ && present_;
    }

    // Fruit for this frame, in world space. Only fruit that cross the
    // camera's slice are visible; they are the ones uploaded.
    void setFruit(const std::vector<FruitInstance>& fruit) { fruit_ = fruit; }
    void setPreview(bool show, const glm::vec4& center, float radius, int type) {
        previewCenter_ = center;
        previewData_ = show ? glm::vec4(radius, float(type), 0, 0) : glm::vec4(0.0f);
    }

    // Draws the frame into the currently bound draw framebuffer (window or a
    // screenshot target) of size width x height.
    void draw(const Camera4D& cam, int width, int height, float time) {
        GLint target = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
        int rw = std::max(1, int(width * renderScale + 0.5f));
        int rh = std::max(1, int(height * renderScale + 0.5f));
        ensureTarget(rw, rh);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(vao_);

        // Scene pass.
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, rw, rh);
        glUseProgram(scene_);
        glm::mat4 basis = cam.basis();
        glm::vec4 pos = cam.position();
        uniform(scene_, "uResolution", glm::vec2(rw, rh));
        glUniform4fv(loc(scene_, "uCameraPos"), 1, glm::value_ptr(pos));
        glUniformMatrix4fv(loc(scene_, "uCameraBasis"), 1, GL_FALSE, glm::value_ptr(basis));
        glUniform1f(loc(scene_, "uTanHalfFov"), std::tan(cam.fovY * 0.5f));
        glUniform1f(loc(scene_, "uTime"), time);
        glUniform4fv(loc(scene_, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform1f(loc(scene_, "uFacet"), facet);
        uploadFruit(pos, basis[3]);
        glUniform4fv(loc(scene_, "uPreviewCenter"), 1, glm::value_ptr(previewCenter_));
        glUniform4fv(loc(scene_, "uPreviewData"), 1, glm::value_ptr(previewData_));
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Present pass.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
        glViewport(0, 0, width, height);
        glUseProgram(present_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, color_);
        glUniform1i(loc(present_, "uScene"), 0);
        glUniformMatrix4fv(loc(present_, "uCameraBasis"), 1, GL_FALSE, glm::value_ptr(basis));
        float size = std::max(28.0f, height * 0.045f);
        glm::vec4 gizmo(size * 1.9f, size * 1.9f, size, cam.offset / Camera4D::kOffsetMax);
        glUniform4fv(loc(present_, "uGizmo"), 1, glm::value_ptr(gizmo));
        glUniform1f(loc(present_, "uDim"), dim);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindVertexArray(0);
        glEnable(GL_BLEND);
    }

private:
    GLuint scene_ = 0, present_ = 0, vao_ = 0;
    GLuint fbo_ = 0, color_ = 0;
    int fboW_ = 0, fboH_ = 0;
    std::vector<FruitInstance> fruit_;
    glm::vec4 previewCenter_{0.0f}, previewData_{0.0f};

    static GLint loc(GLuint p, const char* name) { return glGetUniformLocation(p, name); }
    static void uniform(GLuint p, const char* name, glm::vec2 v) { glUniform2f(loc(p, name), v.x, v.y); }

    void uploadFruit(const glm::vec4& camPos, const glm::vec4& normal) {
        // Rays never leave the slice, so a fruit is visible only if its glome
        // crosses the slice hyperplane. Nearest first if over budget.
        std::vector<std::pair<float, const FruitInstance*>> visible;
        for (const auto& f : fruit_) {
            float off = glm::dot(f.center - camPos, normal);
            if (std::abs(off) < f.radius) visible.push_back({glm::length(f.center - camPos), &f});
        }
        std::sort(visible.begin(), visible.end(),
                  [](auto& a, auto& b) { return a.first < b.first; });
        int n = std::min<int>(int(visible.size()), kMaxFruit);
        std::vector<glm::vec4> centers(n), data(n);
        for (int i = 0; i < n; ++i) {
            const FruitInstance& f = *visible[i].second;
            centers[i] = f.center;
            data[i] = glm::vec4(f.radius, float(f.type), f.flash, 0.0f);
        }
        glUniform1i(loc(scene_, "uFruitCount"), n);
        if (n > 0) {
            glUniform4fv(loc(scene_, "uFruitCenters"), n, glm::value_ptr(centers[0]));
            glUniform4fv(loc(scene_, "uFruitData"), n, glm::value_ptr(data[0]));
        }
    }

    void ensureTarget(int w, int h) {
        if (fbo_ && w == fboW_ && h == fboH_) return;
        if (!fbo_) { glGenFramebuffers(1, &fbo_); glGenTextures(1, &color_); }
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
        fboW_ = w; fboH_ = h;
    }

    static std::string readFile(const std::string& rel) {
        std::ifstream f(FileSystem::getPath(rel));
        if (!f) { std::cerr << "Cannot read " << rel << "\n"; return ""; }
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // Prepends the GLSL version for this platform and pastes `#include "x"`
    // lines with files from resources/shaders/.
    static std::string preprocess(const std::string& src) {
#ifdef __EMSCRIPTEN__
        std::string out = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
        std::string out = "#version 330 core\n";
#endif
        out += "#define MAX_FRUIT " + std::to_string(kMaxFruit) + "\n";
        std::istringstream in(src);
        std::string line;
        while (std::getline(in, line)) {
            auto p = line.find("#include \"");
            if (p != std::string::npos && line.find_first_not_of(" \t") == p) {
                auto q = line.find('"', p + 10);
                out += readFile("resources/shaders/" + line.substr(p + 10, q - p - 10)) + "\n";
            } else {
                out += line + "\n";
            }
        }
        return out;
    }

    static GLuint compile(GLenum type, const std::string& src, const char* name) {
        GLuint s = glCreateShader(type);
        const char* c = src.c_str();
        glShaderSource(s, 1, &c, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            std::cerr << "Shader compile error in " << name << ":\n" << log << "\n";
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    static GLuint buildProgram(const char* vsPath, const char* fsPath) {
        GLuint vs = compile(GL_VERTEX_SHADER, preprocess(readFile(vsPath)), vsPath);
        GLuint fs = compile(GL_FRAGMENT_SHADER, preprocess(readFile(fsPath)), fsPath);
        if (!vs || !fs) return 0;
        GLuint p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetProgramInfoLog(p, sizeof log, nullptr, log);
            std::cerr << "Shader link error (" << fsPath << "):\n" << log << "\n";
            return 0;
        }
        return p;
    }
};
