#pragma once
// Menus in the glazed-ceramic system (ui_kit.hpp, docs/ui.md): a cream card
// with a title, an optional custom header (scores, stats), rows (buttons,
// choices, sliders, read-only key rows, section headers) and a footer of key
// hints. Rows can share a line. The title screen ("hero") drops the card and
// stands in a left column over the scene. Layout is computed from the window
// every frame, so it scales to fit; keyboard and mouse drive one focus, which
// slides between rows, and the card and rows ease in when a menu opens.
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "text_renderer.h"
#include "ui_kit.hpp"

struct MenuItem {
    enum Kind { Button, Choice, Slider, Info, Header };
    Kind kind = Button;
    std::string label;
    std::function<void()> activate;           // Button
    std::function<std::string()> value;       // Choice and Slider read-out; Info description
    std::function<void(int)> step;            // Choice: -1 or +1
    std::function<float()> get;               // Slider, 0..1
    std::function<void(float)> set;
    ui::Icon icon = ui::Icon::None;
    bool primary = false;                     // gold button
    bool joinRow = false;                     // shares the line of the item before it
    std::string key;                          // shortcut shown as a keycap on the button
    int swatch = 0;                           // Info: draw a 4D cue sample instead of keycaps (ui::cueSwatch)
    bool focusable() const { return kind == Button || kind == Choice || kind == Slider; }

    MenuItem& withIcon(ui::Icon i) { icon = i; return *this; }
    MenuItem& asPrimary() { primary = true; return *this; }
    MenuItem& sameRow() { joinRow = true; return *this; }
    MenuItem& withKey(std::string k) { key = std::move(k); return *this; }
    MenuItem& withSwatch(int k) { swatch = k; return *this; }

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
    static MenuItem header(std::string text) { MenuItem m; m.kind = Header; m.label = std::move(text); return m; }
};

struct MenuStyle {
    std::string title, subtitle;
    std::string badge;                 // hero: gold tag after the title ("4D")
    glm::vec3 titleColor = ui::kTerracotta;
    bool hero = false;                 // title screen: no card, left column, big title
    float width = 520.0f;              // row width in design units
    float keyColumn = 0.0f;            // Info rows: width of the key column
    std::vector<std::pair<std::string, std::string>> keys = {{"Enter", "select"}, {"Esc", "back"}};
    std::function<std::string()> note; // optional live line in the footer
    float headerHeight = 0.0f;         // custom block under the title (design units)
    std::function<void(TextRenderer&, const ui::Rect&, float u, float alpha)> header;
    std::vector<std::pair<std::string, std::string>> sceneChips;  // key chips at the bottom of the screen
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
    // UI sounds: 0 focus moved, 1 activated, 2 value changed.
    std::function<void(int)> onSound;
    size_t itemCount() const { return items_.size(); }
    bool hovering() const { return hover_ >= 0; }

    void set(std::vector<MenuItem> items, MenuStyle style) {
        items_ = std::move(items);
        style_ = std::move(style);
        focus = 0;
        while (focus < int(items_.size()) && !items_[focus].focusable()) ++focus;
        dragging_ = -1;
        age_ = 0.0f;
        hoverAnim_.assign(items_.size(), 0.0f);
        pressAnim_.assign(items_.size(), 0.0f);
        focusValid_ = false;
        lines_.clear();
        for (size_t i = 0; i < items_.size(); ++i) {
            if (i == 0 || !items_[i].joinRow) lines_.push_back({});
            lines_.back().push_back(int(i));
        }
    }

