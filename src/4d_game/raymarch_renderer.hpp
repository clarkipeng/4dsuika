#pragma once
// The one renderer: raymarches the 4D SDF scene (resources/shaders/raymarch4d.fs
// with world4d.glsl pasted in) into an offscreen target at `renderScale` of
// the output, then a present pass upscales it and draws the slice indicator.
// Resolution adapts to the measured GPU time; a quality preset caps it and
// sets the march budgets.
//
// Slice cache: the static world (worldStaticSDF) is baked for the current
// slice into a 3D distance texture, rebaked only when the slice turns or
// moves; the scene pass marches that and uses the analytic SDF only near
// surfaces (docs/4D.md, "Slice cache").
#include <algorithm>
#include <cmath>
#include <cstdio>
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
    int id = 0;         // stable per fruit while it lives; varies its facets
};

enum class Quality { Low = 0, Medium = 1, High = 2, Ultra = 3 };

inline const char* qualityName(Quality q) {
    static const char* names[] = {"Low", "Medium", "High", "Ultra"};
    return names[int(q)];
}

class RaymarchRenderer {
public:
    static constexpr int kMaxFruit = 64; // with hints, fits WebGL2's 224-vec4 minimum
    static constexpr int kMaxHints = 16;
    static constexpr float kHintRange = 1.5f; // how far off the slice a fruit still hints

    float dim = 0.0f;         // darken the scene behind menus, 0..1
    float facet = 0.85f;      // low-poly facet strength, 0 = smooth
    glm::vec4 sunDir = glm::normalize(glm::vec4(-0.55f, 0.32f, -0.75f, 0.18f));

    // Resolution control. The scene is marched at `renderScale` of the
    // output, capped by the quality preset's pixel budget. With `adaptive`,
    // the scale follows the measured GPU time of the scene pass towards
    // `targetMs`, between kMinScaleOfCap of the cap and the cap.
    bool adaptive = true;
    float targetMs = 11.0f;   // scene pass budget inside a 16.7 ms frame
    float renderScale = 1.0f; // current scale (set directly when not adaptive)
    bool syncTiming = false;  // wait for each timing result (benchmarks)
    bool useCache = true;     // slice cache on (falls back to analytic if unsupported)
    std::string extraDefines; // added to the scene shader on the next setQuality

    // GPU time of the last cache bake in ms, and how many bakes ran.
    float bakeMs() const { return bakeMs_; }
    int bakeCount() const { return bakeCount_; }

    bool init() {
        present_ = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/present4d.fs", "");
        bake_ = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/bake4d.fs", "");
        readStaticBound();
        // The cache texture always exists (1 texel until the first bake) so
        // the scene shader's sampler is valid even with the cache off.
        glGenTextures(1, &cacheTex_);
        glGenFramebuffers(1, &cacheFbo_);
        glBindTexture(GL_TEXTURE_3D, cacheTex_);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, 1, 1, 1, 0, GL_RED, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#ifndef __EMSCRIPTEN__
        glGenQueries(1, &bakeQuery_);
#endif
        glGenVertexArrays(1, &vao_);
#ifndef __EMSCRIPTEN__
        glGenQueries(kQueries, queries_);
#endif
        setQuality(quality_);
        return scene_ && present_;
    }

    Quality quality() const { return quality_; }
    void setQuality(Quality q) {
        quality_ = q;
        std::string defs = "#define QUALITY " + std::to_string(int(q)) + "\n" + extraDefines;
        GLuint p = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/raymarch4d.fs", defs);
        if (p) {
            if (scene_) glDeleteProgram(scene_);
            scene_ = p;
        }
        renderScale = 1.0f; // re-clamped against the new cap next frame
        cacheValid_ = false;
    }

    // Texels per side of the slice cache for each preset.
    int cacheResolution() const {
        static const int res[] = {96, 128, 160, 192};
        return res[int(quality_)];
    }

    // Largest scale the preset allows at this output size.
    float maxScale(int width, int height) const {
        static const float budget[] = {960.0f * 540.0f, 1280.0f * 720.0f, 1920.0f * 1080.0f, 1e12f};
        return std::min(1.0f, std::sqrt(budget[int(quality_)] / float(std::max(width * height, 1))));
    }

    // Smoothed GPU time of the scene pass in ms (0 when timers are unavailable).
    float gpuMs() const { return gpuMs_; }
    float lastGpuMs() const { return lastGpuMs_; }

