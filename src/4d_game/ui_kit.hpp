#pragma once
// The game's one visual system ("glazed ceramic", docs/ui.md): colours taken
// from the scene, cream glaze panels like the bowl with a clay foot shadow,
// sun-gold primary buttons, Fredoka type, line icons, and easing helpers so
// every state change moves. Everything draws through TextRenderer::Shape
// (antialiased SDF shapes) and RenderText.
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "text_renderer.h"

namespace ui {
inline glm::vec3 hex(unsigned v) { return glm::vec3((v >> 16) & 255, (v >> 8) & 255, v & 255) / 255.0f; }

// Colour tokens. Text pairs reach WCAG AA (4.5:1) on the surface they name.
const glm::vec3 kGlaze = hex(0xfbf3e8);      // panel top (the bowl's glaze)
const glm::vec3 kGlaze2 = hex(0xefe0cd);     // panel bottom, secondary buttons
const glm::vec3 kClay = hex(0xc99a7d);       // the foot under panels and buttons
const glm::vec3 kClayDeep = hex(0x8a5a48);   // secondary text on glaze (5.3:1)
const glm::vec3 kTerracotta = hex(0xb0634c); // titles on glaze (4.6:1)
const glm::vec3 kInk = hex(0x3a2530);        // body text on glaze and gold (12:1 / 9:1)
const glm::vec3 kGoldTop = hex(0xffcf6a);    // primary button gradient
const glm::vec3 kGoldBottom = hex(0xf5a83f);
const glm::vec3 kGoldFoot = hex(0xc06a2e);
const glm::vec3 kShadow = hex(0x2a1820);     // soft drop shadows
// Text straight on the scene (with a soft shadow under it).
const glm::vec3 kCream = hex(0xfff3e3);
const glm::vec3 kMuted = hex(0xe9d6c8);
// Accents, and the w colours shared with the scene's depth rings.
const glm::vec3 kGold = hex(0xffc24a);       // on the slice, focus
const glm::vec3 kWNeg = hex(0xff8a6b);       // -w side (coral)
const glm::vec3 kWPos = hex(0x6cc6f0);       // +w side (sky blue)
const glm::vec3 kCoral = hex(0xf0734f);
const glm::vec3 kRose = hex(0xd9546e);
const glm::vec3 kDanger = hex(0xe5483a);
// Legacy dark surface (still used by photo mode and reel captions).
const glm::vec3 kPanel(0.11f, 0.06f, 0.13f);

// ------------------------------------------------------------------ motion
inline float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float easeOutCubic(float x) { x = clamp01(x); return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); }
inline float easeOutBack(float x) {
    x = clamp01(x);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(x - 1.0f, 3.0f) + c1 * std::pow(x - 1.0f, 2.0f);
}
// Frame-rate independent approach of `v` to `target` (rate per second).
inline void approach(float& v, float target, float dt, float rate = 14.0f) {
    v += (target - v) * (1.0f - std::exp(-dt * rate));
}

// "12,345"
inline std::string thousands(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return n < 0 ? "-" + out : out;
}

// ------------------------------------------------------------------ text
inline float fit(TextRenderer& t, const std::string& s, float scale, float maxW) {
    float w = t.GetTextWidth(s, scale);
    return w > maxW && w > 0.0f ? scale * maxW / w : scale;
}
// Text over the scene: a soft dark halo under it so it reads on sky and grass.
inline void textOnScene(TextRenderer& t, const std::string& s, float x, float y, float scale, glm::vec3 col, float alpha = 1.0f) {
    float o = std::max(1.0f, scale * 1.6f);
    t.RenderText(s, x, y + o, scale, kShadow, 0.45f * alpha);
    t.RenderText(s, x, y, scale, col, alpha);
}
inline void textCentered(TextRenderer& t, const std::string& s, float cx, float y, float scale, glm::vec3 col, float alpha = 1.0f) {
    t.RenderText(s, cx - t.GetTextWidth(s, scale) * 0.5f, y, scale, col, alpha);
}

// ------------------------------------------------------------------ surfaces
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool has(glm::vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    glm::vec2 centre() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

inline void softShadow(TextRenderer& t, const Rect& r, float radius, float u, float alpha = 0.35f) {
    t.Shape(UiShape::rect(r.x + 2.0f * u, r.y + 10.0f * u, r.w - 4.0f * u, r.h, radius).fill(kShadow, alpha).soft(26.0f * u));
}

// A glazed card: soft shadow, clay foot, cream glaze with a light rim.
inline void glazePanel(TextRenderer& t, const Rect& r, float u, float alpha = 1.0f, float radius = -1.0f) {
    if (radius < 0.0f) radius = 22.0f * u;
    softShadow(t, r, radius, u, 0.38f * alpha);
    t.Shape(UiShape::rect(r.x, r.y + 5.0f * u, r.w, r.h, radius).fill(kClay, alpha));
    t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, radius).grad(kGlaze, kGlaze2, alpha));
    t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, radius).ring(1.5f * u).fill(glm::vec3(1.0f), 0.55f * alpha));
}

