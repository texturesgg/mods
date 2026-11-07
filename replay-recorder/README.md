# Replay recorder

Saves each VS match played in tgg-melee as a Slippi replay (`.slp`), so it opens in Slippi's
tools: Slippi Launcher's stats, slippi-js, peppi, and tgg-melee's own replay player.

Files go to `replays` in the port's data folder (`~/.local/share/tgg-melee/replays`), named
as Slippi names them, `Game_20261106T191023.slp`. The log says where:

```text
tgg.replay-recorder: recording /home/me/.local/share/tgg-melee/replays/Game_20261106T191023.slp
tgg.replay-recorder: saved /home/me/.local/share/tgg-melee/replays/Game_20261106T191023.slp, 7324 frames
```

## Settings

| Setting | Key | Default |
|---|---|---|
| Record matches | `record` | on |
| Replay folder | `folder` | empty: `replays` in the data folder |
| Record replays being watched | `playback` | off: a replay the replay player plays isn't recorded again |

For a run, `TGG_SET="tgg.replay-recorder:folder=/home/me/Slippi"`.

## What's in a file

The files say Slippi 3.18.0, the newest version whose fields the recorder fills. The events,
as [SPEC.md](https://github.com/project-slippi/slippi-wiki/blob/master/SPEC.md) lays them out:

- **Game Start**: the Game Info block (the match's `StartMeleeData` as the console holds it,
  from `tgg_match_to_console`), the RNG seed the match was set up with, each port's
  controller fix (UCF, when the port's built-in UCF is on), human players' name tags, frozen
  Stadium (when `tgg.frozen-stadium` is loaded), the scene (VS, in game) and the language.
  Zelda with A held at the start is recorded as Sheik, as Slippi does. Zero: PAL, and what
  only Slippi Online has (display names, connect codes, UIDs, the session and game number).
- **Frame Start**: the RNG seed and the scene's frame counter.
- **Pre-Frame** and **Post-Frame Updates** for every fighter, Nana included: every field to
  3.16's instance ids, the L-cancel status too.
- **Item Updates**, up to 15 a frame. The four "misc" bytes are filled for Samus's missiles,
  Peach's turnips, charge shots and shadow balls, where Slippi documents them, and 0 for other
  items, where the console sends whatever its memory holds.
- **Fountain of Dreams**, **Whispy** and **Stadium** events, where Slippi sends them.
- **Frame Bookend**: every frame in the file is final, so its latest finalized frame is
  itself.
- **Game End**: how the match ended, who quit with L+R+A+Start, and each port's placement.
  A match the game left any other way (the window closed) has none.
- **metadata**: `startAt`, `lastFrame`, each port's characters by frame count, `playedOn`
  `"tgg-melee"`, and `tgg`, which Slippi's tools ignore: the game build, the mod-set id and
  each mod that counts toward it, `"id version"`.

No gecko code list: the port runs mods, not codes. `tgg.mods` says which ran.

## How it works

```text
src/capture.c  the simulation's side: each frame's events, captured where Slippi captures them
src/slp.c      writing the file
src/mod.c      which matches, the settings, writing final frames, finishing the file
```

- The `match_setup` event decides whether to record (a VS match, not one the replay player
  plays) and keeps the match's setup and seed.
- Each frame is captured in simulation code at the points Slippi's own recording code reads
  the game: the `tick` event (Frame Start, or the match's end), `fighter_input` (Pre-Frame),
  after `Fighter_procCamera` (Post-Frame), after each stage's update, and before the first
  `Fighter_procDynamics` (Item Updates: Slippi reads them in an object process of its own,
  after the fighters' collision and before their dynamics).
  A frame the match is paused through is dropped, as a paused console sends nothing.
- The file is written from the `advance` event, before each logic frame, outside the
  simulation: every frame `tgg_frame_final` says is final, in order. Under rollback a frame
  is written once, from its last run. Without rollback every frame is final once it ran.
- The file is finished once the match has ended and every frame is final, when the match
  scene leaves, or when the game quits.

The replay player says whether it's playing through its API, which the recorder asks for
with `tgg_mod_export`, so it works with the player installed or not.

## Build

```sh
cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
cmake --build build
```
