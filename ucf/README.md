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

It replaces the spot dodge checks `ftCo_80099794` and `ftCo_8009980C`, so another mod that
replaces them can't load beside it. Everything it reads is the raw stick history of each
port, which it keeps in `TGG_STATE`, and fighter fields, so it rolls back. It uses the
`fighter_input`, `fighter_turn` and `fighter_check` events (`src/mod.c`).

Not here: UCF's character select screen indicator text.

## Build

```sh
tgg mod build    # packs build/tgg.ucf-<version>.zip
tgg mod dev      # build, install and run the game
```
