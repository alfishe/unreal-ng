# The emulation thread waiting for the ESP jobs (research, 2026-11-22)

DOOM-VDAC4 in game on the Apple M-class build host, master `b84699fa5` plus the DRAM arbiter fast paths: 2.02 ms per
frame. The emulation thread is the bottleneck (about 90 % busy); part of it is waiting for the ESP worker. This note
measures where and why, and lists what could remove it.

## 1. How much

The `threads` block of `GET /vdac4/state`, read after 6000 frames of warm-up and again after 3000 timed frames
(`EspLatencyMs` = 4, `EspThread` = 1):

| Per frame (timed part) | Value |
|------------------------|-------|
| ESP jobs | 20.2 (one per emulated ms) |
| ESP job time on the worker | 0.79 ms |
| waits of the emulation thread | 2.0 |
| wait time | 0.22 ms (11 % of the frame) |

Where (a build that keys every wait by its call stack, timed part only):

| Site | Waits | Time | Mean |
|------|-------|------|------|
| machine frame end (`Vdac4Board::OnFrameEnd` flush, `Vdac4Tang::OnFrameEnd` `WaitEsp`) | 1164 | 247 ms | 212 us |
| app command from the Z80 (`Vdac4EspFirmware::DispatchToApps` -> `EspXtensaRuntime::Command` flush) | 1427 | 240 ms | 168 us |
| `Poll` at a bus access: the job `EspLatencyMs` back must be done | about 3000 | 123 ms | 40 us |
| `PollFtDetach` (the FT detach guard reads the Tang) | 169 | 31 ms | 183 us |

## 2. Why

The worker may run up to `EspLatencyMs` jobs behind the machine (design §7.5); that is what lets the threads
overlap. Every synchronisation point waits for all jobs in flight, so it costs the run time of the last few jobs:
four jobs of about 40 us are the 170-210 us measured at the frame end and at a command. A `Poll` wait is one job.

- **Frame end.** A machine frame ends with the ESP settled: TTD checkpoints, snapshots, the frame-end probes and the
  published state never see a job in flight, and the frame end touches the Tang (the FT812's frame end, the
  picture). The flush also replays the jobs' outputs (events, PCM, replies) at once instead of `EspLatencyMs` later.
- **Command.** The app's command handler runs on the emulation thread (`RunUntilLoopIdle`) against the ESP state
  of now, so every job before it must have run; its reply goes out at once.

The waits are therefore ESP work on the critical path, not overhead: about 0.9 synchronisations a frame, each
waiting for the ESP's last 4 ms of work.

## 3. What the ESP worker spends (1950 samples of `EspAppHost::Advance` in one sampled run)

| Part | Samples |
|------|---------|
| JIT-compiled Xtensa code | 887 |
| `xtensa::Cpu::Exec` (the interpreter: blocks the JIT does not take) | 589 |
| `ft_wr` to RAM_G (`SpiWriteRamGWords`) | 175 |
| FT812 reads (`LinkRead` -> `EveHostCard::AdvanceTo`, the chip's catch-up) | 140 |

## 4. Options

| # | Change | Emulated behaviour | Gain (upper bound, build host) | Risk |
|---|--------|--------------------|--------------------------------|------|
| A | Faster ESP jobs: the JIT takes the blocks that still run in the interpreter (30 % of the worker) | unchanged, bit-exact | the waits shrink with the job time: about -3 % of the frame if the interpreter share went to the JIT's speed | low; JIT work (POC 051 track) |
| B | Frame end without the flush: outputs replayed only by the latency rule, the frame end waits only when it needs a settled ESP (TTD checkpoint, snapshot, state read) and the FT812's frame end goes through the job stream | changes: outputs no longer jump forward at a frame end (deterministic, a new picture hash) | -0.11 ms/frame, about -5 % | high: TTD and snapshot settlement, the Tang shared at the frame end |
| C | Commands as jobs: the command runs on the worker after the jobs before it, its reply replayed with the latency | changes: replies `EspLatencyMs` later (deterministic, new hash); the "unsupported" error decided by the command range | -0.11 ms/frame, about -5 % | medium: Z80 programs that poll for the reply see it later |
| D | `EspLatencyMs` larger | changes, as any latency | none on the flushes (they wait for all jobs whatever the latency) | - |

B and C together remove at most 0.16 ms of 2.02 (-8 %) on the build host, and both change when the ESP's outputs
reach the machine: the runs stay deterministic, but no longer equal the current ones, so the check is gameplay
(DOOM, R-Type, HMM2) instead of a picture hash. A keeps the behaviour bit for bit but gains less.

## 5. Recommendation

A first (no behaviour change), measured on the other VDAC4 games as well; B and C only on the owner's decision,
as a model change of the job design (§7.5) with its own TTD and snapshot tests.

## 6. Option A, measured (2026-11-22)

What the JIT left to the interpreter (DOOM, 9000 frames, block entries without a region by their first instruction):
`ENTRY` 25 M and `RETW.N` 24 M (one-instruction blocks: every windowed call and return), `MOVT.S` 7.4 M (44 M
instructions interpreted after a region exit there), `QUOU` / `REMU` / `QUOS` 2.0 M (13 M), `NSAU` 1.3 M (3.7 M).

Done: the region JIT compiles `MOVT.S` / `MOVF.S`, `QUOU` / `QUOS` / `REMU` / `REMS` (a zero divisor exits before the
instruction, so the interpreter stops with `DivideByZero`; `INT_MIN / -1` as the interpreter on both hosts, x86_64
without IDIV for a divisor of -1) and `NSA` / `NSAU` (arm64 `CLS` / `CLZ`, x86_64 `BSR`), on arm64 and x86_64.
Native share 92.0 -> 94.8 %, loop-end refusals 34 M -> 15 M. `XtensaJit_Test.DivisionsAndBitCountsMatchTheInterpreter`
covers the edge values; both JIT tests pass on macOS arm64 and on Linux x86_64 (gcc, clang).

| DOOM, three interleaved rounds | ms / frame | ESP jobs ms / frame | waits ms / frame |
|------|------|------|------|
| master | 2.024 | 0.775 | 0.215 |
| the kinds above compiled | 2.014 (-0.5 %) | 0.737 (-5 %) | 0.195 (-9 %) |

Not done: `ENTRY` / `RETW` inside a region (the window base changes, with spill and fill). About 12 ns of `Exec`
per interpreter round plus the region's entry and exit, 5.8 k rounds a frame: at most about 0.13 ms of ESP work and
under 2 % of the frame on the build host, for a change of the region model on two backends. Not worth it now.
