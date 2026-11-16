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

Three scripts: `sim.luau` decides, `view.luau` draws, and `landings.luau` is what both share.

- The `fighter_land` event comes when an aerial's landing lag is picked, with the frames since
  L, R or Z was pressed (`press_age`) and the game's window (`window`). Inside it: an L-cancel.
  Up to 20 frames before it: early, by that many frames.
- Otherwise `fighter_update` watches the landing lag for a press
  (`fighter.cancel_press_age`): late, by the frames since the landing. Leaving the landing
  with no press: missed.
- Each result is posted (`tgg.post`), and `view.luau` counts it in `tgg.on.posted`, once,
  after no rollback can change it. `hud_draw` draws it above the player's percent
  (`player.hud_x` / `player.hud_y`).

It only reads, so it's a display script.

## Build

```sh
tgg mod build    # packs build/tgg.lcancel-trainer-<version>.zip
tgg mod dev      # build, install and run the game
```
