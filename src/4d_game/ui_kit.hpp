#pragma once
// The game's one visual system ("sunset facets", docs/ui.md): colours taken
// from the scene, dark translucent plum panels with a thin gold edge and
// chamfered corners like the low-poly island, sun-gold primary buttons, the
// sunset triangle ribbon, Chakra Petch type, line icons, and easing helpers so
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

// Colour tokens. Text pairs reach WCAG AA (4.5:1) on the surface they name
// (panels are drawn at kPanelAlpha over the scene; ratios are for the panel
// over the brightest sky it sits on).
const glm::vec3 kGlaze = hex(0x3e2537);      // panel top (dusk plum)
const glm::vec3 kGlaze2 = hex(0x29172a);     // panel bottom, secondary buttons
const glm::vec3 kClay = hex(0x8a6577);       // separators, tracks, idle dots on panels
const glm::vec3 kFoot = hex(0x160a12);       // the foot under buttons
const glm::vec3 kClayDeep = hex(0xd9bba8);   // secondary text on panels (6:1)
const glm::vec3 kTerracotta = hex(0xffc24a); // titles on panels: sun gold (7:1)
const glm::vec3 kInk = hex(0xfff3e3);        // body text on panels (10:1)
const glm::vec3 kOnGold = hex(0x3a2530);     // text and icons on gold (7:1)
const glm::vec3 kEdge = hex(0xffc24a);       // the thin gold edge on panels
const float kPanelAlpha = 0.88f;
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
// Greedy word wrap of s into lines no wider than maxW (a single long word
// stays on its own line).
inline std::vector<std::string> wrap(TextRenderer& t, const std::string& s, float scale, float maxW) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&] {
        if (word.empty()) return;
        std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && t.GetTextWidth(next, scale) > maxW) { lines.push_back(line); line = word; }
        else line = next;
        word.clear();
    };
    for (char c : s) { if (c == ' ') flush(); else word += c; }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
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

// A panel: dusk plum, translucent so the sunset shows through, with
// chamfered corners (the shape shader cuts them) and a thin gold edge.
inline void glazePanel(TextRenderer& t, const Rect& r, float u, float alpha = 1.0f, float radius = -1.0f) {
    if (radius < 0.0f) radius = 22.0f * u;
    softShadow(t, r, radius, u, 0.30f * alpha);
    t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, radius).grad(kGlaze, kGlaze2, kPanelAlpha * alpha));
    t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, radius).ring(std::max(1.0f, 1.3f * u)).fill(kEdge, 0.6f * alpha));
}

// Low-poly accent from the scene: a strip of alternating sunset triangles.
inline void facetRibbon(TextRenderer& t, float x, float y, float w, float h, float alpha = 1.0f) {
    const glm::vec3 cols[] = {kCoral, kGold, kRose, hex(0xfa994d)};
    int n = std::max(8, int(w / (h * 2.2f)));
    float d = w / float(n);
    for (int i = 0; i < n; ++i) {
        float x0 = x + float(i) * d;
        glm::vec3 c = cols[i % 4] * (0.75f + 0.25f * float((i * 7) % 3) / 2.0f);
        t.Shape(UiShape::tri({x0, y + h}, {x0 + d * 0.5f, y}, {x0 + d, y + h}).fill(c, 0.95f * alpha));
        if (i + 1 < n) t.Shape(UiShape::tri({x0 + d * 0.5f, y}, {x0 + d * 1.5f, y}, {x0 + d, y + h}).fill(cols[(i + 2) % 4] * 0.6f, 0.9f * alpha));
    }
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
        case Tone::Secondary: top = hex(0x4a2c40); bottom = hex(0x331c2e); footCol = kFoot; break;
        case Tone::Dark: top = hex(0x2e1a28); bottom = hex(0x1e1018); footCol = kFoot; break;
        default: top = bottom = glm::vec3(1.0f); footCol = kFoot; break;
    }
    if (tone == Tone::Ghost) {
        t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).fill(kGlaze, (0.55f + 0.2f * hover) * alpha));
        t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).ring(1.3f * u).fill(kEdge, (0.45f + 0.3f * hover) * alpha));
        return;
    }
    t.Shape(UiShape::rect(r.x + 1.0f * u, r.y + 6.0f * u, r.w - 2.0f * u, r.h, rad).fill(kShadow, 0.22f * alpha).soft(10.0f * u));
    t.Shape(UiShape::rect(b.x, b.y + foot, b.w, b.h, rad).fill(footCol, alpha));
    const bool solid = tone == Tone::Primary;
    t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).grad(glm::mix(top, solid ? glm::vec3(1.0f) : hex(0x6a3f58), 0.18f * hover + (solid ? 0.0f : 0.25f * hover)),
                                                        bottom, (solid ? 1.0f : kPanelAlpha + (1.0f - kPanelAlpha) * hover) * alpha));
    // A lit facet across the upper half, like the scene's flat-shaded faces.
    t.Shape(UiShape::tri({b.x + rad * 0.5f, b.y}, {b.x + b.w, b.y}, {b.x + b.w, b.y + b.h * 0.5f}).fill(glm::vec3(1.0f), (solid ? 0.14f : 0.05f) * alpha));
    t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, rad).ring(std::max(1.0f, 1.2f * u)).fill(solid ? hex(0xffe3a0) : kEdge, (solid ? 0.6f : 0.30f + 0.45f * hover) * alpha));
}

