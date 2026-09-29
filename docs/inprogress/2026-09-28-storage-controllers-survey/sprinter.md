# Sprinter Sp2000: two IDE channels

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Machine** | Peters Plus Sprinter Sp2000 (PLAN #59, the last machine program) |
| **Full design** | [tdd-storage.md](../2026-09-28-sprinter/tdd-storage.md) §1, §3, §5; [hardware-reference.md](../2026-09-28-sprinter/hardware-reference.md) §9. This page only measures it against the IDE core as built on `ide-atapi` (on master since `f5fc5f05`) |
| **Effort (storage part)** | **M** (about 1-1.5 weeks), after the Sprinter machine exists |

## 1. Hardware in one table

| Item | Fact | Source |
|---|---|---|
| Channels | two ATA channels, master + slave each: four units | [hardware-reference.md](../2026-09-28-sprinter/hardware-reference.md) §9.2 |
| Port decode | table-driven: the PLD turns a port into an internal code; IDE codes `#20`-`#2B` | MAME `src/mame/sinclair/sprinter.cpp:584-633`, `:755-774` |
| Data register | read `#0050`: 16-bit read, low byte returned, high byte latched; read `#0150`: the latch. Write `#0050`: low byte into the latch; write `#0150`: sends `value << 8 \| latch` | MAME `sprinter.cpp:613-622`, `:755-760`; ZXMAK2 `IdeSprinter.cs:150-195` |
| Task file | registers 1-5 read at `#0051-#0055` (A8 = 0), written at `#0151-#0155` (A8 = 1); device/head and status/command at `#4052`/`#4053` (read) and `#4152`/`#4153` (write) | BIOS-TT `ATA_DRV.ASM:9-31`; MAME `:623-626`, `:761-764` |
| Control block | alternate status `#4054` (read), device control `#4154` (write), drive address `#4055` | MAME `:627-634`, `:765-768` |
| Channel select | `OUT (#BC),#21` = primary, `OUT (#BC),#01` = secondary; a latch, reset = primary | INC `SP2000.inc:1935-1940`; MAME `:769-774`, `:1582` |
| Latch | **one** register for both directions (PLD `HDDR`); on a write it holds the **low** byte (reverse of the Nemo order) | PLD `SP2_1K30.TDF:181`, `:360-373` |
| INTRQ | not wired; the BIOS polls BSY / DRQ | `EXTENDED/shared.asm:6-33` |
| Reset | machine reset selects the primary channel and hard-resets all four units | MAME `:1582` |
| ATAPI | BIOS-TT `ATAPI_DRV.ASM`: packet commands, 2048-byte blocks, eject / close through BIOS function `#5E`; MAME puts a CD on the primary slave | MAME `sprinter.cpp:1967-1968` |
| File systems | DSS reads FAT12 / FAT16 only; the boot loader checks MBR entry 0 only | DSS-162 `ide_drv0.asm:596-640`; DSS `DOSBOOT4.ASM:237-248`, `:351-375` |

(The paths above are the reference sources named in the Sprinter materials; see
[materials.md](../2026-09-28-sprinter/materials.md).)

**Worked example: one word read with `INI`.** The BIOS reads a sector with `LD BC,#0050` and 512
`INI`. `INI` puts B on A15-A8 and decrements B after the input, so B runs `#00, #FF, #FE, ...`. The
first `INI` has A8 = 0 (a real 16-bit read: low byte returned, high byte latched), the second has
A8 = 1 (the latch). Sector bytes arrive low, high, low, high, without the BIOS ever changing the port
(`ATA_DRV.ASM:350-366`).

## 2. What unreal-ng has vs the gap

| Piece | On master today (IDE rollout 1) | Gap for the Sprinter |
|---|---|---|
| Disk, ATAPI CD, formats, slots, TTD blob | `AtaDisk`, `AtapiCdrom`, `AtaChannel`, `IdeUnitSlot`, `HddImageFormats`, TTD id 17 | none for one channel |
| Second channel | `IdeController` owns **one** `AtaChannel` and two slots (`ide0.master`, `ide0.slave`) | **new**: an array of up to two channels, slots `ide1.master` / `ide1.slave`, the TTD blob grows to hold both channels and the channel latch |
| Adapter | one `IdeAdapter` with a `switch` over `IDE_SCHEME` (NEMO ... DIVIDE) | **new** scheme `SPRINTER`: the pattern "A8 half, low-byte latch on write" (`A8HalfLatch`), the channel latch. It is called with the table **code**, not the raw port, because the Sprinter decoder is table-driven |
| Host folder boot | `HostFolderFat` (FAT16 default) on master | **new** `BootProfile` hook: `SprinterDssBootProfile` fills LBA 1-3 with the DSS loader and forces partition type `#06`; FAT32 refused on this model ([tdd-storage.md](../2026-09-28-sprinter/tdd-storage.md) §5) |
| CPU detail | `INI`/`OUTI` drive B on A15-A8 | a CPU test pins the pre/post-decrement timing (Sprinter test plan §2.5) |

Reusable as is: the disk core, ATAPI, formats, media slots and verbs, `state ide` report, write
barriers. New: the second channel (generic, useful for nothing else today), one adapter case, the
boot profile.

## 3. Software to test with

| Software | Where | Proves |
|---|---|---|
| Sprinter BIOS 3.04 / 3.06 | Sprinter S0 provisioning ([materials.md](../2026-09-28-sprinter/materials.md)); not in `testdata/` yet | probe of all four units (T-IDE-8), boot from HDD 0 |
| Estex DSS 1.60R / 1.62 | same | FAT16 from an image and from a folder with the boot profile |
| MAME `sprinter` with `-hard1` | MAME | differential reference for the probe and a sector dump |

## 4. Acceptance test ideas

1. **Truth table** (L2): every IDE code x A8 x direction, as in `ideadapter_test`; word `#ABCD`
   lands in the image as `CD AB`.
2. **Channel select**: `OUT (#BC),#01` then IDENTIFY reaches `ide1.master`; after reset IDENTIFY
   reaches `ide0.master` again.
3. **Real firmware**: BIOS boots DSS from a FAT16 image in `ide0.master`, and from a folder with the
   DSS profile; screen text shows the DSS prompt on `C:`.

## 5. TTD impact

The channel latch and the second channel's `AtaDeviceState`s join the IDE blob. Either the blob
becomes "N channels" (preferred: one serializer, a channel count in front) or the Sprinter
registers a second id. Writes stay barriers through `NoteWrite`, per unit.

## 6. Order and dependencies

Needs the Sprinter machine (S1: model, table decoder), the IDE core merged to master (#13a), and
`HostFolderFat` (on master). The second-channel change can be made early and tested with a
synthetic scheme, but it has no user until the Sprinter.
