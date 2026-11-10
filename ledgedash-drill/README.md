# Ledge dash drill

Practice > Ledge dash (1-P Mode, with `tgg.practice-menu`). Pick your fighter and a stage, and
it's Training Mode with nobody else on the stage and a clean screen. Hang on a ledge once:
that's your reset point. Every try (let go, jump, air dodge onto the stage) puts you back on
the ledge a moment after it's over, so a rep is just the ledge dash.

| Button | Does |
|---|---|
| D-pad left, while hanging | makes this the reset point (another ledge, another percent) |
| D-pad right | back to the reset point now |

A try is over when you're standing on the stage and can act, when you're KO'd, or after
2.5 seconds. With `tgg.ledgedash-trainer` installed, its breakdown ("Intangible: 4 frames
left") shows for every try.

Needs `tgg.practice-stage`. Off in netplay: an offline mod, switched off by the game for a
netplay session.

## How it works

`src/mod.c`, in one file, all presentation code. It adds its entry to the Practice submenu
(`TGG_MENU_ADD("practice/ledgedash", ...)`), whose action is `practice_start`. In its own
matches (`practice_drill()`), it watches P1's motion state each displayed frame: the first
ledge catch keeps the reset point (`practice_reset_here`); leaving the ledge starts a try; and
a beat after the try ends, `practice_reset(0)` puts the match back.

## Build

```sh
tgg mod build    # packs build/tgg.ledgedash-drill-<version>.zip
tgg mod dev      # build, install and run the game
```
