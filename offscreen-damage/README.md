# Offscreen damage

Fighters take offscreen damage whenever they're outside the stage's camera bounds, the way
Slippi online does it.

## For players

- In the stock game, a fighter takes offscreen damage while it shows in a magnifying
  bubble. With this mod it's whenever the fighter is outside the camera bounds, like
  Brawl.
- The damage, the time between ticks and the percent where it stops are the game's own.
- The bubbles on screen don't change.
- The Home-Run Contest's Sandbag never takes offscreen damage, nor does a fighter that's
  already KO'd.
- No settings.
- Matches Slippi's online code (slippi-ssbm-asm `Online/Core/BrawlOffscreenDamage.asm`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

Each frame `Fighter_procAnim` asks `ifMagnify_802FC998` whether the fighter is in a
bubble. The mod answers that one question from the camera bounds and leaves the HUD's own
calls alone (`src/mod.c`):

- `TGG_BEFORE` / `TGG_AFTER` on `Fighter_procAnim` remember which fighter is being
  animated.
- `TGG_REPLACE` on `ifMagnify_802FC998` checks the fighter against
  `Stage_GetCamBounds*Offset` during that call, and calls `TGG_ORIGINAL` otherwise.

## Build

```sh
tgg mod build    # packs build/tgg.offscreen-damage-<version>.zip
tgg mod dev      # build, install and run the game
```
