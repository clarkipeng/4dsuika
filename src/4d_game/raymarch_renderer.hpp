#pragma once
// The one renderer: raymarches the 4D SDF scene (resources/shaders/raymarch4d.fs
// with world4d.glsl pasted in) into an offscreen target at `renderScale` of
// the output, then a present pass upscales it and draws the slice indicator.
// The scene runs as three passes (geometry into a G-buffer, shadow and AO at
// half resolution, compose) when float render targets exist, else as one.
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
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "camera4d.hpp"
#include "filesystem.h"

// A merge burst: a 4D shell growing from the merge point; the slice sees the
// ring where it cuts the shell.
struct BurstInstance {
    glm::vec4 center;
    float radius;   // of the merged fruit
    int type;
    float progress; // 0 (just merged) .. 1 (gone)
    float power;    // 1, more in a combo
};

struct FruitInstance {
    glm::vec4 center;
    float radius;
    int type;
    float flash = 0.0f; // 0..1 highlight, e.g. just merged
    int id = 0;         // stable per fruit while it lives; varies its facets
    glm::mat4 worldToLocal{1.0f}; // fruit orientation, world offset already removed
};

enum class Quality { Low = 0, Medium = 1, High = 2, Ultra = 3 };

// How the 4D scene is shown (docs/4D.md, "View modes"). Slice is the game's
// view; the others are for comparison and are off by default.
enum class ViewMode { Slice = 0, Eye = 1 };
inline const char* viewModeName(ViewMode m) {
    static const char* names[] = {"Slice", "4D eye"};
    return names[int(m)];
}

inline const char* qualityName(Quality q) {
    static const char* names[] = {"Low", "Medium", "High", "Ultra"};
    return names[int(q)];
}

class RaymarchRenderer {
public:
    static constexpr int kMaxFruit = 64;
    static constexpr int kMaxHints = 16;
    static constexpr int kMaxBursts = 8;
    static constexpr float kHintRange = 1.5f; // how far off the slice a fruit still hints
    int viewportX = 0;                        // where in the target the frame is presented (photo mode draws two side by side)
    bool showGizmo = true;                    // false: no corner axis gizmo (--no-ui captures)
    bool cues4D = true;                       // false: no depth rings, hints, glow or slice indicator (the 3D start of the tutorial)
    bool sameKindGlow = true;                 // Settings > Merge glow, --no-glow
    static constexpr float kGlowRange = 1.0f; // 4D gap between same-kind fruit where the glow starts

    float dim = 0.0f;         // darken the scene behind menus, 0..1
    float facet = 0.85f;      // low-poly facet strength, 0 = smooth
    glm::vec4 sunDir = glm::normalize(glm::vec4(-0.55f, 0.32f, -0.75f, 0.18f));

    // Resolution control. The scene is marched at `renderScale` of the
    // output, capped by the quality preset's pixel budget. With `adaptive`,
    // the scale follows the measured GPU time of the scene pass towards
    // `targetMs`, between kMinScaleOfCap of the cap and the cap.
    bool adaptive = true;
    float targetMs = 13.0f;   // scene passes' budget inside a 16.7 ms frame (UI, bakes, the rest)
    float renderScale = 1.0f; // current scale (set directly when not adaptive)
    bool syncTiming = false;  // wait for each timing result (benchmarks)
    bool useCache = true;     // slice cache on (falls back to analytic if unsupported)
    bool useDeferred = true;  // three scene passes with half-resolution lighting (if supported)
    ViewMode viewMode = ViewMode::Slice;
    std::string extraDefines; // added to the scene and bake shaders on the next setQuality

    // GPU time of the last cache bake in ms, and how many bakes ran.
    float bakeMs() const { return bakeMs_; }
    int bakeCount() const { return bakeCount_; }

    bool init() {
        present_ = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/present4d.fs", "");
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
        return scene_[0] && present_;
    }

