# 4D research: what to steal

A short survey of how 4D games render and control 4D, and which tricks fit 4D Suika.
`docs/4D.md` is the contract; this file is background and ideas.

## Sources

- **4D Golf** (CodeParade): 3D-section rendering, multiple views (slice, volume, map), ghost images of objects off the slice, music channels that fade with camera rotation.
  [Devlog #3: How do you build 4D worlds?](https://www.youtube.com/watch?v=dbq9uX_MycY),
  [80.lv overview](https://80.lv/articles/get-the-hang-of-4d-easily-in-this-unique-casual-golfing-game),
  [views explained](https://re-actor.net/4d-golf-beginners-guide).
- **Engine4D** (CodeParade's Unity toolkit behind 4D Golf): slice cameras, "shadow" ghost projections with distance fade and tint, diffuse shading with sky ambient, fog and AO.
  [GitHub](https://github.com/HackerPoet/Engine4D),
  [devlog playlist](https://youtube.com/playlist?list=PLh9DXIT3m6N4GygehtlHl0ukgrgPJZteI).
- **Miegakure** (Marc ten Bosch): 3D section; a single "4D rotate" button that animates a 90° slice turn; geometric detail over texture detail; canopies made of many small leaves so slices never reveal flat interiors.
  [Detail in 4D](https://marctenbosch.com/news/?p=2542),
  [4D as parallel universes](https://marctenbosch.com/).
- **Grove of spherinders** (Miegakure art): standing spherinders slice to cylinders, tilted ones to ellipsoids, fallen ones to spheres; ground rings grow before an object enters the slice and shrink after.
  [80.lv article](https://80.lv/articles/scattering-a-grove-with-4d-spherinders).
- **4D Miner** (Mashpoe): mouse wheel rotates the slice in a w-plane; "4D glasses" and a compass show what lies off the slice.
  [PC Gamer](https://www.pcgamer.com/trying-to-understand-four-dimensional-minecraft-broke-my-brain).
- **Tessimal** (Zack's 4D World, 4D Gravity Simulation, Tessercraft): `[` and `]` rotate the world in the WZ plane; a 4D Golf-style volume mode; 4D rotations pivot around the camera.
  [Zack's 4D World devlog](https://tessimal.itch.io/zacks-4d-world/devlog/694094/4d-rotations),
  [4D Gravity Simulation](https://tessimal.itch.io/4d-gravity-simulation).
  I could not find a YouTube channel under the handle "tessimal256"; the itch.io pages are the best primary source found.
- **Joe Subbiani's 4D Games**: raymarched; a 4D arcball (rotors, after Shoemake) drives several rotation planes from one mouse drag; w-paths drawn as balls because testers kept scrubbing slices to find them.
  [Devlog](https://joesubbi.github.io/2022/11/19/4D-Games2).
- **4D raymarching references**: finite-difference 4D normals, glome SDF `length(p) - r`.
  [Arnav Kumar devlog](https://arnavcs.github.io/projects/4d-raymarching-pathtracer/devlog.html),
  [Inigo Quilez on raymarching SDFs](https://iquilezles.org/articles/raymarchingdf),
  [list of 4D games and their projection](https://en.wikipedia.org/wiki/List_of_four-dimensional_games).

## Techniques worth stealing

### Rotation controls (render)
- One dedicated input per w-plane, like Tessimal's `[`/`]` and 4D Miner's wheel: a held key or wheel turns the slice in xw (and zw); ordinary orbiting stays on the mouse.
- A Miegakure-style "snap" button that animates a 90° turn of the slice, plus a "back to default slice" button that animates home. Animated, never instant, so the player sees the world morph.
- Rotate around the bowl (the play focus), not around the world origin, so the bowl never leaves the frame.
- An optional 4D arcball for power users later; the keys come first.

### Slice cues (render + world)
- Ghosts: render fruit near the slice as faint translucent shells, tinted by their offset along the slice normal (warm for +w, cool for -w) and faded with distance (Engine4D `_ShadowDist`, `_ShadowColor1`).
- A small on-screen compass: the slice normal's components in (x, y, z, w), plus the offset along it.
- Rings on surfaces: where a glome is close to the slice, draw a soft ring on the ground or bowl whose radius grows as it approaches (the spherinder-grove trick). Cheap in a raymarcher: it is a function of distance to the fruit's centre in 4D.
- Sound: fade music stems with rotation angle, as 4D Golf does. This gives a non-visual sense of "how far around" you are.

### Making the world read as 4D (world)
- Use shapes whose slices change in recognisable ways: spherinder trunks (cylinder upright, ellipsoid when tilted in w), glome canopies (shrink and vanish off-slice), duocylinder-ish rocks.
- Place some features only at a range of w (hills, rocks, a second tree), so rotating in xw or zw grows and shrinks them. Small objects appear and vanish quickly, so keep them few and chunky (Miegakure's noise warning).
- Prefer geometric detail to texture detail: textures slide on slices. Faceting from quantized 4D normals is geometric, so it stays stable.
- Solid canopies (many lumps) rather than thin shells, so slices never expose hollow interiors.

### Lighting (render + world)
- 4D sun direction with a w component, so rotating in w moves the light across surfaces too.
- Sky ambient from the 4D sky function, sampled at the 4D normal; warm rim light from the sunset.
- Fog to the sky colour with distance; soft shadows and AO marched in 4D (Engine4D does fog, AO and sky ambient).

## Plan for this game

1. Keys for xw and zw rotation, a home button, and a slice compass (render).
2. Fruit ghosts tinted by w offset, plus contact rings on the bowl (render, data from game).
3. A world built only from shapes whose slices morph legibly, with features that live at specific w (world).
4. Optional later: volume view or a w-strip minimap showing the bowl from the side along w.