    // Handles input for one frame. W, H are the window size in points.
    void update(TextRenderer& t, const MenuInput& in, float dt, float W, float H) {
        (void)dt;
        layout(t, W, H);
        int n = int(items_.size());
        if (n == 0) return;
        if (focus < 0 || focus >= n) focus = 0;
        const int before = focus;

        if (in.up) moveLine(-1);
        if (in.down) moveLine(+1);

        // Mouse: hovering moves focus; pressing acts on the row.
        bool moved = in.mouse != lastMouse_;
        lastMouse_ = in.mouse;
        hover_ = -1;
        for (int i = 0; i < n; ++i)
            if (items_[i].focusable() && rows_[i].has(in.mouse)) hover_ = i;
        if (moved && hover_ >= 0) focus = hover_;

        if (!in.mouseDown) dragging_ = -1;
        bool acted = false;
        if (in.mousePressed && hover_ >= 0) {
            focus = hover_;
            pressAnim_[hover_] = 1.0f;
            MenuItem& m = items_[hover_];
            if (m.kind == MenuItem::Button) { sound(1); run(m.activate); acted = true; }
            else if (m.kind == MenuItem::Choice) {
                ui::Rect z = valueZone(rows_[hover_]);
                m.step(in.mouse.x >= z.x && in.mouse.x < z.x + 0.5f * z.w ? -1 : +1);
                sound(2);
            } else if (m.kind == MenuItem::Slider) dragging_ = hover_;
        }
        if (acted) return;  // the action may have rebuilt this menu
        if (dragging_ >= 0) {
            ui::Rect tr = track(rows_[dragging_]);
            items_[dragging_].set(std::clamp((in.mouse.x - tr.x) / std::max(tr.w, 1.0f), 0.0f, 1.0f));
        }

        // Keyboard on the focused row.
        MenuItem& m = items_[focus];
        if (in.select) {
            if (m.kind == MenuItem::Button) { pressAnim_[focus] = 1.0f; sound(1); run(m.activate); return; }
            if (m.kind == MenuItem::Choice) { m.step(+1); sound(2); }
        }
        if (in.horizontal != 0) {
            if (m.kind == MenuItem::Button) moveInLine(in.horizontal);
            else { apply(m, in.horizontal); sound(2); }
        }
        if (in.horizontalHeld != 0 && m.kind == MenuItem::Slider) {
            held_ += dt;
            if (held_ > 0.4f) {
                repeat_ += dt;
                while (repeat_ > 0.05f) { apply(m, in.horizontalHeld); repeat_ -= 0.05f; }
            }
        } else {
            held_ = repeat_ = 0.0f;
        }
        if (focus != before) sound(0);
        for (int i = 0; i < n; ++i) {
            ui::approach(hoverAnim_[i], i == hover_ ? 1.0f : 0.0f, dt, 18.0f);
            pressAnim_[i] = std::max(0.0f, pressAnim_[i] - dt * 6.0f);
        }
    }