    // Without GPU timers (WebGL), adapt from whole-frame time instead.
    void reportFrameTime(float ms) { frameMs_ += (ms - frameMs_) * 0.1f; }

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
        float cap = maxScale(width, height);
        pollTimers();
        if (adaptive) adapt(cap);
        renderScale = std::clamp(renderScale, 0.25f, cap);
        // The target is sized for the cap; smaller scales use a sub-rectangle,
        // so changing scale never reallocates.
        ensureTarget(std::max(1, int(std::ceil(width * cap))), std::max(1, int(std::ceil(height * cap))));
        int rw = std::clamp(int(width * renderScale + 0.5f), 1, fboW_);
        int rh = std::clamp(int(height * renderScale + 0.5f), 1, fboH_);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(vao_);
        bool cached = useCache && updateCache(cam, time);

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
        glUniform1f(loc(scene_, "uFogStart"), cam.distance + 5.0f);
        uploadFruit(pos, basis[3]);
        glUniform4fv(loc(scene_, "uPreviewCenter"), 1, glm::value_ptr(previewCenter_));
        glUniform4fv(loc(scene_, "uPreviewData"), 1, glm::value_ptr(previewData_));
        glUniform1i(loc(scene_, "uCacheMode"), cached ? 1 : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, cacheTex_);
        glUniform1i(loc(scene_, "uCache"), 1);
        glUniform4fv(loc(scene_, "uCacheOrigin"), 1, glm::value_ptr(cacheOrigin_));
        glUniformMatrix4fv(loc(scene_, "uCacheBasis"), 1, GL_FALSE, glm::value_ptr(cacheBasis_));
        glUniform1f(loc(scene_, "uCacheSize"), cacheSize_);
        glUniform1f(loc(scene_, "uCacheVoxel"), cacheVoxel_);
        beginTimer();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        endTimer();

        // Present pass.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
        glViewport(0, 0, width, height);
        glUseProgram(present_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, color_);
        glUniform1i(loc(present_, "uScene"), 0);
        uniform(present_, "uUVScale", glm::vec2(float(rw) / fboW_, float(rh) / fboH_));
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
    static constexpr int kQueries = 4;
    static constexpr float kMinScaleOfCap = 0.35f; // adaptive floor: keep frame rate over sharpness
    GLuint scene_ = 0, present_ = 0, bake_ = 0, vao_ = 0;
    // Slice cache.
    GLuint cacheTex_ = 0, cacheFbo_ = 0, bakeQuery_ = 0;
    int cacheRes_ = 0;
    bool cacheValid_ = false, cacheUnsupported_ = false;
    glm::vec4 staticCenter_{0.0f, -3.0f, 0.0f, 0.0f};
    float staticRadius_ = 17.0f;
    glm::vec4 bakedSliceOrigin_{0.0f};
    glm::mat4 bakedSlice_{0.0f};
    glm::vec4 cacheOrigin_{0.0f};
    glm::mat4 cacheBasis_{1.0f};
    float cacheSize_ = 0.0f, cacheVoxel_ = 1.0f;
    float bakeMs_ = 0.0f;
    int bakeCount_ = 0;
    GLuint fbo_ = 0, color_ = 0;
    int fboW_ = 0, fboH_ = 0;
    Quality quality_ = Quality::High;
    std::vector<FruitInstance> fruit_;
    glm::vec4 previewCenter_{0.0f}, previewData_{0.0f};
    GLuint queries_[kQueries] = {};
    bool queryPending_[kQueries] = {};
    int queryNext_ = 0;
    float gpuMs_ = 0.0f, lastGpuMs_ = 0.0f, frameMs_ = 0.0f;

