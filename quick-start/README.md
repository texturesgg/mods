# Quick start

The game skips the opening movie and the title screen and starts at the main menu, or at a
character select.

## For players

| Setting | Choices | Default |
|---|---|---|
| Start at (`start`) | Main menu, VS mode, Training mode | Main menu |

Change it on the Mods page (Y on the main menu). It takes effect the next time the game
starts. For one run: `TGG_SET="tgg.quick-start:start=VS mode"`.

Nothing else is skipped. A save loads as usual, and with no save the game makes one as if
you answered yes. The unlock checks the title screen runs on Start still run, and a waiting
challenger still shows up first. The menus are the game's own from there: B on the
character select goes back to the main menu. A reset goes back to the same place.

Works like the Slippi and UnclePunch setups. Netplay: it only
changes where the game starts, so it doesn't count toward the mod-set.

## For mod makers

A display script, `scripts/view.luau`. One call does the work: `tgg.control.boot_mode` in
`tgg.on.init` (where the mod's settings have been read), with the mode picked by the `start`
setting the manifest declares. The game goes to that mode the way Start on the title takes it
to the main menu.

## Build

```sh
tgg mod build    # packs build/tgg.quick-start-<version>.zip
tgg mod dev      # build, install and run the game
```
