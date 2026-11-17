#pragma once
// Menu widgets in the game's sunset low-poly style: a faceted panel with a
// title, rows (button, choice, slider, read-only info) and a hint line.
// Layout is computed from the window size every frame, so the panel scales
// down to fit and nothing is clipped. Keyboard and mouse drive the same
// focus. See TASTE.md: AA contrast, one set of colour tokens.
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "text_renderer.h"

namespace ui {
// Colour tokens. Text pairs below all reach WCAG AA (4.5:1) on the panel.
const glm::vec3 kPanel(0.11f, 0.06f, 0.13f);   // dusk plum
const glm::vec3 kCream(1.00f, 0.93f, 0.85f);   // main text (on panel ~15:1)
const glm::vec3 kMuted(0.84f, 0.77f, 0.79f);   // secondary text (~10:1)
const glm::vec3 kGold(1.00f, 0.76f, 0.32f);    // focus and accents, the w-axis gold
const glm::vec3 kCoral(0.96f, 0.45f, 0.30f);
const glm::vec3 kRose(0.85f, 0.33f, 0.45f);
const glm::vec3 kInk(0.15f, 0.07f, 0.10f);     // text on gold (~10:1)
}

struct MenuItem {
    enum Kind { Button, Choice, Slider, Info };
    Kind kind = Button;
    std::string label;
    std::function<void()> activate;           // Button
    std::function<std::string()> value;       // Choice and Slider read-out; Info description
    std::function<void(int)> step;            // Choice: -1 or +1
    std::function<float()> get;               // Slider, 0..1
    std::function<void(float)> set;
    bool focusable() const { return kind != Info; }

    static MenuItem button(std::string label, std::function<void()> f) {
        MenuItem m; m.kind = Button; m.label = std::move(label); m.activate = std::move(f); return m;
    }
    static MenuItem choice(std::string label, std::function<std::string()> text, std::function<void(int)> step) {
        MenuItem m; m.kind = Choice; m.label = std::move(label); m.value = std::move(text); m.step = std::move(step); return m;
    }
    static MenuItem slider(std::string label, std::function<float()> get, std::function<void(float)> set) {
        MenuItem m; m.kind = Slider; m.label = std::move(label); m.get = std::move(get); m.set = std::move(set);
        m.value = [g = m.get] { return std::to_string(int(g() * 100.0f + 0.5f)) + "%"; };
        return m;
    }
    static MenuItem info(std::string key, std::string text) {
        MenuItem m; m.kind = Info; m.label = std::move(key); m.value = [t = std::move(text)] { return t; }; return m;
    }
};

struct MenuStyle {
    std::string title, subtitle;
    glm::vec3 titleColor = ui::kCream;
    bool hero = false;             // big title, taller buttons (start screen)
    float width = 560.0f;          // row width in design units
    float keyColumn = 0.0f;        // Info rows: width of the key column
    std::string hint = "Up / Down  choose     Left / Right  change     Enter  select     Esc  back";
    std::function<std::string()> note;  // optional live line under the hint
};

struct MenuInput {
    bool up = false, down = false;    // pressed this frame
    int horizontal = 0;               // -1 / +1 pressed this frame
    int horizontalHeld = 0;           // -1 / +1 while held
    bool select = false;
    glm::vec2 mouse{-1.0f};
    bool mouseDown = false, mousePressed = false;
};

class Menu {
public:
    int focus = 0;
    size_t itemCount() const { return items_.size(); }

    void set(std::vector<MenuItem> items, MenuStyle style) {
        items_ = std::move(items);
        style_ = std::move(style);
        focus = 0;
        while (focus < int(items_.size()) && !items_[focus].focusable()) ++focus;
        dragging_ = -1;
    }

