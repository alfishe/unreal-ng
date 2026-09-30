# coemu runner: Kozynax, headless

Runs a test program (by default the contention probe,
[`../../contention/ctprobe`](../../contention/ctprobe/README.md)) on Kozynax's stock machines without its
window, following the [co-emulation contract](../README.md). Kozynax (<https://github.com/kozynax/kozynax>) is
a cross-platform C# emulator built on ZXMAK2's emulation engine, with an SDL and a WinForms front end.

```
KOZYNAX_DIR=<your Kozynax checkout> ./run.sh             # every machine it has
KOZYNAX_DIR=<your Kozynax checkout> ./run.sh 48k plus3   # these
KOZYNAX_BIN=<a built kozynax-harness.dll> ./run.sh       # skip the build
```

Needs the .NET SDK 8 or later (`dotnet`; on macOS `brew install dotnet`). Kozynax targets .NET 8; the runner
also runs on a newer runtime when .NET 8 is not installed. The first build downloads Kozynax's NuGet packages.
The build goes to `build/` (at most 4 build jobs), results to `../out/kozynax/`. The Kozynax checkout is not
changed: the build's intermediate files go to `build/` too. A run of all machines takes about 7 minutes.

Without `KOZYNAX_DIR` or `KOZYNAX_BIN` (or without `dotnet`) every machine is `skipped` and the runner exits 3.

## Machines

Each harness machine is Kozynax's own stock machine of that kind: the `<Bus>` of that name in Kozynax's
`src/ZXMAK2/machines.config`, which is the list the program offers for a new machine. The program is loaded
the way a user would, as in the table. The keys are held 4 frames each, with 8 frames between them.

| Machine | Kozynax machine | Loaded from | How it starts |
|:--|:--|:--|:--|
| `48k` | `ZX Spectrum 48` | `.tap` | `LOAD ""` typed in 48 BASIC, then Play |
| `128k` | `ZX Spectrum 128` | `.tap` | the 128 menu's Tape Loader, then Play |
| `plus2` | - | - | skipped: Kozynax has no grey +2 (no machine uses the +2 ROMs) |
| `plus2a` | - | - | skipped: Kozynax has no +2A |
| `plus3` | `ZX Spectrum +3` | `.tap` | the +3 menu's Loader, then Play (Kozynax's +3 has no floppy controller, so the loader reads the tape) |
| `pentagon` | `PENTAGON 128K` | `.trd` in drive A | the 128 menu's TR-DOS item, then `RUN` |
| `scorpion` | `Scorpion ZS 256` | `.trd` in drive A | the Scorpion menu's "128 TR-DOS", then `RUN` |
| `profscorp` | `Scorpion ZS 256 PROF-ROM` | `.trd` in drive A | as the Scorpion, after the ProfROM's service monitor has tested the machine |
| `atm710` | `ATM Turbo 2+ [V7.10]` | `.trd` in drive A | the ATM BIOS menu's SPECTRUM 128, then as the Pentagon |
| `atm3` | `PENT EVO` | `.trd` in drive A | the EVO Reset Service: `Y` (see below), `S` (TR-DOS, here EVO-DOS), then `RUN` |
| `profi` | `PROFI+ 1024 [V5.XX]` | `.trd` in drive A | nothing to type: the Profi BIOS boots the disk in drive A by itself |

Kozynax has the same machine list as ZXMAK2, so the keys are the same as in the [ZXMAK2 runner](../zxmak2/README.md).

## Settings

Every device keeps Kozynax's defaults. Worth knowing:

- **The 48K and the 128K** are the "late" ULA models (`UlaSpectrum48`, `UlaSpectrum128`, named
  "[late model]"). Those are what the stock machines use. Kozynax also has the "early" models, which a user
  can pick in the machine settings; the runner does not change it.
- **The CPU** is Kozynax's default type (NEC NMOS Z80).
- **The tape** is the emulator's own tape player with its default "traps" on: when the 48K ROM's loader runs,
  a block is copied into memory at once instead of being played in real time. The loaded bytes are the same.
- **The ZX-Evo** starts with a fresh CMOS, as a new machine does. Its EVO Reset Service then has EVO-DOS's
  virtual drive (a TR-DOS drive kept on the SD card) on drive A, which hides the floppy in drive A, so the
  runner presses `Y` once to move the virtual drive to B. The Evo runs at 3.5 MHz.
