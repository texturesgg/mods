# Credits

These mods stand on other people's work.

## Slippi

These mods are GPL-3.0-or-later (`LICENSE`). The rule mods reproduce code from [project-slippi/slippi-ssbm-asm](https://github.com/project-slippi/slippi-ssbm-asm)
(GPL-3.0): its PowerPC logic and constants, ported to C at the same points in the game. Each
mod names the code it follows.

- `ucf`, `ucf-legacy` (and `ucf` 0.8.0, tag `tgg.ucf-0.8.0`): `External/UCF 0.84/` and
  `External/UCF 0.8/`
- `neutral-spawns`: `External/NeutralSpawn`
- `frozen-stadium`: `Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm`
- `offscreen-damage`: `Online/Core/BrawlOffscreenDamage.asm`
- `freeze-dead-up-fall`: `Online/Core/FreezeDeadUpFallPhysics`
- `whispy-blow-dir-fix`: `Online/Core/WhispyBlowDirFix`
- `stadium-monitor`: `Common/PSCameraIndependentMonitor`
- `stadium-preload`: `Common/Preload Stadium Transformations`

`netplay`, `replay-player` and `replay-recorder` read and write Slippi's replay format
(`.slp`), as its [specification](https://github.com/project-slippi/slippi-wiki/blob/master/SPEC.md)
describes it.

## UCF

The Universal Controller Fix is the work of Dan Salvato, tauKhan and the UCF team. The `ucf`
mods follow the versions Slippi ships, through slippi-ssbm-asm.

## Mbed TLS

`http` includes [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) in `http/src/mbedtls` and
`http/include`, under Apache-2.0 (or GPL-2.0-or-later) (`http/src/mbedtls/LICENSE`).