    // Handles input for one frame. W, H are the window size in points.
    void update(TextRenderer& t, const MenuInput& in, float dt, float W, float H) {
        layout(t, W, H);
        int n = int(items_.size());
        if (n == 0) return;
        if (focus < 0 || focus >= n) focus = 0;

        if (in.up) move(-1);
        if (in.down) move(+1);

        // Mouse: hovering moves focus; pressing acts on the row.
        bool moved = in.mouse != lastMouse_;
        lastMouse_ = in.mouse;
        int hover = -1;
        for (int i = 0; i < n; ++i)
            if (items_[i].focusable() && rows_[i].has(in.mouse)) hover = i;
        if (moved && hover >= 0) focus = hover;

        if (!in.mouseDown) dragging_ = -1;
        if (in.mousePressed && hover >= 0) {
            focus = hover;
            MenuItem& m = items_[hover];
            if (m.kind == MenuItem::Button) run(m.activate);
            else if (m.kind == MenuItem::Choice) { Rect z = valueZone(rows_[hover]); m.step(in.mouse.x >= z.x && in.mouse.x < z.x + 0.5f * z.w ? -1 : +1); }
            else if (m.kind == MenuItem::Slider) dragging_ = hover;
        }
        if (dragging_ >= 0) {
            Rect tr = track(rows_[dragging_]);
            items_[dragging_].set(std::clamp((in.mouse.x - tr.x) / std::max(tr.w, 1.0f), 0.0f, 1.0f));
        }

        // Keyboard on the focused row.
        MenuItem& m = items_[focus];
        if (in.select) {
            if (m.kind == MenuItem::Button) run(m.activate);
            else if (m.kind == MenuItem::Choice) m.step(+1);
        }
        if (in.horizontal != 0) apply(m, in.horizontal, true);
        if (in.horizontalHeld != 0 && m.kind == MenuItem::Slider) {
            held_ += dt;
            if (held_ > 0.4f) {
                repeat_ += dt;
                while (repeat_ > 0.05f) { apply(m, in.horizontalHeld, false); repeat_ -= 0.05f; }
            }
        } else {
            held_ = repeat_ = 0.0f;
        }
    }

    void draw(TextRenderer& t, float W, float H) {
        layout(t, W, H);
        const float u = u_;
        // Panel: a chamfered hexagon with a thin gold edge.
        auto hex = [](const Rect& r, float c) {
            return std::vector<glm::vec2>{{r.x + c, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h - c},
                                          {r.x + r.w - c, r.y + r.h}, {r.x, r.y + r.h}, {r.x, r.y + c}};
        };
        auto edge = [&](glm::vec2 a, glm::vec2 b, float th, glm::vec3 col, float alpha) {
            glm::vec2 d = glm::normalize(b - a), nrm(-d.y, d.x);
            t.FillPoly({a + nrm * th, b + nrm * th, b - nrm * th, a - nrm * th}, col, alpha);
        };
        auto outline = [&](const std::vector<glm::vec2>& p, float th, glm::vec3 col, float alpha) {
            for (size_t i = 0; i < p.size(); ++i) edge(p[i], p[(i + 1) % p.size()], th, col, alpha);
        };
        auto panelPoly = hex(panel_, 22.0f * u);
        t.FillPoly(panelPoly, ui::kPanel, style_.hero ? 0.74f : 0.88f);
        outline(panelPoly, std::max(1.0f, 1.2f * u), ui::kGold, 0.55f);

        // Title, then a strip of sunset facets under it.
        float titleScale = (style_.hero ? 2.4f : 1.25f) * u;
        float maxW = panel_.w - 2.0f * kPad * u;
        titleScale = fit(t, style_.title, titleScale, maxW);
        float tx = panel_.x + (panel_.w - t.GetTextWidth(style_.title, titleScale)) * 0.5f;
        float ty = panel_.y + kPad * u;
        t.RenderText(style_.title, tx + 2.0f * u, ty + 2.0f * u, titleScale, glm::vec3(0.0f));  // drop shadow
        t.RenderText(style_.title, tx, ty, titleScale, style_.titleColor);
        float stripY = ty + t.CapHeight(titleScale) + 14.0f * u;
        facets(t, panel_.x + kPad * u, stripY, panel_.w - 2.0f * kPad * u, 8.0f * u);
        if (!style_.subtitle.empty()) {
            float s = fit(t, style_.subtitle, 0.8f * u, maxW);
            t.RenderText(style_.subtitle, panel_.x + (panel_.w - t.GetTextWidth(style_.subtitle, s)) * 0.5f,
                         stripY + 20.0f * u, s, ui::kMuted);
        }

        // Rows.
        for (size_t i = 0; i < items_.size(); ++i) drawRow(t, items_[i], rows_[i], int(i) == focus);

        // Hint lines at the bottom.
        float hy = panel_.y + panel_.h - (kPad - 6.0f) * u - 20.0f * u * footerLines();
        auto line = [&](const std::string& s, glm::vec3 c) {
            float sc = fit(t, s, 0.62f * u, maxW);
            t.RenderText(s, panel_.x + (panel_.w - t.GetTextWidth(s, sc)) * 0.5f, hy, sc, c);
            hy += 20.0f * u;
        };
        line(style_.hint, ui::kMuted);
        if (style_.note) line(style_.note(), ui::kGold);
    }

private:
    struct Rect {
        float x = 0, y = 0, w = 0, h = 0;
        bool has(glm::vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
    };
    static constexpr float kPad = 34.0f;   // design units
    static constexpr float kInner = 18.0f; // row padding
    static constexpr float kZone = 250.0f; // choice / slider value zone

