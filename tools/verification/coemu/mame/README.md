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
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
mame <driver> -bios <bios> -rompath <roms> -cass <program>.tap | -flop1 <program>.trd
     -video none -sound none -window -nomaximize -nothrottle -skip_gameinfo -noreadconfig -noplugins
     -cfg_directory build/run/cfg -nvram_directory build/run/nvram
     -seconds_to_run <MAX_FRAMES / 50 + 30> -autoboot_script coemu.lua
```

`-video none` alone still lets the SDL build open a window, and with `-noreadconfig` MAME's default is full
screen. SDL's dummy drivers stop it opening any window or audio device; `-window` keeps it off the full screen
should a build ignore them.

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
| Scorpion | ALL VALUES AS EXPECTED since the probe handles Even M1 (commit 3952bdc8). The first run ended in `error` (the machine restarted), see [The Scorpion restart](#the-scorpion-restart-2026-09-29) |
| Scorpion + ProfROM | ALL VALUES AS EXPECTED (`scorpiontb`, from the `.trd`) |
| ATM Turbo 2+ | `error`: the probe stops with "The CPU runs faster than 3.5 MHz" (the BIOS menu's stock "TURBO ON") |
| ZX-Evo | `skipped`: no ERS ROM MAME knows (see ROMs) |
| Profi | `skipped`: MAME's `profi` is marked not working. It is the Scorpion board with a Profi ROM and no Profi video, and with `-bios v7` the screen stays black, so there is no menu or prompt to type into |

The first run found a weakness of the probe: it took MAME's 128K, whose timing is off, for a +2A/+3 and ran the
+3 layout checks, which crash a 128K. The probe now tells the two apart by the largest wait (6 on the ULA, 7 on
the gate array), which holds whatever the offset.

## The Scorpion restart (2026-09-29)

**What happened.** The first run on `scorpion` (`scorpio`), with the probe of commit ab1f927c (loaded at 40000,
`DONE` at #9C50), ended with "error: the machine reset while the program ran (disk)". `coemu.lua` saw `DONE`
turn 2 at frame 520, about 10 frames after the probe started. `scorpiontb` passed with the same probe.

**The cause: MAME's Even M1 and the measuring engine.** Nothing reset the machine. The probe jumped to address 0
itself, and the ROM's start routine then wiped the RAM.

1. MAME's `scorpio` has Even M1 turned on. `scorpion_state::machine_reset()` sets `m_is_m1_even = 1`
   (`src/mame/sinclair/scorpion.cpp`). With it on, the M1 handlers (`beta_neutral_r`, `beta_enable_r`,
   `beta_disable_r`) add one T-state to every opcode fetch that would start on an odd T-state.
   `scorpiontb_state::machine_reset()` sets `m_is_m1_even = 0`, which is why `scorpiontb` passed.
2. The engine's `DELAY` loop is `ADD HL,BC` (11 T) plus `JR C` (12 T), 23 T per pass. With Even M1 the fetch
   after the 11 T instruction waits one T-state, so each pass takes 24 T and every delay runs about 4% long.
3. `FRAMETIME` stage 3 (`FtStage3`) waits one frame minus a few T-states with two `DELAY` calls. It expects the
   next interrupt to arrive during the seven `INC E` after them. Because the delay ran long, the interrupt
   came about 2800 T-states early, inside the second `DELAY`. The IM2 handler drops the interrupted address
   and returns to the caller, as the engine intends. Here that caller was the `INC E` chain. No interrupt was
   left to stop the chain, so it ran through to its closing `RST 0`.
4. `RST 0` enters the 48 BASIC ROM at 0 (`DI / XOR A / LD DE,#FFFF / JP START`). Its RAM test fills memory with
   #02 from #FFFF downward. `DONE` became 2, and `coemu.lua` read that as a reset.

It was not the turbo switch. MAME's `scorpio` has no turbo (only `scorpiontb` maps the port reads that switch
it), `TurboOff` found the CPU at 3.5 MHz, and the probe read no port from the #1FFD / #7FFD group. It was
not a hang misreported as a reset either: a hang ends in `timeout`, not in `DONE = 2`.

**How it was confirmed.** The ab1f927c probe files (`.trd`, `.sym`, `.tap`, `-compare.py`) were copied out of git
into a scratch folder and run with `PROGRAM=<that folder>/ctprobe` on the same `mame-zx` binary (MAME 0.289,
source f43983b6). The result was the same "reset" at frame 520. A wrapper around `coemu.lua` added
`install_read_tap` taps on the engine's opcode addresses and a write tap on `DONE`:

