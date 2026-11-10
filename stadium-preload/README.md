# Preloaded Stadium transformations

Pokémon Stadium picks and loads its next transformation as soon as it's back to the
neutral stage, so the transformation starts the moment the neutral timer runs out.

## For players

- In the stock game, Stadium picks its next transformation when the neutral stage's timer
  runs out, then waits for the file to load. With this mod the pick and the load happen
  early, and the transformation starts that same frame.
- Which transformation comes next is still random, and never the one that just ended.
- No settings.
- Matches Slippi's Stadium preload option on console (slippi-ssbm-asm
  `Common/Preload Stadium Transformations`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

`grStadium_801D4548` runs the transformations, one step a frame. `src/mod.c` hooks around
it:

- `TGG_BEFORE` / `TGG_AFTER` on `grStadium_801D4548` note the stage and its step for the
  length of the call.
- `TGG_REPLACE` on `Stage_80225194`, which the step function calls right after the game's
  frozen-stage check, picks and loads the next transformation on a step-0 frame.
- `TGG_REPLACE` on `HSD_Randi` returns the preloaded kind when the timer runs out, without
  drawing from the RNG.
- `TGG_REPLACE` on `lbFile_80016580` skips the game's own load of a file that's already in.
- `TGG_AFTER` on `grStadium_801D13E0` clears the preload when the stage starts.

The preload choice is read on later frames, so it's declared with `TGG_STATE` and rolls
back with the game. The values that only live for one call are plain globals.

## Build

```sh
tgg mod build    # packs build/tgg.stadium-preload-<version>.zip
tgg mod dev      # build, install and run the game
```
