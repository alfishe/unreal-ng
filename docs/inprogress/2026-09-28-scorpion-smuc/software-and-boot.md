# Software that uses SMUC, and what "the Scorpion works with SMUC" means

| | |
|---|---|
| **Date** | 2026-09-28 |
| **References** | named in [hardware-reference.md](hardware-reference.md) §2 |

## 1. Who talks to the card

| Software | Uses | How it reaches the card | Status for us |
|---|---|---|---|
| **ProfROM 4.01** ([scorp_prof401.rom](../../../data/rom/scorp_prof401.rom), shipped) | EEPROM (settings), RTC (menu clock), IDE (identify, partition table, mounting TR-DOS images, hard-disk utility, partition manager), virtual FDD, 8259 test, version, ISA probe | its own TR-DOS pages (page 3, page 7) | boots with the card; IDENTIFY proven (R4). Everything after IDENTIFY untested |
| **LW ProfROM 4.xx** (`v4s`, `v4se`, `v4su` = SMUC builds; `v4n` = Nemo IDE) | everything above plus master / slave selection, several MFS partitions, FAT32 partitions with `.trd` / `.tap` / `.sna` / `.spg` files, boot from a partition boot sector or a file, a small program from NVRAM run at reset, settings in NVRAM | same | not in this repository; ROM compatibility with our PROFSCORP model unknown (Q6) |
| **savelij multi-machine ROM** (ZX-Evo) | IDE: boot menu entries "HDDSmuc MASTER / SLAVE", a FAT boot loader with a SMUC driver, IS-DOS and CD boot helpers that list SMUC as controller type 2 | `#3D2F` into TR-DOS (`drv_smuc.a80`) | a ZX-Evo ROM: useful as a **driver reference**, not as Scorpion software |
| **NedoOS Mr Rabbit** (TR-DOS target "SMUC") | RTC only (hours, minutes) | `#3D2F` | a small real-world RTC client |
| **Wild Commander** | an IDE driver named `DIDESMUC.ASM` exists in the source tree, but the file is empty there | - | nothing to test with |
| **IS-DOS** | the ProfROM partition manager knows an `IsDOS` partition type | - | no SMUC build of the IS-DOS hard-disk driver found; an IS-DOS **Nemo IDE** raw image exists (karabas-pro `software/profi/isdos_nemoide/`) |
| Base Scorpion ROM 2.9x (`scorp295.rom`, `scorpion.rom`) | nothing | - | the card is invisible to it; only third-party drivers would use it |

The practical conclusion: **the ProfROM is the SMUC software.** "The Scorpion works with SMUC"
means the ProfROM works with it end to end.

## 2. How the ProfROM boot changes when the card is fitted

The existing ProfROM tests all run **without** the card, and several of them assert on the boot
timeline. With the card fitted the firmware takes these extra paths (page 7 unless noted):

| Step | Without the card | With the card | Cost |
|---|---|---|---|
| Presence polls (`#0E91`, 200 tries each) | four polls time out, "not found" lines on the boot panel | the EEPROM ACKs at once | faster |
| Power-on latches (`#0D00-#0D1E`) | writes go nowhere | `#FFBA` ← `#F7` (control block, SCL, WP, SDA, D2, D1, IDE out of reset), `#7FBA` ← `#FF` (no virtual drives) | - |
| NVRAM check (`#0D51`, `#0DE8`) | skipped | reads the stored checksum at `#00FE/#00FF`, then 254 bytes over I²C | together with the next two rows: ~200 frames (~4 s) |
| First-boot format (`#0D6F`) | skipped | when the checksum fails (a blank EEPROM), writes the defaults (`#61` at address 0) and re-checks | paid on **every** boot while the NVRAM is not persisted |
| Config save / load | defaults from ROM | settings staged to and from the NVRAM | - |
| RTC | none | menu clock; periodic interrupt only for the 8259 test | - |
| 8259 probe (`#1572`) | - | fails on every emulator (hardware-reference §5.4), the interrupt test is skipped | - |
| IDE init (`#1E74`) | "IDE controller not found" | control block opened, device control ← `#00`, status read; if the bus answers (status ≠ `#FF`) the hard-disk flag is set | - |
| Drive identity (page 4 `#0BC0` strings: `IDE/AT`, `Serial Number:`, `Firmware`) | - | IDENTIFY DEVICE, then READ SECTORS (the R4 test stops here) | - |
| Partition table | - | expected: sector 0 (MBR-style table), then the partitions it knows (the R4 test sees READ SECTORS, not yet which sector) | - |
| TR-DOS disk calls (page 3) | real drives | before each call, `#7FBA` D7 / D6 say whether drive A / B is a mounted image | - |

