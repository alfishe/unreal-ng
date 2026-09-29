# ctprobe on FUSE, headless

Runs the contention probe ([`../../contention/ctprobe`](../../contention/ctprobe/README.md)) on FUSE, the Free
Unix Spectrum Emulator (<https://fuse-emulator.sourceforge.net/>), on all seven machines, without a window and
without sound. It follows the contract in [`../README.md`](../README.md).

```
FUSE_DIR=<fuse source tree> ./run.sh                 # all seven machines
FUSE_DIR=<fuse source tree> ./run.sh 48k plus3       # only these
FUSE_BIN=<a patched fuse binary> ./run.sh            # skip the build
```

Needs a C compiler, `make`, `patch`, `pkg-config`, python3 and libspectrum (on macOS: `brew install libspectrum`,
which brings glib and libgcrypt). Tested with FUSE 1.6.0 and libspectrum 1.5.0.

Output goes to `../out/fuse/`: `<machine>.bin` (the memory dump), `<machine>.log` (what FUSE printed, with the
frame the program finished at), `<machine>.screen.txt` (the final screen as text), `<machine>.compare.txt` and
`<machine>.result`, plus `build.log`.

## How it works

FUSE has no scripting interface, so the runner builds its own copy:

- `run.sh` copies `FUSE_DIR` to `build/<folder name>/` (the source tree itself is not touched), applies
  `coemu-frame-hook.patch`, and configures it with the **null user interface** (`--with-null-ui`: no window,
  no input), the null audio driver, and no libxml2 or joystick. It rebuilds when the patch is newer than the
  binary.
- `coemu-frame-hook.patch` changes `spectrum.c` only: at the end of every frame it checks a few environment
  variables. With `COEMU_DONE_ADDR` unset, FUSE behaves exactly as before. With it set, it waits for the byte
  there to become 1, writes `COEMU_DUMP_FROM`..`COEMU_DUMP_TO - 1` to `COEMU_DUMP_FILE`, runs
  `COEMU_SCREEN_FRAMES` (default 100) more frames so the report is on screen, writes the shown screen to
  `COEMU_SCREEN_FILE`, and exits. After `COEMU_MAX_FRAMES` frames it writes the screen and exits with status 1.
  It reads memory without side effects and changes nothing in the emulation.
- `screen-text.py` turns the screen into text, matching each cell against the 48K ROM font.

## Settings

FUSE runs with its stock settings for each machine, with these exceptions:

| Setting | Why |
|:--|:--|
| `--no-sound`, `--speed 100000` | Run as fast as possible; the program counts CPU clock ticks, not seconds |
| `--auto-load` | Load the program as a user would (see below) |
| `HOME` set to `out/fuse/home` | So a `~/.fuserc` of yours does not change the settings |

FUSE's stock settings include tape traps and fast loading, so the tape loads through the ROM's `LOAD` in
no time; that does not affect what the probe measures.

## How the program is loaded

| Machine | FUSE machine | How |
|:--|:--|:--|
| `48k` | `48` | `--tape`: FUSE's autoload types `LOAD ""` |
| `128k` | `128` | `--tape`: autoload picks **Tape Loader** in the 128K menu (128 BASIC) |
| `plus2` | `plus2` | as the 128K |
| `plus2a` | `plus2a` | `--tape`: autoload uses the +2A menu's loader (+3 BASIC) |
| `plus3` | `plus3` | `--tape`: autoload uses the +3 menu's loader, from tape (+3 BASIC) |
| `pentagon` | `pentagon` | `--betadisk ctprobe.trd`: autoload boots TR-DOS, which runs the disk's `boot` |
| `scorpion` | `scorpion` | skipped: FUSE's autoload does not boot a TR-DOS disk on its Scorpion (see below) |

**Pentagon and Scorpion ROMs.** FUSE's source ships only the Sinclair and Amstrad ROMs. The Pentagon and
Scorpion ROMs come from `FUSE_ROMS`, a folder with FUSE's file names (`128p-0.rom`, `128p-1.rom`,
`256s-0.rom`..`256s-3.rom`, `trdos.rom`), or else from this repository's `data/rom/`: `pentagon128k.rom`
(128 BASIC, 48 BASIC), `scorpion.rom` (128, 48, service monitor, TR-DOS) and `trdos.rom`, split into 16K pages.
They are put under FUSE's own file names into FUSE's working folder (`out/fuse/home`), where FUSE looks first.
They are not passed with `--rom-...` options: FUSE treats ROMs named that way as custom ROMs and turns autoload
off.

## Results (FUSE 1.6.0, 2026-09-29)

| Machine | Result |
|:--|:--|
| 48K, 128K, +2, Pentagon | ALL VALUES AS EXPECTED |
| +2A, +3 | 1 value wrong in 1 check: M1-03 got 4 where 5 is expected at T14489 |
| Scorpion | skipped |

- **+2A, +3**: T14489 is 128 ticks after the first contended tick, just past the last fetch group of the line.
  The +2A/+3 gate array still holds the CPU there for one tick (Rak's Timing Test photographed on real
  machines); FUSE, like every emulator checked, ends the window after 128 ticks.
- **Scorpion**: with `--betadisk`, FUSE's autoload ends in 48 BASIC ("(c) 1982 Sinclair Research Ltd") and
  the disk's `boot` is never started; the tape through the Scorpion menu gives "Tape loading error". The
  program never runs, so the runner reports the machine as skipped.
- The first run of the probe here (before it was fixed) found that its switch of a Scorpion to 3.5 MHz,
  `in a,(#1FFD)`, locked a 128K's paging: a real 128K (and FUSE) decode that read as `#7FFD` and latch the bus
  value. The probe now reads `#1FFD` only when it finds the CPU running fast.
