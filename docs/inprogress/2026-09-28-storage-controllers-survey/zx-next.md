# ZX Spectrum Next: DivMMC-compatible SD, two sockets, NextZXOS and esxDOS

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Machine** | ZX Spectrum Next (no model, no PLAN row; a later machine program) |
| **Slot design** | [integration-next.md](../2026-09-28-storage-manager/integration-next.md) (card contents, boot chain, FAT rules, how other emulators mount the card) - not repeated here |
| **Effort (storage part)** | **S-M** once the Next machine exists and the DivMMC framework of [divide-divmmc-esxdos.md](divide-divmmc-esxdos.md) is built. The machine itself (Z80N, MMU, Layer 2, copper, sprites, DMA, 28 MHz) is **XL** and out of this survey |

## 1. Hardware

Sources: MAME `src/mame/sinclair/next/specnext.cpp`, `specnext_divmmc.{h,cpp}` (VHDL-faithful);
jnext `src/peripheral/divmmc.{h,cpp}`, `spi.cpp`, `sd_card.cpp`; `ZXSpectrumNextTests/ports.txt`,
`nextreg.txt` (paths in the local reference tree).

| Item | Next behavior | Differs from classic DivMMC? | Source |
|---|---|---|---|
| `#E3` | readable; bank = bits 3..0 (16 x 8 KB = 128 KB), bits 5:4 read 0; a write keeps bit 6: `(E3 & #40) \| data` | **yes**: readable, 4 bank bits | MAME `specnext.cpp:3314-3315, 1221-1225`; jnext `divmmc.cpp:117-142` |
| MAPRAM clear | NR `#09` bit 3; a reset writes `#E3` = 0 | **yes** (DivIDE keeps MAPRAM over reset) | MAME `specnext.cpp:2139-2140, 3898`; jnext `divmmc.cpp:38-50, 146-150` |
| Paging | `#0000-#1FFF` ROM, or bank 3 with MAPRAM, always read-only; `#2000-#3FFF` the bank, read-only if MAPRAM and bank 3 | same as DivIDE | MAME `specnext_divmmc.cpp:36-44` |
| `#E7` | write-only; `...10` = SD0, `...01` = SD1 (swapped by NR `#0A` bit 5), `#FB` / `#F7` Raspberry Pi SPI, `#7F` FPGA flash (config mode only), anything else = none | two sockets, more devices | MAME `specnext.cpp:1228-1244`; jnext `spi.cpp:120-152`; `ports.txt:259-273` |
| `#EB` | write: full-duplex exchange. Read: the byte of the **previous** exchange, and a new exchange sending `#FF` | the Z-Controller `#57` rule | MAME `specnext.cpp:1246-1278`; jnext `spi.cpp:176-215` |
| SPI speed | instant in both emulators (MAME has an optional 1/4-CPU-clock mode); no speed register found | - | MAME `specnext.cpp:1255-1270` |
| Port gating | all DivMMC ports gated by NR `#83` bits | yes | MAME `specnext.cpp:326, 329`; jnext `emulator.cpp:5627-5680` |
| Automap enable | NR `#0A` bit 4; NR `#06` bit 4 lets the DRIVE button raise a DivMMC NMI (the older `nextreg.txt:113` describes bit 4 of NR `#06` as both) | yes | MAME `specnext.cpp:2111, 2150`; jnext `divmmc.cpp:179-205` |
| Entry points | programmable through NR `#B8-#BB`. Defaults `B8=#83 B9=#01 BA=#00 BB=#CD`: RST 0 always; RST 8 and `#38` only with ROM 3 paged; `#04C6`, `#0562` ROM-3-only; `#3Dxx` ROM-3-only and instant; `#1FF8-#1FFF` off-area on; `#0066` only while the DRIVE-button NMI is latched | **yes**: programmable, "ROM 3 only" | MAME `specnext.cpp:2961-3060, 4052-4055, 811`; jnext `divmmc.cpp:329-352` |
| RETN | clears the automap hold and the button-NMI latches | yes | MAME `specnext_divmmc.cpp:68-93`; jnext `divmmc.h:126-166` |
| Other storage | none: no FDC (NextZXOS emulates +3 DSK images from the card), no IDE | - | [integration-next.md](../2026-09-28-storage-manager/integration-next.md) §1 |

