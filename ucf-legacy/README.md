# UCF (Slippi 3.16 and earlier)

The Universal Controller Fix as Slippi ran it up to 3.16: the three codes of
slippi-ssbm-asm's `External/UCF 0.8/Logic/`, dashback, shield drop and tumble wiggle. Replays
from those versions play back with it.

The newer UCF 0.84 (Slippi 3.17 and later) is `tgg.ucf`. Both replace the same spot dodge
checks, so only one can be on: turn `tgg.ucf` off to use this.

- No settings. Turn the mod off to play without it.
- Netplay: it changes a match, so it counts toward the mod-set. Both players need the same
  version.

## For mod makers

A sim script, `scripts/sim.luau`: the `fighter_turn` and `fighter_check` changes, the fighter
reads, and the raw controllers (`tgg.pad`), which the `tick` event records for each port in
`state`, so it rolls back. The arithmetic the codes do is rounded to 32 bits with `tgg.f32` at
each step. tgg-melee's `ucf-compare --legacy` test checks it against the C version it replaced
on every raw stick value and every two-frame flick.

## Build

```sh
tgg mod build    # packs build/tgg.ucf-legacy-<version>.zip
tgg mod dev      # build, install and run the game
```

## Credits

The Universal Controller Fix is the work of Dan Salvato, tauKhan and the UCF team. This mod
follows the version Slippi ships, from slippi-ssbm-asm (GPL-3.0); see `CREDITS.md`.
