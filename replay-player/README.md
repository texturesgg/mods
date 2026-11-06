# Replay player

Plays a Slippi replay (`.slp`) in tgg-melee: the game starts straight into the replay's
match and every frame plays the inputs the console recorded, so the match plays out as it
did. You watch it, pause it, step through it a frame at a time, and speed it up or slow it
down.

```sh
TGG_REPLAY_PLAYER_FILE=~/Slippi/Game_20250101T120000.slp tgg mod dev
```

| Key | Does |
|---|---|
| space | pause, play |
| `.` or right arrow | one frame, while paused |
| `=` or up arrow | twice as fast (up to 4x) |
| `-` or down arrow | half as fast (down to 1/16x) |
| `0` | normal speed |

The match pauses when the replay ends. The keys work under X11 (and XWayland); see
[FRICTION.md](FRICTION.md) for why.

## Slippi's rules

A replay plays out as recorded only with the rules the console ran. Slippi adds some to the
game (neutral spawns, online games' off-screen damage, a frozen Pokemon Stadium, an older
UCF), and the mods in `~/dev/texturesgg/mods` reproduce them. At startup the player says
which this replay needs:

```text
tgg.replay-player: Slippi 3.18.0, stage 31, 12034 frames, played online
tgg.replay-player: its console ran Slippi rules these mods reproduce: tgg.neutral-spawns, tgg.offscreen-damage
tgg.replay-player: install and turn them on, or the match plays out differently
```

## How it works

```text
src/mod.c      the playback: boot the match, each frame's inputs and RNG seed, the keys
src/slp.c      reading the replay
src/setup.c    the replay's match as the game sets one up, and the rules report
src/keys.c     the keyboard, through the X server
```

- `tgg_boot_match` (in `TGG_INIT`) starts the game in the replay's match, built from its Game
  Info block (the console's `StartMeleeData`) with `tgg_match_from_console`.
- The `match_setup` event sets the RNG seed the replay started with before anything draws
  from it.
- The `pad_read` event sets every port's input each logic frame: the processed state the game
  uses, and the raw report UCF reads. It also sets the RNG seed to the replay's, as Slippi's
  playback does. The replay's place is in `TGG_STATE`.
- The `tick_end` event moves the replay's place on after each logic frame, unless the match
  was paused through it: a pause on the console leaves no frames in the replay. While the
  match is paused, every port lets go until the pause can end, then the next recorded frame
  presses Start again.
- The `frame` event reads the keys and calls the frame control functions (`tgg_pause`,
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