    Quality quality() const { return quality_; }
    void setQuality(Quality q) {
        quality_ = q;
        std::string defs = "#define QUALITY " + std::to_string(int(q)) + "\n" + extraDefines;
        if (GLuint b = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/bake4d.fs", extraDefines)) {
            if (bake_) glDeleteProgram(bake_);
            bake_ = b;
        }
        for (int pass = 0; pass < 4; ++pass) {
            GLuint p = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/raymarch4d.fs",
                                    defs + "#define PASS " + std::to_string(pass) + "\n");
            if (p) {
                if (scene_[pass]) glDeleteProgram(scene_[pass]);
                scene_[pass] = p;
            }
        }
        renderScale = 1.0f; // re-clamped against the new cap next frame
        cacheValid_ = false;
        for (GLuint& e : eye_) { if (e) glDeleteProgram(e); e = 0; } // rebuilt when next shown
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
    void setEffects(const std::vector<BurstInstance>& bursts, float danger) {
        bursts_ = bursts;
        danger_ = danger;
    }
    // The next drop: the ghost in the air at `center`, in the fruit's own
    // orientation (`worldToLocal`), and optionally the same ghost where it lands.
    void setPreview(bool show, const glm::vec4& center, float radius, int type,
                    const glm::mat4& worldToLocal = glm::mat4(1.0f), bool land = false,
                    const glm::vec4& landCenter = glm::vec4(0.0f)) {
        previewCenter_ = center;
        previewData_ = show ? glm::vec4(radius, float(type), 0, 0) : glm::vec4(0.0f);
        previewFrame_ = worldToLocal;
        landCenter_ = show && land ? landCenter : glm::vec4(0.0f, 0.0f, 0.0f, -1e6f);
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
        if (viewMode == ViewMode::Eye && drawEye(cam, rw, rh, time)) {
            present(cam, target, width, height, rw, rh);
            return;
        }
        bool cached = useCache && updateCache(cam, time);

        // Scene: in three passes (geometry, half-resolution lighting,
        // compose) when float targets are available, else in one.
        glm::mat4 basis = cam.basis();
        glm::vec4 pos = cam.position();
        bool deferred = useDeferred && gbufOk_ && extraDefines.find("COST_MAP") == std::string::npos;
        int lw = (rw + 1) / 2, lh = (rh + 1) / 2;
        beginTimer();
        if (deferred) {
            glBindFramebuffer(GL_FRAMEBUFFER, gbufFbo_);
            glViewport(0, 0, rw, rh);
            sceneUniforms(scene_[1], cam, basis, pos, rw, rh, time, cached);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, lightFbo_);
            glViewport(0, 0, lw, lh);
            sceneUniforms(scene_[2], cam, basis, pos, rw, rh, time, cached);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glViewport(0, 0, rw, rh);
            sceneUniforms(scene_[3], cam, basis, pos, rw, rh, time, cached);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glViewport(0, 0, rw, rh);
            sceneUniforms(scene_[0], cam, basis, pos, rw, rh, time, cached);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        endTimer();
        present(cam, target, width, height, rw, rh);
    }

private:
    // Upscales the scene target (rw x rh) to the output and draws the indicator.
    void present(const Camera4D& cam, GLint target, int width, int height, int rw, int rh) {
        glm::mat4 basis = cam.basis();
        // Both bindings back to the caller's target: readers (recorder,
        // screenshots) see the presented frame, not the scene target.
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(viewportX, 0, width, height);
        glUseProgram(present_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, color_);
        glUniform1i(loc(present_, "uScene"), 0);
        uniform(present_, "uUVScale", glm::vec2(float(rw) / fboW_, float(rh) / fboH_));
        glUniformMatrix4fv(loc(present_, "uCameraBasis"), 1, GL_FALSE, glm::value_ptr(basis));
        float size = std::max(28.0f, height * 0.045f);
        glm::vec4 gizmo(size * 1.9f + viewportX, size * 1.9f, size, cam.offset / Camera4D::kOffsetMax);
        glUniform4fv(loc(present_, "uGizmo"), 1, glm::value_ptr(gizmo));
        glUniform1f(loc(present_, "uDim"), dim);
        glUniform1f(loc(present_, "uShowGizmo"), (cues4D && showGizmo) ? 1.0f : 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindVertexArray(0);
        glEnable(GL_BLEND);
    }

    // 4D eye retina size per preset: v texels (u follows the aspect), s layers.
    void retinaSize(int rw, int rh, int& nu, int& nv, int& ns) const {
        static const int v[] = {64, 84, 108, 144}, s[] = {32, 48, 64, 96};
        nv = v[int(quality_)];
        ns = s[int(quality_)];
        nu = std::max(4, int(std::lround(nv * float(rw) / float(std::max(rh, 1)))));
    }

    // 4D eye: trace the retina (one 4D ray per texel, four layers per draw),
    // then show it as a volume into the scene target. False if unsupported.
    bool drawEye(const Camera4D& cam, int rw, int rh, float time) {
        if (eyeUnsupported_) return false;
        if (!eye_[0]) {
            for (int k = 0; k < 2; ++k)
                eye_[k] = buildProgram("resources/shaders/raymarch4d.vs", "resources/shaders/raymarch4d.fs",
                                       "#define QUALITY " + std::to_string(int(quality_)) + "\n" + extraDefines +
                                           "#define PASS " + std::to_string(4 + k) + "\n");
            if (!eye_[0] || !eye_[1]) { eyeUnsupported_ = true; return false; }
        }
        int nu, nv, ns;
        retinaSize(rw, rh, nu, nv, ns);
        if (!retinaFbo_) { glGenFramebuffers(1, &retinaFbo_); glGenTextures(1, &retinaTex_); }
        glBindTexture(GL_TEXTURE_3D, retinaTex_);
        if (nu != retina_.x || nv != retina_.y || ns != retina_.z) {
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, nu, nv, ns, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            retina_ = glm::ivec3(nu, nv, ns);
        }
        glm::mat4 basis = cam.basis();
        glm::vec4 pos = cam.position();
        beginTimer();
        glBindFramebuffer(GL_FRAMEBUFFER, retinaFbo_);
        glViewport(0, 0, nu, nv);
        sceneUniforms(eye_[0], cam, basis, pos, nu, nv, time, false);
        glUniform3f(loc(eye_[0], "uRetinaSize"), float(nu), float(nv), float(ns));
        // A narrower lens than the slice view's: the retina's few texels go to
        // the bowl, and +-s still spans most of the bowl's extent in w.
        glUniform1f(loc(eye_[0], "uTanHalfFov"), kEyeZoom * std::tan(cam.fovY * 0.5f));
        GLint layerLoc = loc(eye_[0], "uLayer");
        const GLenum bufs[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, bufs);
        for (int z = 0; z < ns; z += 4) {
            for (int k = 0; k < 4; ++k)
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + k, retinaTex_, 0, z + k);
            if (z == 0 && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "RGBA8 3D texture not renderable; no 4D eye\n";
                eyeUnsupported_ = true;
                endTimer();
                return false;
            }
            glUniform1f(layerLoc, float(z));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        // Display: the retina box (u, v as on screen, s in depth) seen from
        // a little right of and above its s axis, so its depth shows.
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, rw, rh);
        glUseProgram(eye_[1]);
        uniform(eye_[1], "uResolution", glm::vec2(rw, rh));
        float aspect = float(nu) / float(nv);
        glUniform3f(loc(eye_[1], "uEyeBox"), aspect, 1.0f, 0.6f);
        glm::mat3 view = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(24.0f), glm::vec3(0, 1, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(-14.0f), glm::vec3(1, 0, 0)));
        glUniformMatrix3fv(loc(eye_[1], "uEyeView"), 1, GL_FALSE, glm::value_ptr(view));
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_3D, retinaTex_);
        glUniform1i(loc(eye_[1], "uRetina"), 5);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        endTimer();
        return true;
    }

    static constexpr int kQueries = 4;
    static constexpr float kMinScaleOfCap = 0.35f; // adaptive floor: keep frame rate over sharpness
    GLuint scene_[4] = {}; // by PASS: 0 single, 1 geometry, 2 lighting, 3 compose
    GLuint present_ = 0, bake_ = 0, vao_ = 0;
    // 4D eye: programs (PASS 4 retina, 5 display), built when first shown.
    GLuint eye_[2] = {}, retinaFbo_ = 0, retinaTex_ = 0;
    glm::ivec3 retina_{0};
    bool eyeUnsupported_ = false;
    static constexpr float kEyeZoom = 0.55f;
    // Deferred targets: G-buffer (hit, normal) at the scene size, light at half.
    GLuint gbufFbo_ = 0, geomTex_ = 0, normalTex_ = 0, lightFbo_ = 0, lightTex_ = 0;
    bool gbufOk_ = false;
    // Slice cache.
    GLuint cacheTex_ = 0, cacheFbo_ = 0, bakeQuery_ = 0;
    int cacheRes_ = 0;      // texture size
    int cacheTexels_ = 0;   // texels per side of the current bake (cacheRes_ or half)
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
    glm::vec4 previewCenter_{0.0f}, previewData_{0.0f}, landCenter_{0.0f, 0.0f, 0.0f, -1e6f};
    glm::mat4 previewFrame_{1.0f};
    std::vector<BurstInstance> bursts_;
    float danger_ = 0.0f;
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
        // While it turns, bake at half resolution (an eighth of the work)
        // into the low corner of the texture; once it holds still, bake once
        // at full resolution.
        glm::mat4 M = cam.slice.matrix();
        glm::vec4 o = cam.target + cam.offset * M[3];
        bool moved = !cacheValid_ || o != bakedSliceOrigin_ || M != bakedSlice_;
        if (!moved && cacheTexels_ == cacheRes_) return true;
        cacheTexels_ = moved && cacheValid_ ? cacheRes_ / 2 : cacheRes_;
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
        cacheVoxel_ = cacheSize_ / float(cacheTexels_);

        glBindFramebuffer(GL_FRAMEBUFFER, cacheFbo_);
        glViewport(0, 0, cacheTexels_, cacheTexels_);
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
        // Four layers per draw, one per colour attachment (all sizes are
        // multiples of 4): fewer, fuller draws.
        const GLenum bufs[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                GL_COLOR_ATTACHMENT3};
        glDrawBuffers(4, bufs);
        for (int z = 0; z < cacheTexels_; z += 4) {
            for (int k = 0; k < 4; ++k)
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + k, cacheTex_, 0, z + k);
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

    // Binds program p with everything a scene pass may read.
    void sceneUniforms(GLuint p, const Camera4D& cam, const glm::mat4& basis, const glm::vec4& pos, int rw, int rh,
                       float time, bool cached) {
        glUseProgram(p);
        uniform(p, "uResolution", glm::vec2(rw, rh));
        glUniform4fv(loc(p, "uCameraPos"), 1, glm::value_ptr(pos));
        glUniformMatrix4fv(loc(p, "uCameraBasis"), 1, GL_FALSE, glm::value_ptr(basis));
        glUniform1f(loc(p, "uTanHalfFov"), std::tan(cam.fovY * 0.5f));
        glUniform1f(loc(p, "uTime"), time);
        glUniform4fv(loc(p, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform1f(loc(p, "uFacet"), facet);
        glUniform1f(loc(p, "uFogStart"), cam.distance + 12.0f);
        glUniform1f(loc(p, "uNearCut"), std::clamp(cam.distance - 7.0f, 0.0f, 5.0f));
        uploadFruit(p, pos, basis[3]);
        uploadEffects(p);
        glUniform4fv(loc(p, "uPreviewCenter"), 1, glm::value_ptr(previewCenter_));
        glUniform4fv(loc(p, "uPreviewData"), 1, glm::value_ptr(previewData_));
        glUniformMatrix4fv(loc(p, "uPreviewWorldToLocal"), 1, GL_FALSE, glm::value_ptr(previewFrame_));
        glUniform4fv(loc(p, "uLandCenter"), 1, glm::value_ptr(landCenter_));
        glUniform1i(loc(p, "uCacheMode"), cached ? 1 : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, cacheTex_);
        glUniform1i(loc(p, "uCache"), 1);
        glUniform4fv(loc(p, "uCacheOrigin"), 1, glm::value_ptr(cacheOrigin_));
        glUniformMatrix4fv(loc(p, "uCacheBasis"), 1, GL_FALSE, glm::value_ptr(cacheBasis_));
        glUniform1f(loc(p, "uCacheSize"), cacheSize_);
        glUniform1f(loc(p, "uCacheVoxel"), cacheVoxel_);
        // Texture coordinates of the baked corner: scale, and the clamp that
        // keeps filtering off texels outside it.
        float span = float(cacheTexels_) / float(std::max(cacheRes_, 1));
        glUniform3f(loc(p, "uCacheUV"), span, 0.5f / std::max(cacheRes_, 1),
                    (cacheTexels_ - 0.5f) / std::max(cacheRes_, 1));
        // G-buffer inputs only for the passes that read them (never bound
        // while being drawn into).
        bool reads = p == scene_[2] || p == scene_[3];
        GLuint inputs[3] = {reads ? geomTex_ : 0, reads ? normalTex_ : 0, p == scene_[3] ? lightTex_ : 0};
        const char* names[3] = {"uGeom", "uNormal", "uLight"};
        for (int k = 0; k < 3; ++k) {
            glActiveTexture(GL_TEXTURE2 + k);
            glBindTexture(GL_TEXTURE_2D, inputs[k]);
            glUniform1i(loc(p, names[k]), 2 + k);
        }
    }

    static GLint loc(GLuint p, const char* name) { return glGetUniformLocation(p, name); }
    static void uniform(GLuint p, const char* name, glm::vec2 v) { glUniform2f(loc(p, name), v.x, v.y); }

    // Same-kind glow 0..1: how close (4D gap between the glomes, whatever the
    // slice shows) the nearest fruit of the same kind is, within kGlowRange.
    float glowOf(const FruitInstance& f) const {
        if (!sameKindGlow) return 0.0f;
        float gap = kGlowRange;
        for (const auto& o : fruit_) {
            if (&o == &f || o.type != f.type) continue;
            gap = std::min(gap, glm::length(f.center - o.center) - f.radius - o.radius);
        }
        return 1.0f - std::max(gap, 0.0f) / kGlowRange;
    }

    void uploadEffects(GLuint prog) {
        int n = std::min<int>(int(bursts_.size()), kMaxBursts);
        std::vector<glm::vec4> centers(n), data(n);
        for (int i = 0; i < n; ++i) {
            centers[i] = bursts_[i].center;
            data[i] = glm::vec4(bursts_[i].radius, float(bursts_[i].type), bursts_[i].progress, bursts_[i].power);
        }
        glUniform1i(loc(prog, "uBurstCount"), n);
        if (n > 0) {
            glUniform4fv(loc(prog, "uBurstCenters"), n, glm::value_ptr(centers[0]));
            glUniform4fv(loc(prog, "uBurstData"), n, glm::value_ptr(data[0]));
        }
        glUniform1f(loc(prog, "uDanger"), danger_);
        glUniform1f(loc(prog, "uCues"), cues4D ? 1.0f : 0.0f);
    }

    void uploadFruit(GLuint prog, const glm::vec4& camPos, const glm::vec4& normal) {
        // Rays never leave the slice, so a fruit is visible only if its glome
        // crosses the slice hyperplane; fruit within kHintRange beyond that
        // become depth hints. Nearest (to the camera, then the slice) first.
        using Entry = std::pair<float, const FruitInstance*>;
        std::vector<Entry> visible, nearby; // (not "near": a Windows macro)
        for (const auto& f : fruit_) {
            float off = glm::dot(f.center - camPos, normal);
            float gap = std::abs(off) - f.radius;
            if (viewMode == ViewMode::Eye) visible.push_back({glm::length(f.center - camPos), &f}); // 4D rays see all
            else if (gap < 0.0f) visible.push_back({glm::length(f.center - camPos), &f});
            else if (gap < kHintRange) nearby.push_back({gap, &f});
        }
        auto byKey = [](const Entry& a, const Entry& b) { return a.first < b.first; };
        std::sort(visible.begin(), visible.end(), byKey);
        std::sort(nearby.begin(), nearby.end(), byKey);

        int n = std::min<int>(int(visible.size()), kMaxFruit);
        std::vector<glm::vec4> centers(n), data(n);
        std::vector<float> glow(n, 0.0f);
        std::vector<glm::mat4> world_to_local(n);
        for (int i = 0; i < n; ++i) {
            const FruitInstance& f = *visible[i].second;
            centers[i] = f.center;
            data[i] = glm::vec4(f.radius, float(f.type), f.flash, float(f.id + 1));
            world_to_local[i] = f.worldToLocal;
            glow[i] = glowOf(f);
        }
        glUniform1i(loc(prog, "uFruitCount"), n);
        if (n > 0) {
            glUniform4fv(loc(prog, "uFruitCenters"), n, glm::value_ptr(centers[0]));
            glUniform4fv(loc(prog, "uFruitData"), n, glm::value_ptr(data[0]));
            glUniform1fv(loc(prog, "uFruitGlow"), n, glow.data());
            glUniformMatrix4fv(loc(prog, "uFruitWorldToLocal"), n, GL_FALSE, glm::value_ptr(world_to_local[0]));
        }

        int h = std::min<int>(int(nearby.size()), kMaxHints);
        std::vector<glm::vec4> hc(h), hd(h);
        for (int i = 0; i < h; ++i) {
            const FruitInstance& f = *nearby[i].second;
            float off = glm::dot(f.center - camPos, normal);
            float gap = nearby[i].first;
            hc[i] = f.center - off * normal; // projected into the slice
            float strength = 1.0f - gap / kHintRange;
            hd[i] = glm::vec4(f.radius, float(f.type), off, std::max(strength * strength, 0.9f * glowOf(f)));
        }
        glUniform1i(loc(prog, "uHintCount"), h);
        if (h > 0) {
            glUniform4fv(loc(prog, "uHintCenters"), h, glm::value_ptr(hc[0]));
            glUniform4fv(loc(prog, "uHintData"), h, glm::value_ptr(hd[0]));
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

        // Deferred targets, read with texelFetch only.
        if (!gbufFbo_) {
            glGenFramebuffers(1, &gbufFbo_);
            glGenFramebuffers(1, &lightFbo_);
            glGenTextures(1, &geomTex_);
            glGenTextures(1, &normalTex_);
            glGenTextures(1, &lightTex_);
        }
        auto alloc = [](GLuint tex, GLenum fmt, int tw, int th) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        };
        alloc(geomTex_, GL_RGBA32F, w, h);
        alloc(normalTex_, GL_RGBA16F, w, h);
        alloc(lightTex_, GL_RGBA16F, (w + 1) / 2, (h + 1) / 2);
        glBindFramebuffer(GL_FRAMEBUFFER, gbufFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, geomTex_, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normalTex_, 0);
        const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, bufs);
        gbufOk_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, lightFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lightTex_, 0);
        gbufOk_ = gbufOk_ && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!gbufOk_) std::cerr << "Float render targets unsupported; scene in one pass\n";
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
        out += "#define MAX_BURSTS " + std::to_string(kMaxBursts) + "\n";
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
