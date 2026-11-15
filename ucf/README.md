# UCF

The Universal Controller Fix, version 0.84, the same as Slippi 3.17 and later: the eight codes
of slippi-ssbm-asm's `External/UCF 0.84/UCF/`. Dashback, shield drop, the 1.0 cardinals, SDI
and shield SDI, tumble wiggle and the crouch release fix.

## For players

- No settings. Turn the mod off to play without UCF.
- Replays from Slippi 3.9 to 3.16, which ran UCF 0.8, play back with `tgg.ucf-legacy`.
- Netplay: it changes a match, so it counts toward the mod-set. Both players need the same
  version.

## For mod makers

A sim script, `scripts/sim.luau`: the `fighter_input`, `fighter_turn` and `fighter_check`
changes (the spot dodge checks among them), the fighter reads and the raw controllers
(`tgg.pad`). It keeps each port's raw stick history in `state`, so it rolls back. The game's
floats are 32-bit, so the arithmetic the codes do is rounded with `tgg.f32` at each step.
tgg-melee's `ucf-compare` test checks it against the C version it replaced on every raw stick
value and every two-frame flick.

Not here: UCF's character select screen indicator text.

## Build

```sh
tgg mod build    # packs build/tgg.ucf-<version>.zip
tgg mod dev      # build, install and run the game
```

## Credits

The Universal Controller Fix is the work of Dan Salvato, tauKhan and the UCF team. This mod
follows the version Slippi ships, from slippi-ssbm-asm (GPL-3.0); see `CREDITS.md`.
