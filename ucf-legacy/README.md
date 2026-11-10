# UCF (Slippi 3.16 and earlier)

The Universal Controller Fix as Slippi ran it up to 3.16: the three codes of
slippi-ssbm-asm's `External/UCF 0.8/Logic/`, dashback, shield drop and tumble wiggle. Replays
from those versions play back with it.

The port has the newer UCF 0.84 (Slippi 3.17 and later) built in, on by default. Both replace
the same spot dodge checks, so turn the built-in one off to use this: `TGG_UCF=0`.

```text
manifest.json    the mod's id, name and version
src/mod.c        the code
CMakeLists.txt   builds src/ against the SDK, if you'd rather use CMake than tgg
.clangd          points clangd at the build's compile_commands.json
```

## Build

```sh
tgg mod build          # a release build, packed into build/
```
