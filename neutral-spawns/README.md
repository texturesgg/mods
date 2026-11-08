# Neutral spawns

Players start each match at the stage's neutral spawn points, the way Slippi online places
them.

## For players

- On the six legal stages (Final Destination, Battlefield, Yoshi's Story, Dream Land,
  Fountain of Dreams, Pokémon Stadium), singles players start at the neutral spawns and
  2v2 teams start side by side.
- On other stages, players take the stage's own spawn points in player order.
- Everyone faces the middle of the stage.
- Training Mode, Target Test and 1P modes keep the game's spawns. So do team matches that
  aren't 2v2.
- No settings.
- Matches Slippi's neutral spawns (slippi-ssbm-asm `External/NeutralSpawn`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

The game stores each player's starting position with `Player_80032768` while it sets up
the match's players (`fn_8016E2BC`). `src/mod.c`:

- `TGG_BEFORE` / `TGG_AFTER` on `fn_8016E2BC` mark the setup, so the mod only touches
  spawns stored there.
- `TGG_BEFORE` on `Player_80032768` rewrites the position argument before the game stores
  it, then sets the facing direction with `Player_SetFacingDirection`.
- The spawn table holds one set of points for singles and one for teams per stage
  (`StKind` numbers). Other stages read the stage's own points with `Stage_80224E64`.

## Build

```sh
tgg mod build    # packs build/tgg.neutral-spawns-<version>.zip
tgg mod dev      # build, install and run the game
```
