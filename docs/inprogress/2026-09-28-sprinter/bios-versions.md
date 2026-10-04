# Sprinter Sp2000 — BIOS versions, sources and boot results

| | |
|---|---|
| **Date** | 2026-10-02 |
| **Status** | Survey done; 3.06 Hotfix 2 and 3.07 BETA 1 built from source, kept in `data/rom/sprinter/` and tested; **3.06 Hotfix 2 is the shipped default again since 2026-10-03** (§6.1; 3.07 BETA 1 was the default 2026-10-02..03). Upstream head checked 2026-10-03: it **is** the kept 3.07 BETA 1 (§5.3) |
| **Related** | [materials.md](materials.md) §5 (ROM provisioning), [tdd-integration.md](tdd-integration.md) §1.1 (`[ROM] SPRINTER=`), `tools/machines/sprinter/bios-build/make-bios.py`, `tools/machines/sprinter/dcp-table/dcp-table.py` |

**In short.** Peters Plus stopped at BIOS 3.04 (2003). The BIOS is alive again as a community
project by Anatoly Belyansky (Tolik-Trek): 3.05 (2022), 3.06 (25.06.2025), 3.06 Hotfix 1 and
Hotfix 2 (19.01.2026, on `master` since 2026-05-01) and the 3.07 beta (last commit 2026-09-24).
**No binary of 3.05 or later is published anywhere** (no releases, no tags, no forum posts, no
mirrors); the project publishes sources only. MAME lists 3.05 and 3.06 by CRC but does not say
where the images came from. So the community versions are **built here from source**
(`tools/machines/sprinter/bios-build/make-bios.py`). Every image tried boots on unreal-ng to the BIOS boot prompt,
with the fast start and with the full start through the image's own PLD loader.

## 1. The table

"Bitstream" is the PLD configuration the ROM carries at `#30100` (59 215 bytes; the loader is the
first `#100` bytes of page `#C`). The emulator identifies a bitstream by the hash its sink
computes over the loader's 473 720 writes (`SprinterPldConfig`, "full") and MAME's hash of the
first 4 096 writes ("head"); only 3.04's hash has a configuration module (Standard). An unknown
hash runs Standard with a warning in the log. "Port table" is the CRC32 of RAM page `#40` right
after the BIOS opened the port decoder.

