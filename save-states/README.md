# Save states

For practice. D-pad left saves the match as it is right now; D-pad right puts it back. Both
fighters where they were, their percents and stocks, the CPU mid-action, items, the stage,
everything. "Saved" and "Loaded" show near the top for a second.

There's one state, and it lasts until the match ends. Save again to replace it. Any controller
works.

It's off in netplay: the game switches it off for the whole session, so having it installed
never stops you playing someone without it. It's also off while a replay plays, since the
replay player has its own controls on the D-pad.

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Use in | Training Mode (default), or every match |

## How it works

`src/mod.c`, in one file. The `frame` event reads the controllers with `tgg_viewer_pad_read`
(the person at this machine, never the simulation's input) and calls `tgg_snapshot_save` or
`tgg_snapshot_load` (`tgg/snapshot.h`); the port does them between two logic frames. The
`state_loaded` event shows "Loaded". The snapshot is freed when the match ends, since it only
loads into its own match.

The manifest says `"class": "offline"`, which the game enforces: while a netplay session is
on, the mod's events don't come, and it never counts toward the mod-set id.

## Build

```sh
tgg mod build    # packs build/tgg.save-states-<version>.zip
tgg mod dev      # build, install and run the game
```
