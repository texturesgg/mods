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

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Show in | Training Mode (default), or every match, replays and netplay included |

It's yours alone: it doesn't change the game, so it doesn't matter for netplay.

## How it works

`src/mod.c`, in one file. `fighter_update` follows each fighter's motion state: hanging
(`CliffCatch`, `CliffWait`), off the ledge, a jump (the fighter's jumps used goes up),
`EscapeAir`, then `LandingFallSpecial`, the waveland's landing. On the first frame after the
landing it reads the intangibility timer the ledge grab set (`fp->x1990`), or, if it already
ran out, how many frames ago. The attempt and each result are in `TGG_STATE`, so they're right
through a rollback, and a result shows once its frame is final (`tgg_frame_final`).

It only reads: `fighter_update`, no hooks, no `pad_read`.

## Build

```sh
tgg mod build    # packs build/tgg.ledgedash-trainer-<version>.zip
tgg mod dev      # build, install and run the game
```
