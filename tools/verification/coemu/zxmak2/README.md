# coemu runner: ZXMAK2, headless

Runs a test program (by default the contention probe,
[`../../contention/ctprobe`](../../contention/ctprobe/README.md)) on ZXMAK2's stock machines without its
GUI, following the [co-emulation contract](../README.md). ZXMAK2 is Alex Makeev's C# emulator
(<https://github.com/zxmak/ZXMAK2>), a .NET Framework / WinForms application.

```
ZXMAK2_DIR=<your ZXMAK2 checkout> ./run.sh             # every machine it has
ZXMAK2_DIR=<your ZXMAK2 checkout> ./run.sh scorpion    # these
ZXMAK2_BIN=<a built zxmak2-harness.dll> ./run.sh       # skip the build
```

Needs the .NET SDK 8 or later (`dotnet`; on macOS `brew install dotnet`). The first build downloads two NuGet
packages (`System.Drawing.Common`, `System.IO.Ports`). The build goes to `build/`, results to
`../out/zxmak2/`. A run of all machines takes about 2 minutes.

## Machines

Each harness machine is ZXMAK2's own stock machine of that kind: the `<Bus>` of that name in ZXMAK2's
`src/ZXMAK2/machines.config`, which is the list the GUI offers for a new machine. (Machine files such as
`MACHINES/*.VMZ` are not used: some of them name devices that no longer exist.) The program
is loaded the way a user would, as in the table. The keys are held 4 frames each, with 8 frames between them.

| Machine | ZXMAK2 machine | Loaded from | How it starts |
|:--|:--|:--|:--|
| `48k` | `ZX Spectrum 48` | `.tap` | `LOAD ""` typed in 48 BASIC, then Play |
| `128k` | `ZX Spectrum 128` | `.tap` | the 128 menu's Tape Loader, then Play |
| `plus2` | - | - | skipped: ZXMAK2 has no grey +2 |
| `plus2a` | - | - | skipped: ZXMAK2 has no +2A |
| `plus3` | `ZX Spectrum +3` | `.tap` | the +3 menu's Loader, then Play (ZXMAK2's +3 has no floppy controller, so the loader reads the tape) |
| `pentagon` | `PENTAGON 128K` | `.trd` in drive A | the 128 menu's TR-DOS item, then `RUN` |
| `scorpion` | `Scorpion ZS 256` | `.trd` in drive A | the Scorpion menu's "128 TR-DOS", then `RUN` |
| `profscorp` | `Scorpion ZS 256 PROF-ROM` | `.trd` in drive A | as the Scorpion, after the ProfROM's service monitor has tested the machine |
| `atm710` | `ATM Turbo 2+ [V7.10]` | `.trd` in drive A | the ATM BIOS menu's SPECTRUM 128, then as the Pentagon |
| `atm3` | `PENT EVO` | `.trd` in drive A | the EVO Reset Service: `Y` (see below), `S` (TR-DOS, here EVO-DOS), then `RUN` |
| `profi` | `PROFI+ 1024 [V5.XX]` | `.trd` in drive A | nothing to type: the Profi BIOS boots the disk in drive A by itself |

## Settings

Every device keeps ZXMAK2's defaults. Worth knowing:

- **The tape** is ZXMAK2's own tape player with its default "traps" on: when the 48K ROM's loader runs, a
  block is copied into memory at once instead of being played in real time. The loaded bytes are the same.
- **The ZX-Evo** starts with a fresh CMOS, as a new ZXMAK2 machine does. Its EVO Reset Service then has
  EVO-DOS's virtual drive (a TR-DOS drive kept on the SD card) on drive A, which hides the floppy in drive A,
  so the runner presses `Y` once to move the virtual drive to B. The service shows "Turbo Mode: 7.0", but
  ZXMAK2 runs the Evo at 3.5 MHz (the probe finds no faster clock).
