# Changelog

## 1.0.0

- Made for mod API 1.0, so it keeps working on every tgg-melee 1.x.

## 1.1.0

- Each landing is counted and logged once, after no rollback can change it. Before, the log
  was written as a frame first ran, so online it could miss a landing or show one that
  didn't happen.

## 1.0.0

- A Luau script now, in place of the C library: one package for every platform and game
  layout, with nothing to compile. Plays as 0.1.1 did.

## 0.1.1

- Built again for the registry's display class: it only changes what you see, so it doesn't count for netplay. Nothing changes in the game.

## 0.1.0

- First release: after every aerial landing you could have L-canceled, "L-cancel", "2 frames early" or "3 frames late" above your percent, with your rate.
