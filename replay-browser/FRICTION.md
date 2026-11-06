# Friction

What got in the way of writing this mod, each with a suggested fix. The port gained general
APIs for it, listed at the end; everything above that is still missing or rough.

## Missing API

### No text entry, so the folder is picked by browsing

The folder is a `TGG_SETTING_TEXT`, but nothing lets a player type one: the Mods page shows it
and can't change it, and a mod can't read typed characters (`tgg/input.h` has key states, not
text, so a layout's `/` or an accented letter can't be told apart). The browser works around
it: you browse to the folder and press Y.

Fix: text input for presentation code, `tgg_text_input(char* buf, int cap)` answered from
SDL's text events (it handles layouts and IMEs), and an on-screen keyboard on the Mods page
for text settings, for controllers.

### Only three kinds of the game's pictures

`tgg_image_stock_icon`, `tgg_image_stage` and `tgg_image_stage_name` cover a replay's preview,
but each is a function the port had to write: it knows which archive, which model and which
animation frame. A character select portrait, a series emblem, an item icon or a mod's own
picture all need another port change. A 3D fighter in the preview (the brief's stretch goal)
has no API at all: there's no way to show a model on a screen.

Fix: `tgg_image_from_archive("MnSlChr.usd", "MnSlChr_...", path, frame)` (the general form the
three use), `tgg_image_load("assets/x.png")` for a mod's own files, and a model view for screens
(a fighter in a costume and an animation, placed in screen coordinates).

### Every screen builds its own list

The Mods page and this browser each wrote the same things: a list that scrolls with its
selection, rows in the menus' colors, a footer of button help, a message that shows for a few
seconds. A lobby, a training menu or a settings page would write them again, and drift apart
in look.

Fix: a small `tgg/ui.h` of menu pieces in the game's look (a list with selection and paging,
a row with a label and a value, a header, a footer, a toast), drawn with `tgg/draw.h`, so a
screen is mostly its own content.

### A match from a screen comes back without its result

`enter(from_match)` says a match ended, not how. A tournament screen or a training menu
wants the result (who won, the stocks, a quit). The browser doesn't need it, since the
replay's file has it.

Fix: `tgg_screen_last_match(MatchExitInfo* out)` or the result as an `enter` argument.

### The replay player's header is copied

The browser carries `src/replay_player.h`, a copy of the player's `include/replay_player.h`.
The docs say to copy it or point the include path at the provider's folder, but an installed
mod's `include/` isn't on any path `tgg mod build` knows, so a copy is the only portable way,
and two copies drift.

Fix: `tgg mod build` puts each dependency's `include/` (from the installed mods, or the
registry) on the include path, as it does for the SDK.

### A replay that needs a rule mod can't turn it on

The preview says which Slippi rule mods a replay needs and which aren't loaded (thanks to
`tgg_mod_loaded`), but a mod that's installed and turned off, or a replay that needs the
built-in UCF off (`TGG_UCF=0`), still plays out wrong, and the only fix is restarting the
game with other mods.

Fix: per-match mod sets (the mod-versioning direction's "replays record the exact mod set"):
`tgg_screen_play_match` taking the mods and settings the match needs, loaded for it.

## Rough edges

- **Records come out in reverse.** GCC places a file's static records in reverse order, so the
  Mods page lists this mod's settings in the opposite order of their declarations ("Order"
  before "Replay folder"). Within one mod, `TGG_ON` subscribers of one event run in that order
  too. Fix: sort a mod's records by their declaration (a counter in the record), or build mods
  with `-fno-toplevel-reorder`.
- **Reading a summary reads 256 KB.** The gecko list that says which rules a replay ran comes
  in parts before the first frame, so a summary reads up to 256 KB at the start of each file.
  It's done only for the rows shown, which is fast on a disk and slow on a network share.
  Fix (in the player): cache summaries by path and modification time in the data folder.
- **Dates.** Slippi names files by the console's local time (`Game_20260815T011823`) and writes
  `startAt` in UTC; the browser shows and sorts by the name and falls back to `startAt`, so a
  renamed file sorts by UTC. Good enough for one machine's replays.
- **Four menu sounds.** `tgg_menu_sound` has move, accept, back and error; the game has more
  (the character select's announcer, a menu's open and close), which a richer screen would want.
- **Full-width punctuation.** The game's font draws `/`, `-` and `.` in their full-width forms,
  so paths look spaced out. That's the font; a narrow form would need glyphs it doesn't have.

## Addressed

What the port gained for this mod, each general and documented:

- **Screens** (`tgg/screen.h`, docs/screens.md): `TGG_SCREEN` declares a menu of a mod's own,
  the Mods page (Y on the main menu) lists it, `tgg_screen_play_match` starts a VS match from
  it and comes back after, `tgg_end_match` ends one, `tgg_menu_sound` plays the menus' sounds.
- **Drawing** (`tgg/draw.h`, docs/drawing.md): the `hud_draw` event, rectangles, text in the
  game's own SIS font with width and fitting, the game's stock icons and stage pictures,
  clipping, and the screen's bounds in a widescreen window. Nothing drawn touches the game's
  state.
- **The viewer's input** (`tgg/input.h`, docs/input.md): the controllers and keyboard of the
  person at the machine, read from what the host already polls, with menu repeat at the
  game's pace. The replay player dropped its X11 `dlopen` for it.
- **Settings** (`tgg/settings.h`, docs/settings.md): `TGG_SETTING_BOOL/INT/CHOICE/TEXT`
  records, `settings.json` in the data folder, `TGG_SET`, the Mods page, `tgg_setting_store`,
  and counted mods' settings in the mod-set id.
- **Files** (`tgg/files.h`, docs/files.md): `tgg_dir_list`, `tgg_has_suffix`,
  `tgg_slippi_folders` and `tgg_data_dir`.
- **Which mods are loaded** (`tgg_mod_loaded`, in `tgg/mod.h`): the player names the rule mods
  a replay needs that aren't there.
- **Providers**: a provider's own `include/` is on its include path, and its header may be
  included by any number of its files (the loader refused the second export record).
- **A replay player API** (`tgg.replay-player`'s `include/replay_player.h`):
  `replay_read_summary` and `replay_play`, so the browser has no `.slp` reader of its own.