    // dt advances the entrance and focus animations (drawing owns the clock,
    // so menus animate even when no input is polled, as in headless captures).
    void draw(TextRenderer& t, float W, float H, float dt) {
        lastDraw_ = age_;
        age_ += std::clamp(dt, 0.0f, 0.1f);
        layout(t, W, H);
        const float u = u_;
        const float enter = ui::easeOutCubic(age_ / 0.28f);
        const float rise = (1.0f - ui::easeOutBack(age_ / 0.38f)) * 28.0f * u;
        const bool hero = style_.hero;
        ui::Rect card = panel_;
        card.y += rise;
        if (!hero) ui::glazePanel(t, card, u, enter, 26.0f * u);

        // Title.
        const float maxW = card.w - 2.0f * kPad * u;
        float y = card.y + kPad * u;
        {
            FaceScope f(t, TextRenderer::Display);
            float ts = fit(t, style_.title, titleScale() * u, maxW - badgeWidth(t, titleScale() * u));
            float tw = t.GetTextWidth(style_.title, ts);
            float bw = badgeWidth(t, ts);
            float tx = hero ? card.x : card.x + (card.w - tw - bw) * 0.5f;
            if (hero) ui::textOnScene(t, style_.title, tx, y, ts, ui::kCream, enter);
            else t.RenderText(style_.title, tx, y, ts, style_.titleColor, enter);
            if (!style_.badge.empty()) {
                float bs = ts * 0.5f, cap = t.CapHeight(ts);
                float bwid = t.GetTextWidth(style_.badge, bs) + 0.5f * cap;
                ui::Rect b{tx + tw + 0.18f * cap, y - 0.04f * cap, bwid, cap * 0.78f};
                float pop = ui::easeOutBack((age_ - 0.2f) / 0.35f);
                t.Shape(UiShape::rect(b.x, b.y + 0.07f * cap, b.w, b.h, b.h * 0.3f).fill(ui::kGoldFoot, enter));
                t.Shape(UiShape::rect(b.x, b.y, b.w, b.h, b.h * 0.3f).grad(ui::kGoldTop, ui::kGoldBottom, enter * pop));
                t.RenderText(style_.badge, b.x + 0.25f * cap, b.y + (b.h - t.CapHeight(bs)) * 0.5f, bs, ui::kInk, enter * pop);
            }
            y += t.CapHeight(ts);
        }
        if (!style_.subtitle.empty()) {
            y += 14.0f * u;
            float s = fit(t, style_.subtitle, 0.78f * u, maxW);
            float sx = hero ? card.x : card.x + (card.w - t.GetTextWidth(style_.subtitle, s)) * 0.5f;
            if (hero) ui::textOnScene(t, style_.subtitle, sx, y, s, ui::kMuted, enter);
            else t.RenderText(style_.subtitle, sx, y, s, ui::kClayDeep, enter);
        }
        if (style_.header && style_.headerHeight > 0.0f) {
            ui::Rect hr{card.x + kPad * u, headerY_ + rise, card.w - 2.0f * kPad * u, style_.headerHeight * u};
            style_.header(t, hr, u, ui::easeOutCubic((age_ - 0.06f) / 0.3f));
        }

        // Focus ring: slides to the focused row.
        if (focus >= 0 && focus < int(items_.size()) && items_[focus].focusable()) {
            ui::Rect target = rows_[focus];
            if (!focusValid_) { focusRect_ = target; focusValid_ = true; }
            float k = 1.0f - std::exp(-lastDt() * 20.0f);
            focusRect_.x += (target.x - focusRect_.x) * k; focusRect_.y += (target.y - focusRect_.y) * k;
            focusRect_.w += (target.w - focusRect_.w) * k; focusRect_.h += (target.h - focusRect_.h) * k;
        }

        // Rows.
        for (size_t i = 0; i < items_.size(); ++i) {
            float ra = ui::easeOutCubic((age_ - 0.05f - 0.035f * float(i)) / 0.25f);
            ui::Rect r = rows_[i];
            r.y += rise + (1.0f - ra) * 14.0f * u;
            drawRow(t, items_[i], r, int(i), ra * enter);
        }
        if (focusValid_ && focus >= 0 && focus < int(items_.size()) && items_[focus].focusable()) {
            const MenuItem& m = items_[focus];
            float g = 4.0f * u;
            ui::Rect f = focusRect_;
            f.y += rise;
            float rad = m.kind == MenuItem::Button ? f.h * 0.5f + g : 14.0f * u + g;
            float pulse = 0.75f + 0.25f * std::sin(age_ * 5.0f);
            glm::vec3 col = hero ? ui::kGlaze : ui::kGoldFoot;
            t.Shape(UiShape::rect(f.x - g, f.y - g, f.w + 2.0f * g, f.h + 2.0f * g + 4.0f * u, rad)
                        .ring(2.5f * u).fill(col, enter * pulse));
        }

        // Footer: key hints and an optional live note.
        float fy = footerY_ + rise;
        if (!style_.keys.empty()) {
            const float kh = 22.0f * u;
            float total = 0.0f;
            std::vector<float> kw;
            FaceScope f(t, TextRenderer::Body);
            float ls = 0.62f * u;
            for (auto& k : style_.keys) total += keyWidth(t, k.first, kh) + 8.0f * u + t.GetTextWidth(k.second, ls) + 22.0f * u;
            total -= 22.0f * u;
            float x = hero ? card.x : card.x + (card.w - total) * 0.5f;
            glm::vec3 lc = hero ? ui::kMuted : ui::kClayDeep;
            for (auto& k : style_.keys) {
                x += ui::keycap(t, k.first, x, fy, kh, enter * 0.9f, hero ? ui::kGlaze : ui::kInk, hero ? ui::kInk : ui::kGlaze) + 8.0f * u;
                if (hero) ui::textOnScene(t, k.second, x, fy + (kh - t.CapHeight(ls)) * 0.5f, ls, lc, enter);
                else t.RenderText(k.second, x, fy + (kh - t.CapHeight(ls)) * 0.5f, ls, lc, enter);
                x += t.GetTextWidth(k.second, ls) + 22.0f * u;
            }
            fy += kh + 12.0f * u;
        }
        if (style_.note) {
            std::string s = style_.note();
            float sc = fit(t, s, 0.62f * u, maxW);
            float x = hero ? card.x : card.x + (card.w - t.GetTextWidth(s, sc)) * 0.5f;
            if (hero) ui::textOnScene(t, s, x, fy, sc, ui::kMuted, enter);
            else t.RenderText(s, x, fy, sc, ui::kClayDeep, enter);
        }
        if (!style_.sceneChips.empty()) {
            float h = std::clamp(H * 0.036f, 26.0f, 54.0f);
            ui::keyChipRow(t, style_.sceneChips, W * 0.5f, H - h - std::max(18.0f, H * 0.03f),
                           h, ui::easeOutCubic((age_ - 0.3f) / 0.4f));
        }
    }

private:
    static constexpr float kPad = 36.0f;   // design units
    static constexpr float kInner = 20.0f; // row padding
    static constexpr float kZone = 230.0f; // choice / slider value zone

