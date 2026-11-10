# Skins

Loads the skins you picked in tgg-studio (or with `tgg skin`) into tgg-melee.

## For players

- Pick skins in tgg-studio. This mod loads them the next time the game starts.
- Costumes and menu files load now. Stages, effects and fighter files don't yet.
- Online, a costume that only changes how the fighter looks shows for you as usual. A costume
  that changes more than that (its skeleton) shows the normal costume in a netplay match,
  on your screen only. The game decides that, not this mod, so a skin never changes who
  you can play.
- No settings.

## For mod makers

The skins are listed in `skins.json` in the game's data folder (`tgg_data_dir()`), which
tgg-studio and `tgg skin` write:

```json
{
  "format": 1,
  "library": "/home/me/.local/share/textures.gg/library/skins",
  "slots": {
    "PlFxOr.dat": { "sha256": "9b88…", "name": "Dark Fox" }
  }
}
```

Each skin is `<library>/<sha256>.dat`. `src/mod.c`:

- In `TGG_INIT` it reads the file and adds each costume with `tgg_files_add(file,
  "PlFxOr.dat", …)`, which converts it and checks whether it only changes looks. A menu or
  trophy file replaces the disc's with `tgg_files_replace`.
- On the `costume_file` event it names the added file for that costume. While rollback is
  on (netplay) the game only takes a file that changes nothing but looks.
