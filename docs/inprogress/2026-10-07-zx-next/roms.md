# ZX Spectrum Next: firmware and ROM inventory

**Date:** 2026-10-07 · part of [README.md](README.md)

Facts below come from MAME's ROM list ([`specnext.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/next/specnext.cpp))
and the boot-sequence page and jnext notes already cited. **No ROM or firmware file is copied into the repository
by this work**; licensing of each must be settled by the owner before anything is added to `data/rom/next/`.

## 1. Boot ROMs (8K, inside the core)

| Image | MAME bios name | CRC32 | Note |
|:--|:--|:--|:--|
| `boot-30100.bin` | `v30100` Next core 3.01.00 | `ccbd55ba` | mirrored into both 8K halves |
| `boot-30200-ab.bin` | `v30200ab` anti-brick 3.02.00 | `1d16e9d4` | first 8K; second 8K = 30100 |
| `boot-30204.bin` | `v30204` Next core 3.02.04 (default) | `95118eb6` | |
| `boot-30204-ab.bin` | `v30204ab` anti-brick 3.02.04 | `96c32007` | second 8K = `boot-30204.bin` |

## 2. Files on the card (loaded by the firmware, not by the emulator)

`TBBLUE.FW`; `/machines/next/config.ini`, `menu.ini`, `menu.def`, `keymap.bin`; `enNextZX.rom` (64K), `enNxtmmc.rom`
(8K), `enNextMf.rom` (8K), `48.rom`, `128.rom`; `/nextzxos/*`; `/sys/ESXDOS.SYS` etc. Source: the distribution
https://www.specnext.com/latestdistro/.

## 3. Policy

| Item | Rule |
|:--|:--|
| `[ROM] NEXT=` | the boot ROM path (default `rom/next/boot-30204.bin` once provisioned); roles named in `rom.cpp` `NEXT_ROLES` |
| Bare personality | uses the tracked Spectrum ROMs (`data/rom/`) via existing `[ROM]` keys; no firmware needed |
| Provisioning | tests locate the card/firmware through an environment variable or `testdata/machines/next/` (untracked); a new variable gets a row in `docs/emulator/environment-variables.md` |
| Checksums | recorded in `data/rom/README-ROMS.md` when a file is added, with source URL |

## 4. Open

Licence and redistribution of the boot ROMs (they are part of the FPGA core, GPL in the FPGA repository: to confirm
in N0), which firmware versions to pin (Q8), and where the card for CI comes from.