```
fetch BB85 FtStage3                   SP=9C1E BC=90EE DE=0000   stage 3 starts, frame measured as #90EE + 32768
fetch BB90 FtStage3 INC E chain       HL=0AE5 DE=0000           entered from the IM2 handler, DELAY had ~2800 T left
fetch BB97 FtStage3 RST 0             DE=0007                   all seven INC E ran, no interrupt
fetch 0000 (reset vector)             (SP)=BB98                 return address: just after FtStage3's RST 0
write DONE=02 by PC=11DE              bytes at 0000: F3 AF 11 FF FF C3   the 48K ROM's RAM fill
```

`scorpiontb` (Even M1 off) ran the same files to ALL VALUES AS EXPECTED. The current probe passes on `scorpio`
("Machine: no contention, Even M1").

**What would bring it back.** Any probe that runs the original Bobrowski / Rak engine (`FRAMETIME`, `CODETIME`,
`DELAY` with odd-length steps) on a machine with Even M1: MAME's `scorpio`, unreal-ng's Scorpion, ZXMAK2,
or a real Scorpion. That includes an old probe build, and a probe whose Even M1 check (`IsEvenM1`, commit
a99126f5) is removed or misses. The engine can also end in `RST 0` from `CtTest` / `CtStage3` in the same way.
Since commit 3952bdc8 the probe detects Even M1 and switches to `DELAYE`, which uses only instructions
whose Even M1 lengths are known. The "503 values wrong" run in between (18:35, loaded at 36000) came from an
unfinished build of that work, before its corrections were in.

## Sprinter

The coemu runner does not use the Sprinter, but the Sprinter reference captures
([testdata/machines/sprinter/reference/](../../../../testdata/machines/sprinter/reference/README.md), scripts in
[tools/machines/sprinter/mame-capture/](../../../machines/sprinter/mame-capture/mame-capture.sh)) need MAME's `sprinter` driver, which the `zx` build
above leaves out. A build with it, under its own name so that it does not replace a `zx` binary, from a MAME 0.289
checkout (`f43983b6`; a `git clone --shared` of a local MAME tree avoids the download):

```
make SUBTARGET=zxsp REGENIE=1 -j<cores> \
  SOURCES=src/mame/sinclair/spectrum.cpp,src/mame/sinclair/spec128.cpp,src/mame/sinclair/specpls3.cpp,src/mame/sinclair/pentagon.cpp,src/mame/sinclair/scorpion.cpp,src/mame/sinclair/atm.cpp,src/mame/sinclair/evo/pentevo.cpp,src/mame/sinclair/sprinter.cpp
```

This leaves a binary named `zxsp` (58 drivers: the `zx` set plus `sprinter`); `zxsp -listfull sprinter` prints
`sprinter "Sprinter Sp2000"`. `sprinter.cpp` needs no other source in the list: the devices it uses (ATA, ISA,
PC keyboard, RS-232 mouse, Z84C15, DS12885, AY-3-8910, DAC) are found from its includes. On an M-series Mac
with 10 jobs, a fresh build took 28 minutes, most of it the shared libraries.

ROMs. `romset.py <zxsp> <rompath> data/rom -- sprinter` gives `sprinter ok bios=v3.04 placeholders=46`:
`sp2k-3.04.rom` is unreal-ng's `data/rom/sprinter/sp2k-3.04.rom` (CRC `1729cb5c`), the 46 placeholders are the
Beta Disk device's TR-DOS versions (the driver keeps it for its Spectrum mode). The other six BIOS versions are
not needed with `-bios v3.04`. MAME's default PC keyboard, the Microsoft Natural (`kb_ms_natural`), is an MCU
whose ROM `natural.bin` unreal-ng does not have; every keyboard option of the slot needs a ROM, so the runner
empties the slot (`-kbd ""`) and `romset.py` skips that device for `sprinter` (`EMPTY_SLOT_DEVICES`). With that,
`zxsp -verifyroms sprinter` still says "bad" (it checks every BIOS and the default keyboard), but
`zxsp sprinter -bios v3.04 -kbd "" -rompath <rompath>` runs: BIOS 3.04 configures the PLD, shows the logo and
stops at "PRESS <ENTER> TO REBOOT" with no media (frame 507, 10.4 s). The command line and the captures are in
the reference folder's README.
