# AGENTS.md

Rules for agents working in this repository. The workspace guide in
`~/dev/AGENTS.md` applies too; this file overrides it where they differ.

Platform-wide facts (the other repositories, the merge and release policy for
every repo, mod platform rules, identity, copy, test machines) are in the
"Platform overview" section of `../core/AGENTS.md`. Read it first.

This repository is `texturesgg/mods` (public): the official mods for
`tgg-melee`, one folder per mod. Read `README.md` for the current split
between Luau scripts (18) and native C mods (12).

## How work lands

- Trunk-based: commit to `main` (or rebase from a short-lived worktree) and
  push once local checks pass. Watch CI go green after pushing.
- Release tags (`<mod-id>-<version>`) and publishing to the registry are
  owner only.
- A mod is published from its own subtree history: clone a
  `git subtree split -P <mod>` into a temporary repo and run `tgg mod publish`
  there.

## Luau vs C

- Luau scripts are the default (`manifest.json` with `"script": true`,
  `scripts/`, optional `assets/`). One package runs on every platform and
  game layout.
- Native C stays only for what scripts cannot do (sockets, files, replays,
  loading external files, patches to game functions). Before adding C code,
  check whether the missing capability belongs in `tgg-melee`'s curated layer
  (`../tgg-melee/docs/curated.md`) instead.
- Clear per-match script state in `match_setup` (a match's first
  `fighter_spawn` fires before `match_start`).

## Checking a change

Run inside the dev shell (`direnv exec .` or `nix develop`):

```sh
ci/check-depends.sh
ci/build-all.sh <sdk-dir>
```

- `ci/check-depends.sh` ensures every mod's `depends` range accepts the
  version of that mod in this repo so the official set loads together.
- `ci/build-all.sh <sdk-dir>` builds every C mod, checks and packs every
  script mod with `tgg-script`, and runs `luau-lsp` `--!strict` type checking
  against `luau/tgg.luau`. Output lands in `<mod>/build/mods/<id>/`.
- CI (`.github/workflows/ci.yml`) builds Linux x86-64, Windows x86-64
  (MinGW-w64), and macOS arm64 against `tgg-melee` `main`'s SDK at
  `dl.textures.gg/tgg-melee/main/`. When pushing a breaking API change to
  `tgg-melee`, wait for `tgg-melee` CI to upload the new SDK before pushing
  or rerunning `mods` CI.
- Before claiming a Windows build of a networking mod works, check its
  imports with MinGW's `nm` for unexpected `__imp_` symbols.
- After changing packet parsers in `netplay`, the replay reader in
  `replay-player`, or `http`, rerun the fuzz targets in `tools/fuzz/`.

## Public repository hygiene

- This repo is public and GPL-3.0-or-later. Never commit game files, secrets,
  private paths, or hostnames.
- Credit upstream or third-party work in `CREDITS.md` (for example Slippi,
  UCF, Mbed TLS).
- Follow `../tgg-melee-goals/copy-pass.md` for manifest descriptions, setting
  labels, in-game text, and READMEs.
