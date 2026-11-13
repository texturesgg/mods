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

`src/mod.c` is one `player_spawn` event: the game is placing a player at the start of a
match, and the mod moves `x`, `y` and `facing` before it does.

- `tgg_stage()` is the stage's content id. The spawn table holds one set of points for singles
  and one for teams per stage.
- Other stages keep the stage's own points, read with `tgg_stage_spawn_x` / `tgg_stage_spawn_y`.
- Teams come from `tgg_player_team`, and 1P modes (`tgg_mode_single_player`) are left alone.

## Build

```sh
tgg mod build    # packs build/tgg.neutral-spawns-<version>.zip
tgg mod dev      # build, install and run the game
```
