# Recording a trailer

Frames are dumped as PNGs at a fixed timestep: every frame advances the game by exactly `1/fps` seconds (physics in 1/60 s steps), however long it takes to render, so a capture is deterministic and plays at real speed. Two runs with the same flags give byte-identical frames.

## Capture

Headless (hidden window, never takes focus):

```bash
4d_game --headless --record recordings/frames --camera-path docs/recording/paths/trailer.path \
        --seconds 20 --fps 60 --size 1920x1080 --demo 20 --quality high
```

- `--record dir` writes `dir/frame_000000.png`, `frame_000001.png`, ...
- `--seconds s` sets the length; without it the length is the camera path's duration (else `--frames`).
- `--fps n` is the video frame rate (default 60). `--demo N` drops N fruit, one every 12 frames. Other flags (`--quality`, `--size`, `--facet`, ...) work as for screenshots.

Live: run `--record dir` without `--headless` to record from the first frame, or press **F9** in a normal game to start and stop (frames go to `recordings/<timestamp>/`, or to the `--record` dir). The window title shows `[REC]`. While recording, the game runs at the fixed timestep (so it may play slower than real time on a slow machine; the video does not), and adaptive resolution is off so the picture stays steady. With `--camera-path`, the path drives the camera from the start of each recording. Web builds do not record.

Readback uses a ring of pixel buffer objects and PNG encoding runs on worker threads, so a live capture costs a few ms per frame. If the disk falls far behind, capture waits rather than filling memory.

## Video

```bash
make video FRAMES=recordings/frames FPS=60 VIDEO=recordings/trailer.mp4
```

Uses `ffmpeg` (override with `FFMPEG=/opt/homebrew/bin/ffmpeg`): H.264, CRF 14, yuv420p, so it plays everywhere.

## Camera path

A text file, one keyframe per line, `key=value` pairs, `#` comments (see `docs/recording/paths/trailer.path`):

```
t=0   yaw=-40 pitch=-18 distance=34 ty=-1.5
t=6   yaw=40  distance=12 xw=50
t=9   yaw=80  pitch=-30 xw=90 yw=25 offset=1.5 ease=linear
```

| field | meaning |
|---|---|
| `t` | time in seconds (required, non-decreasing) |
| `yaw`, `pitch` | orbit inside the slice, degrees |
| `distance` | camera distance from the bowl |
| `offset` | slice offset along its normal |
| `ty` | height of the orbit target (34 distance and ty=-1.5 frame the island) |
| `xw`, `yw`, `zw`, `xy` | 4D turns in degrees, camera-local planes, same as `--turn` |
| `ease` | `smooth` (default, zero speed at each key) or `linear`, for the segment that ends at this key |

A key that omits a field keeps the previous key's value; the first key starts from the game defaults. The included trailer path runs for 20 seconds. The slice is interpolated with slerp, so 4D turns take the shortest path. The camera holds before the first and after the last key.

## Gameplay

`make gameplay` records 20 s of play for the store page into `recordings/gameplay.mp4` (1080p, 30 fps).
A scripted player drops the next fruit every 0.9 s through the real cursor path, so the drop preview shows; it aims at a random spot just under the rim in the current slice.
It plays 30 s off camera first (`--warmup`), so the bowl is busy when the video starts, while `docs/recording/paths/gameplay.path` keeps the camera on the bowl and turns the slice through xw, yw and zw.
The binary takes `--play s` (seconds between drops) and `--warmup s` alongside `--record`; it prints how many drops and game overs there were.

## 4D turntable

A review video and contact sheet per subject: one fruit, or the bowl, alone and centred against a plain backdrop, while the slice turns 0 to 180° through xw, then yw, then zw.
The planes are those of the default slice's axes (x, y, z each turned into w), and the slice pivots on the subject's 4D centre, so every slice cuts it through the middle.
Each turn holds half a second at both ends; the title shows the subject, plane and angle, and the indicator bottom-left shows the slice.

```bash
make turntable                         # all 11 fruit and the bowl into recordings/turntable/
make turntable SUBJECT=pear,bowl TURNTABLE_DIR=/tmp/tt
```

This writes `<subject>.mp4` (960×960, 30 fps, 15 s), `<subject>_sheet.png` (one row per plane, a column every 30°) and the frames in `<subject>/`.
The binary alone takes `--turntable dir --subject all|fruit|bowl|<names> --sweep seconds` plus the usual `--size`, `--fps` and `--quality`; it writes the frames and sheets, `make turntable` adds the videos.
The bowl turns about its glome's centre, the origin at its rim height: at yw 90° the slice is the rim's own hyperplane.