    std::vector<MenuItem> items_;
    MenuStyle style_;
    std::vector<std::vector<int>> lines_;
    std::vector<ui::Rect> rows_;
    std::vector<float> hoverAnim_, pressAnim_;
    ui::Rect panel_, focusRect_;
    bool focusValid_ = false;
    float headerY_ = 0.0f, footerY_ = 0.0f;
    float u_ = 1.0f;
    float age_ = 0.0f, lastDraw_ = 0.0f;
    float held_ = 0.0f, repeat_ = 0.0f;
    int dragging_ = -1, hover_ = -1;
    glm::vec2 lastMouse_{-1.0f};

    float lastDt() const { return std::clamp(age_ - lastDraw_, 0.0f, 0.1f); }
    void sound(int k) { if (onSound) onSound(k); }
    float titleScale() const { return style_.hero ? 3.4f : 1.5f; }

    float badgeWidth(TextRenderer& t, float ts) const {
        if (style_.badge.empty()) return 0.0f;
        float cap = t.CapHeight(ts);
        return t.GetTextWidth(style_.badge, ts * 0.5f) + 0.68f * cap;
    }
    static float keyWidth(TextRenderer& t, const std::string& k, float h) {
        return ui::keycapWidth(t, k, h);
    }

    int lineOf(int item) const {
        for (size_t l = 0; l < lines_.size(); ++l)
            for (int i : lines_[l]) if (i == item) return int(l);
        return 0;
    }

    void moveLine(int dir) {
        int nl = int(lines_.size());
        if (nl == 0) return;
        int l = lineOf(focus);
        float fx = rows_.empty() ? 0.0f : rows_[focus].centre().x;
        for (int k = 0; k < nl; ++k) {
            l = (l + dir + nl) % nl;
            int best = -1;
            float bd = 1e9f;
            for (int i : lines_[l]) {
                if (!items_[i].focusable()) continue;
                float d = std::abs(rows_[i].centre().x - fx);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) { focus = best; return; }
        }
    }

    void moveInLine(int dir) {
        const auto& line = lines_[lineOf(focus)];
        int pos = int(std::find(line.begin(), line.end(), focus) - line.begin());
        for (int p = pos + dir; p >= 0 && p < int(line.size()); p += dir)
            if (items_[line[p]].focusable()) { focus = line[p]; return; }
    }

    // Copy first: the action may rebuild the menu and destroy itself.
    static void run(std::function<void()> f) { f(); }

    static void apply(MenuItem& m, int dir) {
        if (m.kind == MenuItem::Choice) m.step(dir);
        else if (m.kind == MenuItem::Slider) m.set(std::clamp(m.get() + 0.05f * dir, 0.0f, 1.0f));
    }

