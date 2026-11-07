# Competitive rules

VS mode starts with the competitive standard instead of the game's 2-minute time match
with items:

| Rule | Value | Game's default |
|---|---|---|
| Mode | Stock | Time |
| Stocks | 4 | 3 |
| Stock match time limit | 8:00 | none |
| Items | None | Medium |
| Handicap | Off | Off |
| Damage ratio | 1.0 | 1.0 |
| Friendly fire (team attack) | On | Off |

Friendly fire is on because doubles is played with team attack on. Everything else on the
rules screens keeps the game's default: pause stays on (it's the player's call), stage
selection On, score display off, self-destructs -1.

## Your changes stick

Change any rule in the game's menus and it stays that way the next time you start the game.
The game itself doesn't save VS rules: it resets them every start, and only the item switch
is in the save. So the mod keeps every value of the rules screens in its settings, and puts
them back after the game resets its rules and after it loads the save.

The same values are on the Mods page. A change there goes to the game, and a change in the
game's rules menus goes to the Mods page. Only the rules menus count: Custom Rules (also
opened from the rules banner at the top of the character select), Additional Rules and the
item switch. If anything else changes the rules outside a match, the mod puts your rules
back. Tournament mode turns automatic handicap off by itself, so its changes stay until you
leave it.

Stage selection and self-destructs are saved as the text the game shows ("On", "-1"), so
the mod needs a port that reads a choice setting's own text before reading it as a
number or a bool.

Not covered: the individual items on the item switch (only the frequency), and the random
stage list (see `tgg.legal-stages`).

## Build

```sh
cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
cmake --build build
```

The mod lands in `build/mods/tgg.competitive-rules/`.
