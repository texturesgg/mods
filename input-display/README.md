# Input display

A live GameCube controller overlay built on `tgg.draw.*`. Shows the control stick inside an octagonal gate (with an optional fading stick trail), the yellow C-stick, analog and digital L/R triggers, A/B/X/Y/Z/Start, and the D-pad.

Because inputs come from the fighter in the simulation, the overlay works in Training Mode, practice drills, VS matches, netplay, and `.slp` replay playback alike. It never writes to the simulation, so the port classifies it as a display mod and it never affects netplay.

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Show in | Off, Training & replays (default), or Every match |
| Port | Auto (default), or Port 1 through Port 4 |
| Position | Bottom left (default), Bottom right, Top left, or Top right |
| Size | Compact or Normal (default) |
| Stick trail | Show a fading trail behind the control stick (on by default) |

All settings are local and never affect netplay.

## How it works

- `scripts/inputs.luau` defines the per-port input table shared through `tgg.state`.
- `scripts/sim.luau` clears state in `match_setup` and records each fighter's sticks, buttons, and trigger in `fighter_update`.
- `scripts/view.luau` draws the controller in `hud_draw`, shifting around Melee's percent HUD, the top-right netplay ping pill, and the replay timeline bar so nothing overlaps.

## Build

```sh
tgg mod build    # packs build/tgg.input-display-<version>.zip
tgg mod dev      # build, install and run the game
```
