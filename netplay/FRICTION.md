# Friction

What got in the way of writing this mod, as a mod maker would meet it, each with a suggested
fix. The port gained general APIs for it: `tgg/rollback.h` (`tgg_rollback_init`, `tgg_frame`,
`tgg_rollback`, `tgg_rollback_confirm`, `tgg_frame_checksum`), the `advance` and `quit`
events, and `tgg_mod_set_id`, `tgg_mod_set_names` and `tgg_game_build` in `tgg/mod.h`.

## Missing API

### This side's controller is read in simulation code

The input a player gives for a frame is read in `pad_read`, the only place the game's own read
of the controller can be seen, and only on a frame's first run (`!tgg_resim()`), since a
re-run reads the recorded report again. That's the right data, but `pad_read` is simulation
code, and the docs tell a mod not to read anything from outside there; netplay has to, and
then keep it outside the state.

Fix: a presentation-side read of a controller, for example
`tgg_controller(int port, HSD_PadStatus* status, PADStatus* raw)` in `advance`, giving the
report the next frame will read and the state the game will make of it. A netplay mod would
then read and send input outside the simulation, and only set inputs in `pad_read`. The mod
already separates the port a side plays from the controller that drives it (the "Your
controller" setting), but only by reading every port's raw report in `pad_read`.

### Hashing the match's setup by hand

Both sides must play the same match, so the mod hashes `StartMeleeData` in `match_setup`. The
struct holds callbacks and pointers (in the rules), which differ between games, so the mod
hashes from `players` on and adds the stage by hand. That depends on the struct's layout and
on knowing which fields are addresses.

Fix: `uint32_t tgg_match_hash(const StartMeleeData*)`, a hash of the fields that decide the
match, the way `tgg_match_from_console` knows its fields.

### No way to stop the game with a message

When the two games can't play together (another build, other mods, another match) the only
thing a mod can do is log and `_Exit`, which skips the port's own shutdown. `exit` from a mod
hangs in the port's teardown with its threads still running.

Fix: `tgg_quit(int code, const char* why)`, which shows or logs the message, runs the `quit`
event and exits the way the window's close does.

### The checksum is one number

`tgg_frame_checksum` gives one 64-bit value, so a desync says "frame 1834 differs" and nothing
about what. The port knows which part differed (fighters, items, RNG, a mod's variable).

Fix: `tgg_frame_checksum_parts`, or an id for the part, so a mod can log
"fighters differ at frame 1834" or even send the parts and let the other side name it.
`TGG_ROLLBACK_SUMS` writes every part and every `TGG_STATE` variable to a file, which is how
this mod's desyncs were found, but that's a test tool, not an API.

### No frame time

The pacing works out where the other side is from its last frame and the round trip, in
frames of 1/60 s. The mod assumes 60 Hz; a port that ran at another rate, or at
`tgg_set_speed`, would pace wrongly.

Fix: `tgg_frame_time()` (or the rate), the seconds one logic frame stands for.

## Settings

The input delay and which controller a person plays with are their own: two players setting
them differently mustn't keep them apart, while every other setting of a counted mod is part
of the mod-set id. The port gained `TGG_SETTING_*_LOCAL` for this: a local setting stays out
of the id. Netplay's two settings and all of direct-connect's are local.

Between platforms the mod-set id can't match at all (the libraries differ), so the port gained
`tgg_mod_set_versions`: the id without the libraries' hashes, which a Linux game and a macOS
game compare instead.

The test settings (the replay played, the fake network, the ports) are still environment
variables, outside the id.

## Things the mod had to copy

- The Slippi replay reader is the replay player's (`src/slp.c`), the third copy after the
  port's own replay check. A replay library both mods depend on (a mod others import from)
  would keep one.
- Turning a replay's recorded input into what the game holds is the replay player's
  `play_input` again.

## What the port had to change

Kept here for the record of what a netplay mod needs from a port, since none of it was visible
from the API until two games played each other:

- The game's sound code makes decisions the simulation reads (a free voice, a random pan
  drawn from the game's RNG), and the mixer ran on the wall clock. Two games desynced on the
  RNG within the first second. Under rollback the mixer now follows the logic frames.
- ARAM holds files the game streams in, and a frame run on a guess could load something over
  what the real timeline needed. Copies into ARAM are now undone on a restore.
- The game's drawing writes state the next frame reads (the camera's render callback, a
  fighter's on-screen flag for the magnifier). A frame the mod held drew an extra time on one
  machine, a machine running behind ran two logic frames before one drawing, and a re-run
  frame wasn't drawn at all: each desynced Dream Land at frame 9061 of one replay. Under
  rollback the port now draws every logic frame exactly once: one logic frame per displayed
  frame, re-run frames drawn unseen, and a held frame's drawing undone.
- Disc reads finished whenever the disc thread was done; a rollback could land while one was
  in flight and its completion arrived in the restored state. Every request now finishes at
  the frame boundary.

## Mods that weren't ready for rollback

`tgg.stadium-preload` keeps whether it has preloaded a transformation, and which, in plain
statics, so a rollback past its preload left it out of step with the stage, and the next
transformation loaded the wrong file (`lbArchive_InitializeDAT` asserted). Synctest never
saw it: the frames that preload load a file, and synctest skips those. Declaring the two
variables with `TGG_STATE` fixes it (the scratch copy the netplay tests used does; the mod's
own repository doesn't yet).

Fix: `TGG_ROLLBACK_TEST=<n>,guess` with the replay check finds this kind of bug in one
process; the port could run it as part of a mod's checks, or synctest could re-run the frames
that load files now that rollback can.
