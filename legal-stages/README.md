# Legal stages

The stage select shows the other stages faded, where the game puts them, and they can't be
picked; random picks only pick the legal ones: Battlefield,
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

- The `stage_offered` event turns every other stage off (`TGG_STAGE_OFF`): faded, in its place.
  Stages are content ids (`"melee/battlefield"`), compared with `strcmp`.
- `save_loaded` writes the Random Stage Switch list with `tgg_save_set_random_stage`, only
  while `tgg_save_random_stages_untouched()` says the player hasn't changed it.

## Build

```sh
tgg mod build    # packs build/tgg.legal-stages-<version>.zip
tgg mod dev      # build, install and run the game
```
