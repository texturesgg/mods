# Changelog

## 0.6.0

- A replay from the port's recorder plays with the unlocks it was recorded with, instead of everything unlocked, so a match recorded on a partly unlocked save plays back the same. Other replays still play with everything unlocked.

## 0.5.1

- Scripts can ask whether a replay is playing: `tgg.mod("tgg.replay-player").playing()`.

## 0.5.0

- A replay on a stage from a mod plays only if this game has it under the same number. Otherwise it says the stage is missing or numbered differently. Needs tgg-melee API 0.7.

## 0.4.0

- A replay with fighters from mods plays only if this game has them under the same numbers. Otherwise it says which fighter is missing or numbered differently. Players on added costumes show them if this game has them, else the costume it's based on. Needs tgg-melee API 0.6.

## 0.3.1

- Take over: X pauses the replay and asks who plays each player from here (the recording, nobody, or one of your controllers). Then it's a normal match, and D-pad left puts you back to that moment to try again.
- D-pad left goes back about 2 seconds while watching, and D-pad right skips about 10 seconds ahead.
- A replay plays with everything unlocked, like netplay does. Your own save keeps what it had.
