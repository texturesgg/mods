# Whispy blow direction fix

A KO'd fighter no longer decides which way Whispy Woods blows on Dream Land.

## For players

- Whispy blows toward the side most opponents are on. In the stock game, a fighter that's
  been KO'd still counts, at the position of its model. A screen KO's model follows the
  camera, and each player's camera is different in netplay. With this mod a KO'd fighter
  counts for neither side.
- Everything else about the wind is the game's own, including the coin flip on a tie.
- No settings.
- Matches Slippi 3.15 and later online (slippi-ssbm-asm `Online/Core/WhispyBlowDirFix`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

`ftLib_GetOpponentsDir` picks the side Whispy blows toward, and other callers use it to
aim too. `src/mod.c` replaces the whole function with `TGG_REPLACE`: it's the game's own
loop over fighters, with one more check that skips a fighter in a Dead action
(`DeadDown` through `DeadUpFallHitCameraIce`).

## Build

```sh
tgg mod build    # packs build/tgg.whispy-blow-dir-fix-<version>.zip
tgg mod dev      # build, install and run the game
```
