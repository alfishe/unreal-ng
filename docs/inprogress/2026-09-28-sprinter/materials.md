# Sprinter Sp2000 — materials and sources

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Survey done; review round 1 (2026-09-28) applied: the 3.04 ROM source found (§5); S0 provisioning done 2026-10-01 (ROM 3.04, DSS fixtures; 3.06 not found publicly) |
| **Used by** | every other file in this folder; hardware facts cite these sources by the short names below |

The local emulator corpus lives next to this repository at `emulators/github/…`. Repositories
that are not in the corpus yet were cloned into a scratch folder for this design and are cited
with their upstream URL plus a path and line inside the repository at the revision given below.
Since S0 (2026-10-01) all Sprinter sources are in the corpus: `emulators/zxgit/Sprinter-BIOS`
(BIOS-TT), `emulators/zxgit/Shared_Includes` (INC), `emulators/zxgit/2000` (HW-2000),
`emulators/gitlab/sprinter-computer-bios` (BIOS-PP), `…-dos` (DSS), `…-hard` (PLD),
`emulators/github/Sprinter200x` (PLD, MAN), `emulators/gitlab/sprintem` (SPRINTEM).

## 1. Primary sources (authoritative)

| Short name | What | Where | Revision read | Good for |
|---|---|---|---|---|
| **MAN** | Ivan Mak, *Sprinter. Руководство по программированию Sp2000* (the designer's programming manual), 56 pages, 2003-08-15 | `docs/sp2000_man.pdf` in [SaymanNsk/Sprinter200x](https://github.com/SaymanNsk/Sprinter200x); same text in `Docs/sp2000.pdf` of [Tolik-Trek/Shared_Includes](https://zxgit.org/Tolik-Trek/Shared_Includes); a web copy on [doc.sprinter.ru](https://doc.sprinter.ru/) | PDF of 2003-08-15 | memory paging (§3), video memory and screen modes (§4), sound and Covox-Blaster (§5), accelerator (§6), Z84C15 ports, keyboard, mouse (§9), FDD 720/1.44 (§10), CMOS ports (§12), **the port map (§13.1-13.2)**, reset (§14), BIOS calls (§20). Some sections are stubs ("…") |
| **BIOS-TT** | Sprinter BIOS sources, community continuation by Tolik-Trek (v3.05 2022, v3.06 2025, beta 2026) | [zxgit.org/Tolik-Trek/Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS), branch `beta` | `f546c4e` (2026-09-24) | `bios/exp/DCP.ASM` (the port table the BIOS writes, exact bit patterns), `bios/exp/EXTENDED/IDE/ATA_DRV.ASM:9-31` (IDE ports), `…/FDD_DRIVER.asm:597-618` (density switch), `bios/rom/SETUP/MAIN.asm:1019-1196` (boot), `bios/loader/loader.asm` (PLD configuration), `doc/DCP_PAGE.bin` (a dump of the table page), `doc/changes.txt` |
| **INC** | Shared include files of the same author: hardware constants, internal port codes, BIOS function numbers | [zxgit.org/Tolik-Trek/Shared_Includes](https://zxgit.org/Tolik-Trek/Shared_Includes) (submodule of BIOS-TT) | `main` as of 2026-09-28 | `constants/SP2000.inc` (every port, bit and page with comments; ACEX internal codes at `:1279-1560`), `constants/BIOS_equ.inc` (function numbers), `Docs/BIOS functions.asm` (calling conventions, disk functions `:1250-1436`) |
| **DSS** | Estex DSS 1.52-1.70 sources as released by Peters Plus, plus the 1.60R binary release | [gitlab.com/sprinter-computer/dos](https://gitlab.com/sprinter-computer/dos) | `c7f0de8` | `utils/BOOT/DOSBOOT4.ASM` (the DSS boot loader: where it lives on disk, partition types it accepts, `SYSTEM.DOS` / `SYSTEM.EXE`), `utils/BOOT/SYS.ASM` (how `BOOT.EXE` installs it), `release/` (**SYSTEM.DOS, SYSTEM.EXE, BOOT.EXE: the test system**) |
| **DSS-162** | DSS 1.62 sources, "mixed from original sources and recovered from binaries of v1.62.100" | local: `emulators/github/SprinterFirmware/DOS/` ([romychs/SprinterFirmware](https://github.com/romychs/SprinterFirmware)) | `8003040` | `ide_drv0.asm:199-221` (drive scan over 4 IDE units), `ide_drv0.asm:580-640` (MBR and extended partitions, accepted types), `fat_x.asm` (FAT12/FAT16 only), `keyinter.asm:859-860` (keyboard read from the CPU's serial port A) |
| **BIOS-PP** | Original Peters Plus BIOS 2.17 sources + Altera bitstreams 2.12-3.04 | [gitlab.com/sprinter-computer/bios](https://gitlab.com/sprinter-computer/bios) | `1273243` | `SETUP/HDRIVER6.ASM`, `FDRIVER2.ASM` (the original disk drivers), `ALTERA/SP2K_*.BIN` (bitstreams, 59 215 bytes each) |
| **PLD** | Altera PLD design (AHDL) of the Sp2000: DCP decoder, accelerator, video, keyboard, AY | [SaymanNsk/Sprinter200x](https://github.com/SaymanNsk/Sprinter200x) (GPL-3.0) and [gitlab.com/sprinter-computer/hard](https://gitlab.com/sprinter-computer/hard) | `1039391` (2021-03-17) | `Altera_1K30/Last/DCP.TDF`, `ACCELER.TDF`, `VIDEO2.TDF`, `KBD.TDF` (the AT-to-ZX keyboard matrix is **hardware in the PLD**), `SP2_1K30.TDF` (top level). The ground truth when the manual and emulators disagree |
| **PP-WEB** | Peters Plus original web documentation (archived) | [document.htm](https://web.archive.org/web/20031031081751/http://www.petersplus.com/sprinter/document.htm) and its pages `arhitecture.htm`, `accel.htm`, `bioslist.htm`, `estex.htm`, `dss1-60-relnotes.htm`, `dsslist1_60.htm`, `dss_pm1.htm` | archive 2003-10-31 | cross-check of the manual; DSS function list and programmer's manual |
| **DOC-RU** | [doc.sprinter.ru](https://doc.sprinter.ru/): community web edition of MAN plus BIOS/TR-DOS/DSS pages | `blocks/ports/map`, `blocks/ports/defines`, `blocks/fdd`, `blocks/cmos`, `blocks/hdd` (stub), `blocks/reset/boot` (stub), `bios`, `trdos`, `dss` | fetched 2026-09-28 (`bios`, `dss`, `blocks/isa-bus` timed out that day) | easier-to-read MAN; `blocks/reset/boot` and `blocks/hdd` are placeholders, so the boot and IDE facts come from BIOS-TT and DSS |
| **HW-2000** | Board files and history: sp2000, sp2000light (no ISA), 2000s, 2003s prototype, 2016s | [zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000) | last commit 2025-02-27 | schematics (OrCAD, Gerber); board variants; **the BIOS ROM images** `fw/bios/sp2k-3.00…`, `sp2k-3.03…`, `sp2k-3.04.253.bin` with `BIOS_REV.txt` / `SETUP_REV.txt` (§5) |

## 2. Reference emulators

| Short name | What | Where | Good for | Caveats |
|---|---|---|---|---|
| **MAME** | `sprinter` driver (Andrei Holub, BSD-3) | `emulators/github/mame/src/mame/sinclair/sprinter.cpp` (2 059 lines) at `f43983b6` (2026-09-23); floppy side `beta_m.cpp` | the most complete emulation: DCP lookup (`:584`, `:706`), memory paging (`:320-382`), accelerator (`:917-1137`), video (`:397-568`, `:1224-1244`), INT from the mode table (`:1278-1313`), IDE latch (`:613-633`, `:755-773`), Covox-Blaster (`:785-814`, `:1748-1765`), PLD configuration shortcut (`:1139-1166`), reset state (`:1549-1601`), 7 BIOS ROM sets with CRCs (`:2026-2050`) | inherits from the Spectrum 128 driver; ISA memory not done; "Game config" rendering "not fully discovered" (`:42-45`); no software list |
| **ZXMAK2** | Sprinter devices ("Alpha ver" machine) | `emulators/github/ZXMAK2/src/ZXMAK2.Hardware/Sprinter/` (GPL-3.0); machine file `MACHINES/SPRINTER.VMZ`; ROM `ROMS/SPRINTER/SP_304.BIN` | a second opinion: IDE latch (`IdeSprinter.cs:52-195`), FDD + density port (`SprinterFdd.cs:180-230`), CMOS ports (`SprinterRTC.cs:32-35`), SIO keyboard/mouse (`SprinterKBD.cs:238`, `SprinterMouse.cs:48`), DCP codes (`SprinterEnums.cs:21-80`), accelerator opcodes (`SprinterMMU.cs:444-525`) | hard-codes window ports instead of fully following the table; DCP index lacks the map-select bits (`SprinterMMU.cs:534`); one IDE device only; density value stored but ignored; its `SP_304.BIN` differs from MAME's 3.04 image by 5 bytes (a board-id variant, §5) |
| **SPRINTEM** | SprintEm (Alexander Shabarshin, GPL-2.0): runs Sprinter `.EXE` files by emulating BIOS and DSS calls (high-level emulation) onto a host folder | [gitlab.com/nedopc/sprintem](https://gitlab.com/nedopc/sprintem) at `a66ab7a` (2025-11-19): `bios.cpp:155-358`, `disk/` (sample programs) | which BIOS/DSS calls real programs use; **sample `.EXE` programs** (`disk/HELLO.EXE`, `TETRIS.EXE`, `FN.EXE`…) | not a hardware emulator; no DCP, no disks |
| **SP-MAME** | Tolik-Trek's MAME fork | [github.com/Tolik-Trek/sp_mame](https://github.com/Tolik-Trek/sp_mame) | may be ahead of mainline MAME for Sprinter | **not reviewed** for this design |

No other emulator in the local corpus (Xpeccy, xpeccy-plus, UnrealSpeccy, UnrealSpeccyP,
zx-evo-unreal, Zero, ZXSpeculator, zxsp, Spectral) mentions Sprinter (`grep -ril sprinter`,
2026-09-28).

## 3. Software and media

| Item | Where | Status | Use |
|---|---|---|---|
| **DSS 1.62.92 boot floppy** `dss_1_62_92.img` (1 474 560 bytes, FAT12, OEM `DSS 1.60`, 10 reserved sectors, **1 FAT**, loader at LBA 1 starting with `Starting...`) | [app.sprinter.ru/os/estex-dss162](https://app.sprinter.ru/os/estex-dss162) → `dss_1_62_92.zip` (858.88 KB); patch 1.62.93 | downloaded and inspected 2026-09-28 (root holds `SYSTEM.DOS`, `SYSTEM.EXE`, `SYSTEM.BAT`, `INSTALL.BAT`, folders `BIN DEMOS DOCS FM FN GAMES PICS ZX KEYBOARD`, `ESTEX.ZIP`); **provisioned 2026-10-01** as `testdata/machines/sprinter/dss_1_62_92.img` (CRC32 `8D61BE4E`), read by `LoaderRawPcFloppyDss_Test` | **the first DSS acceptance test** (boot from `fdd.a` at 1.44 MB); source of files for the HDD and folder tests |
| DSS 1.60R release files | [sprinter-computer/dos `release/`](https://gitlab.com/sprinter-computer/dos/-/tree/master/release) | public; "we believe … PUBLIC DOMAIN" (repo README); **provisioned 2026-10-01** in `testdata/machines/sprinter/dss160r/` (`SYSTEM.DOS`, `SYSTEM.EXE`, `SYSTEMX.BAT`, `CMD/BOOT.EXE`, `RELNOTES.TXT`; the release has no `SYSTEM.BAT`) | `SYSTEM.DOS` 16 364 B, `SYSTEM.EXE` 6 969 B, `CMD/BOOT.EXE` 2 453 B whose last 1 536 bytes are the boot loader (`Starting...` at offset `#395`) |
| DSS 1.61 beta, 1.60 | [app.sprinter.ru/os](https://app.sprinter.ru/os) | listed, not downloaded | version sweep later |
| BIOS ROM 256 KB | MAME ROM set `sprinter` (`sp2k-2.13.rom` … `sp2k-3.06.rom`, CRCs at `sprinter.cpp:2030-2049`); the exact 3.04 image (CRC `1729cb5c`) as `fw/bios/sp2k-3.04.253.bin` in [zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000); "Sprinter Firmware 3.04.253" on [app.sprinter.ru/os/sprinter-firmware](https://app.sprinter.ru/os/sprinter-firmware) | 3.04 **provisioned 2026-10-01** as `data/rom/sprinter/sp2k-3.04.rom`; 3.06 not found in a public repository (Tolik-Trek publishes sources only, no releases; take it from the MAME set) | S0 provisioning (§5) |
| Sample Sprinter programs | SPRINTEM `disk/*.EXE` (GPL repo; individual program licenses not stated) | available | "a native program runs" acceptance (small, no disk needed beyond a folder) |
| Sprinter software archive | [app.sprinter.ru](https://app.sprinter.ru/) (games, demos, utilities) | not surveyed item by item | demo/game acceptance picks (S2, S5) |
| HDD images with DSS installed | (1) the **owner's MAME pack** (`mame_release_v306_25.05.2025`, `IMG/sp_hdd_sys.chd` and `sp_hdd_media.chd`, run by its `_306.bat` as `-hard1` / `-hard2` with `-bios v3.06`); (2) the **ZXMAK2 bundle** from app.sprinter.ru ("ZXMAK2" 2.9.3.8, 07.10.2021: `SprinterEmu/HDD/sp_disk1.vhd`) | **found 2026-10-02**, kept outside the repository (1-2 GiB each, third-party contents, not redistributed). `sp_hdd_sys`: CHD v5, CHS 4096/16/32, 512-byte sectors; raw 1 GiB after `chdman extractraw`; MBR entry 0 = active FAT16 `#06` at LBA 63, 2 097 089 sectors; the DSS loader ("Starting...") at LBA 1-4; OEM "DSS 1.70"; DSS **1.71.57** (`SYSTEM.DOS` 16 832, `SYSTEM.EXE` 7 922); root `BIN C DEMOS DEV DOCS DSS FM FN GAMES MODEM TESTS TRD UTILS ZX`; `SYSTEM.BAT` sets PATH, runs `ver` and `fn`. `sp_hdd_media`: FAT16 data disk (`MEDIA/`). `sp_disk1.vhd`: fixed VHD, MBR entry 0 = FAT16 at LBA 62, DSS **1.62.93**, the same loader bytes as the DSS 1.62.92 floppy's LBA 1-3 | optional real-disk boots (`UNREAL_SPRINTER_HDD`, `UNREAL_SPRINTER_HDD_VHD`; roadmap §9) and the MAME HDD captures (`testdata/machines/sprinter/reference/hdd-boot-30x.txt`); DSS 1.71 needs BIOS 3.06 (bios-versions.md §5.1). The in-repo test builds its own image (§4) |

## 4. What is missing and how to get it

| Missing | Why it matters | Plan |
|---|---|---|
| A reference HDD image with DSS installed | the "DSS boots from HDD" acceptance test | **Built in the test (S3b, 2026-10-02)**: `BuildDssHdd` in `sprinter_boot_test.cpp` makes a 16 MiB image with an MBR whose entry 0 is an active FAT16 partition (`#06`, LBA 63; the loader checks only the first entry, hardware-reference §9.3), the DSS 1.62.92 floppy's loader (its LBA 1-3, byte-equal to the ZXMAK2 hard disk's) at LBA 1-3, and the floppy's `SYSTEM.DOS` / `SYSTEM.EXE` plus a test `SYSTEM.BAT` in the root (ACC-4). The `BOOT.EXE` route stays for the folder profile (tdd-storage §5). Cross-checked against real disks instead of MAME booting the built image: the owner's MAME-pack disk boots in both emulators with the same reads (roadmap §9) |
| The exact on-disk layout DSS `FORMAT`/`BOOT` produce on an HDD | confidence that the built image matches the real thing | read `utils/BOOT/SYS.ASM:130-140` (done: loader at LBA 1, 3 sectors, device `#80`/`#81`); ask the community (zx-pk.ru Sprinter threads) for a real image |
| ISA card behavior (which cards exist, memory window) | S6 | MAME leaves ISA memory unimplemented (`sprinter.cpp:43`, `:1256`); out of scope beyond the register model until a card is chosen |
| Accelerator edge cases (interrupts during a block, "DooM" stretch modes) | S5 accuracy | PLD `ACCELER.TDF` is the ground truth; compare with MAME in differential tests |
| Tolik-Trek MAME fork differences | may fix MAME bugs | review SP-MAME before S2/S5 |
| The zx-pk.ru ZXMAK2 HDD thread ([t=16830 p.22](https://zx-pk.ru/printthread.php?t=16830&pp=40&page=22)) | notes on ZXMAK2 IDE and images | returned HTTP 403 on 2026-09-28; read in a browser later |

## 5. ROM provisioning (S0)

Every known BIOS build (2.13-3.07 beta), the community builds kept in `data/rom/sprinter/` and how to track new ones: [bios-versions.md](bios-versions.md).

| File | Size | CRC32 (MAME) | Notes |
|---|---|---|---|
| `sp2k-3.04.rom` (default) | 262 144 | `1729cb5c` | BIOS 3.04, 17.06.2003, the last Peters Plus build; **the default** (review round 1, Q1); tests run on 3.04 and 3.06 |
| `sp2k-3.06.rom` | 262 144 | `187f4382` | community BIOS 3.06, 25.06.2025 (Tolik-Trek); selectable |
| `sp2k-3.05.rom`, `3.03`, `3.00`, `2.17`, `2.13` | 262 144 each | see `sprinter.cpp:2030-2047` | regression sweep |

- **Where the exact 3.04 image comes from.** The board repository HW-2000
  ([zxgit.org/Sprinter/2000](https://zxgit.org/Sprinter/2000)) holds `fw/bios/sp2k-3.04.253.bin`:
  262 144 bytes, CRC32 `1729cb5c` (the MAME value), built 17.06.2003; its readme says this build
  was never officially released. Next to it: 3.00 (CRC `0d0aa5d4`) and 3.03 (CRC `88e3e23b`) with
  `BIOS_REV.txt` and `SETUP_REV.txt` (change logs).
- **The ZXMAK2 `SP_304.BIN` is the same build, 5 bytes apart.** Its CRC32 is `a3970620`. A byte
  compare with `sp2k-3.04.253.bin` shows exactly 5 different bytes: the 4-byte checksum at page 0
  offset 4-7, and page 8 offset 5 (`#07` in MAME's image, `#24` in ZXMAK2's), which is the
  `ROM_NUMBER` / board-id byte. It is most likely the variant with a changed BIOS id published in
  the zx-pk.ru thread 31520 for the game "Thunder in the Deep". the first commit of BIOS-TT (`0271ac3`)
  carries the same `a3970620` image as `src/bin/_SPRIN.BIN`. So neither dump is bad: they are two
  variants of one build. The design uses the MAME image and records the ZXMAK2 one as a known variant.
- **Layout of the 3.04 image** (16 pages of 16 KB, found by inspecting the file):

  | ROM page | Contents |
  |---|---|
  | 0 | ROM / SETUP |
  | 1-7 | empty (`#FF`) |
  | 8 | the BIOS proper ("EXP", holds the string `Sprinter BIOS: ver 3.04`) |
  | 9-`#B` | empty (`#FF`) |
  | `#C`-`#F` | the PLD loader and the bitstream (the BIOS-PP bitstream `ALTERA/SP2K_304.BIN` sits at file offset `#30100`) |

  Unlike the newer community builds (BIOS-TT `bios/mem_map.txt`: pages `#10`-`#1F`, with Spectrum
  ROM images, a logo and a recovery ROM disk), the 3.04 image holds **no Spectrum ROM images**.
- **No public source of 3.04 exists.** BIOS-PP (`1273243`) has only the page-0 sources (SETUP,
  EXTENDED, the drivers, up to Setup 2.41/253) and no EXP (page 8). The closest commented source is
  the first commit of BIOS-TT, `0271ac3` (2023-06-12), `src/bios/exp/EXP.asm`, headed "EXPANSION VER
  3.00"; it is already modified (new API, DCP configuration), its origin is unknown, and it is
  **not likely** to match 3.04 byte for byte. `_BIOS.REV` suggests that the changes after 2.17 are
  mostly PLD and video-RAM work (an inference, **unverified**).
- **S0 disassembles ROM pages 8 and 0 of 3.04** and cross-checks them against BIOS-TT `0271ac3`. The
  disassembly goes to `docs/disasm/rom/sprinter/` (next to `docs/disasm/rom/scorpion/`); symbol and
  label files go to `data/symbols/sprinter/` (repository rule: disassemblies under `docs/disasm/`,
  symbol files in `data/` subfolders).
- Placement follows the repo's ROM rules: `data/rom/sprinter/sp2k-3.04.rom` plus an entry in
  `data/rom/README-ROMS.md`; licensing: treated like the other third-party ROMs in `data/rom`.
- **Done in S0 (2026-10-01).** `data/rom/sprinter/sp2k-3.04.rom` + README-ROMS entry + the ROM
  signature catalog (`rom.cpp`: SHA-256 of pages 0, 8, 12 and of the ZXMAK2 variant's pages 0, 8);
  the listings in [docs/disasm/rom/sprinter/](../../disasm/rom/sprinter/README.md) and the symbol
  files in `data/symbols/sprinter/`. Facts found on the way: page 0 also holds SETUP packed with
  Hrust 1.x (13 893 bytes at `#8000`), an ATAPI CD-ROM driver (no CD boot), disk subsystem 2.53;
  the packed port table in page 8 equals BIOS-TT `0271ac3` `old_files/DCP_PAGE.bin`. `sp2k-3.06.rom`
  (CRC `187f4382`) was not found publicly; it stays to be added from the MAME set.

## 6. Licensing notes

| Source | License | Consequence |
|---|---|---|
| MAME `sprinter.cpp` | BSD-3-Clause | may be read and ported with attribution |
| ZXMAK2, Sprinter200x PLD | GPL-3.0 | read for behavior; no code copied |
| SprintEm | GPL-2.0 | read for behavior; sample programs used as test data only after checking each |
| BIOS-TT, Shared_Includes | no license file | treated as MIT (project rule for unlicensed sources) |
| DSS sources and release, BIOS-PP | "believed public domain" (repo READMEs) | release binaries usable as test fixtures; recorded in `testdata/NOTICE.md` |
| BIOS ROM images, DSS 1.62 floppy | not stated | third-party test material, listed in `testdata/NOTICE.md` like the other fixtures |
