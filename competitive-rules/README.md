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

Netplay: this is a code mod, so it counts toward the mod-set. Every player needs it.

## For mod makers

All in `src/mod.c`:

- `TGG_AFTER` on `gmMainLib_8015FBA4` (the game resets its rules at start and on erase)
  writes the competitive rules.
- `TGG_AFTER` on `gmMainLib_8015FA34` (the save loads) turns items off when the item
  switch is the game's default, read with `TGG_GAME_SYMBOL` from
  `gmMainLib_DefaultGamePrefs`.

## Build

```sh
tgg mod build    # packs build/tgg.competitive-rules-<version>.zip
tgg mod dev      # build, install and run the game
```
