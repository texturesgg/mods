# Replays

Browse your Slippi replays in tgg-melee and watch them.

## For players

Press Y on the main menu for the Mods page, then pick Replays.

The list shows a folder's replays, newest first, each with its date and the fighters' stock
icons (the winner's bright). The preview shows the stage, the players with their names and
connect codes, who won, how long it ran and when. A plays it. When it ends, or when you
press B (Escape), you're back in the list where you left off.

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

### Settings

Both are on the Mods page too.

| Setting | Key | Default | Does |
|---|---|---|---|
| Replay folder | `folder` | empty | the folder to open. Empty: Slippi's own replay folder (`~/Slippi` or `~/Documents/Slippi` on Linux, `Documents/Slippi` on Windows and macOS), else your home folder |
| Order | `sort` | Newest first | newest or oldest first |

### Slippi's rules

A replay plays out as recorded only with the rules its console ran. Slippi adds some
(neutral spawns, online off-screen damage, frozen Pokémon Stadium and others), and separate
mods reproduce them (`tgg.neutral-spawns` and the rest). When a replay needs one that isn't
installed, its preview says "Needs" and names them.

Netplay: this is a code mod, so it counts toward the mod-set. Both players need it, or
neither. Its settings are your own and don't count.

## For mod makers

An example of a mod screen built on another mod's API. `tgg.replay-player` reads and plays
the replays; this mod lists them, asks the player for each one's summary
(`replay_read_summary`) and plays one (`replay_play`). `src/replay_player.h` is a copy of
the player's `include/replay_player.h`.

```text
src/browser.c   the screen (TGG_SCREEN), its input and its settings
src/list.c      the folder: subfolders and replays, sorted, summaries read when shown
src/draw.c      the list and the preview, with tgg/draw.h
src/names.c     the game's names for fighters and stages
```

Everything here is presentation code: what's in the player's folders never reaches a match.

## Requirements

`tgg.replay-player` 0.2 or later.

## Build

```sh
tgg mod build    # packs build/tgg.replay-browser-<version>.zip
tgg mod dev      # build, install and run the game
```
