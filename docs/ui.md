# UI system

One visual system for every 2D screen (menus, HUD, overlays): *sunset facets*, taken from the low-poly island at dusk. It brings back the v1.0 look (dark translucent plum panels with a thin gold edge, cut corners, the sunset triangle ribbon, a gold primary bar) on top of the layout, icons, motion and readability of the UI kit (#49). A cream "glazed ceramic" pass (#49–#53) read as off-theme next to the scene and was replaced.

## Pieces

- **Type.** Chakra Petch (SIL OFL, `resources/fonts/ChakraPetch-OFL.txt`), whose cut-corner letters echo the chamfered panels without the old pixel font's strain: SemiBold for body text (`TextRenderer::Body`, baked at 64 px), Bold for titles, numbers and buttons (`TextRenderer::Display`, 128 px). Glyphs live in one mipmapped atlas per face, so text stays crisp from 720p to 4K. Text `scale` is relative to a nominal 24 px font.
- **Shapes.** `TextRenderer::Shape(UiShape)` draws antialiased signed-distance shapes (`resources/shaders/ui_shape.*`): rects and rings with their top-left and bottom-right corners chamfered (circles stay round), capsules, arc rings, rounded triangles, each with a vertical gradient and an optional large feather for soft shadows.
- **Palette** (`src/4d_game/ui_kit.hpp`, namespace `ui`): dusk plum panels (`kGlaze` → `kGlaze2`, drawn at `kPanelAlpha` 0.88 so the sunset shows through) with a gold edge (`kEdge`), sun-gold headings (`kTerracotta`), cream body text (`kInk`) and warm muted secondary text (`kClayDeep`), separators and tracks in `kClay`, button feet in `kFoot`, a gold bar for the primary action (`kGoldTop` → `kGoldBottom` with a `kGoldFoot` lip) with dark text on it (`kOnGold`), cream text with a soft shadow over the scene (`textOnScene`). The w axis keeps its two ends: `kWNeg` coral and `kWPos` sky.
- **Surfaces.** `glazePanel` (translucent plum panel, gold edge, soft shadow), `facetRibbon` (the strip of sunset triangles under every title), `pill` buttons in four tones (Primary gold, Secondary plum with a gold edge that brightens on hover, Ghost, Dark), each with a lit facet across its upper half, `keycap` (cream key, dark letters), `keyChip` (a key and what it does, on a plum chip over the scene), `iconButton`. All text pairs pass WCAG AA over the brightest sky (cream 10:1, gold 7:1, muted 6:1, dark on gold 7:1).
- **Icons.** `ui::icon` draws 16 line icons from shapes (play, pause, restart, gear, home, help, book, quit, eye, slice, keys, back, camera, sound, close, star). Buttons lead with an icon.
- **Motion.** Every state change moves: menu cards rise with a slight overshoot (`easeOutBack`), rows ease in one after another, the focus ring slides between rows and breathes, hover lifts a button and a press sinks it, the backdrop dim eases in, the game-over score counts up and a new best pops. Helpers: `easeOutCubic`, `easeOutBack`, `approach` (frame-rate independent). The menu owns its clock in `Menu::draw(…, dt)`, so animations also run in headless captures.

## Screens

- **Title:** the title, its 4D badge and ribbon, and the buttons sit on a plum panel left of the bowl, Play first and gold; key chips along the bottom name the three inputs that matter and where the rest are (`?`).
- **Pause:** score and best, Resume (gold), Restart, Controls, Settings, Menu.
- **Game over:** "Bowl overflowed!", the score counting up, a new-best badge or the best to beat, the biggest fruit, merges and best combo, then Play again (gold, also **R**), Menu and Settings. The best score is saved in `~/.4dsuika-best` (native builds).
- **Settings:** grouped under Picture, 4D and Sound; values change with ←/→ or a click on the arrows.
- **In-game HUD:** a plum score card top left (the score counts up and pops, the best beside it), with a gold combo badge that pops in and fades below it; a next-drop card top right (a shaded fruit ball sized on the ladder, its name, the ladder dots with the next one ringed, a gold tag when lane snap is on); under it a view pill (icon, name, M) and ? and pause icon buttons with tooltips. Clicking a HUD button never drops a fruit. Bottom: the w track panel with a legend, the slice dial with a Home button while off home, starter key chips for the first 10 s, and the tutorial coach panel. Near the top a red vignette and a pulsing "Bowl almost full!" pill warn before overflow. **F3** shows debug info (perf, view, tilt, offset and the axis gizmo); nothing debug shows otherwise.
- **Controls:** keycaps in two columns grouped by Play, Turn into 4D, View and extras. **?** opens it from any menu.

## Sound and pointer

- Samples: drop (`click.wav`), merge thud (`drop.wav`), lose (`game_over.wav`), music (`theme.ogg`). `src/4d_game/synth.hpp` builds the rest in memory at start: a soft tick when focus moves, a wooden tock on press, a blip when a value changes, a marimba merge chime per fruit on a pentatonic ladder (small fruit high, big fruit low and fuller; a running combo climbs a step per merge, up to three), and a rising arpeggio after the lose sound on a new best. All follow Settings > Sound and the effects volume.
- The pointer turns into a hand over anything clickable (menu rows, HUD buttons, the coach panel's skip).

Keyboard: ↑/↓ move between lines, ←/→ move within a line of buttons or change a value, Enter or Space selects, Esc goes back. Mouse: hover highlights, click selects.
