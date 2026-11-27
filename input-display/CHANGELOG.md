# Changelog

## 1.0.1

- Cache static rounded-rect and octagon geometry, reuse scratch buffers, and batch all shapes before text labels so `hud_draw` stays well under the 2 ms view budget instead of faulting mid-match.
- Default overlay position to Bottom right.

## 1.0.0

- First release: vector-drawn GameCube controller overlay with octagonal stick gate, optional stick trail, yellow C-stick, L/R analog and digital triggers, face buttons, Z, Start, and D-pad.
- Reads fighter inputs from the simulation so it works in matches, training, practice drills, and `.slp` replays as a display mod.
