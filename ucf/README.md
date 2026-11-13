# UCF

The Universal Controller Fix, version 0.84, as Slippi 3.17 and later run it: the eight codes
of slippi-ssbm-asm's `External/UCF 0.84/UCF/`. Dashback, shield drop, the 1.0 cardinals, SDI
and shield SDI, tumble wiggle and the crouch release fix.

## For players

- No settings. Turn the mod off to play without UCF.
- Versions follow UCF's own. 0.8.0 is UCF 0.8, which Slippi 3.9 to 3.16 ran; replays from
  those versions play back with it.
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need the same
  version.

## For mod makers

It's a stable-tier mod: the `fighter_input`, `fighter_turn` and `fighter_check` events
(the spot dodge checks among them), the curated fighter reads and the raw controllers of
`tgg/pad.h` (`src/mod.c`). It keeps each port's raw stick history in `TGG_STATE`, so it
rolls back.

Not here: UCF's character select screen indicator text.

## Build

```sh
tgg mod build    # packs build/tgg.ucf-<version>.zip
tgg mod dev      # build, install and run the game
```

## Credits

The Universal Controller Fix is the work of Dan Salvato, tauKhan and the UCF team. This mod
follows the version Slippi ships, from slippi-ssbm-asm (GPL-3.0); see `CREDITS.md`.
