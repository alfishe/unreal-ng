# ctprobe on MAME, headless

Runs the program (by default the contention probe, [`../../contention/ctprobe`](../../contention/ctprobe/README.md))
on MAME's stock ZX Spectrum drivers without a window or sound, and dumps the probe's memory for
`ctprobe-compare.py`, which `run.sh` then runs on each dump. See [`../README.md`](../README.md) for the contract.

```
MAME_BIN=<your mame binary> ./run.sh                 # every harness machine
MAME_BIN=<your mame binary> ./run.sh 48k plus3       # only these
MAME_ROMPATH=<your MAME rompath> ./run.sh            # mame on PATH, your own ROM sets
```

Needs a MAME binary with the Spectrum drivers and Python 3. Output goes to `../out/mame/`: `<machine>.bin`
(the memory dump), `<machine>.log` (what MAME and the script printed), `<machine>.screen.txt` (the final screen
as text) and `<machine>.compare.txt`.

## Finding MAME

`MAME_BIN` (a path or a command name), else `mame` on `PATH`. If neither is there, every machine is `skipped`
and `run.sh` exits 3. A machine whose driver is not in that binary is `skipped` too.

A full MAME build is not needed. A build with only the Sinclair drivers takes a few minutes; from a MAME
source checkout:

```
make SUBTARGET=zx REGENIE=1 -j<cores> \
  SOURCES=src/mame/sinclair/spectrum.cpp,src/mame/sinclair/spec128.cpp,src/mame/sinclair/specpls3.cpp,src/mame/sinclair/pentagon.cpp,src/mame/sinclair/scorpion.cpp,src/mame/sinclair/atm.cpp,src/mame/sinclair/evo/pentevo.cpp
```

This leaves a binary named `zx` in the checkout's top folder; point `MAME_BIN` at it.

## Machines

| Machine | MAME driver | How the program is loaded |
|:--|:--|:--|
| `48k` | `spectrum` | `LOAD ""` typed (`J`, `SYMBOL SHIFT`+`P` twice, `ENTER`), then the tape's play button |
| `128k` | `spec128` | `ENTER` on the boot menu's "Tape Loader", then play (128 BASIC, paging open) |
| `plus2` | `specpls2` | as the 128K |
| `plus2a` | `specpl2a` | `ENTER` on the menu's "Loader", then play (+3 BASIC; the loader finds no disk and reads the tape) |
| `plus3` | `specpls3` | as the +2A |
| `pentagon` | `pentagon` | the `.trd` in drive A; cursor down four times (`CAPS SHIFT`+`6`) to the 128 menu's "TR-DOS", `ENTER`, then `RUN` at the `A>` prompt (`R`, `ENTER`) |
| `scorpion` | `scorpio` (ZS-256, ROM 2.92) | the `.trd` in drive A; `ENTER` on the menu's first entry, "128 TR-DOS", which runs the disk's `boot` by itself |
| `profscorp` | `scorpiontb` (ZS-256 TURBO+, ProfROM 4.01) | as the Scorpion (the same menu) |
| `atm710` | `atmtb2plus` (ATM Turbo 2+, BIOS 1.07.13) | the `.trd` in drive A; cursor down to the ATM BIOS menu's "TR-DOS 48", `ENTER`; TR-DOS runs `boot` by itself |
| `atm3` | `pentevo` (ZX Evolution BaseConf) | `skipped`: no ROM (see ROMs) |
| `profi` | `profi` | `skipped`: MAME's driver is marked not working; with unreal-ng's Profi ROM the screen stays black |

The first keys go in 150 frames after power-on (200 on the +2A, +3 and the Beta Disk machines), when the boot
menu or the `K` cursor is up; each later step waits for the previous one. Text goes through MAME's natural
keyboard. It has no cursor keys, so a cursor move holds `CAPS SHIFT` and `6` down for 5 frames through the
keyboard's input fields. The tape plays in real time through the ROM loader.

Settings are the driver's stock ones. MAME's own `mame.ini` and plugins are not read (`-noreadconfig
-noplugins`). The BIOS is the driver's default one when its ROMs are found. Otherwise it is the first BIOS
whose ROMs are all there, passed as `-bios` (see ROMs). The command line is

```
mame <driver> -bios <bios> -rompath <roms> -cass <program>.tap | -flop1 <program>.trd
     -video none -sound none -nothrottle -skip_gameinfo -noreadconfig -noplugins
     -cfg_directory build/run/cfg -nvram_directory build/run/nvram
     -seconds_to_run <MAX_FRAMES / 50 + 30> -autoboot_script coemu.lua
```

The ATM Turbo 2+ boots with its BIOS menu set to "TURBO ON" (7 MHz). That is the stock setting, so the probe
finds the CPU too fast, says so and stops; the result is `error`.

## How it works

- `coemu.lua` (run by `-autoboot_script`) counts frames, types the keys, presses play on the tape, and after
  that reads the `DONE` byte every frame. Once `DONE` has been 0 (the program has loaded) and then turns 1, it
  writes `START`..`PROBEEND - 1` to `<machine>.bin`, saves the screen and the ROM region to `build/run/`, and
  exits MAME. If `DONE` turns to anything else, the machine was reset and its memory cleared: the result is
  `error` at once. After `MAX_FRAMES` frames without either it prints `timeout` and exits; the result is `error`.