| Version (as on screen) | Date | Author | Image, where from | CRC32 | Bitstream (CRC32; full / head hash) | Port table | Boot on unreal-ng |
|---|---|---|---|---|---|---|---|
| 2.13 | 23.01.2002 | Peters Plus | MAME set only (`sp2k-2.13.rom`) | `6495575f` | - | - | not tried (no image) |
| 2.17 | 03.03.2002 | Peters Plus | MAME set only; sources of pages 0 in BIOS-PP | `3c7f1025` | - | - | not tried (no image) |
| `Sprinter BIOS: ver 3.00` | 07.04.2002 | Peters Plus | `fw/bios/sp2k-3.00.253.bin` in [zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000) (MAME's 3.00 is `193de3da`, another build) | `0d0aa5d4` | = BIOS-PP `SP2K_300.BIN` (`de49ef93`); `A65B49FC` / `D0953276` | `B7F09600` | boot prompt, fast and full start |
| `Sprinter BIOS: ver 3.03` | 13.05.2003 | Peters Plus | `fw/bios/sp2k-3.03.253.bin`, same repo (MAME: `fe26f578`) | `88e3e23b` | = `SP2K_303.BIN` (`11ee404a`); `643058BB` / `B051AB62` | `B7F09600` | boot prompt, fast and full start |
| `Sprinter BIOS: ver 3.04` | 17.06.2003 | Peters Plus | **kept**: `data/rom/sprinter/sp2k-3.04.rom` (= `fw/bios/sp2k-3.04.253.bin` = MAME = app.sprinter.ru `sp2k_1k30_3.04.253.zip`) | `1729cb5c` | = `SP2K_304.BIN` (`479b3799`); `FC0928F2` / `78EDDFC6` (Standard) | `B7F09600` | boot prompt, SETUP, CMOS save (`SprinterBoot_Test`) |
| 3.04, board-id variant | - | - | ZXMAK2 `SP_304.BIN` = BIOS-TT `src/bin/_SPRIN.BIN` (2023-06-12 .. 2026-05-16) | `a3970620` | as 3.04 | `B7F09600` | boot prompt, fast and full start |
| 3.05 | 01.09.2022 | Tolik-Trek | MAME set only; predates the public BIOS-TT history (first commit 2023-06-12) | `fe1c2685` | - | - | not tried (no image, no source) |
| `Firmware v3.06`, "Release 25.06.2025" | 25.06.2025 | Tolik-Trek | built from BIOS-TT `92f5db8` (not kept; MAME's `sp2k-3.06.rom` is `187f4382`, see §3.3) | `bd9f27fe` | "Core 1K30 v3.05" (`31b40ecd`); `F9F42E59` / `5AA954B6` | `B77BDEA9` | boot prompt, fast and full start; ESC enters the ZX mode (Spectrum 128 menu "Sprinter") |
| 3.06 Hotfix 1 | 04.07.2025 | Tolik-Trek | BIOS-TT `79fc8a8` (authored 2025-07-04): the commit still has `BETA_BUILD=1`, so it builds as "v3.07 BETA 1"; superseded by Hotfix 2 | - | - | - | not kept |
| `Firmware v3.06 Hotfix 2`, "Release 19.01.2026" | 19.01.2026 (on `master` 2026-05-01) | Tolik-Trek | **kept**: `data/rom/sprinter/sp2k-3.06-hf2.rom`, built from BIOS-TT `master` `c14a8c5` | `9aa7bb29` | as 3.06 | `B77BDEA9` | boot prompt, fast and full start, ZX mode (`SprinterBiosVersions_Test`) |
| `Firmware v3.07 BETA 1` | 24.09.2026 | Tolik-Trek | **kept**: `data/rom/sprinter/sp2k-3.07-beta1.rom`, built from BIOS-TT `beta` `f546c4e` | `a06a1a02` | **new**: `Build/ACEX/K30.ACX` of 2026-09-24 (`2d96bb8e`), "ALL_MODE readable"; `29641AB3` / `49861031` | `8E1916C5` | boot prompt, fast and full start, ZX mode (`SprinterBiosVersions_Test`) |

SHA-256 of the kept images:

| File | SHA-256 |
|---|---|
| `sp2k-3.04.rom` | `e166d1557f699cedb65481e784e7f0f17c7b0cdbb6851bf2ed0c4dbf5448de95` |
| `sp2k-3.06-hf2.rom` | `fc910ba4c32f42a8b130b804434f3b449f2f01ed710510a9e8340d8d3ae9a90a` |
| `sp2k-3.07-beta1.rom` | `3745a845e3729189fc5a9590a6c3d5e0dff5ee02da98d120b4a5bb3905505c22` |

The per-page SHA-256 of pages 0, 8 and 12 are in the ROM signature catalog
(`core/src/emulator/memory/rom.cpp`).

The port-table CRCs of the community builds are not only measured: `tools/machines/sprinter/dcp-table/dcp-table.py
--records <commit>:bios/exp/DCP.ASM --constants <Shared_Includes>:constants/SP2000.inc` expands
each build's own record list to the same CRC, so the BIOS builds its table on unreal-ng exactly as
its source says.

## 2. Image layout

All images are 256 KB = 16 pages of 16 KB.

| Page | 3.00-3.04 (Peters Plus) | 3.06 / 3.07 (community, `bios/mem_map.txt`) |
|---|---|---|
| 0 | ROM: disk drivers, packed SETUP | ROM: disk subsystem and SETUP (3.07: 2.56 and 1.60), "Sprinter BIOS v3.0x" |
| 1 | empty | logo (128 x 72, 256 colors) |
| 2-4 | empty | the ZX ROMs: 128 BASIC (menu "Sprinter"), 48 BASIC, TR-DOS |
| 5-7, 9-11 | empty | the recovery ROM disk (96 KB FAT12 image with DSS, FORMAT, FDISK) |
| 8 | the BIOS proper, `Sprinter BIOS: ver 3.04` | the BIOS proper ("EXP"), `Firmware v` at `#006A` |
| `#C`-`#F` | PLD loader (`#100`) + bitstream | PLD loader (`#100`, the "universal" 1K30/1K50 loader) + bitstream |

The community BIOS is built twice, for the two FPGA sizes fitted to Sprinter boards: `_SPRIN.BIN`
(ACEX EP1K30, every production board) and `_SPRIN50.BIN` (EP1K50). Only the 1K30 image is
built and kept here; the emulator models the 1K30 board.

## 3. How to obtain each image

### 3.1 Peters Plus builds (2.13-3.04)

- 3.00, 3.03, 3.04: `fw/bios/` of [zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000)
  (the same three files in `2000/`, `2003s/`, `2016s/`; `BIOS_REV.txt`, `SETUP_REV.txt` beside them).
- 3.04 also from [app.sprinter.ru/os/sprinter-firmware](https://app.sprinter.ru/os/sprinter-firmware)
  (`sp2k_1k30_3.04.253.zip`, published 06.06.2021; the only firmware on that site, "Project is closed").
- 2.13, 2.17 and the MAME builds of 3.00/3.03: only as the MAME `sprinter` ROM set (CRCs in MAME
  `src/mame/sinclair/sprinter.cpp`, `ROM_START(sprinter)`); not redistributed here.
- Bitstreams 2.12-3.04 alone: `ALTERA/SP2K_*.BIN` in [gitlab.com/sprinter-computer/bios](https://gitlab.com/sprinter-computer/bios).

### 3.2 Community builds (3.06 and later): build from source

```bash
python3 tools/machines/sprinter/bios-build/make-bios.py \
    --repo <clone of https://zxgit.org/Tolik-Trek/Sprinter-BIOS> \
    --includes <clone of https://zxgit.org/Tolik-Trek/Shared_Includes> \
    --commit c14a8c5 --sjasmplus <sjasmplus 1.21.1> \
    --bitstream <59 215-byte 1K30 bitstream> --out sp2k-3.06-hf2.rom
```

Worked example: `--commit f546c4e --cmos-date 2026-10-02` (the 3.07 beta of 2026-09-24) prints
`CRC32 a06a1a02 ... Firmware v3.07 BETA 1`, byte-identical to `data/rom/sprinter/sp2k-3.07-beta1.rom`.
The beta tree carries its own bitstream, so `--bitstream` is not needed there. `--commit c14a8c5
--cmos-date 2026-10-02 --bitstream <LOADER_K30.BIN of 4c5d44a without its first 256 bytes>` gives
`sp2k-3.06-hf2.rom` (both checked 2026-10-03 with sjasmplus 1.21.1).

What the tool has to work around (all inside its temporary copy of the sources):

- **The assembler.** The sources are sjasmplus with Lua. sjasmplus 1.22+ rejects
  `TEXT 128,{""," "}` (trees before 2026-06) and `BLOCK 0,filler` (all trees); the tool guards
  the zero-length `BLOCK` and the images here are built with sjasmplus **1.21.1** (built from the
  sjasmplus repository tag `v1.21.1`; Homebrew has no sjasmplus formula). For the 2026-09 beta,
  1.21.1 and 1.23.1 give identical bytes.
- **The final layout.** `bios/BUILD.a80` (`ROM_BUILD`) uses `ORG` past `#FFFF` without a device,
  which current sjasmplus refuses; the tool lays out the 16 pages itself, in the same order.
- **The build date.** The SETUP screen shows the build year ("Copyright (c) 2009-2026 Sprinter
  Team") and a BETA build also the date and time ("Test build! 24.09.2026, 12:00:00"). The tool
  fixes them (`--date`, default the commit's author date; `--time`, default 12:00:00) so that a
  commit always gives the same image.
- **The default CMOS date.** SETUP's `MAIN.asm` takes the date it writes to a CMOS with a bad
  checksum from the build host's clock (`os.date("%Y")`, `%y`, `%m`, `%d` in Lua). Until 2026-10-03
  the tool did not fix it, so a rebuild on another day differed in one byte (page 0 `#0896`, the day).
  `--cmos-date` (default: the build date) fixes it now. The kept 3.06 Hotfix 2 and 3.07 BETA 1 were
  built on 2026-10-02: rebuild them with `--cmos-date 2026-10-02`. The tool stops if a tree uses
  another `os.date` format.
- **The bitstream.** Trees before 2026-06-26 do not carry the bitstream binary (it is compiled
  from `src/altera/acex/k30/*.TDF` with Altera MAX+plus II). The FPGA sources did not change from
  2024-08-16 (`085ef3c`) to 2026-09-24, so the 3.06 images here use the stream that the first
  committed binary holds (`Build/Bin/LOADER_K30.BIN` of `4c5d44a`, 2026-06-26, at `#100`;
  CRC32 `31b40ecd`, shown as "Core 1K30 : v3.05"). 3.07 BETA 1 uses its own `Build/ACEX/K30.ACX`.

### 3.3 Why the 3.06 built here is not MAME's 3.06

MAME's `sp2k-3.06.rom` (CRC32 `187f4382`, "Firmware v3.06, 25.06.2025", added by MAME PR 13946 of
2025-07-19 without a source) is the author's own build of that day. The build of `92f5db8` here,
with the date fixed to 2025-06-25, gives `bd9f27fe`. The image itself is not public, so the
difference cannot be located; the likely causes are the author's local tree (the history was
rewritten after June 2025: Hotfix 1 was authored 2025-07-04 but committed 2026-01-19), the
bitstream binary and the sjasmplus version. Hence the release itself is not kept: the kept 3.06
is Hotfix 2, the current release on `master`.

## 4. What changed (from `doc/changes.txt` and the commit log)

- **3.05-3.06 (2022-2025)**, against 3.04: a universal bitstream loader for 1K30 and 1K50; the
  second IDE channel; IDE units numbered and chosen for boot physically (four units scanned, 3.04
  scanned two); boot from a RAM disk; the RECOVERY image in the ROM disk; SETUP 1.60 with screen
  timing (Scorpion / Pentagon / Spectrum), 312- or 320-line frame, HDD settings saved after
  auto-detect; a ZX mode from the BIOS prompt ("<ESC> TO ZX-MODE", Pentagon timing unless CMOS
  says otherwise); new BIOS functions `FN_SINC` (#F2), `FN_RESET` (#FD), `DCP_CONFIG` (port
  decoder control), `GET_RAMD_NUM` (#9B), the 5x disk functions moved to pages 0 and 8; a
  256-color 128 x 72 logo; the port table built from a record list (`DCP.ASM`) instead of the
  packed 16 KB image of 3.04.
- **3.06 Hotfix 1** (04.07.2025): ATAPI media eject fixed; `LP_PR_LINE_DIR` can switch scrolling
  off; new RECOVERY image.
- **3.06 Hotfix 2** (19.01.2026): a delay for the density switch of slow 5.25" drives, longer
  WD1793 wait in `EXECOM`; DSS from its hotfix 3 and FORMAT 1.17 in the recovery disk.
- **3.07 beta** (2026-01..2026-09): the FDD driver rewritten on IY with a buffer per drive,
  `FDD_5x_GET_PAR` returns the drive type; `[5x] DRV_GET_NAME` for ATA/ATAPI; ATAPI reset fixes;
  new DSS and FORMAT in the recovery disk; the scaling port (`ACEX.SCALE`) dropped from the port
  table init; `PIC_SET_PAL` / `PIC_GET_PAL` without the stack; and, with the new bitstream of
  2026-09-24, **ALL_MODE readable**: its table records lose the write-only bit, and the SETUP
  starter reads the port (`IN H,(C)`) before clearing bit 0. unreal-ng's Standard configuration
  already answers a read of code `#C3` with the stored cell, so the beta runs on Standard.

## 5. Boot results on unreal-ng in detail

Measured with `SprinterBiosProbe_Test.ProbeDirectory` (§6) and pinned by
`SprinterBiosVersions_Test` (`core/tests/emulator/machines/sprinter/sprinterbiosversions_test.cpp`).

| Image | Fast start: DCP opened | Boot prompt (fast / full) | Full start: loader done |
|---|---|---|---|
| 3.00, 3.03, 3.04, 3.04 variant | frame 32, PC `#0CD5` (3.00) / `#0CD8` | frame 288 / 405 | frame 118, 473 720 writes |
| 3.06, 3.06 Hotfix 2 | frame 27, PC `#3244` / `#325E` | frame 333 / 498 | frame 165, 473 720 writes |
| 3.07 BETA 1 | frame 27, PC `#335D` | frame 518 / 683 | frame 165, 473 720 writes |

The community screen (3.06 Hotfix 2, no drives, blank CMOS):

```
Model     : Sprinter                    Firmware v3.06 Hotfix 2
Board ID  : 52-83-0000047E8             Copyright (c) 2002 Peters Plus
Core 1K30 : v3.05                       Copyright (c) 2009-2026 Sprinter Team
Memory    : 4096K                       Release 19.01.2026
Available : 3664K
CMOS      : Found, 07:00:30             <ALT>  for Alternative boot
WARNING! CMOS CHECKSUM ERROR, DEFAULT VALUES SET!
 Detecting IDE Primary Master    ... Skipped
 ...
Boot from HDD Primary IDE Master fail
Alternative Boot from Diskette fail
PRESS <ENTER> TO REBOOT, <DEL> TO ENTER SETUP OR <ESC> TO ZX-MODE . . .
```

- (Before S3b; since 2026-10-02 an empty channel reads `#7F` and each unit is "None" at once, tdd-storage
  §3.4.) No IDE yet: each of the four units waits for BSY until F4 is pressed (the tests
  send it, as the 3.04 test does); with no floppy (S3a) both boot attempts fail. Nothing in the
  community builds needs a feature the emulator lacks to reach this point.
- ESC at the prompt starts the ZX mode: the Spectrum 128 menu titled "Sprinter" (TR-DOS,
  Hardware, 128 BASIC, Calculator, 48 BASIC, Options) appears on all community images.
- The new bitstream hashes (`F9F42E59`, `29641AB3`) are unknown to the configuration registry, so
  the log says "unknown PLD bitstream ... using Standard". Whether the 3.05 core or the 2026-09
  core needs a configuration module of its own (beyond the readable ALL_MODE, which Standard
  already gives) is open; nothing seen so far needs one.

### 5.1 DSS versions and the BIOS (S3b, 2026-10-02)

**DSS 1.71 needs a newer BIOS than 3.04.** The owner's MAME-pack system disk (DSS 1.71.57) and the DSS 1.71
floppy (`dss171u.img`, same pack) boot their loader and SYSTEM.DOS on BIOS 3.04, then SYSTEM.DOS's start-up
returns an error and the DSS loader prints a stray character and "Fatal error! Press RESET to restart." (the
loader's own message; it prints whatever HL points at as the reason). MAME 0.289 with `-bios v3.04` shows the
same screen at frame 475 ([reference/hdd-boot-304.txt](../../../testdata/machines/sprinter/reference/hdd-boot-304.txt)),
so it is a version requirement, not an emulation fault. On 3.06 (MAME's `v3.06` and the kept 3.06 Hotfix 2)
DSS 1.71.57 boots ("Estex DSS version 1.71.57. Shell version 1.2.522."); 3.05 is untried (no image). Which
BIOS function DSS 1.71 needs is open (the public DSS sources end at 1.70). DSS 1.62.92 and 1.62.93 boot on
3.04 and on 3.06 Hotfix 2.

| DSS | Medium | BIOS 3.04 | BIOS 3.06 Hotfix 2 | BIOS 3.07 BETA 1 (default) |
|---|---|---|---|---|
| 1.62.92 | floppy, built hard disk image | boots (ACC-3, ACC-4) | boots (`Bios306_DssUsesBothChannels`) | see §5.2 |
| 1.62.93 | ZXMAK2 `sp_disk1.vhd` | boots (`RealHdd_Dss16293BootsFromTheZxmak2Vhd`) | not tried | not tried |
| 1.71.57 | MAME pack `sp_hdd_sys.img` / `.chd`, `dss171u.img` | "Fatal error" (MAME agrees) | boots (`RealHdd_Dss171BootsFromTheMamePackImage`) | boots from the CHD to Flex Navigator 1.15 with proper glyphs (owner, 2026-10-02) |

### 5.2 Known behavior of the community builds (2026-10-02)

- **3.06 Hotfix 2: DSS text does not scroll at the bottom line.** Once DSS output reaches the last text
  line, new lines overwrite it instead of scrolling the screen up. MAME (`-bios v3.06`) shows the same, so it is
  the firmware, not the emulation (3.06 Hotfix 1's `LP_PR_LINE_DIR` scrolling switch is the likely area).
- **3.07 BETA 1 keeps ALL_MODE at `#FE` after the return to DSS** (the readable ALL_MODE of its new bitstream,
  §4: the SETUP starter reads the port before it clears bit 0). Seen on unreal-ng's Standard configuration; no
  program has been found to mind it so far.
- **3.07 BETA 1 boots DSS 1.71.57 from the MAME pack's `sp_hdd_sys.chd`**: Flex Navigator 1.15 comes up with
  proper glyphs (the owner's run, 2026-10-02; `.recipe/machines/sprinter.md` §6 was checked on it).
- **3.07 BETA 1 + DSS 1.71.57: programs on a floppy do not start ("Invalid EXE file")** (2026-10-03). On the
  MAME pack's system disk, `b:\netcfg.exe` (or any EXE, drive A or B, 1.44 MB or 720 KB) gets Flex Navigator's
  "Invalid EXE file" dialog, while directory listings work; on 3.06 Hotfix 2 the same floppy runs. DSS never
  reads the file: the port trace stops after the FAT read, and its EXEC finds no "EX" in a header it never
  loaded. The cause is the beta's FDD driver, rewritten on IY (`FDD_DRIVER.asm`, BIOS-TT `f546c4e`):
  `SELECT_FDD` points IY at the drive's table, and RESET, GET_PAR, SET_PAR, DETECT, READ and WRITE return with
  IY changed (the `PUSH IY` / `POP IY` of `.Start` is commented out). The 3.06 driver never changed IY (one table
  at fixed addresses, `.Start` saved IY). DSS 1.71.57 keeps its own pointer in IY across these calls: a BIOS-entry
  log shows IY `#31D0` going into RESET and `#C1E8` coming out on 3.07, unchanged on 3.06. Newer DSS wraps every
  call in its floppy driver in `PUSH IY` / `POP IY` (Estex-DSS `master`, `drivers/media/fdd-drv.asm`; the public
  1.70 beta did not), and the DSS of the beta's recovery disk (`SYSTEM.DOS` of 11.08.2026) runs the program on
  3.07 BETA 1. So the beta needs the matching DSS; it is not an emulation fault:
  - MAME 0.289 (`zxsp`, the beta image under the `v3.06` name, the same CHD, the floppy in drive A) shows the same
    dialog; with MAME's own 3.06 the program runs.
  - A 3.07 BETA 1 rebuilt with `PUSH IY` / `POP IY` around the six FDD functions runs it on unreal-ng.
  - The controller side is pinned by `PortDecoderSprinter_Test.Fdc_Bios307SectorReadLoop_HdSide1` (the beta's own
    transfer loop reads a side-1 HD sector whole), the firmware side by `SprinterFloppyExe_Test` (env-gated:
    `UNREAL_SPRINTER_HDD`; at the DSS prompt 3.06 Hotfix 2 runs `b:\bin\type.exe b:\install.bat` from the DSS 1.62.92
    test floppy, 3.07 BETA 1 prints "Bad command or file name", the shell's word for the failed EXEC; with
    `UNREAL_SPRINTER_ZX_BIOS` pointing at the IY build the 3.07 case prints the file).
  Copying does not help either: `copy b:\netcfg.exe c:\` on 3.07 BETA 1 writes a 0-byte file (byte-identical on 3.06).
  Workaround: BIOS 3.06 Hotfix 2 for DSS 1.71.57, or update the system disk to the DSS of the 3.07 recovery disk.
  The default stays 3.07 BETA 1 (owner decision 2026-10-03, faithful firmware); the BIOS report lists the issue
  under `known_issues` on every surface, unreal-qt's status bar shows "BIOS: known issue". Note for the BIOS author
  with the fix: [upstream-bios-307-fdd-iy.md](upstream-bios-307-fdd-iy.md).
- **The alternative boot device is floppy A on 3.06 / 3.07, floppy B on 3.04** (blank CMOS, SETUP defaults).
  Checked live 2026-10-02 on unreal-ng: `dss_1_62_92.img` in drive A boots DSS 1.62.92 on 3.07 BETA 1 into Flex
  Navigator 1.10; in drive B both 3.06 Hotfix 2 and 3.07 BETA 1 print "Alternative Boot from Diskette fail" and
  stop at the prompt. The DSS-floppy tests and recipe steps use 3.04 with the disk in B (selected explicitly).

### 5.3 The upstream head (checked 2026-10-03)

The BIOS author said the upstream head has many fixes over "3.07 BETA 1". Checked on 2026-10-03:

- **The public head is the kept image.** `git ls-remote` and the zxgit API give `beta` = `f546c4e`
  (2026-09-24, "ALL MODE port read enabled"), `master` = `32f5f8f` (2026-05-16), Shared_Includes `main` =
  `dd760c8`; the repository was last pushed 2026-09-23 (UTC). No other branch, fork, tag or release.
  Nothing newer exists elsewhere either: zxgit has no other BIOS repository, the GitHub user Tolik-Trek has
  none (`sp_mame` last pushed 2026-06-20), and the MAME set still stops at 3.06. A rebuild of `f546c4e`
  (clone in `scratch/bios-head/`) is byte-identical to `sp2k-3.07-beta1.rom` (with `--cmos-date
  2026-10-02`, §3.2).
- **Why the confusion is likely.** The version string has read "Firmware v3.07 BETA 1" since `e0c7051`
  (2026-01-19, `SET_EXPID_MOD 07`), and `BETA_BUILD` / `BETA_RC` have not changed since. That covers all 31
  commits from 2026-01-19 to 2026-09-24: the IY rewrite of the FDD driver (`67c829e`, 2026-01-24), a buffer
  per drive (`2a4e06d`), the drive type from `FDD_5x_GET_PAR` (`684c322`), the drive A/B fix (`7346ac6`), the
  FDD delays (`5d3a690`, `1c0d877`), `[5x] DRV_GET_NAME` (`bd90859`, `2c5d438`), the ATAPI reset fix
  (`ee91527`), the TR-DOS `/hdd` fix (`4307490`), new DSS / FORMAT / recovery images (`2e74cde`, `314ba26`,
  `2638bcb`), the bitstream binaries (`4c5d44a`), no ACEX.SCALE in the port table (`a2986d7`), the stackless
  `PIC_SET_PAL` (`c891536`) and the readable ALL_MODE (`f546c4e`). An image called "3.07 BETA 1" can be
  any of them; the one kept here is the last. Only the SETUP line "Test build! 24.09.2026, 12:00:00"
  tells them apart.
- **The FDD driver still changes IY at the head.** `FDD_DRIVER.asm` of `f546c4e` still has `;PUSH IY` at
  `.Start` and the two `;POP IY` commented out, so the "Invalid EXE file" of §5.2 stays. Any fix the author
  has is not pushed. To the author: please push it, or say which build is meant (the "Test build" line of
  the SETUP screen).
- **No new image is added**, and no `head` alias: it would be a second copy of the default. The results
  of the head are therefore those of 3.07 BETA 1 (the emulator is deterministic; the same bytes give the
  same run):

| Test | 3.06 Hotfix 2 | 3.07 BETA 1 (`f546c4e`) | Upstream head (= `f546c4e`) |
|---|---|---|---|
| Boot to the prompt, fast and full start | yes | yes | same image |
| DSS 1.71.57 from the MAME pack's CHD, Flex Navigator 1.15 | yes | yes | same image |
| EXE from a floppy (`b:\netcfg.exe -i`, `SprinterFloppyExe_Test`) | runs | "Invalid EXE file" / "Bad command or file name" (IY, §5.2) | same as 3.07 BETA 1 |
| `copy b:\file c:\` | correct copy | 0-byte file | same as 3.07 BETA 1 |
| dontBlink (`C:\DEMOS\DNTBLINK\dntblink.exe`) | freezes at about 305 s too (the same race; an earlier "plays to the end" was one lucky run) | freezes at about 305-310 s (a race in the demo, branch `sprinter-dntblink-freeze`) | same as 3.07 BETA 1 |
| DSS text scrolls at the bottom line | no (MAME too) | yes | same image |
| ZX mode from the prompt (ESC), Spectrum 128 menu | yes | yes | same image |
| ALL_MODE readable (stays `#FE` after the return to DSS) | no (old bitstream) | yes (§5.2) | same image |

**Decision (owner, 2026-10-03):** the default goes back to 3.06 Hotfix 2 until the author publishes his
fixed head (§6.1). 3.07 BETA 1 stays selectable with its `known_issues` warning. When the author pushes a
build with the IY fix: build it with `make-bios.py`, rerun `SprinterFloppyExe_Test` and the dontBlink run,
then add the image or replace the beta.

## 6. Picking and testing a BIOS

- **Config.** `[ROM] SPRINTER=` in `data/configs/sprinter/unreal.ini` (read into
  `config.sprinter_rom_path`, loaded raw as 16 pages; tdd-integration §1.1). The shipped default is
  `rom/sprinter/sp2k-3.06-hf2.rom` (§6.1); `rom/sprinter/sp2k-3.04.rom` and
  `rom/sprinter/sp2k-3.07-beta1.rom` stay selectable here, at create (`"sprinter": {"bios": "3.04"}`) and
  on a running machine (`POST /sprinter/bios`, every automation surface).
- **Tests.** Tests pinned to one BIOS select it explicitly (`SprinterFixture::SelectBios`): the 3.04
  suites (`SprinterBoot_Test`, `SprinterInputBoot_Test`, `SprinterReference_Test`, `SprinterVideoBoot_Test`,
  `TTDSprinterMachine_Test`) select `sp2k-3.04.rom`; `SprinterBiosReload_Test` checks that the shipped config
  loads 3.06 Hotfix 2 (and the 3.07 BETA 1 warning after a switch); the TTD corpus fixture
  `testdata/machines/sprinter/ttd/boot.ttd` is the cold start of the shipped config, so it records 3.06 Hotfix 2. `SprinterBiosVersions_Test` boots each kept community image with the fast start to
  the boot prompt (banner, memory, port-table CRC, bitstream hash) and with the full start through
  its own loader.
- **A new build.** Put 256 KB images in a folder and run
  `SPRINTER_BIOS_PROBE_DIR=<folder> SPRINTER_BIOS_PROBE_OUT=<folder for PNGs> core-tests --gtest_filter=SprinterBiosProbe_Test.*`:
  per image and start mode it prints the bitstream hashes, the frame and PC where the port
  decoder opened, the page `#40` CRC, whether the boot prompt came and where the CPU spends its
  time, the screen text, and saves screenshots (prompt, and after ESC). Compare the CRC with
  `dcp-table.py --records` over the build's `DCP.ASM`.

### 6.1 Decision record: the default BIOS

| Date | Decision | Why |
|---|---|---|
| 2026-09-28 | 3.04 by default, 3.06 selectable (review round 1, Q1, [roadmap-and-plan.md](roadmap-and-plan.md) §5) | the last Peters Plus release, the one MAME and every capture used |
| 2026-10-02 | Owner: switch the default straight to 3.07 (3.07 BETA 1, `sp2k-3.07-beta1.rom`); 3.04 and 3.06 Hotfix 2 stay selectable. Replaces Q1, and an intermediate decision of the same day for 3.06 Hotfix 2 | the main path is now DSS 1.71 from a hard disk, which needs 3.06 or newer (§5.1); 3.06 Hotfix 2 has the bottom-line scroll quirk (§5.2); 3.07 BETA 1 boots the MAME pack's DSS 1.71 / Flex Navigator 1.15 |
| 2026-10-03 | **Owner: back to 3.06 Hotfix 2** (`sp2k-3.06-hf2.rom`) until the BIOS author publishes his fixed head; 3.07 BETA 1 and 3.04 stay selectable | the upstream head is the kept 3.07 BETA 1 and still changes IY in the floppy driver: DSS 1.71.57 cannot start programs from a floppy (§5.2, §5.3). 3.06 Hotfix 2 runs them and plays dontBlink to the end; its only known quirk is the bottom-line scroll (§5.2) |

## 7. Who develops it, and how to follow new builds

- **Developer.** Anatoly Belyansky (Tolik-Trek; commits as "Tolik", "Anatoliy Belyanskiy",
  "Анатолий Белянский"; tolik.trek@gmail.com). The only active BIOS developer. Work also flows in
  from a private "Sprinter-Core" remote (merge `53ee837`, 2026-01-24).
- **Where.** [zxgit.org/Tolik-Trek/Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS):
  branch `beta` = the next version (3.07 BETA 1, `f546c4e`, 2026-09-24), branch `master` =
  releases and hotfixes (3.06 Hotfix 2, `32f5f8f`, 2026-05-19). Submodule
  [Shared_Includes](https://zxgit.org/Tolik-Trek/Shared_Includes). No releases, no tags, no
  binaries. Tolik-Trek's MAME forks ([sp_mame](https://github.com/Tolik-Trek/sp_mame), zxgit
  `Tolik-Trek/MAME` branch `Vibe`) only add a `dev` BIOS slot that loads a local `_sprin.bin`.
- **How often.** Commits every 1-4 weeks on `beta` (2026: January, February, March, April, May,
  June, August, September); a release about once a year (3.05 2022, 3.06 2025), hotfixes in
  between.
- **Announcements.** In chat, not on forums: Telegram [t.me/zx_sprinter](https://t.me/zx_sprinter)
  and Discord [discord.gg/x59TnYWkSN](https://discord.gg/x59TnYWkSN) ("Sprinter"). zx-pk.ru (newest
  BIOS thread t=31520, 3.00-3.04) and nedopc.org (t=20272, 2020-21) have nothing on 3.05+.
- **Tracking.** zxgit (Gitea) has its RSS/Atom feeds switched off, so poll:
  - `git ls-remote https://zxgit.org/Tolik-Trek/Sprinter-BIOS.git` (branch heads), or
  - `https://zxgit.org/api/v1/repos/Tolik-Trek/Sprinter-BIOS/branches` and
    `https://zxgit.org/api/v1/repos/Tolik-Trek/Sprinter-BIOS/commits?sha=beta&limit=5` (also `sha=master`),
  - `https://zxgit.org/api/v1/repos/Tolik-Trek/Sprinter-BIOS/releases` (empty so far),
  - MAME's ROM set: `https://github.com/mamedev/mame/commits/master/src/mame/sinclair/sprinter.cpp.atom`
    (still 2.13-3.06 on 2026-10-02).

  A version change shows in `bios/shared/DEFINES.INC` (`SET_EXPID_VER` / `SET_EXPID_MOD`,
  `BETA_BUILD`, `BETA_RC`, `RELEASEhotFIX`, `RELEASE_BUILD_DATE`) and in `doc/changes.txt`.
  To refresh: pull the corpus clones, build the new head with `make-bios.py`, run the probe,
  and if it is worth keeping replace `sp2k-3.07-beta1.rom` (and its row in `kBiosImages`, the
  catalog and `README-ROMS.md`).

Upstream rechecked 2026-10-03 (§5.3): `beta` `f546c4e`, `master` `32f5f8f`, Shared_Includes `dd760c8`, unchanged.

Corpus state used: `emulators/zxgit/Sprinter-BIOS` (`beta` `f546c4e`, `master` `32f5f8f`),
`Shared_Includes` `dd760c8`, `zxgit/2000` `6be4f60`, `gitlab/sprinter-computer-bios` `1273243`
(all fetched 2026-10-02, no new commits), `github/mame` fetched to `f8f6b22c` (2026-10-02,
`sprinter.cpp` unchanged).
