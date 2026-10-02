# Sprinter Sp2000 reference captures (MAME, BIOS 3.04)

Reference data for the Sprinter work (design:
[docs/inprogress/2026-09-28-sprinter/](../../../../docs/inprogress/2026-09-28-sprinter/)), taken from
MAME's `sprinter` driver: the S0 items that needed MAME (roadmap S0, test plan "reference fixtures").
Made 2026-10-01.

- **MAME:** 0.289, source `f43983b6`, a subset build with the `sprinter` driver (binary `zxsp`, see
  [tools/verification/coemu/mame/README.md](../../../../tools/verification/coemu/mame/README.md#sprinter)).
- **BIOS:** 3.04 (`data/rom/sprinter/sp2k-3.04.rom`, CRC `1729cb5c`), `-bios v3.04`.
- **Machine:** stock settings, no media, no PC keyboard (`-kbd ""`: MAME's default Microsoft Natural
  keyboard needs `natural.bin`, which unreal-ng does not have), a fresh CMOS each run (MAME's default
  contents, so the BIOS reports "CMOS checksum error, install default values").
- **Scripts:** [tools/machines/sprinter/mame-capture/mame-capture.sh](../../../../tools/machines/sprinter/mame-capture/mame-capture.sh)
  runs MAME headless with
  [mame-capture.lua](../../../../tools/machines/sprinter/mame-capture/mame-capture.lua) as `-autoboot_script`.

## How to make them again

```
cd tools/machines/sprinter/mame-capture
export MAME_BIN=<path to the zxsp binary> SPC_OUT=../../../testdata/machines/sprinter/reference
./mame-capture.sh boot SPC_END=520 SPC_SNAP_AT=60:logo,507:boot-screen SPC_DUMP_AT=507
./mame-capture.sh loader SPC_END=600
./mame-capture.sh sync SPC_END=600 SPC_SYNC_AT=520
./mame-capture.sh palette SPC_END=200
```

The floppy run (S3a, 2026-10-01; `SPC_FLOP2` puts the image into drive B, a 3.5" HD drive):
`./mame-capture.sh boot SPC_FLOP2=../../../../testdata/machines/sprinter/dss_1_62_92.img SPC_CODES=10-17 SPC_PORTS=3000000 SPC_END=2500`,
then the rows with code `#15` removed (245 765 polls of port `#FF`) give `fdc-probe.csv`.

The hard disk runs (S3b, 2026-10-02) boot the owner's MAME-pack system disk (`sp_hdd_sys.chd`, DSS 1.71.57; not
in the repo, see [materials.md](../../../../docs/inprogress/2026-09-28-sprinter/materials.md) §3) on the primary
master, with MAME's own BIOS images (`MAME_ROMPATH` = the pack's `roms/`, which holds MAME's `sprinter.zip`):
`./mame-capture.sh boot SPC_BIOS=v3.06 SPC_HARD1=<sp_hdd_sys.chd> SPC_CODES=20-2B SPC_PORTS=300000 SPC_END=700 "SPC_WAIT_TEXT=Estex DSS" SPC_PORTS_FILE=ide-ports.csv`
(and `SPC_BIOS=v3.04 ... SPC_END=900 SPC_WAIT_TEXT=Fatal`); the events, the screen at the milestone and the ATA
command list of the IDE trace give `hdd-boot-306.txt` / `hdd-boot-304.txt`.

The boot mode can also trace one range of internal codes over the whole boot instead of the first
10 000 accesses, e.g. the IDE (Z84C15 ports are always included):
`./mame-capture.sh boot SPC_END=520 SPC_PORTS=200000 SPC_CODES=20-29 SPC_PORTS_FILE=ide.csv`
(not checked in; the result is summarized in hardware-reference §9.1).

The script builds a rompath under `tools/machines/sprinter/mame-capture/build/` (git-ignored) with the coemu
`romset.py`, and runs

```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
zxsp sprinter -bios v3.04 -kbd "" -rompath <roms> \
     -video none -sound none -window -nomaximize -nothrottle -skip_gameinfo -noreadconfig -noplugins \
     -cfg_directory build/run/cfg -nvram_directory build/run/nvram -snapshot_directory <out> -snapview native \
     -seconds_to_run <SPC_END / 48 + 10> -autoboot_script mame-capture.lua
```

Each mode takes a few seconds. Two runs give the same files, except the CMOS clock line of
`boot-screen.png` (MAME's RTC starts from the host clock).

Times are emulated time from power-on. "Frame" counts MAME's 20.48 ms frames (320 lines x 224 T at
3.5 MHz); frame `n` ends at `n x 20.48 ms`. "t35" = time x 3 500 000, i.e. T-states of a 3.5 MHz clock,
whatever the turbo state (the BIOS runs in turbo, 21 MHz, from the configuration on).

MAME runs the autoboot script again after each soft reset, and the driver soft-resets itself when its
configuration shortcut ends; the script only works in its first run (power-on, t = 0).

## Files

| File | What |
|:--|:--|
| `page40.bin` | RAM page `#40` (the port-decoder table, 16 KB) at frame 507, the boot screen |
| `logo.png` | the logo frame, frame 60 (1.2288 s) |
| `boot-screen.png` | the final boot screen with no media, frame 507 (10.383 s) |
| `ports.csv` | the first 10 000 port accesses from power-on |
| `loader.txt` | the runtime count of the PLD loader's memory writes |
| `int.csv` | INT positions for the BIOS `FN_SYNC` (`#F2`) modes |
| `events.txt`, `events-sync.txt` | the milestones of the `boot` and `sync` runs |
| `palette.csv` | the sum of the palette bytes in video RAM, per frame (rows where it changed) |
| `fdc-probe.csv` | the floppy controller accesses (codes `#10-#17`) of a boot with the DSS 1.62 floppy in drive B, without the `#15` status polls |
| `hdd-boot-306.txt` | BIOS 3.06 with the DSS 1.71.57 hard disk on the primary master: events, the screen at the DSS banner (frame 385, 7.885 s), the ATA commands of the boot |
| `hdd-boot-304.txt` | the same disk on BIOS 3.04: the DSS loader reads SYSTEM.DOS, then DSS 1.71 stops with "Fatal error" (frame 475, 9.728 s) |
| `mame-acctest-306.png` | the accelerator test `TESTS\ACCTEST.EXE` after it drew its 64 x 64 picture, frame 1500 (S5) |
| `mame-fn115-hdd-306.png` | Flex Navigator 1.15 from the HDD system disk, frame 1500 (S5) |

### `page40.bin`: the port table after POST

CRC32 `b7f09600`: **byte for byte the table `tools/machines/sprinter/dcp-table/dcp-table.py --rom` unpacks statically
from the ROM** (`--compare-page`: 0 bytes differ). The table is the same at the first port read that
opens the decoder (0.677 s) and at the boot screen (10.383 s): with no media and no SETUP, nothing
calls `DCP_CONFIG` (`#F4`) or patches the table (SETUP's `ApplyScreenPosition`). CNF = `#05` (map 0),
`#7FFD` = `#10`, DOS off at the dump.

### `logo.png`, `boot-screen.png`: the boot screens

Snapshots of MAME's native screen (736 x 288: 640 x 256 plus MAME's border).

| Frame | Time | Screen |
|:--|:--|:--|
| 1-55 | 0-1.13 s | black |
| 56 | 1.147 s | cyan (palette set-up) |
| 59 | 1.208 s | logo, palette half written |
| **60** | **1.229 s** | **the logo at full brightness**, BIOS version, "CMOS checksum error" (`logo.png`) |
| 61-186 | 1.25-3.81 s | the logo fades out, one step per frame |
| 191-471 | 3.91-9.65 s | configuration table, IDE detection ("None" for both units) |
| **507** | **10.383 s** | "Start from Hard disk...fail / Alternative Start from Diskette...fail / PRESS <ENTER> TO REBOOT, <ESC> TO CANCEL" (`boot-screen.png`), unchanged from then on |

### `ports.csv`: the first 10 000 port accesses

Columns: `n, frame, time_us, dir (R/W), port, value, pc, phase, index, code`.

- `phase`: `loader` while the PLD is unconfigured (MAME's `m_conf_loading`), `closed` until the first
  port read (MAME's `m_starting`: writes are ignored), `open` afterwards.
- `index`: the byte index into page `#40` that MAME builds for the access (`sprinter.cpp` `dcp_r` /
  `dcp_w`: CNF, `#7FFD` bit 5, DOS, read/write, A15, A14, A6, A5, A13, A7, A2-A0); `code`: the byte
  there at the time of the access, the internal device code (hardware reference §4.3). `z84` = a
  Z84C15 on-chip port (8-bit decoded: `#10-#13`, `#18-#1F`, `#EE`, `#EF`, `#F0`, `#F1`, `#F4`), which the
  table never sees. Before the table is filled (`closed`), `code` is whatever page `#40` holds.

The first 11 accesses are the loader's (ROM page `#C`, `OUT`s to the Z84C15 system registers, SIO A and
PIO); the BIOS starts at 16.6 ms after MAME's configuration shortcut (see below). The decoder opens at
0.677 s (`IN A,(#E2)` at page 8 `#0CD8`); the 10 000th access is at 0.742 s. 593 reads, 9 407 writes;
the open accesses are mostly `#C4` (PORT_Y, 8 871: the screen clear), `#F0` (window 3 page, 533), `#88`
(Covox, 256) and code `#00` (no port, 256).

### `loader.txt`: the PLD loader's writes (Q4)

MAME's driver ends the configuration after **4 096** writes and soft-resets the CPU
(`sprinter.cpp` `bootstrap_w`). For this capture the script holds MAME's write counter at 0, so the ROM
loader runs through the whole bitstream, counts every CPU memory write while the PLD is unconfigured,
and releases the counter once the source pointer HL has passed `#E84E` by 256 bytes.

- **Writes before the stream: 0. Writes with HL in `#0100-#E84E`: 473 720 = 59 215 x 8**, the static
  figure of tdd-ports-memory §6. All writes go to `#FE00-#FEFF`; the first at PC `#0089` with HL =
  `#0100`, DE = `#FE00`.
- The last bitstream write is at 6 691 665 T, **1.912 s at 3.5 MHz** (MAME's clock while loading): the
  loop takes 113 T per byte (`LD A,(HL)` 7, 8 x `LD (DE),A` 56, 7 x `RRCA` 28, `INC E` 4, `INC HL` 6,
  `JR` 12), 59 215 x 113 = 6 691 295 T, plus the set-up (the first write is at 406 T).
- The D0 of the writes rebuild the ROM bytes exactly from `#0100` to `#3FFF`. From `#4000` they differ:
  while loading, MAME maps only ROM page `#C` (window 0) and not pages `#D-#F`, since its shortcut needs
  only the first 512 bytes. So MAME confirms the count and the bit order, not the data past `#3FFF`.
- What MAME cannot show: when CONF_DONE rises and how many clocks the PLD takes before it resets the
  CPU (MAME has no PLD model). That stays for S1 / real hardware.

### `int.csv`: INT positions for the `FN_SYNC` modes

At frame 520 (the boot screen, IM 2, I = `#80`, turbo on) the script calls BIOS function `#F2` with A =
1 (Scorpion), 2 (Pentagon), 3 (Spectrum) and 0 (from the system variable), each through a stub below
the stack (`LD A,n : LD C,#F2 : RST #18`, registers saved; `RST #18` is the BIOS call from system mode),
and measures 5 frames after each. BIOS 3.04 returns with carry set even on success (the bug BIOS-TT
`doc/changes.txt` records as fixed later). `FN_SYNC` also blanks the screen (it rewrites the mode
table), so the screens after the calls are black.

MAME models the INT from the mode table (`sprinter.cpp` `update_int`): the INT starts on the 8th line
of the square after a run of "blank + INT" squares (mode byte `& #FD = #FD`) and lasts 32 clocks of
3.5 MHz. Rows:

- `model`: MAME's INT list recomputed from video RAM the way `update_int` does it: MAME screen line
  (0 = the first line of MAME's frame; paper lines 16-271), pixel x (14 MHz pixels, 4 per T), and
  `t35_in_frame` = line x 224 + x / 4.
- `ack`: the first opcode fetch of the interrupt routine (`#8101`), measured in the same frame
  coordinates: 6-11 T after the modeled edge (the instruction in progress at 21 MHz plus the
  acknowledge cycle).

| Mode | INT (MAME line, x) | T in MAME's frame | One INT per frame |
|:--|:--|:--|:--|
| as booted (cold start, default CMOS) | 271, 768 | 60 896 | yes |
| Scorpion (A = 1) | 271, 768 | 60 896 | yes |
| Pentagon (A = 2) | 287, 768 | 64 480 | yes |
| Spectrum (A = 3) | 295, 768 | 66 272 | yes |
| from the system variable (A = 0, after A = 3) | 295, 768 | 66 272 | yes |

So BIOS 3.04 cold-starts with the Scorpion position (the "Pentagon at cold start" of BIOS-TT
`changes.txt` is a later change). Pentagon is 16 lines (3 584 T) after Scorpion, Spectrum 8 lines
(1 792 T) after Pentagon; all three at the same horizontal position. Only the 320-line frame was
measured (BIOS 3.04's `FN_SYNC` has no frame-height option; the 312-line frame is code `#2D`).

### `palette.csv`: the logo palette, frame by frame

Columns: `frame, palette_sum`; a row only when the sum changed. The sum is over the palette bytes of
video RAM: the last 32 bytes (`#3E0-#3FF`) of each of the 256 lines of 1 KB (MAME `vram_w`). It shows
the logo without a renderer: the palette is written in frames 55-57 (the exact frames move by one
between runs, with the CMOS clock MAME takes from the host), is full at **frame 58** (302 720), and
fades one step per frame from frame 59 to frame 186 (297 696). `logo.png` (frame 60) is sum 302 548.
unreal-ng gives the same sums from frame 58 on (`SprinterReference_Test`).

### `fdc-probe.csv`: the BIOS floppy probe (S3a)

The DSS 1.62 floppy (`dss_1_62_92.img`, 1.44 MB) in drive B, everything else as above. A blank CMOS boots
the IDE master, then the alternative device, floppy B (SETUP's default CMOS `#10` = `#12`). Columns as
`ports.csv`.

- Frame 33: the BIOS resets the WD1793 (`OUT (#FF),#1C`, `#3C`, command `#00`).
- Frame 471 (9.655 s): `FddProbeDensity` (ROM page 0 `#0669`) selects drive B (`OUT (#FF),#3D`), sets 720 KB
  (`OUT (#01BD),#01`), seeks (`#18`) and issues READ ADDRESS (`#C0`), then polls port `#FF` (code `#15`)
  for `#F000` loops: **175.55 ms** at 21 MHz. It times out, flips the latch (`#21BD`, `#01BD`, ...) and
  issues `#C0` again, four tries in all (frames 471, 479, 488, 497), then FORCE INTERRUPT (`#D0`, frame 505)
  and "Alternative Start from Diskette...fail".
- **MAME never reads the HD disk**: its WD1793 sets the PLL clock when a command starts and defers a command
  written while a search runs, so the 2 MHz clock (`set_clock_scale`) never reaches the running READ
  ADDRESS. On the board the data separator is outside the chip and switches at once. unreal-ng issues the
  same accesses with the same 175.55 ms spacing up to the first flip, then finds the ID and boots DSS
  (Sprinter roadmap §8); MAME gives no time-to-prompt reference for the floppy boot.

### `hdd-boot-306.txt`, `hdd-boot-304.txt`: DSS from a hard disk (S3b)

MAME's default IDE slots hold a hard disk (here with the image) on the primary master, an ATAPI CD unit on
the primary slave and image-less hard disks on the secondary channel (`sprinter.cpp:1966-1968`), so the BIOS
detects "MAME Virtual CDROM" and "None" quickly; on the board (and in unreal-ng) an empty channel floats and
the BIOS waits for BSY (hardware-reference §9.1, tdd-storage §3.4).

- **BIOS 3.06**: IDENTIFY and INITIALIZE DEVICE PARAMETERS on the master at frame 175, then 200 frames of
  packet commands to the CD unit, the secondary channel at frame 375, the boot at 376: LBA 1 (the loader),
  LBA 2-4, the MBR, the partition boot sector (63), the FAT (176), the root (432) and SYSTEM.DOS (448 x 32,
  480 x 3), then DSS reads on its own. The banner "Estex DSS version 1.71.57. Shell version 1.2.522." is at
  frame 385. unreal-ng reads the same LBAs in the same order (`SprinterBoot_Test.RealHdd_Dss171BootsFromTheMamePackImage`).
- **BIOS 3.04**: the same loader reads up to SYSTEM.DOS (frames 472-474), then SYSTEM.DOS's start-up fails and
  the loader prints "Fatal error! Press RESET to restart." (frame 475). The DSS 1.71 floppy stops the same
  way on BIOS 3.04: DSS 1.71 needs a newer BIOS (bios-versions.md). unreal-ng shows the same screen.
### `mame-acctest-306.png`, `mame-fn115-hdd-306.png`: the accelerator (S5)

MAME cannot boot the HD floppy (above), so these two run from the owner's MAME pack: BIOS 3.06
(`-bios v3.06`, ROMs from the pack's `roms/` folder) and the HDD system disk `sp-hdd-sys.chd` on
`-hard1`, through `mame-capture.lua` (mode `boot`, `SPC_SNAP_EVERY=100`, `SPC_END=1500`):

- `mame-fn115-hdd-306.png`: the disk as it is; its `SYSTEM.BAT` ends with `fn`.
- `mame-acctest-306.png`: a copy of the raw image (`sp_hdd_sys.img`) whose `SYSTEM.BAT` is
  `@echo off`, `set PATH=...`, `ver`, `cd tests`, `acctest` (mtools at offset 32 256), converted with
  `chdman createhd`.

unreal-ng's tests run the same programs from the DSS 1.62 floppy with BIOS 3.04
(`SprinterBoot_Test.Dss162_AccTestCopiesItsPictureWithTheAccelerator`,
`..._FlexNavigatorDrawsWithTheAccelerator`). The ACCTEST picture (display pixels x 48-175, y 16-79) is
identical; the Flex Navigator F-key bar has the same pixels with one color mapped (light gray 170 here,
192 in FN 1.15). See `docs/inprogress/2026-09-28-sprinter/s5-accelerator-outcome.md`.