Measured with the stubs (2026-09-10): menu idle at frame ~350 with the card, ~150 without.

## 3. What is on the hard disk

The ProfROM partition manager names the partition types it understands (page 5 `#2B27` and its
duplicate in page 21): `Unused`, `Unknown DOS`, `MS-DOS 12FAT`, `MS-DOS 16FAT`, `MS-DOS Ext`,
`MS-DOS 3.31+`, `OS/2 HPFS`, `OS/2 Boot`, **`SMFS`**, **`TR-DOS`**, **`MicroDOS`**, **`IsDOS`**, `BAD`.

| Format | What it is | Who reads it | Can we build a test image? |
|---|---|---|---|
| MBR-style partition table in sector 0 | the PC layout, with Spectrum partition type codes | ProfROM | yes, once the type codes are read from the ROM |
| **SMFS** ("MFS" in the LW log) | the ProfROM's own volume: a **collection of TR-DOS disk images** (and, in LW builds, sub-partitions), mounted onto drives A-D | ProfROM | the layout is undocumented; easiest: let the ProfROM partition manager create it on a blank image (§4, T1) |
| TR-DOS partition | a TR-DOS disk image area | ProfROM | as SMFS |
| FAT12 / FAT16 (4.01: named in the table), **FAT32** (LW: `.trd` mounting "FAT32 only, no long names") | PC volumes | LW ProfROM navigator | yes: the media manager's host folder as a FAT32 volume, or a host-built image |
| IsDOS, MicroDOS | other Spectrum systems | their own loaders | no image with a SMUC driver |

Worked example (what a user would do on the real machine): attach a blank 64 MB disk, open the
ProfROM hard-disk utility, create an SMFS partition, copy a floppy into the collection, mount it
on drive A (the ProfROM writes `#7FBA` with D7 = 0), return to TR-DOS and type `RUN`: the TR-DOS
code in page 3 sees D7 = 0 and reads the sectors from the hard disk instead of the floppy.

## 4. Test images

None exists in `testdata/` or in the reference trees for SMUC. What is needed:

| Id | Image | How to get it | Used by |
|---|---|---|---|
| T0 | blank raw image (4096 sectors) | generated in the test (the R4 test does this today) | IDENTIFY, reset, gating tests |
| T1 | a disk formatted by the ProfROM itself: MBR + one SMFS partition + one TR-DOS image in the collection | drive the ProfROM hard-disk utility from a test (scripted keys on a blank image), keep the result as a compressed fixture, or regenerate it in the test | mount-and-run acceptance (S6) |
| T2 | a FAT32 volume with `.trd` files | the media manager host folder in FAT32 mode, or a host-built image | LW ProfROM only (Q6) |
| T3 | a disk with a bootable partition (LW `!HDDboot.txt` boot-sector format) | built by the LW ROM, or by a small tool once the format is read | LW ProfROM boot-from-HDD (optional) |

## 5. Levels of "works"

| Level | The user sees | Needs |
|---|---|---|
| L0 | today's default: no card, "not found" lines, fast boot | nothing |
| L1 | card fitted, no disk: settings survive a restart, the menu clock shows the time, no first-boot format on the second boot | S1-S4 |
| L2 | a disk is identified and its partitions listed | S1-S3 (IDENTIFY already works) |
| L3 | a TR-DOS image on the disk mounted on drive A and a program run from it | S1-S3 + T1 + S6 |
| L4 | boot from the hard disk (LW ROM) | L3 + LW ROM + T3 |
| L5 | the 8259 test passes, interrupts through the card | S8 (optional) |