    static float fit(TextRenderer& t, const std::string& s, float scale, float maxW) { return ui::fit(t, s, scale, maxW); }

    ui::Rect valueZone(const ui::Rect& r) const { return {r.x + r.w - (kInner * 0.5f + kZone) * u_, r.y, kZone * u_, r.h}; }
    ui::Rect track(const ui::Rect& r) const {
        ui::Rect z = valueZone(r);
        return {z.x + 14.0f * u_, r.y, z.w - 84.0f * u_, r.h};
    }

    float rowHeight(const MenuItem& m) const {
        switch (m.kind) {
            case MenuItem::Info: return 34.0f;
            case MenuItem::Header: return 36.0f;
            case MenuItem::Button: return m.primary ? (style_.hero ? 70.0f : 60.0f) : (style_.hero ? 54.0f : 50.0f);
            default: return 48.0f;
        }
    }
    float rowGap(const MenuItem& m) const { return m.kind == MenuItem::Info ? 4.0f : m.kind == MenuItem::Header ? 2.0f : 12.0f; }
    float lineHeight(const std::vector<int>& l) const {
        float h = 0.0f;
        for (int i : l) h = std::max(h, rowHeight(items_[i]));
        return h;
    }
    float lineGap(const std::vector<int>& l) const {
        float g = 0.0f;
        for (int i : l) g = std::max(g, rowGap(items_[i]));
        // Breathing room between a list of key rows and the buttons after it.
        size_t li = size_t(&l - lines_.data());
        if (li + 1 < lines_.size() && items_[l[0]].kind == MenuItem::Info && items_[lines_[li + 1][0]].kind == MenuItem::Button)
            g += 18.0f;
        return g;
    }

    // Design size of the card, then the scale that fits it in the window.
    void layout(TextRenderer& t, float W, float H) {
        float ts = titleScale();
        float titleW, titleCap;
        {
            FaceScope f(t, TextRenderer::Display);
            titleW = t.GetTextWidth(style_.title, ts) + badgeWidth(t, ts);
            titleCap = t.CapHeight(ts);
        }
        float titleBlock = titleCap + (style_.subtitle.empty() ? 0.0f : 14.0f + t.CapHeight(0.78f)) + 26.0f;
        float headerBlock = style_.headerHeight > 0.0f ? style_.headerHeight + 18.0f : 0.0f;
        float rowsH = 0.0f;
        for (auto& l : lines_) rowsH += lineHeight(l) + lineGap(l);
        float footer = (style_.keys.empty() ? 0.0f : 22.0f + 12.0f) + (style_.note ? 22.0f : 0.0f);
        float Wd = std::max(style_.width, titleW) + (style_.hero ? 0.0f : 2.0f * kPad);
        float Hd = kPad + titleBlock + headerBlock + rowsH + 8.0f + footer + kPad * 0.6f;
        float roomH = style_.sceneChips.empty() ? H : H - std::clamp(H * 0.036f, 26.0f, 54.0f) - 40.0f;
        u_ = std::clamp(std::min(0.92f * W / Wd, 0.9f * roomH / Hd), 0.3f, std::max(1.0f, H / 720.0f) * 1.05f);
        float u = u_;
        float px = style_.hero ? std::max(W * 0.075f, 24.0f) : (W - Wd * u) * 0.5f;
        if (style_.hero && px + Wd * u > W * 0.95f) px = (W - Wd * u) * 0.5f;
        panel_ = {px, (roomH - Hd * u) * 0.5f, Wd * u, Hd * u};
        headerY_ = panel_.y + (kPad + titleBlock) * u;
        rows_.assign(items_.size(), {});
        float y = headerY_ + headerBlock * u;
        float rw = style_.width * u;
        float rx = style_.hero ? panel_.x : panel_.x + (panel_.w - rw) * 0.5f;
        const float gap = 12.0f * u;
        for (auto& l : lines_) {
            float lh = lineHeight(l) * u;
            float cw = (rw - gap * float(l.size() - 1)) / float(l.size());
            for (size_t k = 0; k < l.size(); ++k) {
                float h = rowHeight(items_[l[k]]) * u;
                rows_[l[k]] = {rx + (cw + gap) * float(k), y + (lh - h) * 0.5f, cw, h};
            }
            y += lh + lineGap(l) * u;
        }
        footerY_ = y + 8.0f * u;
    }

