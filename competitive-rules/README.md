# Competitive rules

VS mode starts with tournament rules instead of the game's 2-minute time match with items,
and the rules you pick in the menus stay picked from one start to the next.

## For players

The defaults:

| Rule | Value | Game's default |
|---|---|---|
| Mode | Stock | Time |
| Stocks | 4 | 3 |
| Stock match time limit | 8:00 | none |
| Items | None | Medium |
| Handicap | Off | Off |
| Damage ratio | 1.0 | 1.0 |
| Team attack | On | Off |

Team attack is on because doubles is played with it on. The rest keeps the game's default:
pause on, stage selection On, score display off, self-destructs -1.

Change any rule in the game's menus and it stays that way next time. The game normally
resets VS rules every start and only saves the item switch. The menus that count are
Custom Rules (also opened from the rules banner on the character select), Additional Rules
and the item switch. If anything else changes the rules outside a match, the mod puts yours
back. Tournament mode changes the handicap on its own, so its changes last until you leave
it.

The same rules are on the Mods page, and the two stay in sync:

| Setting | Default | Range |
|---|---|---|
| Mode (`mode`) | Stock | Time, Stock, Coin, Bonus |
| Stocks (`stocks`) | 4 | 1 to 99 |
| Stock time limit (`stock-time`) | 8 | minutes, 0 = none |
| Time match (`time`) | 2 | minutes, 0 = none |
| Items (`items`) | None | None to Very high |
| Handicap (`handicap`) | Off | Off, Auto, On |
| Damage ratio (`damage`) | 1.0 | 0.5 to 2.0 |
| Stage selection (`stage-select`) | On | On, Random, Ordered, Turns, Loser |
| Team attack (`team-attack`) | On | |
| Pause (`pause`) | On | |
| Score display (`score`) | Off | |
| SD penalty (`sd-penalty`) | -1 | -1, 0, -2 |

Not covered: which items are on in the item switch (only the frequency), and the random
stage list (see `tgg.legal-stages`).

Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

All in `src/mod.c`:

- Each rule is a setting (`TGG_SETTING_CHOICE`, `TGG_SETTING_INT`, `TGG_SETTING_BOOL`), so
  the settings file is where the rules persist.
- `TGG_AFTER` on `gmMainLib_8015FBA4` (the game resets its rules) and `gmMainLib_8015FA34`
  (the save loads, which overwrites the item switch) write the settings into the game.
- `TGG_AFTER` on each rules menu's input and value functions reads what the menu changed
  and saves it with `tgg_setting_store`.
- A `frame` event outside a match (`tgg_in_match`) compares the game's rules with the
  settings: a Mods page change goes to the game, any other change is put back.

It's an example of two-way sync between a setting and game state the game doesn't save.

## Build

```sh
tgg mod build    # packs build/tgg.competitive-rules-<version>.zip
tgg mod dev      # build, install and run the game
```
