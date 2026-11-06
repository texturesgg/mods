# Frozen Stadium

A starting point for a tgg-melee mod. It logs a line when the game starts and one each
time a fighter appears.

```text
manifest.json    the mod's id, name and version
src/mod.c        the code
CMakeLists.txt   builds src/ against the SDK, if you'd rather use CMake than tgg
.clangd          points clangd at the build's compile_commands.json
```

## Make it yours

Give the mod an id of its own in `manifest.json`: 1 to 64 characters of `a-z`, `0-9`,
`.`, `-` and `_`, like `yourname.my-mod`. The id names the installed folder and is how the
log and other mods refer to it.

## Build and run

```sh
tgg mod dev            # build, put it in the game's mods folder, run the game
tgg mod dev --watch    # same, and rebuild and restart each time you save
tgg mod build          # a release build, packed into build/
```

The log shows `tgg.frozen-stadium: hello` at startup and `tgg.frozen-stadium: P1 spawned` when a match starts.
Add `.c` files under `src/` as the mod grows: every `src/**/*.c` is built in.

## Where to look next

- The docs: https://textures.gg/docs/mods/latest/README. Start with [writing mods](https://textures.gg/docs/mods/latest/writing-mods); `tgg docs` opens them, and
  the SDK has a copy in `$(tgg sdk path)/docs`.
- The game's own source is in `$(tgg sdk path)/game`, to read and search for what to
  change.
- Editors that use clangd (VS Code with the clangd extension, Zed, Neovim) get completion
  and go-to-definition across your mod and the game after the first build.
