# Wavedash drill

Practice > Wavedash (1-P Mode, with `tgg.practice-menu`). Pick your fighter, and the stage select starts on Final Destination (pick another stage like Battlefield if you want platforms for wavelands). It's Training Mode with nobody else on the stage and a clean screen.

| Button | Does |
|---|---|
| D-pad left | sets a new reset point right where you are |
| D-pad right | resets immediately to your reset point |

The timing, angle, slide distance, and recent-attempt strip come from `tgg.wavedash-trainer`. Without it installed, the drill notes to install it.

Needs `tgg.practice-stage`. It's an offline mod, so the game switches it off for netplay.

## How it works

A Luau script, `scripts/view.luau`. It adds `practice/wavedash` to the Practice submenu, starts Training Mode through `tgg.practice-stage` (`start`), keeps a clean center-stage snapshot once Training's "GO!" banner fades, and binds D-pad left/right to `reset_here` and `reset`.

## Build

```sh
tgg mod build    # packs build/tgg.wavedash-drill-<version>.zip
tgg mod dev      # build, install and run the game
```