- Device state files (the Evo's CMOS, the IDE settings) go to `../out/kozynax/<machine>.cmos` etc. and are
  deleted before each run. Nothing is written to Kozynax's settings folder in your home directory.

## How it works

- `harness/kozynax-harness.csproj` references Kozynax's own project `src/Kozynax.Sdl.csproj` from
  `KOZYNAX_DIR`, unmodified, and uses it as a library. Kozynax.Sdl is published as a self-contained single
  file; here it is built as an ordinary library (`SelfContained=false`), and the check that forbids
  referencing a self-contained program is switched off for the runner. Its ROM folder (`src/ZXMAK2/roms`) is
  copied next to the runner by Kozynax's own build rules; its `machines.config` is built into the DLL.
- `harness/Program.cs` is the runner, a copy of the ZXMAK2 runner's with Kozynax's small API differences:
  the machine list comes from the DLL, and the services the emulation needs (the PSG chip, messages to the
  user, questions answered "no") are registered the way Kozynax's own program does it. Messages go straight
  to the log: Kozynax's `ZXMAK2.Logger` has no log behind it. It builds the machine from `machines.config`
  as the program does, opens the image through Kozynax's own loaders (File > Open), types the keys into
  Kozynax's keyboard device, runs frames instruction by instruction until the program's `DONE` byte is 1,
  then dumps `START`..`PROBEEND - 1`. If the CPU reaches `#0000` after the program has started, the program
  has crashed: the runner prints the last 48 instructions (address and frame T-state), stops, and the
  machine is reported as `error`.
- The screen is written as text (`<machine>.screen.txt`, matched against the 48K ROM font), also just
  before the first key (`<machine>.boot.screen.txt`).

For finding the keys of a new machine the runner takes three environment variables: `KOZYNAX_SHOTS=300,600`
writes the rendered picture at those frames (`<machine>.f300.ppm`), `KOZYNAX_BOOT=<frames>` sets the frames
before the first key, and `KOZYNAX_KEYS` replaces the keys, e.g. `KOZYNAX_KEYS='CS+6,ENTER:150,R,ENTER'`
(`+` keys together, `*<frames>` how long it is held, `:<frames>` the wait after it).

## Results (Kozynax cd960758, .NET 10, 2026-09-30)

| Machine | Result |
|:--|:--|
| 48K | 490 values wrong in 33 checks, all of them the expected row moved by 1 tick: the stock 48K is the "late" ULA |
| 128K | 560 values wrong in 40 checks, all 1 tick late, as the 48K |
| +3 | 85 values wrong in 10 checks: the contended banks, the all-RAM layouts, the end of a line and the floating bus (below) |
| Pentagon | all as expected |
| Scorpion, Scorpion ProfROM | all as expected (Even M1, no attribute bus) |
| ATM Turbo 2+ | all as expected |
| ZX-Evo | all as expected |
| Profi | all as expected |

On the 48K, 128K and +3 the memory dumps are byte for byte the same as ZXMAK2's (4964327): Kozynax has not
changed the timing of these machines. What differs, in `../out/kozynax/<machine>.compare.txt`:

**48K and 128K: one tick late.** Every wrong row is the expected row moved one tick to the right. For
example, check M1-01 runs a `NOP` at `#4000` starting at T-state 14333, 14334, ... and measures how long it
takes:

```
  got   9   8   7  14  13  12  11  10 ...
  exp   8   7  14  13  12  11  10   9 ...
```

The long wait (14) starts at tick 14336 instead of 14335. The probe's expected values are for the "early"
ULA timing (see the probe README, "Check the machine settings first"). Kozynax's stock machines use the
"late" classes, which in `src/ZXMAK2.Hardware/Spectrum/UlaSpectrum48.cs` and `UlaSpectrum128.cs` are the
early ones with the first picture tick moved by one (`c_ulaFirstPaperTact += 1`). Picking the early model in
the machine settings should remove the difference; that is not a stock setting, so it was not run here.

**+3: four different things** (all from `src/ZXMAK2.Hardware/Spectrum/UlaPlus3.cs`):

- *Which pages wait (M1-P1, P3, P4, P6).* On a +2A/+3, RAM pages 4, 5, 6 and 7 are the slow ones; on a 128K
  it is pages 1, 3, 5 and 7. Kozynax's +3 uses the 128K rule (`(m_pageC000 & 1) != 0`): code in page 1 or 3
  at `#C000` waits, code in page 4 or 6 does not. Pages 5 and 7 are slow under both rules, so those checks
  pass.
- *The all-RAM layouts (M1-L0 .. L3).* The +3 can replace the ROM with RAM, in four fixed layouts of four RAM
  pages. The waits then follow the pages: layout 0 (pages 0, 1, 2, 3) never waits, layout 1 (pages 4, 5, 6,
  7) waits everywhere. Kozynax decides by the address (always at `#4000`-`#7FFF`, at `#C000` by the page
  number as above), not by the page, so all four layouts measure the same as the normal layout
  (`100 99 106 105 ...`), where the probe expects `99 99 99 ...` for layout 0 and larger values for 1 to 3.
- *The end of a line (M1-03).* The +3's gate array holds the CPU for one more tick after the last screen
  byte of a line. Kozynax's +3 uses the 48K's wait table (`1, 0, 7, 6, 5, 4, 3, 2`) without that tick, so
  one value reads 4 where 5 is expected. FUSE has the same difference.
- *The floating bus (P-02).* On a 48K or 128K, reading an unused port such as `#FF` while the picture is
  drawn gives the byte the screen hardware is reading at that moment. On a +2A/+3, port `#FF` reads `#FF`.
  Kozynax's +3 answers every port read with the screen byte, as the 48K does (`ReadPortAll` calls
  `ReadPortFF`), so the probe reads `16 64 17 65 ...` (its test pattern) where it expects `255`.

The [ZXMAK2 runner's README](../zxmak2/README.md) still reports a crash on the Scorpion: that result was
taken before the probe learned to measure a machine with Even M1 (the probe's own results table lists ZXMAK2's
Scorpion as all as expected). Kozynax's Scorpion code is the same as ZXMAK2's.
