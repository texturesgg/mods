# Changelog

## 0.6.0

- Find a match: pairs you with someone with the same gameplay mods, nearby first. VS is 4 stocks, 8 minutes, items off, a random legal stage each game, drawn the way Slippi's unranked does (no repeats against the same person until every stage has come up). Play again or find someone else after each match.
- Modes from other mods that play online can be searched too.
- Shows how many are searching. If the connection fails, both players go back to searching.
- `TGG_DIRECT_CONNECT=search[:mode[:character[:costume]]]` starts a search from the command line.
- Needs tgg.netplay 0.10 and mod API 0.10.

## 0.5.0

- Works with tgg.netplay 0.9, the version current games play with. Needs tgg.netplay 0.9 and tgg.http.

## 0.4.0

- Online is on the main menu, in the game's own look: Make a room, Join a room, Host by address, Join by address.
- Rooms with a 4-letter code, typed on the game's keyboard. The room code leads every Online line.
- After connecting, everyone picks on the game's own character select, and the host picks the stage.
- Up to four players.
- Players' names (Name Tag, else textures.gg name) and ping on the character select panels.
- Falls back on a relay when two games can't reach each other directly, and says so.
- Needs tgg.netplay 0.6 and tgg.http.
