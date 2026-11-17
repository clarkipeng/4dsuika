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
