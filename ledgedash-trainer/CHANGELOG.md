# Changelog

## 1.0.0

- Made for mod API 1.0, so it keeps working on every tgg-melee 1.x.

## 1.1.0

- Each ledge dash is logged once, after no rollback can change it. Before, the log was
  written as a frame first ran, so online it could miss one or show one that didn't happen.

## 1.0.0

- A Luau script now, in place of the C library: one package for every platform and game
  layout, with nothing to compile. Plays as 0.1.2 did.

## 0.1.2

- Built again for the registry's display class: it only changes what you see, so it doesn't count for netplay. Nothing changes in the game.

## 0.1.1

- First release: after each ledge dash, how much intangibility you had left once you could act, and where the frames went (ledge, jump, air dodge).
