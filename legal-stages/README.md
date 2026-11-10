# Legal stages

The stage select shows only the legal stages, and random picks only pick them: Battlefield,
Final Destination, Dream Land, Yoshi's Story, Fountain of Dreams and Pokémon Stadium.

## For players

- No settings. It sets the game's own Random Stage Switch (VS. Mode, Custom Rules,
  Additional Rules, Random Stage), which every random pick uses: the random button on the
  stage select and the Random stage selection rule.
- It only sets the list on a new save, or one whose list was never changed. A list you
  changed stays as it is, and you can still change it in the menu.
- The list only applies once the Random Stage Switch is unlocked. The port unlocks
  everything by default (`tgg.unlocks`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

- The `stage_offered` event hides every other stage on the stage select.
The list lives in the save (`GamePrefs.stage_mask`), so the mod only writes it after the
game sets it up (`src/mod.c`):

- `TGG_AFTER` on `gmMainLib_8015F600` (the save block holding the list is reset: at start,
  for a new save, and on erase) and on `gmMainLib_8015FA34` (the save loads).
- `TGG_GAME_SYMBOL` reads `gmMainLib_DefaultGamePrefs`, to tell an untouched list from one
  the player changed.

## Build

```sh
tgg mod build    # packs build/tgg.legal-stages-<version>.zip
tgg mod dev      # build, install and run the game
```