    std::vector<MenuItem> items_;
    MenuStyle style_;
    std::vector<Rect> rows_;
    Rect panel_;
    float u_ = 1.0f;
    float held_ = 0.0f, repeat_ = 0.0f;
    int dragging_ = -1;
    glm::vec2 lastMouse_{-1.0f};

    int footerLines() const { return style_.note ? 2 : 1; }

    void move(int dir) {
        int n = int(items_.size());
        for (int k = 0; k < n; ++k) {
            focus = (focus + dir + n) % n;
            if (items_[focus].focusable()) return;
        }
    }

    // Copy first: the action may rebuild the menu and destroy itself.
    static void run(std::function<void()> f) { f(); }

    static void apply(MenuItem& m, int dir, bool) {
        if (m.kind == MenuItem::Choice) m.step(dir);
        else if (m.kind == MenuItem::Slider) m.set(std::clamp(m.get() + 0.05f * dir, 0.0f, 1.0f));
    }

    static float fit(TextRenderer& t, const std::string& s, float scale, float maxW) {
        float w = t.GetTextWidth(s, scale);
        return w > maxW && w > 0.0f ? scale * maxW / w : scale;
    }

    Rect valueZone(const Rect& r) const { return {r.x + r.w - (kInner + kZone) * u_, r.y, kZone * u_, r.h}; }
    Rect track(const Rect& r) const {
        Rect z = valueZone(r);
        return {z.x + 12.0f * u_, r.y, z.w - 80.0f * u_, r.h};
    }

    float rowHeight(const MenuItem& m) const { return (m.kind == MenuItem::Info ? 30.0f : style_.hero ? 56.0f : 48.0f); }
    float rowGap(const MenuItem& m) const { return m.kind == MenuItem::Info ? 0.0f : 8.0f; }

    // Breathing room where a read-only block ends and buttons begin.
    float extraGap(size_t i) const {
        return i > 0 && i < items_.size() && items_[i].kind != MenuItem::Info && items_[i - 1].kind == MenuItem::Info ? 12.0f : 0.0f;
    }

    // Design size of the panel, then the scale that fits it in the window.
    void layout(TextRenderer& t, float W, float H) {
        float titleScale = style_.hero ? 2.4f : 1.25f;
        float titleW = t.GetTextWidth(style_.title, titleScale);
        float titleBlock = t.CapHeight(titleScale) + 14.0f + 8.0f + (style_.subtitle.empty() ? 0.0f : 34.0f) + 22.0f;
        float rowsH = 0.0f;
        for (size_t i = 0; i < items_.size(); ++i) rowsH += rowHeight(items_[i]) + rowGap(items_[i]) + extraGap(i);
        float footer = 14.0f + 20.0f * footerLines();
        float Wd = std::max(style_.width, titleW) + 2.0f * kPad;
        float Hd = kPad + titleBlock + rowsH + footer + kPad - 6.0f;
        u_ = std::clamp(std::min(0.94f * W / Wd, 0.94f * H / Hd), 0.3f, 1.5f);
        float u = u_;
        panel_ = {(W - Wd * u) * 0.5f, (H - Hd * u) * 0.5f, Wd * u, Hd * u};
        rows_.resize(items_.size());
        float y = panel_.y + (kPad + titleBlock) * u;
        float rw = style_.width * u;
        y += extraGap(0) * u;
        for (size_t i = 0; i < items_.size(); ++i) {
            rows_[i] = {panel_.x + (panel_.w - rw) * 0.5f, y, rw, rowHeight(items_[i]) * u};
            y += (rowHeight(items_[i]) + rowGap(items_[i])) * u;
            if (i + 1 < items_.size()) y += extraGap(i + 1) * u;
        }
    }

