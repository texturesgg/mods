# Practice

Adds Practice to 1-P Mode, after Training. It opens a menu with every practice drill you have
installed: install `tgg.ledgedash-drill` and Ledge dash shows up there, and so on. It has no
practice of its own.

Off in netplay: an offline mod, switched off by the game for a netplay session.

## How it works

One menu entry, `TGG_MENU_ADD("1p/practice", ...)`, whose `submenu` is `"practice"`. Each
drill adds its own entry to that submenu (`"practice/<name>"`), so the menu fills itself from
whatever drills are installed, and no drill depends on this mod.

## Build

```sh
tgg mod build    # packs build/tgg.practice-menu-<version>.zip
tgg mod dev      # build, install and run the game
```
