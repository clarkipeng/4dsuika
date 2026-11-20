# Mouse-first controls

The game plays with one hand on the mouse. The keyboard still works for everything (A/D, R/F, Q/E turn into 4D, W/S move the slice, arrows orbit, H or Space go home, M cycles the view), so nothing here replaces it. Research behind the choices is in `docs/4D-research.md`: 4D Miner turns the slice with the wheel, and Joe Subbiani's games drive several rotation planes from one drag (a 4D arcball).

## Mapping

| Input | Slice | Thick slice | 4D eye |
|---|---|---|---|
| Move | aim the drop | aim the drop | aim the drop |
| Left click | drop | drop | drop |
| Right-drag | orbit the camera | orbit the camera | orbit the camera |
| Middle-drag, or Shift + right-drag | turn into 4D (arcball: horizontal xw, vertical yw) | same | same |
| Wheel | turn the slice through w (xw, about 5 degrees a notch, eased) | same | set the w of the drop (0.5 a notch, within the bowl) |
| Ctrl + wheel | | band width | |
| Shift + wheel | zoom | zoom | zoom |

## Views

**M** (or Settings > View) cycles Slice (default), 4D eye, then thick slice at band 0.5, 1 and 2. A label bottom-left names the current view and the key, and the choice is saved for the next run. Ctrl+wheel fine-tunes the thick band.

## Why

- **Left and right keep their meaning in every mode.** Aim, drop and orbit are what players already do, and orbiting is the same drag in 3D and 4D (it stays inside the slice and never rebakes it).
- **Turning into 4D is a different drag, not a different mode.** Middle-drag, or Shift+right-drag on mice without a middle button, is the arcball: the horizontal drag is xw and the vertical drag yw, the two planes that matter most (docs/4D.md, rotation sheets). It acts at once while dragging, so the slice follows the hand. Orbit glides out after release; the 4D turn does not, because a gliding slice moves the aim under the cursor.
- **The wheel does the thing each view needs most.** In Slice and Thick the slice is the view, so the wheel scrubs it through w, like 4D Miner. In 4D eye the player already sees all of w, so turning the slice is less useful than choosing *where in w the fruit lands*; the wheel sets the drop's w (the preview and drop move together, and the strip tick follows). Zoom, the old wheel job, moves to Shift+wheel so the wheel can be the 4D control. Thick keeps one more job on Ctrl+wheel: the band width, the only setting that mode adds.
- **No jumps when switching views (M).** Nothing is stored per mode. The slice, orbit and zoom carry over; the drop's w eases back to the slice when leaving 4D eye and out to its value when entering it; wheel turns are eased, never applied in one frame.
- **Before 4D is unlocked** (the 3D start of the onboarding) the wheel only zooms and the 4D drags do nothing, so the first minute stays plain 3D. The tutorial's step 3 names the wheel, middle-drag and A/D.
- Any 4D input takes over from the H/Space home animation, like the keys do.

## Review tools

`--mouse-script file` (with `--record`) drives the same handlers from a script and draws a cursor and a caption naming each input; see `docs/recording/mouse/*.mouse` and `docs/recording.md`.
