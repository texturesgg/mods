# Quick start

The game skips the opening movie and the title screen and starts straight at the main menu,
as the Slippi and UnclePunch setups do. A setting can start it at VS mode's or Training
mode's character select instead.

| Setting | Choices | Default |
|---|---|---|
| Start at (`start`) | Main menu, VS mode, Training mode | Main menu |

Change it on the Mods page (Y on the main menu). It takes effect the next time the game
starts. For one run, `TGG_SET="tgg.quick-start:start=VS mode"`.

Nothing else is skipped. The memory card is checked as usual: a save loads, and with no save
the game makes a new one as if you answered yes. The unlock checks the title screen does
when you press Start still run, and a waiting challenger still shows up first. From there the
menus are the game's own: B on the character select goes back to the main menu. A reset
goes back to the same place.

The mod needs a port with `tgg_boot_mode` (`tgg/control.h`).

## Build

```sh
cmake -S . -B build -G Ninja -DTGG_SDK=$(tgg sdk path)
cmake --build build
```

The mod lands in `build/mods/tgg.quick-start/`.
