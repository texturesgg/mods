# Friction

What got in the way of writing this mod, as a mod maker would meet it, each with a
suggested fix. The port gained three general APIs for it: `tgg_frame_final`
(`tgg/rollback.h`), `tgg_mod_export` (`tgg/mod.h`) and `tgg_match_to_console`
(`tgg/control.h`). Everything below is what's still missing or rough.

## Netplay

### A recorder counts toward the mod-set id

Every mod with code counts, so two players can only play each other if both run the
recorder, at the same version and build, with the same settings. A player who sets their own
replay folder no longer matches anyone. Synctest already says the recorder "doesn't change the
game".

Fix: settings that only presentation reads (a folder, a toggle for writing files) could be
declared local, `TGG_SETTING_TEXT_LOCAL`, and left out of the id. For the mod itself, a way to
say "presentation only" that the port checks (no hooks that write game memory, synctest's
per-mod check) would let a recorder, a HUD or a stats mod join any match.

## Missing API

### No events where Slippi records

Slippi reads the game at fixed points: after a fighter's input is processed, after its
camera process, between the fighters' collision and dynamics processes for items, and in the
stage updates. Only the first has an event (`fighter_input`). The rest are hooks on decomp
functions (`Fighter_procCamera`, `Fighter_procDynamics`, `Fighter_procMap`,
`ftCo_LandingAir_EnterWithLag`, `grIzumi_801CC358`, `grStadium_801D4548`,
`grOldPupupu_802113E0`), which tie the mod to names that can change.

Fix: a `fighter_frame_end` event (after the fighter's last process of the frame), and an
`items_updated` event between the item processes and the fighters' dynamics.

### Console bytes of a fighter or item

Slippi sends some fields as the console holds them: five bitfield bytes of the fighter
(0x2218 to 0x221F) and words of an item's variables. The port lays bitfields out the other way
round and widens pointers, so the recorder rebuilds each byte from 40 named bits, and an
item's variables can only be read at the console's offsets when no pointer comes before them
(it fills the four documented "misc" bytes for the items Slippi documents, and 0 elsewhere).

Fix: `tgg_match_to_console`'s pattern for these: `tgg_fighter_flags_to_console(fp, out[5])`,
generated from the DWARF types like the converter's, and the same for an item's variables.

### Game variables no header declares

`HSD_PadCopyStatus` (whether A is held as Zelda's match starts) and `gm_80479D58` (the scene's
frame counter) needed `TGG_GAME_SYMBOL`. The build's message said exactly what to write, which
made it quick.

### Making a folder

`tgg/files.h` lists folders but can't make one, so the mod has its own `mkdir -p` with a
`_WIN32` branch. Fix: `tgg_dir_make(path)`, making the folders above it too.

### Frames without rollback

`tgg_frame()` is -1 with rollback off, so a mod that keeps data by frame has to count frames
itself, and `tgg_frame_final` is `INT_MAX`. It works, but the two cases are two code paths.
Fix: number frames in every match, rollback or not.

## The tools around it

- **Optional dependencies.** The recorder needs one thing from the replay player (is it
  playing?), and `depends` would make the player required. `tgg_mod_export` fills the gap;
  a TGG_API import marked optional (NULL when the provider isn't loaded) would be the typed
  version of it.
- **The replay check ends one frame past the replay.** For an online game ended with
  L+R+A+Start, the port's run exits before the quit takes effect, so the recording has one
  frame more than the console's and no end. The port doesn't reproduce Slippi Online's
  L+R+A+Start without a pause either; no rule mod does it yet.
- **The replay player pauses at the end.** Booted from the environment, it pauses the match
  when the replay ends. Under `TGG_ROLLBACK_TEST` the last frames then never become final, so
  the check never finishes and the recording stops 7 frames short. A run that quits after the
  end would let both finish.
- **Three `.slp` readers.** `host/replay.c`, the replay player and netplay each read replays,
  and this mod writes them. A shared C library would serve all four (see the report).