// Text scale and width of a keycap of height h.
inline float keycapScale(TextRenderer& t, float h) {
    FaceScope f(t, TextRenderer::Display);
    return h * 0.62f / std::max(t.CapHeight(1.0f), 1.0f);
}
// Samples of the 4D cues for the Controls page, radius r around c:
// 1 the held fruit's ghost (solid cream outline), 2 a fruit just off the slice
// (dotted ring), 3 a fruit the slice cuts (depth band), 4 the band's colours.
inline void cueSwatch(TextRenderer& t, int kind, glm::vec2 c, float r, float u, float alpha) {
    const glm::vec3 gold(1.0f, 0.76f, 0.32f), coral(1.0f, 0.42f, 0.30f), sky(0.30f, 0.68f, 1.0f), cream(1.0f, 0.96f, 0.86f);
    // On a dark chip, as they appear over the bowl.
    float cw = kind == 4 ? 2.1f * r : 1.35f * r;
    t.Shape(UiShape::rect(c.x - cw, c.y - 1.35f * r, 2.0f * cw, 2.7f * r, 1.35f * r).grad(glm::vec3(0.36f, 0.27f, 0.27f), glm::vec3(0.25f, 0.18f, 0.19f), alpha));
    r *= 0.92f;
    if (kind == 1) {
        t.Shape(UiShape::circle(c, r).fill(glm::vec3(0.93f, 0.55f, 0.28f), 0.45f * alpha));
        t.Shape(UiShape::circle(c, r).ring(2.2f * u).fill(cream, alpha));
            } else if (kind == 2) {
        for (int i = 0; i < 12; ++i) {
            float a = 6.2832f * i / 12.0f;
            t.Shape(UiShape::circle(c + r * glm::vec2(std::cos(a), std::sin(a)), 2.4f * u).fill(coral, alpha));
        }
    } else if (kind == 3) {
        t.Shape(UiShape::circle(c, r).fill(glm::vec3(0.55f, 0.66f, 0.22f), alpha));
        t.Shape(UiShape::circle(c, r).ring(3.4f * u).fill(gold, alpha));
        t.Shape(UiShape::circle(c, r - 3.4f * u).ring(1.0f * u).fill(kOnGold, 0.5f * alpha));
    } else {
        const glm::vec3 cols[3] = {coral, gold, sky};
        for (int i = 0; i < 3; ++i)
            t.Shape(UiShape::circle(c + glm::vec2((i - 1) * 0.95f * r, 0.0f), 0.42f * r).ring(3.0f * u).fill(cols[i], alpha));
    }
}

inline float keycapWidth(TextRenderer& t, const std::string& key, float h) {
    FaceScope f(t, TextRenderer::Display);
    return std::max(h, t.GetTextWidth(key, keycapScale(t, h)) + h * 0.6f);
}

// A keyboard key: small dark rounded square with the key's name. Returns its width.
inline float keycap(TextRenderer& t, const std::string& key, float x, float y, float h, float alpha = 1.0f,
                    glm::vec3 bg = kInk, glm::vec3 fg = kOnGold) {
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
    t.Shape(UiShape::circle(b + glm::vec2(0.0f, foot), r).fill(active ? kGoldFoot : kFoot, alpha));
    t.Shape(UiShape::circle(b, r).grad(glm::mix(active ? kGoldTop : hex(0x4a2c40), active ? glm::vec3(1.0f) : hex(0x6a3f58), 0.3f * hover),
                                       active ? kGoldBottom : hex(0x331c2e), (active ? 1.0f : kPanelAlpha) * alpha));
    t.Shape(UiShape::circle(b, r).ring(std::max(1.0f, 1.2f * u)).fill(kEdge, (active ? 0.0f : 0.35f + 0.4f * hover) * alpha));
    icon(t, ic, b, r * 1.15f, active ? kOnGold : kInk, alpha);
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
    t.Shape(UiShape::rect(x, y, w, h, h * 0.4f).fill(kGlaze2, 0.78f * alpha));
    keycap(t, key, x + h * 0.16f, y + (h - kh) * 0.5f - h * 0.03f, kh, alpha, kCream, kOnGold);
    t.RenderText(label, x + h * 0.16f + kw + h * 0.24f, y + (h - t.CapHeight(s)) * 0.5f, s, kCream, alpha);
    return w;
}

// A row of key chips centred on cx, wrapped to rows no wider than maxW
// (narrow phone screens); extra rows stack upwards, so y is the last row.
// Returns the number of rows.
inline int keyChipRow(TextRenderer& t, const std::vector<std::pair<std::string, std::string>>& chips, float cx, float y, float h,
                      float alpha, float maxW = 1e9f) {
    const float gap = h * 0.25f;
    std::vector<std::pair<size_t, float>> rows; // first chip of each row, row width
    for (size_t i = 0; i < chips.size(); ++i) {
        float w = keyChip(t, chips[i].first, chips[i].second, 0, 0, h, alpha, false);
        if (rows.empty() || rows.back().second + gap + w > maxW) rows.push_back({i, w});
        else rows.back().second += gap + w;
    }
    for (size_t r = 0; r < rows.size(); ++r) {
        size_t end = r + 1 < rows.size() ? rows[r + 1].first : chips.size();
        float x = cx - rows[r].second * 0.5f, ry = y - float(rows.size() - 1 - r) * (h + gap);
        for (size_t i = rows[r].first; i < end; ++i) x += keyChip(t, chips[i].first, chips[i].second, x, ry, h, alpha) + gap;
    }
    return int(rows.size());
}
}  // namespace ui
