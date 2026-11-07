# Legal stages

Random stage select picks only the legal stages: Battlefield, Final Destination, Dream
Land, Yoshi's Story, Fountain of Dreams and Pokémon Stadium.

It uses the game's own list, the Random Stage Switch (VS. Mode, Custom Rules, Additional
Rules, Random Stage). The list is in the save, and every random pick asks it: the random
button on the stage select and the Random stage selection rule.

A new save, or a save whose list was never changed, has every stage on. On those the mod
sets the list to the legal stages, after the game loads or makes the save. A list you
changed is yours and stays as it is.

The list only counts once the Random Stage Switch is unlocked. The port's built-in
`tgg.unlocks` does that. With `TGG_UNLOCK_ALL=0` and the switch still locked, the game
picks from every stage, as it always does before the unlock.

## Build

```sh
cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
cmake --build build
```

The mod lands in `build/mods/tgg.legal-stages/`.
