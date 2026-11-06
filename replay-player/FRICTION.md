# Friction

What got in the way of writing this mod, as a mod maker would meet it, each with a
suggested fix. The port gained general APIs for it: `tgg_boot_match` and frame control
(`tgg/control.h`), and the `pad_read` event (`tgg/events.h`) at first, then the
`match_setup` and `tick_end` events, `tgg_pad_process` and `tgg_match_from_console`. The
last section lists what those took away; everything else is what's still missing or rough.

## Missing API

### `tgg_boot_match` needs a whole `StartMeleeData`

That's right for a replay, which has every field. For a mod that wants "Fox against Marth
on Battlefield", it means knowing to call `gm_SetupRulesDefaults` and
`gm_SetupAllPlayerDefaults` first, then which of 80-odd fields (many named `x3_4`) matter.
The saved rules from the options menu don't apply, which is what a replay wants and maybe
not what a training mod wants.

Fix: the docs' example covers it for now. A `tgg_boot_match_defaults(StartMeleeData*)` that
fills in what the VS menus would (including the saved rules) would make the short case
short.

## Build and CLI

- **Watching the check.** The replay check sets `TGG_SYNC_DISC=1`, whose clock moves 100 us
  per read between frames drawn; paced (not `TGG_UNPACED`), on a busy machine, the game then
  runs at a few frames a second, and at a speed below 1 much slower still. It looks hung. The
  check could leave the clock alone when it isn't driving the input (`TGG_REPLAY_CHECK_ONLY`),
  or the sync clock could fall back to real time while paced.

## Things the mod had to copy

The mod carries its own copies of the check's `.slp` reader and the input mapping, since a
mod can't use the port's host code. That's the right boundary, but it means two copies to
keep in step. If a replay player is wanted in the port itself, a built-in module (as UCF is)
would let the check use it too, and the check would become a comparison only.

## Addressed

- **Picking a replay.** A replay no longer needs `TGG_REPLAY_PLAYER_FILE`: another mod's
  screen (`tgg.replay-browser`) lists the player's replays and calls `replay_play`, which starts
  the match with `tgg_screen_play_match` (`tgg/screen.h`) and gets the screen back after. The
  environment variable stays for scripted runs and the replay check.
- **The viewer's keys.** `tgg/input.h` reads the viewer's keyboard and controllers from what
  the host already polls, on every platform; `keys.c` no longer `dlopen`s X11, and the
  controls work on a controller too.
- **Which mods are loaded.** `tgg_mod_loaded(id)` (`tgg/mod.h`): the player says which of the
  Slippi rule mods a replay needs aren't loaded, in the log and in `replay_read_summary`, so
  a browser can show it before playing. Turning one on for a match is still missing (see the
  browser's FRICTION.md).
- **The match setup.** The `match_setup` event comes at the start of a match's setup, before
  the stage and the players draw from the RNG; the mod sets the replay's starting seed there
  instead of hooking `fn_8016E730`. The port's replay check sets its seed at the same point.
- **The end of a frame.** The `tick_end` event comes after each logic frame's objects ran; the
  mod moves the replay's place on there, from `gm_GetDbPauseFlag(1)` for the frame that just
  ran, instead of working out the frame before at the next `pad_read`.
- **Raw controller reports.** `tgg_pad_process(status, raw)` makes a port's state from a
  report as the game's own read does (clamp, stick directions, scale). This mod doesn't need
  it, since a replay records the processed state too, but a netplay or controller-recording
  mod does.
- **The Game Info block's bitfields.** `tgg_match_from_console` fills a `StartMeleeData` from
  the console's 0x138 bytes, and the game-control docs give the bit order for anyone reading
  them by hand. The mod and the replay check both use it, so the mapping isn't copied.
- **Which game functions a mod may call.** The writing-mods page's "finding your way around"
  lists the match and controller entry points (`gmvs.c`, `gmscene.c`, `controller.c`,
  `gm_SetupRulesDefaults`, `gm_GetDbPauseFlag`).
- **Where the seed goes.** The state-and-rollback page says setting `*HSD_RandSeedPtr` from
  simulation code is fine and rolls back, and points at `match_setup`.
- **The CLI and a local port build** (tgg-cli on core's `cli/mod-lab` branch, not yet in a
  release). `tgg mod new` takes `--sdk`/`TGG_GAME_SDK` as `tgg mod build` does, makes a first
  commit in the repository it creates, and gives a mod started from the template its own
  README; `tgg mod dev --game <build folder>` (or `TGG_GAME`) runs a local port build.
