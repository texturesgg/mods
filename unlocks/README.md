# Unlock everything

Every character, stage, unlockable and trophy is unlocked, as on Slippi.

## For players

- A new save starts with everything unlocked, and a save you already have gets whatever it
  was still missing. Unlocking is one way: turning the mod off later doesn't lock anything.
- To unlock things by playing, turn it off on the Mods page before you start a new save.
- Netplay: it's a code mod, so it counts toward the mod-set. It's in the Competitive set,
  so players who use that set have it.

## For mod makers

`src/mod.c` is one `save_loaded` event that calls `tgg_save_unlock_all()`: the game's own
unlock-everything functions, the ones its debug build runs. It logs when the save had
something still locked.
