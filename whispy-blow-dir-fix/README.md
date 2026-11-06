# Whispy blow direction fix

Whispy Woods on Dream Land blows toward the side most opponents are on. A fighter that's
been KO'd (its action is one of the Dead states) counts for neither side. In the game's own
rule it counts where its model is, and a screen KO's model follows the camera, which each
player's game draws differently. Slippi does the same online (slippi-ssbm-asm
`Online/Core/WhispyBlowDirFix`).

```sh
tgg mod build    # a release build, packed into build/
```
