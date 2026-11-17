# Save states

Save states for practice. D-pad left saves the match as it is right now and D-pad right puts it
back: both fighters where they were, their percents and stocks, the CPU mid-action, items, the
stage, everything. "Saved" and "Loaded" show near the top for a second.

There's one state, and it lasts until the match ends. Save again to replace it. Any controller
works.

With the Practice set (`tgg.practice-menu` and `tgg.practice-stage`), it's also Practice > Free
practice in 1-P Mode: Training Mode with nobody else on the stage and a clean screen, and save
states on. In another drill's match, like Ledge dash, it stays out of the way, since that drill
has its own resets on the D-pad.

It's off in netplay. The game switches it off for the whole session, so having it installed
never stops you from playing someone without it. It's also off while a replay plays, since the
replay player has its own controls on the D-pad.

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Use in | Training Mode (default), or every match |

## How it works

`scripts/view.luau`. The `frame` event reads the controllers with `tgg.input.controller` (the person
at this machine, never the simulation's input) and calls `tgg.snapshot.save` or
`tgg.snapshot.load`; the port does them between two logic frames. The `snapshot_loaded` event
shows "Loaded". Its Practice entry (`tgg.menu.add("practice/free", ...)`) starts Free practice
through `tgg.practice-stage`, found with `tgg.mod`, so it doesn't depend on it. The snapshot is
freed when the match ends, since it only loads into its own match.

The manifest says `"class": "offline"`, which the game enforces: while a netplay session is
on, the mod's events don't come, and it never counts toward the mod-set id.

## Build

```sh
tgg mod build    # packs build/tgg.save-states-<version>.zip
tgg mod dev      # build, install and run the game
```