enum class Tone { Primary, Secondary, Ghost, Dark };

// A pill button. `press` 0..1 sinks it onto its foot; `hover` 0..1 lifts the glaze.
inline void pill(TextRenderer& t, const Rect& r, float u, Tone tone, float hover, float press, float alpha = 1.0f) {
    const float foot = 4.0f * u * (1.0f - press);
    const float rad = r.h * 0.5f;
    Rect b = r;
    b.y += 4.0f * u - foot;
    glm::vec3 top, bottom, footCol;
    switch (tone) {
        case Tone::Primary: top = kGoldTop; bottom = kGoldBottom; footCol = kGoldFoot; break;
        case Tone::Secondary: top = kGlaze; bottom = kGlaze2; footCol = kClay; break;
        case Tone::Dark: top = hex(0x4a3340); bottom = kInk; footCol = hex(0x1e1218); break;
        default: top = bottom = glm::vec3(1.0f); footCol = kClay; break;
    }
    if (tone == Tone::Ghost) {
        t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).fill(kGlaze, (0.10f + 0.12f * hover) * alpha));
        t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).ring(1.5f * u).fill(kGlaze, (0.45f + 0.3f * hover) * alpha));
        return;
    }
    t.Shape(UiShape::rect(r.x + 1.0f * u, r.y + 6.0f * u, r.w - 2.0f * u, r.h, rad).fill(kShadow, 0.22f * alpha).soft(10.0f * u));
    t.Shape(UiShape::rect(b.x, b.y + foot, b.w, b.h, rad).fill(footCol, alpha));
    t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).grad(glm::mix(top, glm::vec3(1.0f), 0.18f * hover), bottom, alpha));
    t.Shape(UiShape::rect(b.x + rad * 0.5f, b.y + 2.0f * u, b.w - rad, b.h * 0.34f, b.h * 0.17f)
                .fill(glm::vec3(1.0f), (tone == Tone::Primary ? 0.28f : 0.45f) * alpha));
}

// Text scale and width of a keycap of height h.
inline float keycapScale(TextRenderer& t, float h) {
    FaceScope f(t, TextRenderer::Display);
    return h * 0.62f / std::max(t.CapHeight(1.0f), 1.0f);
}
inline float keycapWidth(TextRenderer& t, const std::string& key, float h) {
    FaceScope f(t, TextRenderer::Display);
    return std::max(h, t.GetTextWidth(key, keycapScale(t, h)) + h * 0.6f);
}

// A keyboard key: small dark rounded square with the key's name. Returns its width.
inline float keycap(TextRenderer& t, const std::string& key, float x, float y, float h, float alpha = 1.0f,
                    glm::vec3 bg = kInk, glm::vec3 fg = kGlaze) {
    FaceScope f(t, TextRenderer::Display);
    float s = keycapScale(t, h);
    float tw = t.GetTextWidth(key, s);
    float w = keycapWidth(t, key, h);
    t.Shape(UiShape::rect(x, y + h * 0.08f, w, h, h * 0.3f).fill(glm::mix(bg, glm::vec3(0.0f), 0.35f), alpha));
    t.Shape(UiShape::rect(x, y, w, h, h * 0.3f).fill(bg, alpha));
    t.RenderText(key, x + (w - tw) * 0.5f, y + (h - t.CapHeight(s)) * 0.5f, s, fg, alpha);
    return w;
}

// ------------------------------------------------------------------ icons
enum class Icon { None, Play, Pause, Restart, Gear, Home, Help, Book, Quit, Eye, Slice, Keys, Back, Camera, Sound, Close, Star };

