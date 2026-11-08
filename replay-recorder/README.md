# Replay recorder

Saves every VS match you play in tgg-melee as a Slippi replay (`.slp`).

## For players

The files open in Slippi's tools (Slippi Launcher's stats, slippi-js, peppi) and in
tgg-melee's own replay player. They go to `replays` in the port's data folder
(`~/.local/share/tgg-melee/replays` on Linux), named as Slippi names them,
`Game_20261106T191023.slp`. The log says where:

```text
tgg.replay-recorder: recording /home/me/.local/share/tgg-melee/replays/Game_20261106T191023.slp
tgg.replay-recorder: saved /home/me/.local/share/tgg-melee/replays/Game_20261106T191023.slp, 7324 frames
```

### Settings

| Setting | Key | Default | Does |
|---|---|---|---|
| Record matches | `record` | on | record VS matches |
| Replay folder | `folder` | empty | where files go. Empty: `replays` in the data folder |
| Record replays being watched | `playback` | off | also record matches the replay player plays |

For one run: `TGG_SET="tgg.replay-recorder:folder=/home/me/Slippi"`.

Netplay: this is a code mod, so it counts toward the mod-set. Both players need it, or
neither. Its settings are your own and don't count.

### What's in a file

The files declare Slippi 3.18.0, the newest version whose fields the recorder fills. The
events, as [SPEC.md](https://github.com/project-slippi/slippi-wiki/blob/master/SPEC.md)
lays them out:

- **Game Start**: the Game Info block (the match's `StartMeleeData` as the console holds
  it), the RNG seed, each port's controller fix (UCF when the built-in UCF is on), human
  players' name tags, frozen Stadium (when `tgg.frozen-stadium` is loaded), the scene and
  the language. Zelda with A held at the start is recorded as Sheik, as Slippi does. PAL
  and the Slippi Online fields (display names, connect codes, UIDs, session and game
  number) are zero.
- **Frame Start**: the RNG seed and the scene's frame counter.
- **Pre-Frame** and **Post-Frame Updates** for every fighter, Nana included, with every
  field up to 3.16's instance ids, L-cancel status too.
- **Item Updates**, up to 15 a frame. The four "misc" bytes are filled for Samus's
  missiles, Peach's turnips, charge shots and shadow balls, where Slippi documents them,
  and 0 for other items.
- **Fountain of Dreams**, **Whispy** and **Stadium** events.
- **Frame Bookend**: every frame in the file is final.
- **Game End**: how the match ended, who quit with L+R+A+Start, and each port's
  placement. A match left any other way (the window closed) has none.
- **metadata**: `startAt`, `lastFrame`, each port's characters by frame count, `playedOn`
  `"tgg-melee"`, and `tgg` (ignored by Slippi's tools): the game build, the mod-set id and
  each mod that counts toward it, as `"id version"`, and `profile`: the exact mods and their
  settings, as JSON text.

There's no gecko code list, since the port runs mods, not codes. `tgg.profile` in the
metadata says which ran, with their settings, so a replay player can get the same mods to
play it back.

## For mod makers

An example of recording a match under rollback: capture in simulation code, write only
final frames from presentation code.

```text
src/capture.c  the simulation's side: each frame's events, captured where Slippi captures them
src/slp.c      writing the file
src/mod.c      which matches to record, the settings, writing final frames, finishing the file
```

- `match_setup` decides whether to record (a VS match the replay player isn't playing) and
  keeps the setup (`tgg_match_to_console`) and the seed.
- Each frame is captured at the points Slippi's recording code reads the game: the `tick`
  event (Frame Start, or the match's end), `fighter_input` (Pre-Frame), `TGG_AFTER`
  `Fighter_procCamera` (Post-Frame), after each stage's update, and before the first
  `Fighter_procDynamics` (Item Updates, which Slippi reads between the fighters' collision
  and dynamics). A frame the match is paused through is dropped, as a paused console sends
  nothing.
- The file is written from the `advance` event, outside the simulation: every frame
  `tgg_frame_final` says is final, in order. Under rollback each frame is written once,
  from its last run.
- The file is finished once the match has ended and every frame is final, when the match
  scene leaves, or when the game quits.
- It asks for the replay player's `replay_playing` with `tgg_mod_export`, so it works with
  the player installed or not.

## Build

```sh
tgg mod build    # packs build/tgg.replay-recorder-<version>.zip
tgg mod dev      # build, install and run the game
```
