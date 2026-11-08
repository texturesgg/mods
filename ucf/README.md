# UCF

The Universal Controller Fix, version 0.8: the three codes of slippi-ssbm-asm's
`External/UCF 0.8/Logic/`, dashback, shield drop and tumble wiggle, as Slippi 3.9 to 3.16
ran them.

## For players

- No settings. Turn the mod off to play without UCF.
- Versions follow UCF's own: 0.8 here, 0.84 (Slippi 3.17 and later) in the next release.
  Replays from Slippi 3.9 to 3.16 play back with this one.
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need the same
  version.

## For mod makers

It replaces the spot dodge checks `ftCo_80099794` and `ftCo_8009980C`, so another mod that
replaces them can't load beside it. It keeps the last few raw stick values of each port in
`TGG_STATE`, so it rolls back (`src/mod.c`).

## Build

```sh
tgg mod build    # packs build/tgg.ucf-<version>.zip
tgg mod dev      # build, install and run the game
```
