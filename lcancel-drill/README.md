# L-cancel drill

Practice > L-cancel (1-P Mode, with `tgg.practice-menu`). Pick your fighter, and the stage
select starts on Final Destination (pick another if you like). It's Training Mode with nobody
else on the stage and a clean screen, for SHFFLing on your own.

The timing comes from `tgg.lcancel-trainer`: with it installed, every aerial landing says
"L-cancel", "2 frames early" or "3 frames late", with your rate. Without it, the drill says to
install it.

Needs `tgg.practice-stage`. It's an offline mod, so the game switches it off for netplay.

## How it works

A display script, `scripts/view.luau`. It adds its entry to the Practice submenu
(`tgg.menu.add("practice/lcancel", ...)`), whose action starts a match through
`tgg.practice-stage` (`tgg.mod("tgg.practice-stage")`, its `start`) with Final Destination as
the stage. In its own matches it checks for `tgg.lcancel-trainer` and shows the hint for a few
seconds when the trainer isn't there.

## Build

```sh
tgg mod build    # packs build/tgg.lcancel-drill-<version>.zip
tgg mod dev      # build, install and run the game
```
