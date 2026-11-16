# Frozen Stadium

Pokémon Stadium never transforms. It stays the neutral stage for the whole match.

## For players

- No settings. Install it and every match on Pokémon Stadium stays on the base layout.
- Matches Slippi's frozen Stadium toggle
  (slippi-ssbm-asm `Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm`).
- Netplay: it changes a match, so it counts toward the mod-set. Both players need it.

## For mod makers

Each frame Stadium asks whether it may go on with its transformations: the `stage_check`
event with `tgg.check.STADIUM_TRANSFORM`. The game says no only in Training Mode.
`scripts/sim.luau` says no always.

## Build

```sh
tgg mod build    # packs build/tgg.frozen-stadium-<version>.zip
tgg mod dev      # install it and run the game
```
