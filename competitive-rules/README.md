# Competitive rules

VS mode starts with tournament rules instead of the game's 2-minute time match with items.

## For players

The rules the game starts with:

| Rule | Value | Game's default |
|---|---|---|
| Mode | Stock | Time |
| Stocks | 4 | 3 |
| Stock match time limit | 8:00 | none |
| Items | None | Medium |
| Team attack | On | Off |

The rest is the game's default: handicap off, damage 1.0, pause on, stage selection On,
score display off, self-destructs -1.

Change them in the game's own menus (VS Mode, Custom Rules). As in the game, your changes
last until you quit, and the item switch is saved. The mod only turns items off on a save
whose item switch you never changed.

Not covered: the random stage list (see `tgg.legal-stages`).

Netplay: it only sets the rules a match starts from, and netplay agrees on the rules itself, so
it doesn't count toward the mod-set.

## For mod makers

A display script, `scripts/view.luau`, with two save events and the save calls:

- `tgg.on.rules_defaults` (the game resets its rules: at start and on erase) writes the
  competitive rules with `tgg.save.set_rule`.
- `tgg.on.save_loaded` turns items off when the item switch is still the game's default
  (`tgg.save.rule_default`), so a player's own choice stays.

## Build

```sh
tgg mod build    # packs build/tgg.competitive-rules-<version>.zip
tgg mod dev      # build, install and run the game
```