// Line icons on a `size` box centred at c. Stroke scales with size.
inline void icon(TextRenderer& t, Icon ic, glm::vec2 c, float size, glm::vec3 col, float alpha = 1.0f) {
    const float s = size, k = std::max(1.2f, s * 0.11f);  // k: stroke half-width
    auto line = [&](glm::vec2 a, glm::vec2 b, float r) { t.Shape(UiShape::capsule(c + a * s, c + b * s, r).fill(col, alpha)); };
    switch (ic) {
        case Icon::Play:
            t.Shape(UiShape::tri(c + glm::vec2(-0.26f, -0.34f) * s, c + glm::vec2(0.36f, 0.0f) * s, c + glm::vec2(-0.26f, 0.34f) * s, s * 0.06f).fill(col, alpha));
            break;
        case Icon::Pause:
            line({-0.17f, -0.28f}, {-0.17f, 0.28f}, k * 1.1f);
            line({0.17f, -0.28f}, {0.17f, 0.28f}, k * 1.1f);
            break;
        case Icon::Restart: {
            const float r = 0.32f * s;
            t.Shape(UiShape::arcRing(c, r, 2.0f * k, -1.2f, 4.9f).fill(col, alpha));
            float a = -1.2f;  // arrowhead at the arc's start, pointing along it (clockwise)
            glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * r;
            glm::vec2 tang(-std::sin(a), std::cos(a)), nrm(std::cos(a), std::sin(a));
            t.Shape(UiShape::tri(p - tang * (0.22f * s), p + nrm * (0.2f * s) + tang * (0.04f * s), p - nrm * (0.2f * s) + tang * (0.04f * s), s * 0.02f).fill(col, alpha));
            break;
        }
        case Icon::Gear:
            for (int i = 0; i < 8; ++i) {
                float a = i * 0.785398f;
                glm::vec2 d(std::cos(a), std::sin(a));
                line(d * 0.30f, d * 0.40f, k * 1.05f);
            }
            t.Shape(UiShape::arcRing(c, 0.26f * s, 2.2f * k, 0.0f, 6.2832f).fill(col, alpha));
            break;
        case Icon::Home:
            line({-0.36f, -0.02f}, {0.0f, -0.36f}, k);
            line({0.0f, -0.36f}, {0.36f, -0.02f}, k);
            t.Shape(UiShape::rect(c.x - 0.24f * s, c.y - 0.06f * s, 0.48f * s, 0.40f * s, 0.06f * s).ring(2.0f * k).fill(col, alpha));
            break;
        case Icon::Help: {
            FaceScope f(t, TextRenderer::Display);
            float sc = 0.72f * s / std::max(t.CapHeight(1.0f), 1.0f);
            textCentered(t, "?", c.x, c.y - t.CapHeight(sc) * 0.5f, sc, col, alpha);
            break;
        }
        case Icon::Book:
            t.Shape(UiShape::rect(c.x - 0.38f * s, c.y - 0.28f * s, 0.36f * s, 0.56f * s, 0.06f * s).ring(1.8f * k).fill(col, alpha));
            t.Shape(UiShape::rect(c.x + 0.02f * s, c.y - 0.28f * s, 0.36f * s, 0.56f * s, 0.06f * s).ring(1.8f * k).fill(col, alpha));
            break;
        case Icon::Quit:
            t.Shape(UiShape::arcRing(c + glm::vec2(0.0f, 0.04f) * s, 0.30f * s, 2.0f * k, -0.95f, 5.04f).fill(col, alpha));
            line({0.0f, -0.40f}, {0.0f, -0.04f}, k);
            break;
        case Icon::Eye:
            t.Shape(UiShape::rect(c.x - 0.42f * s, c.y - 0.22f * s, 0.84f * s, 0.44f * s, 0.22f * s).ring(1.8f * k).fill(col, alpha));
            t.Shape(UiShape::circle(c, 0.12f * s).fill(col, alpha));
            break;
        case Icon::Slice:  // a sphere cut by a plane
            t.Shape(UiShape::arcRing(c, 0.32f * s, 1.8f * k, 0.0f, 6.2832f).fill(col, alpha));
            line({-0.44f, 0.08f}, {0.44f, -0.08f}, k);
            break;
        case Icon::Keys:
            t.Shape(UiShape::rect(c.x - 0.42f * s, c.y - 0.26f * s, 0.84f * s, 0.52f * s, 0.1f * s).ring(1.8f * k).fill(col, alpha));
            for (int i = 0; i < 3; ++i) t.Shape(UiShape::circle(c + glm::vec2(-0.2f + 0.2f * i, -0.07f) * s, k * 0.9f).fill(col, alpha));
            line({-0.18f, 0.11f}, {0.18f, 0.11f}, k * 0.9f);
            break;
        case Icon::Back:
            line({0.10f, -0.28f}, {-0.16f, 0.0f}, k);
            line({-0.16f, 0.0f}, {0.10f, 0.28f}, k);
            break;
        case Icon::Close:
            line({-0.24f, -0.24f}, {0.24f, 0.24f}, k);
            line({-0.24f, 0.24f}, {0.24f, -0.24f}, k);
            break;
        case Icon::Camera:
            t.Shape(UiShape::rect(c.x - 0.40f * s, c.y - 0.22f * s, 0.80f * s, 0.52f * s, 0.1f * s).ring(1.8f * k).fill(col, alpha));
            t.Shape(UiShape::arcRing(c + glm::vec2(0.0f, 0.04f) * s, 0.13f * s, 1.8f * k, 0.0f, 6.2832f).fill(col, alpha));
            line({-0.14f, -0.30f}, {0.14f, -0.30f}, k);
            break;
        case Icon::Sound:
            t.Shape(UiShape::rect(c.x - 0.38f * s, c.y - 0.12f * s, 0.16f * s, 0.24f * s, 0.03f * s).fill(col, alpha));
            t.Shape(UiShape::tri(c + glm::vec2(-0.26f, -0.12f) * s, c + glm::vec2(0.0f, -0.32f) * s, c + glm::vec2(0.0f, 0.32f) * s).fill(col, alpha));
            t.Shape(UiShape::tri(c + glm::vec2(-0.26f, -0.12f) * s, c + glm::vec2(0.0f, 0.32f) * s, c + glm::vec2(-0.26f, 0.12f) * s).fill(col, alpha));
            t.Shape(UiShape::arcRing(c + glm::vec2(0.02f, 0.0f) * s, 0.2f * s, 1.5f * k, -0.8f, 1.6f).fill(col, alpha));
            t.Shape(UiShape::arcRing(c + glm::vec2(0.02f, 0.0f) * s, 0.36f * s, 1.5f * k, -0.8f, 1.6f).fill(col, alpha));
            break;
        case Icon::Star: {
            std::vector<glm::vec2> p;
            for (int i = 0; i < 10; ++i) {
                float a = -1.5708f + i * 0.6283f, r = (i % 2 ? 0.19f : 0.44f) * s;
                p.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
            }
            for (int i = 0; i < 10; i += 2) t.Shape(UiShape::tri(c, p[i], p[(i + 1) % 10]).fill(col, alpha));
            for (int i = 0; i < 10; i += 2) t.Shape(UiShape::tri(c, p[i], p[(i + 9) % 10]).fill(col, alpha));
            break;
        }
        default: break;
    }
}