    // Low-poly accent: alternating triangles in sunset colours.
    void facets(TextRenderer& t, float x, float y, float w, float h) {
        const glm::vec3 cols[] = {ui::kCoral, ui::kGold, ui::kRose, glm::vec3(0.98f, 0.60f, 0.30f)};
        int n = std::max(8, int(w / (h * 2.2f)));
        float d = w / n;
        for (int i = 0; i < n; ++i) {
            float x0 = x + i * d;
            glm::vec3 c = cols[i % 4] * (0.75f + 0.25f * float((i * 7) % 3) / 2.0f);
            t.FillPoly({{x0, y + h}, {x0 + d * 0.5f, y}, {x0 + d, y + h}}, c, 0.95f);
            glm::vec3 c2 = cols[(i + 2) % 4] * 0.6f;
            t.FillPoly({{x0 + d * 0.5f, y}, {x0 + d * 1.5f, y}, {x0 + d, y + h}}, c2, 0.0f + (i + 1 < n ? 0.9f : 0.0f));
        }
    }

    static std::vector<glm::vec2> chamfer(const Rect& r, float c) {
        return {{r.x + c, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h - c},
                {r.x + r.w - c, r.y + r.h}, {r.x, r.y + r.h}, {r.x, r.y + c}};
    }

    void drawRow(TextRenderer& t, const MenuItem& m, const Rect& r, bool focused) {
        const float u = u_;
        float scale = (style_.hero ? 1.0f : 0.85f) * u;
        float pad = kInner * u;
        auto centreY = [&](float sc) { return r.y + (r.h - t.CapHeight(sc)) * 0.5f; };

        if (m.kind == MenuItem::Info) {
            float sc = 0.68f * u;
            float keyW = style_.keyColumn * u;
            std::string text = m.value();
            float s1 = fit(t, m.label, sc, keyW - pad);
            float s2 = fit(t, text, sc, r.w - keyW - pad);
            t.RenderText(m.label, r.x + pad, centreY(s1), s1, ui::kGold);
            t.RenderText(text, r.x + keyW, centreY(s2), s2, ui::kCream);
            return;
        }

        glm::vec3 fg = focused ? ui::kInk : ui::kCream;
        auto poly = chamfer(r, 9.0f * u);
        if (focused) {
            t.FillPoly(poly, ui::kGold, 1.0f);
        } else {
            t.FillPoly(poly, glm::vec3(1.0f), 0.07f);
        }

        if (m.kind == MenuItem::Button) {
            float s = fit(t, m.label, scale, r.w - 2.0f * pad);
            t.RenderText(m.label, r.x + (r.w - t.GetTextWidth(m.label, s)) * 0.5f, centreY(s), s, fg);
            return;
        }

        Rect z = valueZone(r);
        float s = fit(t, m.label, scale, z.x - r.x - pad - 8.0f * u);
        t.RenderText(m.label, r.x + pad, centreY(s), s, fg);

        if (m.kind == MenuItem::Choice) {
            // "<  value  >" with triangle arrows.
            float ah = 7.0f * u, cy = r.y + r.h * 0.5f;
            t.FillPoly({{z.x + 2.0f * u, cy}, {z.x + 2.0f * u + ah, cy - ah}, {z.x + 2.0f * u + ah, cy + ah}}, fg, 1.0f);
            float zr = z.x + z.w;
            t.FillPoly({{zr - 2.0f * u, cy}, {zr - 2.0f * u - ah, cy - ah}, {zr - 2.0f * u - ah, cy + ah}}, fg, 1.0f);
            std::string v = m.value();
            float sv = fit(t, v, scale, z.w - 2.0f * (ah + 12.0f * u));
            t.RenderText(v, z.x + (z.w - t.GetTextWidth(v, sv)) * 0.5f, centreY(sv), sv, fg);
        } else {
            Rect tr = track(r);
            float v = std::clamp(m.get(), 0.0f, 1.0f), cy = r.y + r.h * 0.5f, th = 4.0f * u;
            t.FillPoly({{tr.x, cy - th}, {tr.x + tr.w, cy - th}, {tr.x + tr.w, cy + th}, {tr.x, cy + th}},
                       fg, focused ? 0.28f : 0.22f);
            float kx = tr.x + tr.w * v;
            t.FillPoly({{tr.x, cy - th}, {kx, cy - th}, {kx, cy + th}, {tr.x, cy + th}}, focused ? ui::kInk : ui::kGold, 1.0f);
            float kr = 9.0f * u;  // diamond knob
            t.FillPoly({{kx, cy - kr}, {kx + kr, cy}, {kx, cy + kr}, {kx - kr, cy}}, focused ? ui::kInk : ui::kCream, 1.0f);
            std::string p = m.value();
            float sp = fit(t, p, 0.72f * u, 56.0f * u);
            t.RenderText(p, z.x + z.w - t.GetTextWidth(p, sp), centreY(sp), sp, fg);
        }
    }
};
