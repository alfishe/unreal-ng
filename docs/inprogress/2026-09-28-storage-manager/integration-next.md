# Integration: ZX Spectrum Next SD cards (`sd.next0`, `sd.next1`) — a later machine

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed; applies when a ZX Next machine is built (no PLAN row yet) |
| **Why here now** | the Next is the most demanding SD user: the card holds the whole system. Checking it now proves that the media manager and `HostFolderFat` need no Next-specific changes |

Sources are marked; **[unverified]** marks what no source confirmed.

## 1. Storage on the Next

| Device | Facts | Source |
|---|---|---|
| SPI chip select | port `#E7`, active low: bit 0 SD card 0, bit 1 SD card 1, bits 2-3 Raspberry Pi SPI, bit 7 FPGA flash (config mode only); only one bit may be 0 | `ZXSpectrumNextTests/ports.txt:261-273` |
| SPI data | port `#EB` | same |
| Socket swap | NR `#0A` bit 5 swaps SD0 and SD1; hard reset clears it; writable only in config mode; added in core 3.02.03 (the wiki page still calls the bit "reserved") | gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA `nextreg.txt:170-186`, `changelog.md:18` |
| Floppy | none; NextZXOS emulates +3 DSK images from the SD card | jnext `emulator.cpp` (`#2FFD/#3FFD` traps, NR D8-DA) |
| FPGA flash | 16 MB, 32 slots of 512 KB (slot 0 anti-brick, slot 1 Next core); the updater writes `TBBLUE.TBU` into slot 1 | ZX_Spectrum_Next_FPGA `cores/zxnext/README.md:5-38` |
| RTC | DS1307 on I²C (ports `#103B` / `#113B`, address `#68`), optional on the board; `RTC.SYS` keeps a signature in its RAM | MAME `specnext.cpp:4179`; tbblue `docs/extra-hw/rtc/RTCTIMEDATEreadme.txt:13-27` |

## 2. What the card must be

The Next boots through three programs, each with its own FAT code:

| Stage | FAT support | Source |
|---|---|---|
| 1. The loader in the FPGA core (loads `TBBLUE.FW`) | FAT16 or FAT32, chosen by the file-system name in the boot sector (offset `#36` / `#52`), no cluster count; a card with no partition table works; otherwise **only MBR entry 0**, type byte ignored, `#55AA` required; reads 8.3 names `TBBLUE  FW`, `TBBLUE  TBU` | tbblue `src/firmware/loader/src/fat.c:138-169`, `hardware.h:53-56` |
| 2. `TBBLUE.FW` (the firmware file) | ChaN FatFs R0.12b: exFAT off, **long names off**, 512-byte sectors; scans the 4 primary MBR entries and uses the first FAT one; the type comes from the cluster count (≤ 4 085 FAT12, ≤ 65 525 FAT16, else FAT32), so a FAT32-shaped volume with too few clusters is rejected | tbblue `src/firmware/app/inc/ffconf.h:101,173,212`, `app/src/ff/ff.c:3043-3066, 3144-3159` |
| 3. NextZXOS | "any FAT16 and FAT32 partitions on the SD card or cards", all mapped to drive letters from C: | tbblue `docs/nextzxos/NextBASIC_File_Commands.pdf` |

The official FAQ lists FAT32 with long names, FAT16 and FAT12, and no exFAT
(https://wiki.specnext.dev/FAQ). The Quick Start guide says "FAT16 or 32".

Files the boot reads:
- `/TBBLUE.FW`;
- `/machines/next/config.ini`, `menu.ini`, `menu.def`, `timing.ini`, and the ROMs `menu.def` names
  (`enNextZX.rom`, `enNxtmmc.rom`, `enNextMf.rom`);
- then `/nextzxos/*`, which runs `AUTOEXEC.BAS` if present
  (tbblue `hardware.h:57-61`, `machines/next/menu.def`; https://wiki.specnext.dev/Boot_Sequence).

The distribution is `sn-complete-<version>.zip` (and `sn-emulator-<version>.zip` with a 1 GB image)
from https://www.specnext.com/latestdistro/.

**The first boot writes to the card.** The distribution ships no `config.ini`; the default
`timing=8` forces the video test, which writes `config.ini` (tbblue `app/src/config.c:110, 309`,
`videomagic.c:88-95`). So the card must accept writes. `Session` access covers this.

## 3. How it maps onto the media manager

| Need | Covered by | Next-specific? |
|---|---|---|
| two card sockets | slots `sd.next0`, `sd.next1`; `#E7` bits 0 / 1 and the NR `#0A` swap decide which slot an exchange reaches | only the decode |
| one card must not answer both selects (jnext: NextZXOS then mounts a phantom card in a loop, `jnext/src/core/emulator.cpp:6102-6113`) | "one source in one slot" (technical design §3) | no |
| the machine cannot start without a card | `required` on `sd.next0` (technical design §2) | the flag only |
| FAT16 or FAT32, MBR entry 0, 8.3 names | `HostFolderFat`: one partition in entry 0, 8.3 names always generated, FAT16 by default, FAT32 on request with ≥ 65 526 clusters | no |
| multi-GB images (the emulator distribution is 1 GB) | `RawImage` (64-bit offsets, never loaded whole), `SdCardSpi` SDHC above 2 GB | no |
| writes at first boot | `Session` access, change layer, export | no |
| RTC RAM, FPGA flash | not slots: persistent blobs (technical design §2) | — |

**A folder as the Next's card** is new: no emulator does it. CSpect's and ZEsarUX's folder modes
only emulate the esxDOS file API for programs run without NextZXOS
(https://specnext.dev/tutorials/cspect-mac/; ZEsarUX `src/FAQ:648-665`). With `HostFolderFat`, an
unpacked `sn-complete` folder can boot the real firmware, with every write going into the change
layer.

## 4. How other emulators mount the card

| Emulator | Mount | Source |
|---|---|---|
| CSpect | `-mmc=<file.img>`; a folder only for the esxDOS API | https://specnext.dev/tutorials/cspect-mac/ |
| ZEsarUX | `--mmc-file`, persistent-write and write-protect options; a separate "esxDOS handler" folder mode | ZEsarUX `src/FAQ:648-665`, `src/Changelog:249, 1976-1979` |
| MAME | two `spi_sdcard` devices; CHD or raw `hd` images; SDHC up to ~32 GB; software list `specnext_sd` | `mame/src/mame/sinclair/next/specnext.cpp:1231-1243, 4220-4226`; `devices/machine/spi_sdcard.cpp:249`; `hash/specnext_sd.xml` |
| jnext | one card in socket 0; raw MBR + FAT32 images, re-clustered to ≥ 65 526 clusters | `jnext/src/core/emulator.cpp:6102-6113`, `fat32_image.h:8-17` |

## 5. Unverified

- Whether NextZXOS's own FAT driver (not open source) enforces the cluster rule. CSpect boots the
  under-clustered 1 GB image, so probably not (`jnext/tools/fix-sdcard-image.sh:12`).
- Which socket is internal and which external (jnext says SD0 internal).
- How ZEsarUX's second MMC card maps onto the Next's SD1.
- esxDOS (in non-Next personalities) always selecting SD0: an inference from the divMMC convention.
