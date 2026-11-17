# Ice Climbers freeze glitch fix

Stops the Ice Climbers freeze glitch, like Slippi.

## For players

- In the stock game, a Nana who is hurt or grabbed when Popo starts a special can lose track
  of the fighter she's tied to, which can leave an opponent frozen. With this mod she keeps it.
- Nothing else changes: she still can't join in that special.
- No settings.
- Matches Slippi's code (slippi-ssbm-asm `External/FreezeGlitchFix`), in every Slippi game
  since 3.7.
- Netplay: it changes a match, so it counts toward the mod-set. Both players need it.

## For mod makers

`src/mod.c`, in C because Slippi's code removes one store from a game function, not a yes or
no the game asks. `TGG_BEFORE` on `ftNn_Init_80123954` keeps Nana's link (`x1A5C`) when she
can't join in (action kinds 1 to 13), and `TGG_AFTER` puts it back after the game clears it.
Nothing is kept between frames.

What a Luau script would need to do this: a before and after hook on that function, and a read
and write of a fighter's link to the fighter it's tied to.

## Build

```sh
tgg mod build    # packs build/tgg.freeze-glitch-fix-<version>.zip
tgg mod dev      # build, install and run the game
```

## Credits

Slippi's code is tauKhan's, in slippi-ssbm-asm (GPL-3.0); see `CREDITS.md`.
