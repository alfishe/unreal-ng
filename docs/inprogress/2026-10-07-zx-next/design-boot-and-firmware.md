# ZX Spectrum Next: boot chain and firmware

**Date:** 2026-10-07 · part of [README.md](README.md) · roms in [roms.md](roms.md)

## 1. The layers on a real Next

| Layer | What it is | Who provides it | In our emulator |
|:--|:--|:--|:--|
| FPGA core | the hardware (bitstream) | flash, slot 1 (older boards: 32 slots of 512 KB; slot 0 anti-brick) | **the machine we write**; there is no bitstream to load |
| Anti-brick core | a fixed 48K Spectrum that checks the update button combination | flash slot 0 | not emulated; the boot ROM variant `v30xxxab` exists in MAME's ROM list (a different first 8K) |
| Boot ROM / IPL | 8K overlay at `#0000-#1FFF` while `bootrom_en` is set (set by a reset in config mode, cleared by **any write to NR `#03`**); SD init over SPI, reads `TBBLUE.FW` from the root | inside the core (MAME: `boot-30100.bin`, `boot-30204.bin`; jnext: `nextboot.rom`) | a ROM file of the firmware set |
| `TBBLUE.FW` | the firmware: boot module (config, menu, ROM load, keymap), updater, test screens; FatFs-based; Z80 code in blocks | SD card root | **runs on our CPU** (D1) |
| Personality ROMs | `enNextZX.rom` (64K: 128K editor, syntax checker, +3DOS, 48K), `enNxtmmc.rom` (8K DivMMC), `enNextMf.rom` (8K Multiface), plus `48.rom` / `128.rom` for other personalities | `/machines/next/` on the card, named by `menu.def` | loaded by the firmware's Z80 code into the system area |
| NextZXOS | the OS (+3DOS API, NextBASIC, file browser), `/nextzxos/*`, runs `AUTOEXEC.BAS` if present | SD card | runs on the machine |
| esxDOS | the DivMMC firmware; on the Next, inside NextZXOS's stack and in an esxDOS personality | card, `enNxtmmc.rom` + `/sys/ESXDOS.SYS`, `NMI.SYS`, `RTC.SYS`, `BETADISK.SYS` | runs on the machine |

(Stages and files: https://wiki.specnext.dev/Boot_Sequence, fetched 2026-10-07.)

## 2. What the firmware does before the OS (jnext's trace of `boot.c`)

From jnext's `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` (which cites tbblue `boot.c`, `config.c`, `misc.c`
line by line). It was re-read against the `tbblue` sources (`boot.c`, `loader/src/main.c`) on 2026-10-08 and
holds; the verified chain, the `TBBLUE.FW` index format and the per-mode port-enable values are in
[research-fpga-vhdl.md](research-fpga-vhdl.md) section 14:

1. NR `#07` = 3 (28 MHz) for the boot; NR `#06` = `#A0`; `reset_settings` holds the ESP bus reset (NR `#02` = `#80`); joystick defaults NR `#05`; keyjoy keymaps through NR `#28`-`#2B`.
2. NR `#03` = 0 disables the boot ROM (it keeps config mode).
3. Video timing NR `#11` = `#80 | timing`, scanlines NR `#09`; palette writes for the boot screen (NR `#40`-`#44`, `#4A`-`#4C`), Layer 2 screens from firmware block 8.
4. `config.ini`, then `menu.ini` or `menu.def` are read from the card; the "Press SPACEBAR for menu" screen.
5. The keymap (1K) loaded through NR `#28`-`#2B`.
6. `load_roms()`: the ROM bytes are written at `#0000` in config mode with NR `#04` (the 16K SRAM bank for the config mapping) stepping; for the default entry that fills 8K pages 0-7 (64K Next ROM), 8 (DivMMC ROM) and 10 (Multiface ROM).
7. `init_registers()`: NR `#05`-`#0A` (from the 28 settings), NR `#82`-`#85` per machine mode (mode 2/+3: `hwenables[0] = #DA`), DivMMC/MF enables.
8. NR `#03` = `0x80 | (mode+1) << 4 | (mode+1)` (+3 mode: `#B3`), a pause, then NR `#02` = `#01` (soft reset) or `#81`. The write of a nonzero machine type leaves config mode; the soft reset starts the personality ROM at `#0000`.
9. In the 128K / +3 ROM, esxDOS's DivMMC automap traps the ROM and loads `/sys/*.SYS`, then NextZXOS (`/nextzxos/*`).

