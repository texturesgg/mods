# Ledge dash drill

Practice > Ledge dash (1-P Mode, with `tgg.practice-menu`). Pick your fighter, and the stage
select starts on Final Destination (pick another if you like). It's Training Mode with nobody
else on the stage and a clean screen. Grab a ledge once and that's your reset point. Every try
(let go, jump, air dodge onto the stage) puts you back on the ledge a moment after it's over,
so a rep is just the ledge dash.

| Button | Does |
|---|---|
| D-pad left, while hanging | makes this the reset point (another ledge, another percent) |
| D-pad right | back to the reset point now |

A try is over when you're standing on the stage and can act, when you're KO'd, or after
2.5 seconds. With `tgg.ledgedash-trainer` installed, its breakdown ("Intangible: 4 frames
left") shows for every try.

Needs `tgg.practice-stage`. It's an offline mod, so the game switches it off for netplay.

## How it works

A display script, `scripts/view.luau`. It adds its entry to the Practice submenu
(`tgg.menu.add("practice/ledgedash", ...)`), whose action starts a match through
`tgg.practice-stage` (`tgg.mod("tgg.practice-stage")`, its `start`). In its own matches it
watches P1's action state each displayed frame: the first ledge catch keeps the reset point
(`reset_here`); leaving the ledge starts a try; and a beat after the try ends, `reset` puts the
match back.

## Build

```sh
tgg mod build    # packs build/tgg.ledgedash-drill-<version>.zip
tgg mod dev      # build, install and run the game
```