    void drawRow(TextRenderer& t, const MenuItem& m, const ui::Rect& r, int index, float alpha) {
        const float u = u_;
        const float pad = kInner * u;
        auto centreY = [&](float sc) { return r.y + (r.h - t.CapHeight(sc)) * 0.5f; };
        const bool hero = style_.hero;
        const float hov = hoverAnim_[index], press = pressAnim_[index];

        if (m.kind == MenuItem::Header) {
            FaceScope f(t, TextRenderer::Display);
            float sc = 0.62f * u;
            float ty = r.y + r.h - t.CapHeight(sc) - 8.0f * u;
            t.RenderText(m.label, r.x + 4.0f * u, ty, sc, ui::kTerracotta, alpha);
            float lx = r.x + 4.0f * u + t.GetTextWidth(m.label, sc) + 12.0f * u;
            float ly = ty + t.CapHeight(sc) * 0.5f;
            if (lx < r.x + r.w) t.Shape(UiShape::capsule({lx, ly}, {r.x + r.w - 4.0f * u, ly}, 0.8f * u).fill(ui::kClay, 0.6f * alpha));
            return;
        }

        if (m.kind == MenuItem::Info) {
            // Keys as keycaps ("A / D" becomes two caps), then the description.
            float kh = std::min(r.h - 6.0f * u, 26.0f * u);
            float x = r.x + 4.0f * u;
            float keyW = style_.keyColumn * u;
            std::string rest = m.swatch ? std::string() : m.label;
            if (m.swatch) ui::cueSwatch(t, m.swatch, {x + kh * (m.swatch == 4 ? 1.1f : 0.7f), r.y + r.h * 0.5f}, kh * 0.5f, u, alpha);
            while (!rest.empty()) {
                size_t sep = rest.find(" / ");
                std::string tok = rest.substr(0, sep);
                float w = keyWidth(t, tok, kh);
                if (x + w > r.x + keyW - 6.0f * u && x > r.x + 4.0f * u) break;
                ui::keycap(t, tok, x, r.y + (r.h - kh) * 0.5f - 1.0f * u, kh, alpha);
                x += w + 6.0f * u;
                if (sep == std::string::npos) break;
                rest = rest.substr(sep + 3);
            }
            FaceScope f(t, TextRenderer::Body);
            std::string text = m.value();
            float s2 = fit(t, text, 0.68f * u, r.w - keyW - pad * 0.5f);
            t.RenderText(text, r.x + keyW, centreY(s2), s2, ui::kInk, alpha);
            return;
        }

        if (m.kind == MenuItem::Button) {
            ui::Tone tone = m.primary ? ui::Tone::Primary : (hero ? ui::Tone::Secondary : ui::Tone::Secondary);
            ui::pill(t, r, u, tone, hov, press, alpha);
            float sink = 4.0f * u * press;
            FaceScope f(t, TextRenderer::Display);
            float cap = r.h * (m.primary ? 0.34f : 0.31f);
            float s = cap / std::max(t.CapHeight(1.0f), 1.0f);
            float keyW = m.key.empty() ? 0.0f : keyWidth(t, m.key, r.h * 0.46f) + 10.0f * u;
            float iconW = m.icon == ui::Icon::None ? 0.0f : cap * 1.5f + 8.0f * u;
            s = fit(t, m.label, s, r.w - 2.0f * pad - iconW - keyW);
            float lw = t.GetTextWidth(m.label, s);
            float x = r.x + (r.w - (iconW + lw + keyW)) * 0.5f;
            float cy = r.y + r.h * 0.5f + sink;
            if (m.icon != ui::Icon::None) ui::icon(t, m.icon, {x + cap * 0.75f, cy}, cap * 1.5f, ui::kInk, alpha);
            t.RenderText(m.label, x + iconW, cy - t.CapHeight(s) * 0.5f, s, ui::kInk, alpha);
            if (!m.key.empty()) {
                float kh = r.h * 0.46f;
                ui::keycap(t, m.key, x + iconW + lw + 10.0f * u, cy - kh * 0.5f - 1.0f * u, kh, alpha * 0.85f);
            }
            return;
        }

        // Choice and slider rows: a light glaze strip, label left, control right.
        const bool focused = index == focus;
        t.Shape(UiShape::rect(r.x, r.y, r.w, r.h, 14.0f * u)
                    .fill(glm::vec3(1.0f), (0.35f + 0.35f * std::max(hov, focused ? 1.0f : 0.0f)) * alpha));
        FaceScope f(t, TextRenderer::Body);
        ui::Rect z = valueZone(r);
        float s = fit(t, m.label, 0.8f * u, z.x - r.x - pad - 8.0f * u);
        t.RenderText(m.label, r.x + pad, centreY(s), s, ui::kInk, alpha);

        if (m.kind == MenuItem::Choice) {
            float ih = r.h - 12.0f * u;
            ui::Rect inset{z.x, r.y + 6.0f * u, z.w, ih};
            t.Shape(UiShape::rect(inset.x, inset.y, inset.w, inset.h, ih * 0.5f).fill(ui::kGlaze2, alpha));
            t.Shape(UiShape::rect(inset.x, inset.y, inset.w, inset.h, ih * 0.5f).ring(1.2f * u).fill(ui::kClay, 0.7f * alpha));
            float cy = r.y + r.h * 0.5f, ic = ih * 0.7f;
            ui::icon(t, ui::Icon::Back, {inset.x + ih * 0.5f, cy}, ic, ui::kClayDeep, alpha);
            // Mirrored chevron for "next".
            float k = std::max(1.2f, ic * 0.11f);
            glm::vec2 c(inset.x + inset.w - ih * 0.5f, cy);
            t.Shape(UiShape::capsule(c + glm::vec2(-0.10f, -0.28f) * ic, c + glm::vec2(0.16f, 0.0f) * ic, k).fill(ui::kClayDeep, alpha));
            t.Shape(UiShape::capsule(c + glm::vec2(0.16f, 0.0f) * ic, c + glm::vec2(-0.10f, 0.28f) * ic, k).fill(ui::kClayDeep, alpha));
            std::string v = m.value();
            FaceScope fd(t, TextRenderer::Display);
            float sv = fit(t, v, 0.72f * u, inset.w - 2.0f * ih);
            t.RenderText(v, inset.x + (inset.w - t.GetTextWidth(v, sv)) * 0.5f, centreY(sv), sv, ui::kInk, alpha);
        } else {
            ui::Rect tr = track(r);
            float v = std::clamp(m.get(), 0.0f, 1.0f), cy = r.y + r.h * 0.5f, th = 5.0f * u;
            t.Shape(UiShape::capsule({tr.x, cy}, {tr.x + tr.w, cy}, th).fill(ui::kClay, 0.45f * alpha));
            float kx = tr.x + tr.w * v;
            if (v > 0.0f) t.Shape(UiShape::capsule({tr.x, cy}, {kx, cy}, th).grad(ui::kGoldTop, ui::kGoldBottom, alpha));
            float kr = (10.0f + 2.0f * std::max(hov, focused ? 1.0f : 0.0f)) * u;
            t.Shape(UiShape::circle({kx, cy + 2.5f * u}, kr).fill(ui::kClay, alpha));
            t.Shape(UiShape::circle({kx, cy}, kr).grad(glm::vec3(1.0f), ui::kGlaze2, alpha));
            std::string p = m.value();
            FaceScope fd(t, TextRenderer::Display);
            float sp = fit(t, p, 0.7f * u, 58.0f * u);
            t.RenderText(p, z.x + z.w - t.GetTextWidth(p, sp), centreY(sp), sp, ui::kInk, alpha);
        }
    }
};
