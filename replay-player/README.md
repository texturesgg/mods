# Replay player

Plays a Slippi replay (`.slp`) in tgg-melee. You watch the match as it was played: pause it,
step through it a frame at a time, speed it up or slow it down, go back a couple of seconds
or skip ahead. At any frame you can take over and play from there with your own controllers.

## For players

Most players start replays from `tgg.replay-browser` (Data, then Replays), which needs this
mod. You can also start one straight from the environment:

```sh
TGG_REPLAY_PLAYER_FILE=~/Slippi/Game_20250101T120000.slp tgg mod dev
```

| Key | Button | Does |
|---|---|---|
| space | Start | pause, play |
| `.` or right arrow | D-pad right | one frame while paused; skip ahead 10 seconds while playing |
| `,` or left arrow | D-pad left | back about 2 seconds (as far as the last 30 seconds) |
| `=` or up arrow | D-pad up | twice as fast (up to 4x) |
| `-` or down arrow | D-pad down | half as fast (down to 1/16x) |
| `0` | Z | normal speed |
| | X | take over |
| Escape | B | back to the screen that started it |

The controls are the viewer's own keyboard and controllers, whatever the replay plays on
the game's ports. The corner shows when the replay is paused, sped up or skipping ahead.
Started from the environment, the match pauses when the replay ends. In a game restarted to
play it (see the port's [restart docs](https://textures.gg/docs/mods/latest/restart)), the
player's own game comes back when the match is over or they leave.

### Taking over

X pauses the replay and asks who plays each player from here: the recording (they keep doing
what they really did), nobody (they stand still), or one of your controllers. The controller
that pressed X starts on the first player. Left and right change a player, A plays from here,
B goes back to watching.

From then on it's an ordinary match, and every button on your controller goes to the game.
D-pad left (or the left arrow) puts everything back to the moment you took over, so you can
try it again as often as you like. Leave with the game's own L+R+A+Start, or Escape.

No settings. Netplay: this is a code mod, so it counts toward the mod-set. Both players
need it, or neither.

### Slippi's rules

A replay plays out as recorded only with the rules its console ran. Slippi adds some
(neutral spawns, online off-screen damage, frozen Pokémon Stadium, UCF), and
separate mods reproduce them. At startup the log says which ones this replay needs and
which aren't loaded:

```text
tgg.replay-player: Slippi 3.18.0, stage 31, 12034 frames, played online
tgg.replay-player: its console ran Slippi rules these mods reproduce: tgg.neutral-spawns, tgg.offscreen-damage, tgg.ucf
tgg.replay-player: not loaded: tgg.offscreen-damage; install and turn them on, or the match plays out differently
```

## For mod makers

### The API

`include/replay_player.h` is the player's API. List `"tgg.replay-player": "^0.3"` in your
manifest's `depends` and copy the header (or add this mod's `include/` to your include path).

- `replay_read_summary(path, &summary)` reads what a replay holds without playing it, from
  the start and end of the file: stage, players (fighter, costume, name, connect code,
  placement), winner, how it ended, length, date, Slippi version, and the rule mods it
  needs that aren't loaded.
- `replay_play(path, &why)` plays it from your screen (`tgg/screen.h`). The screen comes
  back when the replay ends or the viewer leaves.
- `replay_playing()` is nonzero while a replay is playing, so a recording mod can skip it.
  A mod that only needs this can look it up with
  `tgg_mod_export("tgg.replay-player", "replay_playing@1")` instead of depending on the
  player. A script asks `tgg.mod("tgg.replay-player")`, which is nil without the player, and
  calls `playing()` (`assets/script-api.json`).

### How it works

```text
src/mod.c      playback: starting the match, each frame's inputs and RNG seed, the controls
src/slp.c      reading the replay
src/summary.c  the summary other mods read
src/setup.c    the replay's match setup, and the rule mods it needs
src/keys.c     the viewer's keys and buttons (tgg/input.h)
src/takeover.c rewinding, skipping ahead and taking over: snapshots, fast forward, the panel
```

- The match comes from the replay's Game Info block (the console's `StartMeleeData`) via
  `tgg_match_from_console`. A screen starts it with `tgg_screen_play_match`; from the
  environment, `tgg_boot_match` in `TGG_INIT` does.
- `match_setup` sets the replay's starting RNG seed before anything draws from it.
- `pad_read` sets every port's input each logic frame: the processed state the game uses
  and the raw report UCF reads. It also sets the RNG seed, as Slippi's playback does. The
  replay's position lives in `TGG_STATE`.
- `tick_end` moves to the next recorded frame, unless the match was paused: a paused
  console records no frames. While paused, every port lets go until the pause can end, then
  the next recorded frame presses Start again.
- `frame` reads the viewer's controls and calls `tgg_pause`, `tgg_step` and
  `tgg_set_speed`, which change when frames run, never what they do.
- `advance` keeps a snapshot (`tgg/snapshot.h`) about once a second for the last 30
  seconds; going back loads one, and skipping ahead is `tgg_fast_forward`.
- Taking over: a snapshot marks the moment, and from then `pad_read` gives each taken port
  that controller's own report (`tgg_pad_process`), a port standing still a released
  controller, and the rest the recording, without setting the RNG seed any more. Who plays
  which port is kept outside `TGG_STATE`: it changes only while the game is held, and going
  back to the takeover point keeps it.
- `hud_draw` draws the pause and speed indicator, the takeover panel and its help line.

To check playback frame by frame against the console, see the port's replay check
(`TGG_REPLAY`) in the debugging docs.

## Build

```sh
tgg mod build    # packs build/tgg.replay-player-<version>.zip
tgg mod dev      # build, install and run the game
```
