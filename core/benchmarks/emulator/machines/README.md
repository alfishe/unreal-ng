# Sprinter demo benchmarks (`sprinter_demo_benchmark.cpp`)

`BM_SprinterDemo_<demo>_{Bare,Shipped}` measures the host CPU time of one emulated frame of the Sprinter under a
real heavy load: a demo from the DSS system disk. The other host-frame benchmarks
(`BM_HostFrame_*`, `memory/hostbusoverlay_benchmark.cpp`) are different: their Sprinter case is the BIOS logo,
where the CPU sits in `HALT` most of the time and the sound cards are idle. None of the Sprinter fast paths shows
there. The design and the history of those fast paths:
[sprinter-cpu-and-peripherals.md §8](../../../../docs/emulator/design/core/sprinter-cpu-and-peripherals.md).

## Running

```bash
tools/build/build.sh core-benchmarks
UNREAL_SPRINTER_HDD=/path/to/sp_hdd_sys.img \
    cmake-build-agent-release/bin/core-benchmarks --benchmark_filter=BM_SprinterDemo 2>&1 | grep '^BM_'
```

- **The disk.** `UNREAL_SPRINTER_HDD` is the raw `sp_hdd_sys.img` of the MAME pack: the `sp_hdd_sys.chd`
  converted by `chdman extracthd`. It is not in the repository. Without it every case reports
  `ERROR OCCURRED: 'UNREAL_SPRINTER_HDD ... not set'` and is skipped. The same variable serves the Sprinter disk
  tests ([environment-variables.md](../../../../docs/emulator/environment-variables.md)).
- **The DNTBLINK build.** The disk's own DNTBLINK is measured. The profile of 2026-10-09
  (sprinter-cpu-and-peripherals.md §8.1) used the owner's `dont_blink_test1` build (a local
  `testdata/machines/sprinter/demo/` folder, not committed), copied onto a copy of the image. Its numbers are close to these, but they are not the same workload.
- **The log.** The emulator is created with `LogNone`, but `EmulatorManager` still prints its `Info:` lines.
  `grep '^BM_'` keeps the results.
- **The build.** The benchmarks need one CMake configure with `-DBENCHMARKS=ON` in the build folder; after that
  `build.sh` keeps the setting. Build them in Release: a Debug build measures something else.

## What one case does

1. It creates `SPRINTER` with 4 MB of RAM.
   - **Bare**: the config override takes the sound cards out (no GS, no MoonSound, one AY). This is what the core
     tests run (`SoundCardScope` policy).
   - **Shipped**: the shipped config's cards, NeoGS and AY. The label says `(NeoGS)` when the card is there. If a
     changed config drops it, the label shows that.
2. It fixes the CMOS clock at 2026-01-01 12:00:30 UTC (`SetFixedTime`) and sets `fast_start`: every run boots the
   same way.
3. It inserts the disk as `ide0.master` in **session** mode. Writes stay in memory and the image file is never
   changed.
4. It rewrites the session copy of `SYSTEM.BAT` with `FatInPlace`
   (`core/src/emulator/io/storage/fat/fatinplace.h`). The new text is `cd \demos\<name>`, then `<name>`, then a
   `rem` padded with spaces to the file's old length. The file's size and cluster chain stay as they were. This
   is the same start as `SprinterFastPathsDemo_Test`.
5. It resets and runs the load in turbo: DSS boots, `SYSTEM.BAT` starts the demo, and the demo loads. The load
   takes 1 200 frames for DNTBLINK and 700 for the others, as in the exactness test.
6. Then, at the real speed, each iteration is one `MainLoop` frame (`RunFramePublic`), 500 iterations. That is
   the GUI's frame: the CPU, the devices, the sound and the screen.

The measured stretch is frames 1 200-1 700 after the reset for DNTBLINK and 700-1 200 for the others. That is
well before DNTBLINK's own freeze at about 5:10 ([demo-status.md](../../../../docs/inprogress/2026-09-28-sprinter/demo-status.md)).

## Settings that matter

- **Fast paths are on**, as in the GUI: idle cycles in one go, the kept INT answer, the fused bus, the
  accelerator fetch filter, the screen drawn on its own events, and the NeoGS idle sleep. To measure a path off,
  compare two builds. The switches (`Z84C15Engine::Set*`, `ScreenSprinter::SetCatchUpOnEvents`,
  `SoundChip_NeoGS::setIdleSleep`) exist for the exactness tests and are not exposed here.
- **Features as configured.** `BM_HostFrame_*` turns the HQ screen and sound off. This benchmark does not: the
  shipped config's choice, as a user runs it, is part of what it measures (the AY's HQ rendering is a large share
  of the "Shipped" cost).
- **The workload is deterministic.** The same disk, clock and start give the same emulated frames on every run.
  Turbo renders only the frames the host clock allows, but that changes only the picture, not the machine
  (`SprinterFastPathsDemo_Test` compares the CPU state during the turbo load).
- **Read the CPU column.** Wall time includes the host's scheduling. On a loaded machine the two can differ by
  10-20 %.

## Comparing two builds

- Build A and B in separate worktrees.
- Interleave the runs, A B B A, several rounds (at least 3 pairs). One pair is not enough: the machine is often
  loaded by other builds.
- Compare the minimum per side and the paired differences.
- Do not wait for the machine to go quiet: more interleaved rounds work better.
- This benchmark measures only time. Check exactness separately, with the two-machine tests:
  - `SprinterIdleCycles_Test`, `SprinterVideoRenderer_Test`, `SoundChip_NeoGS_IdleSleep` without the disk;
  - `SprinterFastPathsDemo_Test` with `UNREAL_SPRINTER_HDD`.

  They run a fast and a per-step machine and require equal pictures, sound, CPU state with R and T, RAM and the
  NeoGS state.

## Results

Apple M3 Max, 2026-10-09, master `a5101b964`, Release, CPU time per frame (the emulated frame is 20.48 ms):

| Demo | Bare | Shipped sound |
|---|---|---|
| DNTBLINK | 1.28 ms | 1.64 ms |
| ROTOZOOM | 2.32 ms | 2.96 ms |
| PLASMA2 | 0.60 ms | 0.93 ms |
| BADAPPLE | 1.00 ms | 1.29 ms |

Before the fast paths (master `22e79acfd`, measured with the profiling driver that this benchmark replaces),
bare: DNTBLINK 3.52 ms, ROTOZOOM 3.66, PLASMA2 2.64, BADAPPLE 3.08. DNTBLINK with the shipped sound was 4.42 ms.
