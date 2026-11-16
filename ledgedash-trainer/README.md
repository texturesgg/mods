# Ledge dash trainer

After each ledge dash (let go of the ledge, jump, air dodge onto the stage), a breakdown shows
above your percent:

```text
Intangible: 4 frames left
Ledge 8   Jump 1   Air dodge 6
```

The top line is the one that matters: how much of the ledge's intangibility was left on the
first frame you could act after the landing. Above zero, nobody could hit you then. When you
ran out first it says "Too slow by 3 frames" instead. The second line is where the frames
went: on the ledge (from the grab to letting go), from letting go to the jump, and from the
jump to the air dodge. An air dodge that doesn't land on the stage says "Air dodge missed the
stage".

It uses the game's own intangibility timer, so the numbers are exact. Getting up, rolling,
attacking or jumping from the ledge don't count. Each result also goes to the log.

With `tgg.save-states`, save while hanging on the ledge (D-pad left) and load after each try
(D-pad right).

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Show in | Training Mode (default), or every match, replays and netplay included |

It's yours alone: it doesn't change the game, so it doesn't matter for netplay.

## How it works

Three scripts: `sim.luau` decides, `view.luau` draws, and `ledges.luau` is what both share.
`fighter_update` follows each fighter's action state: hanging (`CLIFF_CATCH`, `CLIFF_WAIT`),
off the ledge, a jump (`fighter.jumps_used` goes up), `ESCAPE_AIR`, then
`LANDING_FALL_SPECIAL`, the waveland's landing. On the first frame after the landing it reads
the intangibility the ledge grab set (`fighter.intangible`), or, if it already ran out, how
many frames ago. The attempt is in `state`, so it's right through a rollback. Each result is
posted (`tgg.post`), and `view.luau` shows it in `tgg.on.posted`, once, after no rollback can
change it.

It only reads, so it's a display script.

## Build

```sh
tgg mod build    # packs build/tgg.ledgedash-trainer-<version>.zip
tgg mod dev      # build, install and run the game
```
