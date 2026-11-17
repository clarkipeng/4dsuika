# Taste

Read `~/.agentworld/lab/taste.md` first; this file refines it for this repo.

- The target look is `footage/gameImage.png`: warm sunset light, low-poly faceted shapes, a glowing bowl, a floating island in soft clouds. Every visual change is judged against it, side by side.
- 4D is the point. Everything in the world is a 4D object, and rotating in 4D must visibly change the whole scene, background included. No 3D-only props.
- It must stay a fun game first: readable fruit, a clear drop, satisfying merges. A 4D effect that makes play confusing loses to one that is clear.
- Smooth on a laptop: 60 fps at 1080p on an Apple Silicon Mac, with a quality setting for slower machines.
- Small, readable code. One renderer, one physics path, no copies of old versions (`game copy.hpp` style files are deleted, not kept).
- Show, don't tell: every PR that changes what you see includes screenshots or a short capture.
