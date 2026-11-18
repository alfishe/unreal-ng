# 051 measurement 1 — where the ESP worker's time goes

Date: 2026-11-17. Host: the M-class macOS build host (arm64), Release build of tsconf-vdac4 9e73d2fa7 with
`-DUNREAL_XTENSA_PROFILE` (compile-time hooks, see `core/src/3rdparty/unreal-xtensa/xtensa-cpu.cpp` and
`espapphost.cpp` in the POC worktree; not in the product build). Workload: DOOM-VDAC4 in game, deterministic
(created paused, `_DOOM.SPG`, `run_frames` 6000, then the timed frames), ESP jobs on the worker thread, FT812
work inline in the jobs (the default), latency 4 ms.

Tools: `python/analyze_profile.py` (the dumps), `python/sample_self.py` (self time per function per thread of a
macOS `sample` report). Raw data: `scratch/2026-11-17-poc-051/` (`profile-122-128.txt`, `samp1.sample.txt`,
`esp-thread-self.txt`).

The profile build costs nothing visible on the frame: 6.18 ms per frame against 6.05-6.34 for the product build
in the same scene (one round; the hooks are compile-time, the product build has none).

## 1. The ESP app's own counters (emulated seconds 122-128, 6000 ms)

| Item | Value |
|------|-------|
| Guest instructions | 286.6 M (47.8 M per emulated second) |
| Core 0 load | 19.9 % of its 240 MHz |
| Core 1 load | 0 % (DOOM runs one task) |
| Host time in `Advance` | 1336 ms, 0.223 ms per emulated ms (4.45 ms per 20 ms frame) |
| — inside `Cpu::Run` | 576 ms, 43 % — **2.01 ns per guest instruction, 497 M instructions per host second** |
| — in traps (`HandleTrap`) | 743 ms, 56 % |
| — task switches (Load/SaveState) | 15 ms, 1 % |
| — PCM | 1 ms |
| `Cpu::Run` calls | 1.03 M (277 instructions each: almost every run ends at a trap) |

Traps by host time:

| Trap | Calls | Host ms | Each | What |
|------|-------|---------|------|------|
| `0x4000088c` zm slot 35 `ZmFtRead` | 5 508 | 414 | 75 µs | one FT812 register read: the FT812 catches up to the stamp first |
| `0x40000888` zm slot 34 `ZmFtWrite` | 3 712 | 232 | 62 µs | a burst of FT812 words: the FT812 catches up and takes them |
| `0x400000c4` ROM `__divsf3` | 997 k | 96 | 97 ns | soft-float divide; most of it is `RomTrapByName` (a linear `strcmp` table) per call |
| everything else | ~35 k | 1 | | |

So the "trap" time is the FT812 emulation, run synchronously inside the ESP job at each FT read or write, plus
the ROM trap lookup.

Guest code shape:

| Item | Value |
|------|-------|
| Blocks run | 3 610 |
| Concentration | top 10 blocks 55.8 %, top 100 87.4 %, top 1000 99.6 % of the instructions |
| Two hottest blocks | `4201bbec` (10 insns, 21.9 %) and `4201c0c0` (14 insns, 17.5 %): byte loads through a table and a byte store, DOOM's column / span drawers |
| Instructions per block entry | 5.5 |
| Block length (share of instructions) | 1-3: 18.5 %, 4-7: 13.5 %, 8-15: 57.6 %, 16-31: 10.3 % |
| Opcodes | `l32i.n` 17 %, `add.n` 15 %, `l8ui` 8 %, `extui` 6 %, `l32r` 5 %, stores 8 %, FP (`lsi`/`wfr`/`mul.s`/...) ~5 % |
| Block ends | conditional branches ~50 %, `j` 9 %, `callx8` 6 %, `retw.n` / `entry` 4.5 % each, the 64-insn / loop cut rest |

## 2. Host self time on the ESP worker thread (`sample`, 3000 frames)

12 433 busy samples. Without the profile's own timers (~500, in `RunCore`) and the worker's idle spin (~260):

| Part | Samples | Share |
|------|---------|-------|
| Xtensa interpreter (`Cpu::Exec`, `Store`, `Run`, `Decode`, `RunCore`, `Arg`) | ~4 940 | **~42 %** |
| FT812 emulation (`EveHostCard::AdvanceTo`, `EveLib::DrawBitmap`, `BilinearBlend`, `BlendSrcAlpha`, `NextEvent`, `CatchUp`, SPI / bus, `LatchExternalFrame`, capture) | ~5 450 | **~47 %** |
| `strcmp` from `RomTrapByName` (the `__divsf3` trap) | ~810 | **~7 %** |
| allocations, memset, rest | ~470 | ~4 % |

`Cpu::Exec` self is 3 927 samples; at 2 ns per instruction the interpreter is already about 8 host cycles per guest
instruction on this host.

## 3. What it means for the JIT

- The charter's expectation "guest code > 70 % of the ESP's host time" is **wrong**: guest code is about 42 %. The
  FT812, run inside the ESP job, is as large.
