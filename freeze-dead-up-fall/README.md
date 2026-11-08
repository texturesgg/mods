# Freeze screen KO fall

A fighter KO'd into the screen stays where it was KO'd in the game's simulation. Only its
model follows the camera.

## For players

- Looks the same as the stock game. The difference is underneath: in the stock game, the
  falling fighter's position comes from the camera, and the camera differs between
  players' screens. With this mod it doesn't, which keeps netplay games in sync.
- No settings.
- Matches Slippi's online code (slippi-ssbm-asm `Online/Core/FreezeDeadUpFallPhysics`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

The game keeps the fighter's flight in screen space (`mv.x50`, moved by `mv.x5C` each
frame). When it draws the fighter, it writes the camera's inverse view into both the model
and the fighter's position. `src/mod.c` keeps the camera out of the position:

- `TGG_BEFORE` / `TGG_AFTER` on `ftCo_DeadUpFall_Anim` catch the frame the fighter hits
  the screen, and move its velocity out of `self_vel` into spare state fields (the same
  bytes Slippi uses, `fp+0x2348`).
- `TGG_REPLACE` on `ftCo_DeadUpFall_Phys` adds that velocity, with gravity, to the
  screen-space step.
- `TGG_BEFORE` / `TGG_AFTER` on `ftDrawCommon_80080E18` put the fighter's position back
  after the game draws it, so only the model moves.

Keeping state in the fighter struct, not in mod globals, means it rolls back with the
match.

## Build

```sh
tgg mod build    # packs build/tgg.freeze-dead-up-fall-<version>.zip
tgg mod dev      # build, install and run the game
```
