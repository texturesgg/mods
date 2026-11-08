# Camera-free Stadium monitor

Pokémon Stadium's big screen leaves its close-up of a fighter based on where the fighter
is, not on the camera.

## For players

- In the stock game, the big screen leaves a close-up when it would leave the camera's
  view. Leaving the close-up draws from the RNG, so the camera can change the match, and
  each player's camera is different in netplay. With this mod the close-up ends when the
  fighter leaves fixed bounds (x -120 to 120, y -20 to 80).
- The close-up itself still shows where the game puts it.
- No settings.
- Matches Slippi, which does this in every game (slippi-ssbm-asm
  `Common/PSCameraIndependentMonitor`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

`grStadium_801D32D0` places the close-up and answers whether it fits on screen.
`src/mod.c` replaces it with `TGG_REPLACE`, calls `TGG_ORIGINAL` so the close-up is still
placed, and returns its own answer from the fighter's position. It's the shortest example
of keeping a function's side effects and changing only its return value.

## Build

```sh
tgg mod build    # packs build/tgg.stadium-monitor-<version>.zip
tgg mod dev      # build, install and run the game
```