- Amdahl on the ESP worker (4.45 ms per frame here): a 3x JIT takes the interpreter's 42 % to 14 %, the worker to
  about 3.2 ms (-28 %); an infinitely fast one to about 2.6 ms. Then the emulation thread (about 2.5 ms of its own
  plus the waits) is the critical path; the frame would go from 6.1 to roughly 4.5-5 ms at best.
- Cheaper levers on the same path, each without a JIT:
  1. `RomTrapByName` per ROM trap: resolve the trap kind once per table index. About -7 % of the worker.
  2. The FT812 inside the ESP job (47 %): the pixel output (`DrawBitmap`, the blends, `OutputLine`) does not
     feed any register the ESP reads; only the chip state (registers, command FIFO, frame counters) does. Splitting
     the scan-out render from the chip state lets the render go to the FT thread without the read syncs that made
     the FT thread useless (19 reads per frame each drain it). eve-emu already sketches the lazy drawing this
     needs (`core/src/3rdparty/eve-emu/docs/performance.md` §5.2: due lines only counted, drawn before anything
     they read changes, at the frame end and before `REG_TAG` is read); not implemented yet. DOOM writes its
     frame into RAM_G in bursts (about 12 `ZmFtWrite` a frame), so the draw points stay frequent unless the
     render works on its own copy of what it reads.
  3. `__divsf3` and the other soft-float ROM routines as interpreter-native helpers (no trap, no Run exit): 1 M
     Run exits per 6 s are 1 M dispatch restarts.
- The hot guest code is very concentrated (top 100 blocks 87 %) and block-shaped (8-15 instructions), which suits a
  block JIT; the JIT stays the way to multiply the guest part.

Next: the cheap levers 1 and 3 measured in the machine (A/B), the render split sized (design note), then the
capture / replay harness for the JIT variants.

## 4. Lever 1 measured: ROM trap kind resolved once

`RomTrapAt(index)` (a table built once from `RomTrapByName`) instead of the name lookup per ROM trap
(`espapphost.cpp`). Five interleaved rounds, the same scene, product Release builds (raw:
`scratch/2026-11-17-poc-051/ab-romtrap.txt`):

| Build | ms per frame (5 rounds) | Mean | Picture |
|-------|-------------------------|------|---------|
| current (9e73d2fa7) | 6.30 6.29 6.39 6.36 6.31 | 6.33 | e76dcca4e472 |
| + RomTrapAt | 6.00 6.14 6.03 6.04 6.01 | 6.04 | e76dcca4e472 |

-4.6 % on the frame, the picture identical. In the profile a `__divsf3` trap went from 97 to 21 ns of host time;
what is left of lever 3 is the Run exit and restart per call (about 1 M a 6 s window), a few percent at most.
This change is a product fix, not POC code: it lands on its own.

## 5. Lever 4 measured: one SPI transaction per FT word

The Tang's FT word path (`Vdac4Tang::ChipWrite32` / `ChipRead32`) did 9 calls of `EveHostCard::select` /
`exchange` per word, each catching the clock up (`AdvanceTo`), sampling INT_N and planning the next event (a
64-bit division and the chip's next-event scan). No time passes inside a transaction (the job stamp is pinned), so
`EveHostCard::Transaction` catches up once, keeps `EveExchange` and the per-byte INT_N sampling, and plans once; a
capture file or a device history takes the byte path. First try kept the byte path whenever `CaptureLive()` (which
only means "not a TTD replay") and gained nothing: the fast path needs `_capture.IsOpen() && CaptureLive()`.

Four interleaved rounds (raw: `scratch/2026-11-17-poc-051/ab-spi2.txt`):

| Build | ms per frame | Mean | Picture |
|-------|--------------|------|---------|
| current | 5.95 5.95 6.26 6.29 | 6.11 | e76dcca4e472 |
| + RomTrapAt | 5.72 5.65 5.97 5.97 | 5.83 (-4.7 %) | e76dcca4e472 |
| + RomTrapAt + Transaction | 5.25 5.30 5.61 5.61 | 5.44 (-11 %) | e76dcca4e472 |

Unit test: `Vdac2Card_Test.TransactionMatchesTheBytePath` (memory, the INT_SWAP edge at the same tact, the
clear-on-read flags). Product fix, lands on its own with lever 1.

## 6. Lever 5 measured: the frame conversion

`EveHostCard::AdvanceTo` showed 1 100-1 300 self samples: the card's methods are inlined into it, among them
`ConvertFrameToPicture`, a byte-by-byte ARGB -> RGBA shuffle of the whole picture at every chip frame. Rewritten on
whole words (little-endian hosts; the byte loop stays for big-endian), the compiler vectorises it. Four
interleaved rounds (raw: `scratch/2026-11-17-poc-051/ab-conv.txt`):

| Build | ms per frame | Mean | Picture |
|-------|--------------|------|---------|
| current | 6.41 5.96 6.31 6.29 | 6.24 | e76dcca4e472 |
| + RomTrapAt | 6.00 5.66 5.99 5.97 | 5.91 (-5.3 %) | e76dcca4e472 |
| + RomTrapAt + Transaction + conversion | 5.20 4.89 5.20 5.20 | 5.12 (-18 %) | e76dcca4e472 |

Against the Transaction step alone (section 5, its ratio to RomTrapAt 0.933) the conversion adds about 7 %.
