# Replay player

Plays a Slippi replay (`.slp`) in tgg-melee: the match starts and every frame plays the
inputs the console recorded, so the match plays out as it did. You watch it, pause it, step
through it a frame at a time, and speed it up or slow it down.

Two ways to start one. A mod's screen asks for it, as `tgg.replay-browser` (Replays, on the
Mods page) does, and the screen comes back when the replay ends. Or the environment names one,
and the game starts straight into it:

```sh
TGG_REPLAY_PLAYER_FILE=~/Slippi/Game_20250101T120000.slp tgg mod dev
```

| Key | Button | Does |
|---|---|---|
| space | Start | pause, play |
| `.` or right arrow | D-pad right | one frame, while paused |
| `=` or up arrow | D-pad up | twice as fast (up to 4x) |
| `-` or down arrow | D-pad down | half as fast (down to 1/16x) |
| `0` | Z | normal speed |
| Escape | B | back to the screen that started it |

The keys and buttons are the viewer's own (`tgg/input.h`), whatever the replay plays on the
game's ports. The corner says when the replay is paused or sped up. Started from the
environment, the match pauses when the replay ends.

## For other mods

`include/replay_player.h` is the player's API. A mod lists `"tgg.replay-player": "^0.2"` in
its manifest's `depends` and includes the header:

- `replay_read_summary(path, &summary)` reads what a replay holds without playing it, from the
  start and the end of the file: the stage, the players (fighter, costume, name, connect code,
  placement), who won and how it ended, its length, date and Slippi version, and the Slippi
  rule mods it needs that aren't loaded.
- `replay_play(path, &why)` plays it from the mod's screen (`tgg/screen.h`); the screen comes
  back when the replay ends or the viewer leaves it.
- `replay_playing()` is nonzero while the player has a replay to play, so a mod that records
  matches can skip them. A mod that only wants this asks for it with
  `tgg_mod_export("tgg.replay-player", "replay_playing@1")` instead of depending on the player.

## Slippi's rules

A replay plays out as recorded only with the rules the console ran. Slippi adds some to the
game (neutral spawns, online games' off-screen damage, a frozen Pokemon Stadium, an older
UCF), and the mods in `~/dev/texturesgg/mods` reproduce them. At startup the player says
which this replay needs:

```text
tgg.replay-player: Slippi 3.18.0, stage 31, 12034 frames, played online
tgg.replay-player: its console ran Slippi rules these mods reproduce: tgg.neutral-spawns, tgg.offscreen-damage
tgg.replay-player: not loaded: tgg.offscreen-damage; install and turn them on, or the match plays out differently
```

## How it works

```text
src/mod.c      the playback: start the match, each frame's inputs and RNG seed, the controls
src/slp.c      reading the replay
src/summary.c  a replay's summary for other mods, from the start and end of its file
src/setup.c    the replay's match as the game sets one up, and the rules it needs
src/keys.c     the viewer's keys and buttons (tgg/input.h)
```

- The match is built from the replay's Game Info block (the console's `StartMeleeData`) with
  `tgg_match_from_console`. A screen starts it with `tgg_screen_play_match`; from the
  environment, `tgg_boot_match` (in `TGG_INIT`) does.
- The `match_setup` event sets the RNG seed the replay started with before anything draws
  from it.
- The `pad_read` event sets every port's input each logic frame: the processed state the game
  uses, and the raw report UCF reads. It also sets the RNG seed to the replay's, as Slippi's
  playback does. The replay's place is in `TGG_STATE`.
- The `tick_end` event moves the replay's place on after each logic frame, unless the match
  was paused through it: a pause on the console leaves no frames in the replay. While the
  match is paused, every port lets go until the pause can end, then the next recorded frame
  presses Start again.
- The `frame` event reads the viewer's controls and calls the frame control functions (`tgg_pause`,
  `tgg_step`, `tgg_set_speed`), which only change when frames run, never what they do.

## Checking it against the console

The port's replay check compares every frame with what the console recorded. With
`TGG_REPLAY_CHECK_ONLY=1` it only compares, and this mod plays the match:

```sh
TGG_REPLAY=game.slp TGG_REPLAY_CHECK_ONLY=1 TGG_REPLAY_PLAYER_FILE=game.slp tgg mod dev
```

It exits when the replay ends, saying `replay: MATCH` if every frame matched. The check turns
on `TGG_SYNC_DISC=1`, whose clock moves with the frames drawn, which is slow to watch: add
`TGG_UNPACED=1 TGG_BACKEND=null` to check as fast as the machine goes, or `TGG_SYNC_DISC=0` to
watch it in real time. Pausing, stepping and changing the speed don't change the result.

## Build

```sh
tgg mod build          # packed into build/
tgg mod dev            # build, install and run
```
