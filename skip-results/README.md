# Skip results

After a VS match the game goes straight back to the character select, without the results
screen, as Slippi does.

## For players

- No settings.
- Records still count: VS records, KO counts, play time and unlock checks are saved as
  usual. A new challenger or a prize still shows up when one is due.
- Only plain VS mode skips the screen. Tournament mode and the special melees keep theirs.
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

The results screen only shows the match. The records are written when VS mode leaves its
results step (`gmVsMelee_ExitResults`, `gm_801623A4`), whatever the screen did. So the mod
keeps the step and leaves out the screen (`src/mod.c`):

- `TGG_REPLACE` on `gm_Scene_Results_OnEnter` builds nothing in VS mode and calls
  `TGG_ORIGINAL` in every other mode.
- A `tick` event ends the scene (`gm_801A4B60`) on the next frame. Ending it from the setup
  wouldn't stick: the scene loop clears an end asked for before it starts.

## Build

```sh
tgg mod build    # packs build/tgg.skip-results-<version>.zip
tgg mod dev      # build, install and run the game
```
