# Skip results

After a VS match the game goes straight back to the character select, without the results
screen, as Slippi does.

Records still count. The results screen only shows the match: the VS records, KO counts,
play time and unlock checks are written when VS mode leaves its results step, and the mod
keeps that step. It only leaves out the screen, which ends on its first frame. A new
challenger or a prize still shows up when one is due.

Only plain VS mode skips the screen. Tournament mode and the special melees keep theirs.

## Build

```sh
cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
cmake --build build
```

The mod lands in `build/mods/tgg.skip-results/`.
