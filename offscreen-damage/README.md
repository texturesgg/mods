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

Each frame the game asks whether a fighter is in a magnifying bubble, and takes the
off-screen damage when it is. The mod answers that question with the `fighter_check` event,
kind `TGG_CHECK_OFFSCREEN_DAMAGE` (`src/mod.c`): the fighter is off screen when its position
(`tgg_fighter_x` / `tgg_fighter_y`) is outside the camera bounds (`tgg_stage_camera_left` and
the rest). Fighters out of play or on their way out are left out, and so is the Home-Run
Contest.

## Build

```sh
tgg mod build    # packs build/tgg.offscreen-damage-<version>.zip
tgg mod dev      # build, install and run the game
```