    void beginTimer() {
#ifndef __EMSCRIPTEN__
        if (queryPending_[queryNext_]) return; // slot still in flight: skip timing this frame
        glBeginQuery(GL_TIME_ELAPSED, queries_[queryNext_]);
#endif
    }
    void endTimer() {
#ifndef __EMSCRIPTEN__
        if (queryPending_[queryNext_]) return;
        glEndQuery(GL_TIME_ELAPSED);
        queryPending_[queryNext_] = true;
        if (syncTiming) pollTimers(true);
        queryNext_ = (queryNext_ + 1) % kQueries;
#endif
    }
    void pollTimers(bool wait = false) {
#ifndef __EMSCRIPTEN__
        for (int i = 0; i < kQueries; ++i) {
            if (!queryPending_[i]) continue;
            GLint ready = 0;
            if (!wait) glGetQueryObjectiv(queries_[i], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!wait && !ready) continue;
            GLuint64 ns = 0;
            glGetQueryObjectui64v(queries_[i], GL_QUERY_RESULT, &ns);
            queryPending_[i] = false;
            lastGpuMs_ = float(ns) * 1e-6f;
            gpuMs_ = gpuMs_ == 0.0f ? lastGpuMs_ : gpuMs_ + (lastGpuMs_ - gpuMs_) * 0.15f;
        }
#endif
    }
    // Cost scales with pixel count (scale squared), so step the scale by the
    // square root of the time ratio, gently, in both directions.
    void adapt(float cap) {
        float ms = gpuMs_ > 0.0f ? gpuMs_ : 0.0f;
        if (ms == 0.0f) {
            // Frame-time fallback: vsync hides headroom, so only back off when
            // frames run long and creep up slowly otherwise.
            if (frameMs_ <= 0.0f) return;
            ms = frameMs_ > 18.5f ? targetMs * frameMs_ / 16.7f : targetMs * 0.9f;
        }
        float ratio = std::sqrt(targetMs / std::max(ms, 0.1f));
        float step = std::clamp(ratio, 0.97f, 1.01f);
        renderScale = std::clamp(renderScale * step, cap * kMinScaleOfCap, cap);
    }

    // The static world's bound glome, read from world4d.glsl so it has one source.
    void readStaticBound() {
        std::string w = readFile("resources/shaders/world4d.glsl");
        auto c = w.find("#define WORLD_STATIC_CENTER vec4(");
        auto r = w.find("#define WORLD_STATIC_RADIUS ");
        if (c == std::string::npos || r == std::string::npos) {
            std::cerr << "world4d.glsl has no WORLD_STATIC_CENTER/RADIUS; slice cache off\n";
            cacheUnsupported_ = true;
            return;
        }
        glm::vec4& v = staticCenter_;
        std::sscanf(w.c_str() + c + 33, "%f , %f , %f , %f", &v.x, &v.y, &v.z, &v.w);
        std::sscanf(w.c_str() + r + 28, "%f", &staticRadius_);
    }

