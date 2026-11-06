# Friction

What got in the way of writing this mod, as a mod maker would meet it, each with a
suggested fix. The port gained three general APIs for it (`tgg_boot_match`, the `pad_read`
event and frame control, in `tgg/control.h` and `tgg/events.h`); everything below is what's
still missing or rough after them.

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

### No event before the match setup draws from the RNG

Slippi plays a replay from the RNG seed the match was set up with, and the stage and the
players draw from it during the setup. `match_start` runs after the setup, too late, so the
mod hooks the setup itself: `TGG_BEFORE(fn_8016E730, ...)`, a function with an address name
it had to find by reading `gmvs.c` and the replay check.

Fix: a `match_setup` sim event at the start of the VS match setup, with the
`StartMeleeData`, before anything draws from the RNG. Netplay will need the same point to
agree on a seed.

### No end-of-frame event

The replay's frame count moves on after each logic frame that wasn't paused, which the check
works out after the frame (`host_replay_frame_end`). There's no sim event after the game's
objects run, so the mod works it out at the next `pad_read` from `gm_GetDbPauseFlag(1)`, as
"was the frame before this one paused". It works, but it's a subtle off-by-one to get
right.

Fix: a `tick_end` (or `frame_end`) sim event after `HSD_GObj_RunProcs`. The game has
`StartMeleeRules::on_frame_end`, but it's one slot per match, which a mod shouldn't take.

### Turning a raw controller report into the game's pad state

`pad_read` hands the processed state (`HSD_PadStatus`) and the raw report (`PADStatus`).
The replay has both, so it sets both. A mod with only raw reports (netplay, a recorded
controller) would want the game to process them as it does its own, but `HSD_PadClamp`,
`HSD_PadADConvert`, `HSD_PadScale` and `HSD_PadCrossDir` are statics in `controller.c`,
reachable only by `TGG_ORIGINAL_NAMED` with a hand-written signature.

Fix: a field in `tgg_pads` a subscriber sets to say "I replaced the raw report, process it",
or a helper `tgg_pad_process(int port)`.

### `tgg_boot_match` needs a whole `StartMeleeData`

That's right for a replay, which has every field. For a mod that wants "Fox against Marth
on Battlefield", it means knowing to call `gm_SetupRulesDefaults` and
`gm_SetupAllPlayerDefaults` first, then which of 80-odd fields (many named `x3_4`) matter.
The saved rules from the options menu don't apply, which is what a replay wants and maybe
not what a training mod wants.

Fix: the docs' example covers it for now. A `tgg_boot_match_defaults(StartMeleeData*)` that
fills in what the VS menus would (including the saved rules) would make the short case
short.

## Confusing or missing docs

- **The Game Info block's bitfields.** `StartMeleeRules` is the console's struct, and Slippi
  records it as the console laid it out, most significant bit first. The port reorders
  bitfields on PC, so the mod sets each field by name from the right bit (`src/setup.c`,
  copied from the check's mapping). Without the check to copy from, finding that order means
  reading `melee/mn/types.h` against the console layout bit by bit. A note in the docs (or a
  helper that takes the console's bytes) would save a mod maker an afternoon.
- **Which game functions a mod may call.** `gm_SetupRulesDefaults`, `gm_GetDbPauseFlag` and
  `HSD_RandSeedPtr` all work, but finding them is grep. The docs' "finding your way around"
  section could list the match and pad entry points (`gmvs.c`, `gmscene.c`, `controller.c`).
- **Where the seed goes.** The docs say to use the game's RNG in simulation code, and nothing
  about setting it. A replay or netplay mod has to set it; one line in
  `state-and-rollback.md` saying writing `*HSD_RandSeedPtr` from simulation code is fine (it
  rolls back) would help.

## Build and CLI

- **`tgg mod new` makes a git repository with no commit.** It runs `git init` and leaves
  everything untracked. Either commit the template ("Start from the template") or don't
  init; a half-made repo surprises.
- **The template's README stays.** It describes the template ("It logs a line when the game
  starts"), and every mod made from it ships that text until someone rewrites it (the rule
  mods in `~/dev/texturesgg/mods` still do). `tgg mod new` could write a README with only
  the mod's name and the build commands.
- **`HOME` and `TGG_GAME_SDK`.** Building against a port build that isn't installed means
  `TGG_GAME_SDK=<build>/tgg-melee-sdk`, and a scratch `HOME` so `tgg` doesn't touch the real
  one. Both work, but `tgg mod build --sdk` exists and `tgg mod dev` has no equivalent for a
  local port build's executable; `--game <path>` would make a port developer's loop shorter.
- **`dlopen` in a mod.** Nothing says whether a mod may use `dlopen`/`dlsym` (it may: they're
  in the C library). Worth a line in the hooks page's list of what a mod can call.

## Things the mod had to copy

The mod carries its own copies of the check's `.slp` reader, the Game Info mapping and the
input mapping, since a mod can't use the port's host code. That's the right boundary, but
it means two copies to keep in step. If a replay player is wanted in the port itself, a
built-in module (as UCF is) would let the check use it too, and the check would become a
comparison only.
