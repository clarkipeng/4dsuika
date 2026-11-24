# Web build

The game runs in the browser from the same C++ and GLSL as desktop, compiled with Emscripten to WebAssembly and WebGL2.
It is the HTML5 build on the itch page, played in the page on desktop and on phones.

## Build and run

- `make web` builds into `build-web/`: `index.html`, `index.js`, `index.wasm`, `index.data` (the preloaded `resources/`), and `web.zip` of those four for itch.
- It uses `tools/emsdk` when present (gitignored; see the Makefile for the two install lines), else `emcmake` on the PATH.
- `make web-serve` builds and serves it at `http://localhost:8642`.
- `web_template.html` is the page: a plum loading card in the v1.0 theme, the canvas, and the boot code.

## What differs from desktop

- **Libraries.** Emscripten's ports of GLFW, SDL2_mixer (Ogg only, so the theme is `theme.ogg`) and FreeType; WebGL2 is GL ES 3.0, so shaders get `#version 300 es` (docs/4D.md).
- **Loading.** The download is about 4.6 MB zipped. The card shows download progress, then "Preparing graphics" with a moving sweep while the shaders compile (the page's thread is blocked, but the sweep runs on the compositor), and fades when the game has drawn its first frame.
- **Shader compile time.** Browsers on Windows translate GLSL to HLSL through ANGLE, and its compiler is slow on long unrolled loops. The shaders keep each SDF to one call site and, on GL ES only, wrap loop bounds in `NO_UNROLL` (docs/4D.md). The three scene programs compile in parallel and the single-pass fallback only on demand. First visit, cold shader cache: about 16 s on the lab PC (Windows Chrome, D3D11); later visits about 1.5 s.
- **Saves.** Best score, the tutorial flag and the view live in IndexedDB, mounted at `/persist` before `main` runs; `FileSystem::writeUserFile` syncs after each write.
- **Window.** The canvas fills the page at the device pixel ratio; resolution still adapts to the frame time. Quit is hidden (the tab closes the game). The page keeps every touch, so it never scrolls, zooms or opens a long-press menu.
- **Touch.** Gestures map onto the mouse and wheel input; see docs/controls.md, Touch. A coarse pointer (a phone or tablet) starts with touch hints.
- **Narrow screens.** Narrower than 3:4 the camera widens its horizontal view, so the bowl fits a portrait phone; the UI scales by the shorter of height/720 and width/400, chip rows wrap, and the Controls page goes to one column.
- **No recording, F9 or timer queries.** Resolution adapts from frame time.

## Measured

Chrome on the lab PC, 1280×800: 45–55 fps in play at the default quality. Emulated phone (390×844 at 3×): 69 fps.

## itch

The HTML5 upload goes to the `html5` channel with `butler push build-web/web.zip werus23/4d-suika:html5` (butler lives in the gitignored `tools/butler/`), and only with the PI's word.
The page marks it "played in the browser", with fullscreen and mobile-friendly on.