    // Rebakes the cache if the slice moved; false when the cache cannot be used.
    bool updateCache(const Camera4D& cam, float time) {
        if (cacheUnsupported_ || !bake_) return false;
        int res = cacheResolution();
        if (res != cacheRes_) {
            glBindTexture(GL_TEXTURE_3D, cacheTex_);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_R16F, res, res, res, 0, GL_RED, GL_HALF_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, cacheFbo_);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, cacheTex_, 0, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "R16F 3D texture not renderable; slice cache off\n";
                cacheUnsupported_ = true;
                return false;
            }
            cacheRes_ = res;
            cacheValid_ = false;
        }

        // The slice: through target + offset * normal, spanned by the slice's
        // first three axes. The orbit turns only within it, so it never rebakes.
        glm::mat4 M = cam.slice.matrix();
        glm::vec4 o = cam.target + cam.offset * M[3];
        if (cacheValid_ && o == bakedSliceOrigin_ && M == bakedSlice_) return true;
        bakedSliceOrigin_ = o;
        bakedSlice_ = M;
        cacheValid_ = true;

        // The static bound glome meets the slice in a ball; bake its bounding cube.
        float off = glm::dot(staticCenter_ - o, M[3]);
        float rho2 = staticRadius_ * staticRadius_ - off * off;
        cacheBasis_ = M;
        if (rho2 <= 0.0f) { cacheSize_ = 0.0f; return true; }
        float rho = std::sqrt(rho2);
        glm::vec3 centre = glm::vec3(glm::transpose(M) * (staticCenter_ - o));
        glm::vec3 corner = centre - glm::vec3(rho);
        cacheOrigin_ = o + M * glm::vec4(corner, 0.0f);
        cacheSize_ = 2.0f * rho;
        cacheVoxel_ = cacheSize_ / float(cacheRes_);

        glBindFramebuffer(GL_FRAMEBUFFER, cacheFbo_);
        glViewport(0, 0, cacheRes_, cacheRes_);
        glUseProgram(bake_);
        glUniform1f(loc(bake_, "uTime"), time);
        glUniform4fv(loc(bake_, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform4fv(loc(bake_, "uCacheOrigin"), 1, glm::value_ptr(cacheOrigin_));
        glUniformMatrix4fv(loc(bake_, "uCacheBasis"), 1, GL_FALSE, glm::value_ptr(cacheBasis_));
        glUniform1f(loc(bake_, "uCacheVoxel"), cacheVoxel_);
        GLint layerLoc = loc(bake_, "uLayer");
#ifndef __EMSCRIPTEN__
        bool timed = syncTiming && bakeQuery_;
        if (timed) glBeginQuery(GL_TIME_ELAPSED, bakeQuery_);
#endif
        for (int z = 0; z < cacheRes_; ++z) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, cacheTex_, 0, z);
            glUniform1f(layerLoc, float(z));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
#ifndef __EMSCRIPTEN__
        if (timed) {
            glEndQuery(GL_TIME_ELAPSED);
            GLuint64 ns = 0;
            glGetQueryObjectui64v(bakeQuery_, GL_QUERY_RESULT, &ns);
            bakeMs_ = float(ns) * 1e-6f;
        }
#endif
        ++bakeCount_;
        return true;
    }

    static GLint loc(GLuint p, const char* name) { return glGetUniformLocation(p, name); }
    static void uniform(GLuint p, const char* name, glm::vec2 v) { glUniform2f(loc(p, name), v.x, v.y); }

    void uploadFruit(const glm::vec4& camPos, const glm::vec4& normal) {
        // Rays never leave the slice, so a fruit is visible only if its glome
        // crosses the slice hyperplane; fruit within kHintRange beyond that
        // become depth hints. Nearest (to the camera, then the slice) first.
        using Entry = std::pair<float, const FruitInstance*>;
        std::vector<Entry> visible, nearby; // (not "near": a Windows macro)
        for (const auto& f : fruit_) {
            float gap = std::abs(glm::dot(f.center - camPos, normal)) - f.radius;
            if (gap < 0.0f) visible.push_back({glm::length(f.center - camPos), &f});
            else if (gap < kHintRange) nearby.push_back({gap, &f});
        }
        auto byKey = [](const Entry& a, const Entry& b) { return a.first < b.first; };
        std::sort(visible.begin(), visible.end(), byKey);
        std::sort(nearby.begin(), nearby.end(), byKey);

        int n = std::min<int>(int(visible.size()), kMaxFruit);
        std::vector<glm::vec4> centers(n), data(n);
        for (int i = 0; i < n; ++i) {
            const FruitInstance& f = *visible[i].second;
            centers[i] = f.center;
            data[i] = glm::vec4(f.radius, float(f.type), f.flash, float(f.id + 1));
        }
        glUniform1i(loc(scene_, "uFruitCount"), n);
        if (n > 0) {
            glUniform4fv(loc(scene_, "uFruitCenters"), n, glm::value_ptr(centers[0]));
            glUniform4fv(loc(scene_, "uFruitData"), n, glm::value_ptr(data[0]));
        }

        int h = std::min<int>(int(nearby.size()), kMaxHints);
        std::vector<glm::vec4> hc(h), hd(h);
        for (int i = 0; i < h; ++i) {
            const FruitInstance& f = *nearby[i].second;
            float off = glm::dot(f.center - camPos, normal);
            float gap = nearby[i].first;
            hc[i] = f.center - off * normal; // projected into the slice
            float strength = 1.0f - gap / kHintRange;
            hd[i] = glm::vec4(f.radius, float(f.type), off, strength * strength);
        }
        glUniform1i(loc(scene_, "uHintCount"), h);
        if (h > 0) {
            glUniform4fv(loc(scene_, "uHintCenters"), h, glm::value_ptr(hc[0]));
            glUniform4fv(loc(scene_, "uHintData"), h, glm::value_ptr(hd[0]));
        }
    }

    void ensureTarget(int w, int h) {
        // Keep the buffer if it is already exactly this size.
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
    static std::string preprocess(const std::string& src, const std::string& defines) {
#ifdef __EMSCRIPTEN__
        std::string out = "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                          "precision highp sampler3D;\n";
#else
        std::string out = "#version 330 core\n";
#endif
        out += "#define MAX_FRUIT " + std::to_string(kMaxFruit) + "\n";
        out += "#define MAX_HINTS " + std::to_string(kMaxHints) + "\n";
        out += defines;
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

    static GLuint buildProgram(const char* vsPath, const char* fsPath, const std::string& defines) {
        GLuint vs = compile(GL_VERTEX_SHADER, preprocess(readFile(vsPath), defines), vsPath);
        GLuint fs = compile(GL_FRAGMENT_SHADER, preprocess(readFile(fsPath), defines), fsPath);
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
