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
- **In-game HUD:** a glaze score card top left (the score counts up and pops, the best beside it), with a gold combo badge that pops in and fades below it; a next-drop card top right (a shaded fruit ball sized on the ladder, its name, the ladder dots with the next one ringed, a gold tag when lane snap is on); under it a view pill (icon, name, M) and ? and pause icon buttons with tooltips. Clicking a HUD button never drops a fruit. Bottom: the w track panel with a legend, the slice dial with a Home button while off home, starter key chips for the first 10 s, and the tutorial coach panel. Near the top a red vignette and a pulsing "Bowl almost full!" pill warn before overflow. **F3** shows debug info (perf, view, tilt, offset and the axis gizmo); nothing debug shows otherwise.
- **Controls:** keycaps in two columns grouped by Play, Turn into 4D, View and extras. **?** opens it from any menu.

## Sound and pointer

- Samples: drop (`click.wav`), merge thud (`drop.wav`), lose (`game_over.wav`), music (`theme.wav`). `src/4d_game/synth.hpp` builds the rest in memory at start: a soft tick when focus moves, a wooden tock on press, a blip when a value changes, a marimba merge chime per fruit on a pentatonic ladder (small fruit high, big fruit low and fuller; a running combo climbs a step per merge, up to three), and a rising arpeggio after the lose sound on a new best. All follow Settings > Sound and the effects volume.
- The pointer turns into a hand over anything clickable (menu rows, HUD buttons, the coach panel's skip).

Keyboard: ↑/↓ move between lines, ←/→ move within a line of buttons or change a value, Enter or Space selects, Esc goes back. Mouse: hover highlights, click selects.
