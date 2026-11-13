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
results step, whatever the screen did. So the mod keeps the step and leaves out the screen
(`src/mod.c`): the `results_enter` event sets `skip` in VS mode (`tgg_mode() == TGG_MODE_VS`),
and every other mode shows its results.

## Build

```sh
tgg mod build    # packs build/tgg.skip-results-<version>.zip
tgg mod dev      # build, install and run the game
```
