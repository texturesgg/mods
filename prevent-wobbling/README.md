# Prevent wobbling

Stops wobbling, like Slippi online. In singles, an Ice Climber's grab breaks after the fourth
pummel that lands while the one held never got back to waiting in the grab.

## For players

- Ordinary pummels are untouched: the count starts over each time the one held goes back to
  waiting in the grab. Only hits that keep them reeling, Nana's during a wobble, add up.
- On the fourth, Popo lets go and so does Nana (on the ground, or jumping off in the air).
- Singles only, as on Slippi. Doubles and other modes play as the stock game.
- No settings.
- Matches Slippi's code (slippi-ssbm-asm `External/PreventWobbling`), in every online game
  from Slippi 3.14 on. Replays from 3.9 to 3.13 used an older version (it broke the grab on the
  third hit and counted Popo's only), which isn't here.
- Netplay: it changes a match, so it counts toward the mod-set. Both players need it.

## For mod makers

`src/mod.c`, in C because it changes how the game works out a grab, not a yes or no it already
asks:

- `TGG_AFTER` on the two functions that start the held fighter waiting (`fn_800DB790`,
  `fn_800DBAE4`) starts the count over.
- `TGG_BEFORE` / `TGG_AFTER` on `ftCo_8008EC90` and `TGG_BEFORE` on `ftCo_800C8D00` find the
  pummel: the held fighter is hit with no knockback while the grabber takes none.
- `TGG_AFTER` on `ftCo_800C8D00` counts it when it came from the grabber, or an item the
  grabber owns, and isn't the same attack as the last one counted. On the fourth it calls the
  game's own grab release for both Ice Climbers.
- `TGG_REPLACE` on `ftCo_800DC284` and `ftCo_800DC3A4` skips the held fighter's damage
  reaction to the hit that broke the grab.

The count and the last attack live where Slippi keeps them, in the held fighter's action
variables, so they roll back with the match.

What a Luau script would need to do this:

- a check on the hit's damage reaction for a held fighter (the game already decides whether
  the held fighter reacts), so the script can say no;
- reads of a hit's attack instance (the fighter's and an item's) and of an item's owner;
- a way to put a fighter into an action (CatchCut, CaptureJump) and set its velocity, which is
  the action-states work after launch.

## Build

```sh
tgg mod build    # packs build/tgg.prevent-wobbling-<version>.zip
tgg mod dev      # build, install and run the game
```

## Credits

Slippi's code is UnclePunch's, in slippi-ssbm-asm (GPL-3.0); see `CREDITS.md`.