- Device state files (the Evo's CMOS, the IDE settings) go to `../out/zxmak2/<machine>.cmos` etc. and are
  deleted before each run.

## How it works

- `engine/` has one .NET project per ZXMAK2 assembly the emulation needs (`ZXMAK2.Engine`, `.Engine.Cpu`,
  `.Hardware`, `.Hardware.Circuits`, `.Model.Disk`, `.Model.Tape`, `.Crc`, `.Host`, `.Host.Presentation`,
  `.Mvvm`, `ZipLib`). Each compiles that assembly's sources straight from `ZXMAK2_DIR`, unmodified, under the
  same assembly name (ZXMAK2 finds device types by name in `ZXMAK2.Hardware`). The only file left out of
  them is `ZXMAK2.Hardware/GeneralSoundDevice.cs`, which ZXMAK2's own project does not compile either.
- `engine/stubs/` replaces what needs a desktop, with the same API: `ZXMAK2.Logging` (log4net; here stderr),
  the service locator in `ZXMAK2.Dependency` (Unity; here the runner registers the PSG chip and a
  message service that prints, and no windows), `ZXMAK2.Resources` (the on-screen icons, GDI+ bitmaps; here
  none) and `ZXMAK2.Host`'s `IconDescriptor` (same, without the image).
- `harness/Program.cs` is the runner: it builds the machine from `machines.config` as the GUI does, opens
  the image through ZXMAK2's own loaders (File > Open), types the keys into ZXMAK2's keyboard device, runs
  frames instruction by instruction until the program's `DONE` byte is 1, then dumps `START`..`PROBEEND - 1`.
  If the CPU reaches `#0000` after the program has started, the program has crashed: the runner prints the
  last 48 instructions (address and frame T-state), stops, and the machine is reported as `error`.
  ZXMAK2's `ROMS.PAK`, `machines.config` and `Keyboard.config` are copied next to the build, as in
  ZXMAK2's own build output.
- The screen is written as text (`<machine>.screen.txt`, matched against the 48K ROM font), also just
  before the first key (`<machine>.boot.screen.txt`).

For finding the keys of a new machine the runner takes three environment variables: `ZXMAK2_SHOTS=300,600`
writes the rendered picture at those frames (`<machine>.f300.ppm`), `ZXMAK2_BOOT=<frames>` sets the frames
before the first key, and `ZXMAK2_KEYS` replaces the keys, e.g. `ZXMAK2_KEYS='CS+6,ENTER:150,R,ENTER'`
(`+` keys together, `*<frames>` how long it is held, `:<frames>` the wait after it). `ZXMAK2_LOG=1` prints
ZXMAK2's debug log.

## Results (ZXMAK2 4964327, .NET 10, 2026-09-29)

| Machine | Result |
|:--|:--|
| Scorpion, Scorpion ProfROM | ok since the probe's Even M1 mode (3952bdc8). Before it: error, the program crashed 8 frames after it started, while measuring the frame. ZXMAK2's Scorpion (`UlaScorpionYellow.cs`, `busRDM1`) moves every opcode fetch from `#4000`-`#FFFF` to an even T-state (Even M1). That made the probe's 23-tick delay loop take 24, so the measured frame length was wrong, the next interrupt came in the middle of a delay instead of at a `HALT`, and the return chain unwound to `#0000` (the same failure as MAME's `scorpio`, [../mame/README.md](../mame/README.md)). The probe now detects Even M1 and delays in 2-tick steps |
| 48K | 490 values wrong in 33 checks, all of them the expected row moved by 1 tick: ZXMAK2's stock 48K ULA is the "late" one |
| 128K | 560 values wrong in 40 checks, all 1 tick late, as the 48K |
| +3 | 85 values wrong in 10 checks: the contended banks are 1, 3, 5, 7 (as on the 128K) instead of 4, 5, 6, 7 (M1-P1/P3/P4/P6), the all-RAM layouts are contended wrong (M1-L0..L3), and M1-03 and the floating bus (P-02) differ |
| Pentagon | all as expected |
| ATM Turbo 2+ | all as expected |
| ZX-Evo | all as expected |
| Profi | all as expected |
