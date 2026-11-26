# Save states

Three quick save slots for practice. D-pad left saves to the active slot, D-pad right loads it, and D-pad up cycles through slots 1, 2, and 3. Saves everything: both fighters where they were, their percents and stocks, the CPU mid-action, items, the stage, and the RNG.

A compact `[1] [2] [3]` pill bar at top-center shows the active slot and marks occupied slots with a green dot. Slots last until the match ends. Save again to replace a slot. Any controller works.

With the Practice set (`tgg.practice-menu` and `tgg.practice-stage`), it also adds Practice > Free practice in 1-P Mode: Training Mode with nobody else on the stage and a clean screen, with optional auto-load on KO. In another drill's match, like Ledge dash or Wavedash, it stays out of the way, since that drill has its own resets on the D-pad.

It's off in netplay. The game switches it off for the whole session, so having it installed never stops you from playing someone without it. It's also off while a replay plays, since the replay player has its own controls on the D-pad.

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Use in | Training Mode (default), or Every match |
| Auto-load on KO | Automatically load the active slot after a KO in Free practice (off by default) |

## How it works

`scripts/view.luau`. The `frame` event reads the controllers with `tgg.input.controller` (the person at this machine, never the simulation's input) and calls `tgg.snapshot.save` or `tgg.snapshot.load` on the active slot. The port runs them between two logic frames. The `snapshot_loaded` event shows the confirmation pill. Its Practice entry (`tgg.menu.add("practice/free", ...)`) starts Free practice through `tgg.practice-stage`, found with `tgg.mod`, so it doesn't depend on it. All snapshots are freed when the match ends, since they only load into their own match.

The manifest says `"class": "offline"`, which the game enforces: while a netplay session is on, the mod's events don't come, and it never counts toward the mod-set id.

## Build

```sh
tgg mod build    # packs build/tgg.save-states-<version>.zip
tgg mod dev      # build, install and run the game
```
