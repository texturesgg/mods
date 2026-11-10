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
mods reproduce them (`tgg.neutral-spawns` and the rest).

Press A: `tgg` (beside the game) gets the mods it was played with and the replay player,
asking first about any that aren't official, and the game restarts with them to play it. When the replay is over the
game restarts again with your own mods, back on this screen. A replay your mods already play
as it was played starts right away.

Netplay: textures.gg's review marks it a display mod, so it doesn't count toward the mod-set;
players with and without it play each other. Its settings are your own.

## For mod makers

A mod screen that lists replays and reads each one's summary itself (`src/summary.c`, the
same reader as `tgg.replay-player`'s). It doesn't depend on the player: the player only
loads in a game restarted for a replay, where `tgg_mod_export("tgg.replay-player",
"replay_play")` finds its `replay_play`.

```text
src/browser.c   the screen (TGG_SCREEN), its input and its settings
src/list.c      the folder: subfolders and replays, sorted, summaries read when shown
src/draw.c      the list and the preview, with tgg/draw.h
src/names.c     the game's names for fighters and stages
src/summary.c   a replay's summary, from the start and end of the file
```

Everything here is presentation code: what's in the player's folders never reaches a match.

## Build

```sh
tgg mod build    # packs build/tgg.replay-browser-<version>.zip
tgg mod dev      # build, install and run the game
```
