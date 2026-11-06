# Freeze screen KO fall

A fighter KO'd off the top that falls toward the screen (DeadUpFall) stays where it was
KO'd in the simulation; only its model follows the camera. Without it the fighter's
position comes from the camera, which differs between players' screens. Slippi's online
code does the same (slippi-ssbm-asm `Online/Core/FreezeDeadUpFallPhysics`).

```sh
tgg mod build    # a release build, packed into build/
```
