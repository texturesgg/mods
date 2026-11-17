# Changelog

## 1.0.0

- A match with more than 4 players isn't recorded, since a replay holds 4. The log says so.
- Made for mod API 1.0. It uses parts of the API that still change, so each tgg-melee update comes with a new version of it.

## 0.5.0

- Every match starts the stage state the game carries between matches (Princess Peach's Castle's wind, Kongo Jungle's log bridge) as a fresh game has it, so the replay player plays it back the same.

## 0.4.0

- A replay says which unlocks the save had (`tgg.unlocks` in the metadata), since a match's setup reads them, so the replay player can play it back on the same.

## 0.3.0

- A match on a stage from a mod says which in the replay's metadata too. Needs tgg-melee API 0.7.

## 0.2.0

- A match with fighters or costumes from mods says which in the replay's metadata (`tgg.content`), so the replay player can check it has them. Needs tgg-melee API 0.6.

## 0.1.5

- Built again for the registry's display class: it only changes what you see, so it doesn't count for netplay. Nothing changes in the game.

## 0.1.4

- The source carries its license (GPL-3.0-or-later). Nothing changes in the game.
- Built for tgg-melee's new API.
