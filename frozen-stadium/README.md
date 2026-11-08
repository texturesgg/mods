# Frozen Stadium

Pokémon Stadium never transforms. It stays the neutral stage for the whole match.

## For players

- No settings. Install it and every match on Pokémon Stadium stays on the base layout.
- Matches Slippi's frozen Stadium toggle
  (slippi-ssbm-asm `Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm`).
- Netplay: this is a code mod, so it counts toward the mod-set. Both players need it.

## For mod makers

The game already keeps Stadium neutral in Training Mode. When it decides whether to start
a transformation (`grStadium_801D4548`), it asks `gm_8018841C` whether this is Training
Mode. The mod answers yes to that one question and leaves every other caller alone
(`src/mod.c`):

- `TGG_BEFORE` / `TGG_AFTER` on `grStadium_801D4548` set a flag for the length of the call.
- `TGG_REPLACE` on `gm_8018841C` returns true while the flag is set, and calls
  `TGG_ORIGINAL` otherwise.

It's a small example of scoping a replacement to one caller instead of patching the
caller's code.

## Build

```sh
tgg mod build    # packs build/tgg.frozen-stadium-<version>.zip
tgg mod dev      # build, install and run the game
```
