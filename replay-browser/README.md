# Replays

Browse your Slippi replays in tgg-melee and watch them. Press Y on the main menu for the Mods
page, then pick Replays.

The list shows a folder's replays, newest first, each with its date and the fighters' stock
icons (the winner's bright). The selected replay's preview has the stage, the players with
their names and connect codes, who won, how long it ran and when. A plays it; when it ends,
or when you press B (Escape on the keyboard), you're back in the list where you left it.

| Button | Key | Does |
|---|---|---|
| Up, Down | arrows | move |
| Left, Right, L, R | | a page at a time |
| A | Enter | play the replay, or open the folder |
| Y | | open this folder from now on |
| X | | newest or oldest first |
| B | Escape | back |

While a replay plays: Start (or space) pauses, D-pad right (or `.`) steps a frame while
paused, D-pad up and down (or `=` and `-`) double and halve the speed, Z (or `0`) goes back
to normal speed, and B (or Escape) goes back to the list.

It starts in the folder you picked with Y, else Slippi's own replay folder (`~/Slippi` or
`~/Documents/Slippi` on Linux, `Documents/Slippi` on Windows and macOS), else your home
folder to find it from. The folder and the order are settings, also on the Mods page, kept
in tgg-melee's `settings.json`.

## Slippi's rules

A replay plays out as recorded only with the rules its console ran. Slippi adds some to the
game (neutral spawns, online games' off-screen damage, frozen Pokemon Stadium and others), and
mods reproduce them (`tgg.neutral-spawns` and the rest). When a replay needs one that isn't
installed, its preview says "Needs" and names them.

## How it works

It needs `tgg.replay-player`, which reads and plays the replays: this mod lists them and asks
the player for each one's summary (`replay_read_summary`) and to play one (`replay_play`), as
`src/replay_player.h` declares (a copy of the player's `include/replay_player.h`).

```text
src/browser.c   the screen (TGG_SCREEN), its input and its settings
src/list.c      the folder: its subfolders and replays, sorted, summaries read when shown
src/draw.c      the list and the preview, with tgg/draw.h
src/names.c     the game's names for fighters and stages
```

## Build

```sh
tgg mod build          # packed into build/
tgg mod dev            # build, install and run
```

Install `tgg.replay-player` (0.2 or later) beside it.
