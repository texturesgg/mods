# tgg-melee mods

The official mods for [tgg-melee](https://textures.gg), one folder each. Players install them
from textures.gg or with `tgg`, and this repository is their source.

Most are Luau scripts: a folder with `manifest.json` (`"script": true`), `scripts/` and
`assets/`, and nothing to compile, so one package runs on every platform and game layout:

| Scripts | |
|---|---|
| Rules | `ucf`, `ucf-legacy`, `neutral-spawns`, `offscreen-damage`, `stadium-monitor`, `frozen-stadium`, `whispy-blow-dir-fix`, `skip-results`, `competitive-rules`, `legal-stages`, `unlocks`, `quick-start` |
| Practice | `input-display`, `lcancel-trainer`, `ledgedash-trainer`, `wavedash-trainer`, `lcancel-drill`, `ledgedash-drill`, `wavedash-drill`, `practice-menu`, `save-states` |

The rest are C, for what a script can't do: sockets, files, replays, loading files from outside
the mod, and patches to the game's own functions. Those are `netplay`, `direct-connect`,
`http`, `replay-recorder`, `replay-player`, `replay-browser`, `skins`, `practice-stage`,
`stadium-preload`, `freeze-dead-up-fall`, `prevent-wobbling` and `freeze-glitch-fix`.

Both kinds build against the tgg-melee mod SDK:

```sh
tgg mod build <folder> -o <mod>.zip
```

`ci/build-all.sh <SDK>` builds every C mod and checks every script mod (`tgg-script check` and
`pack` from the SDK's `tools/`, and a `--!strict` type check against the SDK's `luau/tgg.luau`
when `luau-lsp` is on the path, as it is in `nix develop`), each into `<folder>/build/mods/<id>/`.
CI runs it on every push against the SDK of tgg-melee's latest `main`, after
`ci/check-depends.sh`, which fails when a mod's `depends` range doesn't accept the version of
that mod here: the official set has to load together.

`tools/fuzz/` fuzzes the parsers that read what other people send: netplay's packets, replay
files and HTTP answers. See its README.

See `CREDITS.md` for the work these mods build on.
