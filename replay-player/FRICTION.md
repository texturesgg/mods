# Friction

What got in the way of writing this mod, as a mod maker would meet it, each with a
suggested fix. The port gained general APIs for it: `tgg_boot_match` and frame control
(`tgg/control.h`), and the `pad_read` event (`tgg/events.h`) at first, then the
`match_setup` and `tick_end` events, `tgg_pad_process` and `tgg_match_from_console`. The
last section lists what those took away; everything else is what's still missing or rough.

## Missing API

### No settings, so the replay is an environment variable

The replay to play is `TGG_REPLAY_PLAYER_FILE`. There's no settings API
(`docs/state-and-rollback.md#settings` says to read the environment in `TGG_INIT`), so a
player who starts the game from a launcher can't pick a replay at all, and nothing tells
them the variable exists unless they read this README.

Fix: the settings records the design doc reserves (kind 12: `TGG_SETTING_*`, with a Mods
page in the options menu). A path setting, or better a file picker, is what a replay player
needs; a list of replays from Slippi's folder would be nicer still.

### No keyboard for presentation code

The playback keys (pause, step, speed) have no API to read from. The game reads the
keyboard through SDL, which is linked into the executable and not exported, so a mod can't
reach it, and `HSD_PadMasterStatus` is the replay's input, not the viewer's. The mod asks the
X server directly: it `dlopen`s `libX11.so.6` and polls `XQueryKeymap` in the `frame`
event (`src/keys.c`). That reads the whole display's keyboard, whatever window has focus,
works only under X11 and XWayland (nothing on native Wayland, Windows or macOS), and opens
a second X connection.

Fix: a presentation-only input API, for example `tgg_key_down(TGG_KEY_SPACE)` and
`tgg_key_pressed(...)` (went down since the last displayed frame), answered from the SDL
events the host already pumps, valid only in the `frame` event and never in simulation
code. A replay player, a training mode's menu, a debug overlay and a camera mod all want
it. The same for the viewer's own controller, separate from what `pad_read` plays.

### No way to ask which mods are loaded

The player knows which Slippi rule mods a replay needs (it reads the console's gecko list),
but it can't tell whether they're installed and on, so it can only say "install and turn
them on". A replay that needs `tgg.neutral-spawns` and doesn't have it plays out wrong, and
the viewer has no idea.

Fix: `int tgg_mod_loaded(const char* id)` (and maybe its version), or an optional
dependency in the manifest (`"optional": {"tgg.neutral-spawns": "*"}`) that loads it first
when present. Further out, a way for a mod to turn on another installed mod for the match
it boots.

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
- **`dlopen` in a mod.** Nothing says whether a mod may use `dlopen`/`dlsym` (it may: they're
  in the C library). Worth a line in the hooks page's list of what a mod can call.

## Things the mod had to copy

The mod carries its own copies of the check's `.slp` reader and the input mapping, since a
mod can't use the port's host code. That's the right boundary, but it means two copies to
keep in step. If a replay player is wanted in the port itself, a built-in module (as UCF is)
would let the check use it too, and the check would become a comparison only.

## Addressed

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