// A round icon button: glaze disc on a clay foot. Returns nothing; hit-test with the rect.
inline void iconButton(TextRenderer& t, glm::vec2 c, float r, Icon ic, float u, float hover, float press, float alpha = 1.0f,
                       bool active = false) {
    const float foot = 3.0f * u * (1.0f - press);
    glm::vec2 b = c + glm::vec2(0.0f, 3.0f * u - foot);
    t.Shape(UiShape::circle(c + glm::vec2(0.0f, 5.0f * u), r).fill(kShadow, 0.25f * alpha).soft(9.0f * u));
    t.Shape(UiShape::circle(b + glm::vec2(0.0f, foot), r).fill(active ? kGoldFoot : kClay, alpha));
    t.Shape(UiShape::circle(b, r).grad(glm::mix(active ? kGoldTop : kGlaze, glm::vec3(1.0f), 0.25f * hover),
                                       active ? kGoldBottom : kGlaze2, alpha));
    icon(t, ic, b, r * 1.15f, kInk, alpha);
}

// A key hint chip on the scene: [key] label. Returns its width (draw=false to measure).
inline float keyChip(TextRenderer& t, const std::string& key, const std::string& label, float x, float y, float h, float alpha,
                     bool draw = true) {
    FaceScope f(t, TextRenderer::Body);
    float s = h * 0.42f / std::max(t.CapHeight(1.0f), 1.0f);
    float kh = h * 0.68f;
    float kw = keycapWidth(t, key, kh);
    float lw = t.GetTextWidth(label, s);
    float w = h * 0.16f + kw + h * 0.24f + lw + h * 0.36f;
    if (!draw) return w;
    t.Shape(UiShape::rect(x, y, w, h, h * 0.5f).fill(kShadow, 0.6f * alpha));
    keycap(t, key, x + h * 0.16f, y + (h - kh) * 0.5f - h * 0.03f, kh, alpha, kGlaze, kInk);
    t.RenderText(label, x + h * 0.16f + kw + h * 0.24f, y + (h - t.CapHeight(s)) * 0.5f, s, kCream, alpha);
    return w;
}

// A row of key chips centred on cx. Returns the total width.
inline float keyChipRow(TextRenderer& t, const std::vector<std::pair<std::string, std::string>>& chips, float cx, float y, float h,
                        float alpha) {
    float gap = h * 0.25f, total = 0.0f;
    for (auto& c : chips) total += keyChip(t, c.first, c.second, 0, 0, h, alpha, false) + gap;
    total -= gap;
    float x = cx - total * 0.5f;
    for (auto& c : chips) x += keyChip(t, c.first, c.second, x, y, h, alpha) + gap;
    return total;
}
}  // namespace ui
