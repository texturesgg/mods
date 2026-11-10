# Unlock everything

Every character, stage, unlockable and trophy is unlocked, as on Slippi.

## For players

- A new save starts with everything unlocked, and a save you already have gets whatever it
  was still missing. Unlocking is one way: turning the mod off later doesn't lock anything.
- To unlock things by playing, turn it off on the Mods page before you start a new save.
- Netplay: it's a code mod, so it counts toward the mod-set. It's in the Competitive set,
  so players who use that set have it.

## For mod makers

`src/mod.c` is two after hooks: after the game resets the save block that holds the unlocks
(`gmMainLib_8015F600`), and after it loads a save from the card (`gmMainLib_8015FA34`). Both
call the game's own unlock-everything functions, the ones its debug build runs.
