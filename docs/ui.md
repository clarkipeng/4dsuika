# UI system

One visual system for every 2D screen (menus, HUD, overlays): *glazed ceramic*, taken from the scene's bowl, clay and sunset. The mockup directions and the audit behind it are in the UI kit and menus PR (#49).

## Pieces

- **Type.** Fredoka (SIL OFL, `resources/fonts/Fredoka-OFL.txt`): SemiBold for body text (`TextRenderer::Body`, baked at 64 px), Bold for titles, numbers and buttons (`TextRenderer::Display`, 128 px). Glyphs live in one mipmapped atlas per face, so text stays crisp from 720p to 4K. Text `scale` is relative to a nominal 24 px font. The web build falls back to the built-in pixel font.
- **Shapes.** `TextRenderer::Shape(UiShape)` draws antialiased signed-distance shapes (`resources/shaders/ui_shape.*`): rounded rects and rings, capsules, arc rings, rounded triangles, each with a vertical gradient and an optional large feather for soft shadows.
- **Palette** (`src/4d_game/ui_kit.hpp`, namespace `ui`): glaze cream panels (`kGlaze` → `kGlaze2`), clay rims (`kClay`, `kClayDeep`), terracotta headings (`kTerracotta`), ink text (`kInk`), a gold glaze for the primary action (`kGoldTop` → `kGoldBottom` with a `kGoldFoot` lip), cream text with a soft shadow over the scene (`textOnScene`). The w axis keeps its two ends: `kWNeg` coral and `kWPos` sky.
- **Surfaces.** `glazePanel` (cream card with a clay lip and a soft shadow), `pill` buttons in four tones (Primary gold, Secondary cream, Ghost, Dark), `keycap`, `keyChip` (a key and what it does, on a dark chip over the scene), `iconButton`.
- **Icons.** `ui::icon` draws 16 line icons from shapes (play, pause, restart, gear, home, help, book, quit, eye, slice, keys, back, camera, sound, close, star). Buttons lead with an icon.
- **Motion.** Every state change moves: menu cards rise with a slight overshoot (`easeOutBack`), rows ease in one after another, the focus ring slides between rows and breathes, hover lifts a button and a press sinks it, the backdrop dim eases in, the game-over score counts up and a new best pops. Helpers: `easeOutCubic`, `easeOutBack`, `approach` (frame-rate independent). The menu owns its clock in `Menu::draw(…, dt)`, so animations also run in headless captures.

## Screens

- **Title:** the title, badge and buttons sit in a column left of the bowl (no card), Play first and gold; key chips along the bottom name the three inputs that matter and where the rest are (`?`).
- **Pause:** score and best, Resume (gold), Restart, Controls, Settings, Menu.
- **Game over:** "Bowl overflowed!", the score counting up, a new-best badge or the best to beat, the biggest fruit, merges and best combo, then Play again (gold, also **R**), Menu and Settings. The best score is saved in `~/.4dsuika-best` (native builds).
- **Settings:** grouped under Picture, 4D and Sound; values change with ←/→ or a click on the arrows.
- **Controls:** keycaps in two columns grouped by Play, Turn into 4D, View and extras. **?** opens it from any menu.

Keyboard: ↑/↓ move between lines, ←/→ move within a line of buttons or change a value, Enter or Space selects, Esc goes back. Mouse: hover highlights, click selects.