- `screentext.py` turns the saved screen into text with the Sinclair font found in the machine's ROM.
- The addresses come from the program's `.sym`; `run.sh` passes them to the script in the environment
  (`COEMU_DONE`, `COEMU_START`, `COEMU_END`, ...).

## ROMs

MAME needs its own ROM sets, under its own file names.

- `MAME_ROMPATH` set: used as it is, with each driver's default BIOS.
- Otherwise `romset.py` builds `build/roms/` from unreal-ng's own ROM files (`data/rom/`, `testdata/`). It asks
  the MAME binary which files each driver needs (`-listxml`: name, size, CRC32, BIOS, region, offset, for the
  driver and its devices). It cuts every file found into slices of those sizes at 16K steps (unreal-ng keeps a
  128K machine's two ROMs in one file) and writes each slice whose CRC32 matches under MAME's name.
  `build/romstatus.txt` gives each driver's BIOS or its missing files; a machine with missing ROMs is `skipped`
  with the names.

A driver can run without every file it lists. MAME stops when a required file is missing, but when a file is
there with other contents it only warns (`WRONG CHECKSUMS`) and runs. There is no BIOS or command-line option
that skips a ROM; `optional` and `nodump` entries are the only ones MAME does not require. So `romset.py`
writes a placeholder (zeros, the right size) for a file MAME loads but never uses:

- **Overwritten.** Later ROMs of the same set cover its whole range in the same region. MAME's Beta Disk
  device (`beta_disk`, used by the Pentagon, Scorpion, ATM and Profi drivers) loads 58 TR-DOS versions, one
  after the other, to offset 0 of one 16K region. Only the last one stays: `trd503.rom` (TR-DOS 5.03), which
  is unreal-ng's `trdos503.rom`. The other versions are placeholders.
- **Never read.** A region the driver's source never reads: the keyboard controller ROMs of the Scorpion
  (`scrpkey.rom`) and the ATM Turbo (`rf2ve3.rom`, `rfat710.rom`), and the ZX-Evo's AVR firmware
  (`zxevo_fw.bin`). They are listed in `romset.py`, checked against the MAME 0.289 sources.

With unreal-ng's ROMs:

| Driver | BIOS used | Same bytes as unreal-ng's | Placeholders |
|:--|:--|:--|:--|
| `spectrum` .. `specpls3` | default | the Sinclair and Amstrad ROMs | none |
| `pentagon` | `v1` (default) | `pentagon128k.rom` | 46 TR-DOS versions |
| `scorpio` | `v1` (ROM 2.92, split); the default `v3` (2.94) is not there | `scorpion.rom` | 46 TR-DOS versions, `scrpkey.rom` |
| `scorpiontb` | `v4.01_02` (ProfROM 4.01); the default `v4.43su` is not there | the first half of `scorp_prof401.rom` | 46 TR-DOS versions |
| `atmtb2plus` | `v1.07.13`; the default `v1.37` is not there | `atm2.rom` | 46 TR-DOS versions, 2 keyboard ROMs |
| `profi` | `v7` ("Power Of Sound Group"); the default `v1` is not there | `profi.rom` | 46 TR-DOS versions |
| `pentevo` | none | - | missing `zxevo_06002.rom`: MAME knows ERS 0.59.02 to 0.60.02, unreal-ng has ERS 0.60.05 FE (`zxevo-fe.rom`) and an older custom image, neither with a matching CRC |

## Results (MAME 0.289, source f43983b6, 2026-09-29)

| Machine | Result |
|:--|:--|
| 48K | ALL VALUES AS EXPECTED |
| 128K, +2 | 600 values wrong in 40 checks. In the first analysis every wrong value was the real machine's value 2 ticks earlier: MAME's 128K contends, and reads the floating bus, 2 ticks late |
| +2A, +3 | 523 values wrong in 38 checks. In the first analysis most were the real machine's values 4 ticks earlier (the gate array's waits 4 ticks late). The +3 layout checks and the port checks did not fit a shift: MAME holds the CPU on port accesses, which the gate array does not |
| Pentagon | ALL VALUES AS EXPECTED (from the `.trd`) |
| Scorpion | `error`: the machine resets while the probe runs. The engine's frame-length measurement (`FRAMETIME`) got no interrupt in the window it waits for and ran into its `RST 0`. MAME's `scorpiontb` with the same probe does not |
| Scorpion + ProfROM | ALL VALUES AS EXPECTED (`scorpiontb`, from the `.trd`) |
| ATM Turbo 2+ | `error`: the probe stops with "The CPU runs faster than 3.5 MHz" (the BIOS menu's stock "TURBO ON") |
| ZX-Evo | `skipped`: no ERS ROM MAME knows (see ROMs) |
| Profi | `skipped`: MAME's `profi` is marked not working. It is the Scorpion board with a Profi ROM and no Profi video, and with `-bios v7` the screen stays black, so there is no menu or prompt to type into |

The first run found a weakness of the probe: it took MAME's 128K, whose timing is off, for a +2A/+3 and ran the
+3 layout checks, which crash a 128K. The probe now tells the two apart by the largest wait (6 on the ULA, 7 on
the gate array), which holds whatever the offset.
