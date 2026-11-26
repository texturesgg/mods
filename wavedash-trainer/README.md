# Wavedash trainer

After every wavedash attempt, a rounded space-navy card above your percent shows your airdodge timing after jumpsquat (`Perfect 1f`, `2f`, `3f`, `Early`, or `Too late`), your stick angle in degrees below horizontal (17 to 22 deg is the shallowest max-length sweet spot), your horizontal slide distance in stage units, and a color-coded strip of your last 8 attempts.

Also flashes your fighter's model 20XX-style on each attempt: cyan on a frame-1 wavedash, green on 2f, gold on 3f, and red when late, early, or missed.

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Show in | Training Mode (default), or Every match (VS, netplay, and replays included) |
| Flash fighter | 20XX-style color flash on your fighter after each attempt |
| Recent attempts | Show the 8-attempt timing strip at the bottom of the card |

All settings are local and never affect netplay.

## How it works

- `scripts/wavedashes.luau` defines the shared attempt state, result payload, and labels.
- `scripts/sim.luau` clears state in `match_setup` and watches `KNEE_BEND`, `JUMP_F`/`JUMP_B`, `ESCAPE_AIR`, and `LANDING_FALL_SPECIAL` in `fighter_update`, posting each completed attempt with `tgg.post`.
- `scripts/view.luau` draws the card, recent-attempts strip, and `tgg.draw.fighter_tint` flash in `hud_draw`.

Read-only, so the port classifies it as a display mod.

## Build

```sh
tgg mod build    # packs build/tgg.wavedash-trainer-<version>.zip
tgg mod dev      # build, install and run the game
```