NextZXOS normally keeps automap **off** and drives the card through its own `enNxtmmc.rom` code
(`ports.txt:367-378`); the automap matters for the esxDOS personality and for 48K-mode programs
that call esxDOS through `RST 8`.

**Worked example: selecting the second card.** `OUT (#E7),#FD` (bit 1 low) selects SD1; `OUT
(#EB),#40` then sends the first byte of CMD0 to it. With NR `#0A` bit 5 set, the same `#FD` selects
SD0 instead (socket swap, used when the system card sits in the other slot).

## 2. Boot chain (short)

FPGA `nextboot` loader → `TBBLUE.FW` from SD0 (FAT16/FAT32, MBR entry 0) → `config.ini`,
`menu.ini`, ROMs from `/machines/next/` (`enNextZX.rom`, `enNxtmmc.rom` = the 8 KB DivMMC ROM,
`enNextMf.rom`) → NR `#02` soft reset → NextZXOS. The first boot **writes** `config.ini`, so the card
must accept writes (`Session` access). Details, FAT rules and the jnext phantom-card bug:
[integration-next.md](../2026-09-28-storage-manager/integration-next.md) §2-§4.

## 3. unreal-ng now vs gap

| Piece | Reusable | New |
|---|---|---|
| SD cards | `SdCardSpi` x 2 (SDHC above 2 GB, 64-bit `RawImage`), slots `sd.next0` / `sd.next1` (names reserved), `required` flag on `sd.next0`, "one image in two slots" refused, `HostFolderFat` with the FAT32 cluster minimum | the slots' registration in the Next decoder |
| SPI port | `ZControllerSpi` semantics (read = previous byte + new exchange) | `SpiPort` with an 8-bit select decode (SD0/SD1/RPi/flash) and the swap bit |
| DivMMC paging + automap | the framework of [divide-divmmc-esxdos.md](divide-divmmc-esxdos.md) §5 (8 KB windows, `DivPaging`, M1 hook) | a Next profile: readable `#E3`, 4 bank bits, reset clears `#E3`, NR `#09`/`#0A`/`#83`/`#B8-#BB`, "ROM 3 only" condition, RETN clears the latches |
| FPGA flash, RTC RAM | not slots (persistent blobs, storage-manager G11) | blobs |
| TTD | `EvoSdCard`-style protocol blob per card | two card blobs + the DivMMC blob; DivMMC RAM as a TTD v2 region |

## 4. Software to test with

| Item | Where |
|---|---|
| `sn-complete-<version>.zip` (NextZXOS distribution) / `sn-emulator-<version>.zip` (1 GB image) | specnext.com; nothing on disk (no `tbblue.fw`, `enNxtmmc.rom` or SD image under the references or `testdata/`) |
| CSpect 1 GB image (`cspect-next-1gb.img`) | what jnext uses; user-supplied |
| `ZXSpectrumNextTests` | local reference tree: port and nextreg test programs, some DivMMC-specific |

## 5. Acceptance test ideas

1. `#E7` decode table (every value x swap bit → which device is selected).
2. Default NR `#B8-#BB`: `RST 8` automaps only with ROM 3 paged.
3. `TBBLUE.FW` boots to the NextZXOS menu from an **unpacked folder** in `sd.next0` (no emulator does
   this today: [integration-next.md](../2026-09-28-storage-manager/integration-next.md) §3).
4. Two cards: `sd.next1` mounted as a second drive letter in NextZXOS.

## 6. Order

Only after the Next exists as a machine (not planned). Everything storage-specific it needs is
built first by DivMMC (for 8 KB windows, `DivPaging`, `SpiPort`), so the Next storage part is a
profile and two slots.