Each step is a register or memory effect our machine must provide. The list doubles as the **checklist for N9**: the
first boot trace (port writes in order, with the TTD port trace feature) is compared with this list.

## 3. Design

| Item | Decision |
|:--|:--|
| Power-on | hard reset: config mode on, boot ROM overlay visible, CPU at `#0000`; the boot ROM runs from our memory interface's first decode step |
| Boot ROM file | `[ROM] NEXT=` names the 8K image (default from `data/rom/next/`, see [roms.md](roms.md)); the anti-brick image selectable (`NextBoard`/key) |
| SD card | `sd.next0` required; without a card the boot ROM shows its error as on hardware; a "bare" start with no card exists as a separate personality for tests ([phases.md](phases.md) N2) |
| Direct boot paths | (a) **bare personality**: `[NEXT] Personality=48|128|plus3|pentagon`, ROM files from the configuration (`[ROM]` keys, user-supplied or the tracked 48K/128K/+3 ROMs), no firmware, no card: for tests, snapshots, early phases; (b) **bypass**: jnext's contract (populate the system area from the card with a host-side FAT reader, apply the post-firmware NextREGs, synthesize the soft reset): **not built**, kept as an option once the full chain is measured for speed. The boot takes seconds of emulated time with turbo; a snapshot or TTD checkpoint after the boot gives instant start |
| Speed-up | the boot is CPU-bound at 28 MHz (the firmware sets 28 MHz itself) and SD-bound at SPI speed (instant in our model); `EnableTurboMode()` in tests |
| Config files | the firmware reads them from the card; we never emulate `config.ini`. The first boot writes `config.ini` (and the video test, `timing=8`): the card is `Session` access, changes in the change layer |
| Updates | `TBBLUE.TBU` flashing: out of scope; the flash blob answers reads so the updater screens show |
| Reset buttons | F1/F4 and the NR `#02` writes call `Reset(Hard/Soft)`; the DRIVE and M1 buttons are NMI requests ([design-cpu.md](design-cpu.md) section 4) |
| Firmware versions | the set is pinned in [roms.md](roms.md): boot ROM 3.02.04 first, 3.01.00 second; the matching `TBBLUE.FW` comes with the `sn-complete` distribution. The register map of a newer firmware than the core we model is detected by the trace (unknown NR writes are logged once) |

## 4. How a "Next card" gets built for tests

| Source | How |
|:--|:--|
| The official distribution `sn-complete-<version>.zip` (https://www.specnext.com/latestdistro/) | unpack to a folder, plug the folder in `sd.next0` (`HostFolderFat`: FAT16 by default, MBR entry 0, 8.3 names, FAT32 with at least 65526 clusters on request) |
| A 1 GB image (the CSpect image, the `sn-emulator` zip) | `RawImage`, session write layer |
| A minimal card for CI | a folder with `TBBLUE.FW`, `machines/next/` with `config.ini` (timing 0), `menu.def`, the three ROMs, and what `/nextzxos` needs for the first screen; jnext's analysis notes that an image without `config.ini` fails at "Error opening 'menu.ini/.def'!" |

**Nothing from the distribution is copied into the repository** (license: the firmware and NextZXOS are the
SpecNext team's). Tests that need the card skip with a message when it is not provisioned (the
`testdata/` provisioning pattern: [roms.md](roms.md) section 4).

## 5. Flash, RTC RAM, persistent blobs

| Blob | Content |
|:--|:--|
| FPGA flash | 16 MB, default a placeholder pattern; written through SPI flash commands from config mode; persisted by `[NEXT] FlashFile=` |
| RTC RAM | the DS1307's 56 bytes |
| Keymap | 1K loaded by the firmware each boot; state only |
| `config.ini` | on the card |

## 6. Verification

| Test | Expected |
|:--|:--|
| Boot trace | with `TBBLUE.FW` from a card: the NR writes of section 2 in order, ending with NR `#03` and NR `#02`; compared against a golden list generated from the real firmware in N9 |
| NextZXOS menu | the main menu text in video memory (Layer 2 or tilemap text) within N frames |
| esxDOS | `.ls` of a folder from a dot command through NextBASIC (`LOAD`/`CAT`) |
| Writes | the card changed layer shows `config.ini` written |
