# L-cancel trainer

After every aerial landing you could have L-canceled, a line above your percent says how it
went: "L-cancel", or "2 frames early" / "3 frames late" when you missed, and "Missed" when you
didn't press at all. Under it is your rate for the match ("14/17 82%"). It fades after a
couple of seconds.

It uses the game's own check, so it's never off by a frame: you have to press L, R or Z in
the 7 frames before you land. Aerials that auto-cancel don't count, since there's nothing to
L-cancel.

Each result also goes to the log ("tgg.lcancel-trainer: P1: 2 frames early") wherever it shows.

## Settings

On the Mods page (Y on the main menu):

| Setting | Does |
|---|---|
| Show in | Training Mode (default), or every match, replays and netplay included |
| Show the rate | the hits and percent under each result |

Both are yours alone: they don't change the game, so they don't matter for netplay.

## How it works

`src/mod.c`, in one file:

- A before hook on `ftCo_LandingAir_EnterWithLag`, where the game picks an aerial's landing
  lag, reads the frames since L, R or Z was pressed (`fp->x67F`) and the game's window
  (`p_ftCommonData->xE4`). Inside it: an L-cancel. Up to 20 frames before it: early, by that
  many frames.
- Otherwise `fighter_update` watches the landing lag for a press: late, by the frames since
  the landing. Leaving the landing with no press: missed.
- Each result is kept in `TGG_STATE`, so it comes out right through a rollback. The `advance`
  event counts a result once its frame is final (`tgg_frame_final`), and `hud_draw` draws it
  above the player's percent, where the game's HUD puts it.

It only reads: before hooks and `fighter_update`, no replaced function and no `pad_read`.

## Build

```sh
tgg mod build    # packs build/tgg.lcancel-trainer-<version>.zip
tgg mod dev      # build, install and run the game
```
