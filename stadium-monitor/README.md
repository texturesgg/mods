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

The game places the close-up and asks whether it fits on screen. `src/mod.c` answers with
the `fighter_check` event, kind `TGG_CHECK_STADIUM_CLOSE_UP`, from the fighter's position. The
close-up is still placed by the game; only the answer changes. It's the shortest example of a
check.

## Build

```sh
tgg mod build    # packs build/tgg.stadium-monitor-<version>.zip
tgg mod dev      # build, install and run the game
```
