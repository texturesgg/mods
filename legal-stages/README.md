# Legal stages

The stage select shows the other stages faded, where the game puts them, and they can't be
picked. Random only picks the legal ones: Battlefield,
Final Destination, Dream Land, Yoshi's Story, Fountain of Dreams and Pokémon Stadium.

## For players

- No settings. It sets the game's own Random Stage Switch (VS. Mode, Custom Rules,
  Additional Rules, Random Stage), which every random pick uses: the random button on the
  stage select and the Random stage selection rule.
- It only sets the list on a new save, or one whose list was never changed. A list you
  changed stays as it is, and you can still change it in the menu.
- The list only applies once the Random Stage Switch is unlocked. The port unlocks
  everything by default (`tgg.unlocks`).
- Netplay: it doesn't change a match (the stage select and the save are each player's own),
  so it doesn't count toward the mod-set.

## For mod makers

A display script, `scripts/view.luau`:

- `tgg.change.stage_offered` turns every other stage off (`tgg.offer.OFF`): faded, in its
  place. Stages are content ids (`"melee/battlefield"`).
- `tgg.on.save_loaded` writes the Random Stage Switch list with `tgg.save.set_random_stage`,
  only while `tgg.save.random_stages_untouched()` says the player hasn't changed it.

## Build

```sh
tgg mod build    # packs build/tgg.legal-stages-<version>.zip
tgg mod dev      # build, install and run the game
```
