# TS-Conf: booting, SD / IDE storage and the keyboard - field notes

**Created:** 2026-09-30 · what we learned running the real firmware (TS-BIOS of
28.04.2018 in `data/rom/zxevo.rom`) and Wild Commander v1.11 RC7 on the
emulated machine. Tests: `core/tests/emulator/machines/tsconf/tsconf_boot_test.cpp`
(BOOT-1...4), `tsconfstorage_test.cpp` (IDE-5), `portdecoder_tsconf_test.cpp` (PS2-1).

## Glossary

| Term | Meaning |
|:--|:--|
| TS-BIOS | the TS-Conf firmware in ROM page 0: Setup Utility and the boot loader |
| NVRAM | the BIOS settings, kept in the Gluk CMOS (cells #B0-#E7 with a CRC) |
| Boot device | where the BIOS loads `boot.$C` from: SD card, Nemo IDE master / slave, SMUC IDE, RS-232 |
| Wild Commander (WC) | the TS-Conf file shell; the program *is* `boot.$C` in the card's root, its plug-ins live in `WC/` |
| Superfloppy | a FAT volume starting at sector 0 of the device, no partition table (how SD card images usually come) |
| Nemo IDE | the IDE board of the ZX-Evo, emulated with `[HDD] Scheme=NEMO-DIVIDE`; media slots `ide0.master`, `ide0.slave` |

## 1. The BIOS Setup Utility

A new machine starts with **blank NVRAM**: the CMOS has no NVRAM file in the
ts-conf config, so every new instance opens the Setup Utility first (text
mode). Any option change saves NVRAM with a valid CRC; the next reset boots
with it. There is no F12 on the ZX keyboard, so "change one option, then reset"
is the way out.

Keys (the ZX matrix works here): CAPS SHIFT+6 = down, ENTER = next value.

| Row (from the top) | Option | Values, in ENTER order |
|:--|:--|:--|
| 0 | CPU Speed, MHz | 3.5, 7, 14 |
| 3 | Reset to | ROM #00 → ROM #04 → RAM #F8 → **BD boot.$c** → BD sys.com → back |
| 5 | CS Reset to | (default BD boot.$c: a reset with CAPS SHIFT held) |
| 7 | Boot Device | **SD Z-contr** → **IDE Nemo M** → IDE Nemo S → RS-232 → IDE Smuc M → IDE Smuc S → back |

Worked example - boot Wild Commander from the SD card (BOOT-3): 3 × down,
3 × ENTER (Reset to = BD boot.$c), reset. From the Nemo IDE master (BOOT-4):
the same, then 4 × down more and 1 × ENTER (Boot Device = IDE Nemo M), reset.

If the boot device holds no `boot.$C` the BIOS shows **"Boot-Device NOT READY!
Press SS + Reset to change start-up options"**. Holding SYMBOL SHIFT through a
reset from automation did not reach the BIOS in our tests (the press is
dropped by the reset); creating a new instance (blank NVRAM) is the reliable
way back into Setup.

## 2. Wild Commander

- WC is loaded by the BIOS as `boot.$C` (a Hobeta file) from the boot device's
  root; it then reads `WC/wc.ini` and loads the plug-ins listed there.
- **Its panels do not follow the boot device.** `wc.ini` names the drive of
  each panel: `[LPANEL] DRV=` / `[RPANEL] DRV=` with 0 = SD (Z-controller),
  1 = IDE Nemo master, 2 = IDE Nemo slave, 3 / 4 = SMUC master / slave, 5 = SD 2.
  The shipped packages say 0, so WC booted from IDE with no SD card inserted
  shows **"Device Not Found!"** - correct behavior, not an emulation fault.
  Set `DRV=1` for an IDE disk (BOOT-4 patches a scratch copy of the image;
  with mtools: `mcopy -o -i disk.img ::WC/wc.ini .`, edit, copy back).
- **WC reads the keyboard only through the AVR's PS/2 scan code log** (Gluk
  cell #F0, AVR extension 2), not through the ZX matrix. Until 2026-09-30 the
  TS-Conf machine had no PS/2 keyboard attached, so WC did not react to keys;
  now host keys and automation typing reach the AVR as on ATM3 (TTD blob
  `EvoPs2` = 19). The BIOS Setup still reads the matrix.
- WC remembers panel paths and cursor positions (`SavePaths`, `SavePosition`),
  so a second boot of the same disk may open where the last session stopped.

## 3. IDE on TS-Conf

- **Ports** (Nemo, [V] zports.v): register r at `(r << 5) | #10` - #30 error /
  features, #50 sector count, #70 sector, #90 / #B0 cylinder low / high,
  #D0 drive / head (WC writes it as #FFD0), #F0 status / command; data #10 with
  the high byte through the #11 latch; #C8 alternate status.
- **No partition table needed.** Both TS-BIOS and WC boot / mount a FAT32
  superfloppy on IDE (the SD card image serves unchanged) as well as an
  MBR-partitioned disk (partition at LBA 2048, type #0C). The media manager's
  insert report "this IDE hard-disk boot path expects an MBR partition table
  ... it likely will not boot here" is written for other machines' HDD boot
  loaders and does not apply to TS-Conf.
- **How WC detects the disk** (`IDE_NEMO.ASM` NEMOini): select the master
  (#E0), DEVICE RESET (#08, a disk aborts it), write sector 2 / cylinder 0 /
  head 0, IDENTIFY (#EC), wait for DRQ, read the task file back and require
  sector 2, cylinder 0, head 0 - IDENTIFY must not touch those registers
  (ours does not). The IDENTIFY block and all sectors are then read by DMA
  (`DMA_CTRL` #43: IDE → RAM, 256 words) while STATUS (#00AF) bits 1:0 read 0,
  as on the emulated standard build; otherwise by `INI` pairs.
- **TTD** (IDE-5): the shared `AtaChannel` blob (id 17) holds the transfer
  state including the sector buffer and the adapter's #11 latch, so a
  checkpoint between the two halves of a word or mid-sector continues exactly.
  The disk contents are media, not TTD state.
- **CPU stall** on IDE bus cycles: `[HDD] IdeStall=1` (off by default,
  hardware-spec §8.3).

## 4. Test data

The Wild Commander packages and ready SD images are **not in git** (kept in
the main checkout at `testdata/machines/tsconf/wildcommander/`, README there;
sources: TS-Labs zx-evo, the Wild-Commander-Improved fork, MiSTer and BigMist
SD cards). BOOT-3 / BOOT-4 skip without them. In a worktree, symlink the folder
in to run them.

To make an MBR hard-disk image from the SD image (what we tried for IDE):
prepend 2048 sectors with a partition entry (type #0C, start 2048, the volume's
sector count) and set the FAT32 BPB "hidden sectors" (offset #1C, also in the
backup boot sector) to 2048 - not needed for TS-Conf, see §3.
