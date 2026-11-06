# Camera-free Stadium monitor

Pokémon Stadium's big screen leaves its close-up of a fighter when the fighter leaves fixed
bounds (x -120 to 120, y -20 to 80) instead of when the close-up would leave the camera's
view. Leaving the close-up draws from the RNG, so the game's own rule lets the camera change
the match. Slippi does the same in every game (slippi-ssbm-asm
`Common/PSCameraIndependentMonitor`).

```sh
tgg mod build    # a release build, packed into build/
```
