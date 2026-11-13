# Practice stage

Training Mode with nobody else on the stage and a clean screen. It's the base the practice
drills (`tgg.ledgedash-drill` and the rest) start their matches on, and it works on its own
too.

## For players

On its own, two settings on the Mods page (Y on the main menu) change plain Training Mode:

| Setting | Does |
|---|---|
| Training: nobody else | takes Training's CPU out, so it's just you on the stage |
| Training: clean screen | hides Training's readouts (speed, enemy, damage, combo panels); they come back with its pause menu |

Both are off by default. Training's own pause menu still works for speed, camera and the
rest, and its "No. of CPs" row says 0 while nobody else is on.

The practice drills turn both on for their own matches, whatever these settings say.

Off in netplay: an offline mod, switched off by the game for a netplay session.

## For mod makers

`include/practice_stage.h` is the API a drill uses. Look the functions up with
`tgg_mod_export("tgg.practice-stage", "practice_start@1")`, or list
`"tgg.practice-stage": "^0.1"` in `depends` and copy the header.

- `practice_start(&how)` from a menu entry's action: opens Training Mode's character select,
  and the match after it is your drill's (`how.drill` is your mod id; `keep_cpu` and
  `keep_screen` keep what you want of Training; `stage`, a content id like
  `"melee/final-destination"` (NULL for the default), is where the stage select starts, still free to move).
- `practice_drill()`: the drill this Training Mode was started for, or NULL.
- `practice_reset_here()` keeps this moment as the reset point ("Ready" shows), and
  `practice_reset(say)` goes back to it ("Reset" shows when `say` is nonzero, for a reset the
  player asked for; keep a drill's own resets after every try quiet).

Put your drill's entry in the Practice submenu with `TGG_MENU_ADD("practice/<name>", ...)`;
`tgg.practice-menu` opens it from 1-P Mode.

## How it works

`src/mod.c`, in one file.

- Nobody else: each frame of a Training match, the CPUs are taken out with the game's own
  removal (`fn_8016EF98`, what Training's "No. of CPs" row uses), so its menu can't bring one
  back, and an after hook on Training's HUD update (`fn_80188EE8`) shows its CPU count as 0.
- Clean screen: Training's readouts and its pause menu are one model
  (`gm_80473814.gobj`); it's hidden while the game isn't paused.
- Starting a drill does what the 1-P menu does for Training (`mn_80229860(GM_TRAINING)`). A
  drill's stage: once the stage select's icons have settled (a second in), a before hook on its
  cursor update (`fn_8025A310`) puts the cursor on that stage's icon, unless the player has
  already moved it.
- The reset point is a snapshot (`tgg/snapshot.h`).

## Build

```sh
tgg mod build    # packs build/tgg.practice-stage-<version>.zip
tgg mod dev      # build, install and run the game
```
