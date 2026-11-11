# L-cancel drill

Practice > L-cancel (1-P Mode, with `tgg.practice-menu`). Pick your fighter, and the stage
select starts on Final Destination (pick another if you like). It's Training Mode with nobody
else on the stage and a clean screen, for SHFFLing on your own.

The timing comes from `tgg.lcancel-trainer`: with it installed, every aerial landing says
"L-cancel", "2 frames early" or "3 frames late", with your rate. Without it, the drill says to
install it.

Needs `tgg.practice-stage`. Off in netplay: an offline mod, switched off by the game for a
netplay session.

## How it works

`src/mod.c`, in one file. It adds its entry to the Practice submenu
(`TGG_MENU_ADD("practice/lcancel", ...)`), whose action is `practice_start` with Final
Destination as the stage. In its own matches (`practice_drill()`) it checks
`tgg_mod_loaded("tgg.lcancel-trainer")` and shows the hint for a few seconds when the trainer
isn't there.

## Build

```sh
tgg mod build    # packs build/tgg.lcancel-drill-<version>.zip
tgg mod dev      # build, install and run the game
```
