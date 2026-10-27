# Walkthrough: how the idle-card overhead was removed

A step-by-step account of the work behind [results.md](results.md): what was measured, what each card did while
idle, why the state may be advanced in one go, how it was coded and how it was proven exact. Landed on master as
`bd4692633` (+ merges, published at `1eddcb419`).

## Contents

- [1. Starting point](#1-starting-point)
- [2. Method](#2-method)
- [3. Classic GS](#3-classic-gs)
- [4. NeoGS](#4-neogs)
- [5. MoonSound (OPL4 core)](#5-moonsound-opl4-core)
- [6. MoonSound render](#6-moonsound-render)
- [7. TSFM in turbo: deferred sync](#7-tsfm-in-turbo-deferred-sync)
- [8. TSFM at 1x: ymfm idle fast path](#8-tsfm-at-1x-ymfm-idle-fast-path)
- [9. Decimators: skipping the FIR of silence](#9-decimators-skipping-the-fir-of-silence)
- [10. Proving exactness](#10-proving-exactness)
- [11. Measuring](#11-measuring)
- [12. Pitfalls met on the way](#12-pitfalls-met-on-the-way)

## 1. Starting point

Branch `perf/frame-profile-benchmarks` added `BM_Prof_*` ([core/benchmarks/emulator/profiles](../../../core/benchmarks/emulator/profiles/README.md)):
one emulated frame of a machine built from its shipped config, in a ladder of run profiles where neighbouring rows
differ in one thing. First local run (dev Mac, µs per frame):

| Row | µs | over NoCards |
|:--|--:|--:|
| Pentagon_ShipTurbo (TSFM + NeoGS + MoonSound + SounDrive) | 526 | +381 |
| Pentagon_NoCardsTurbo | 145 | |
| OnlyMoonSound | 444 | +299 |
| OnlyGS | 373 | +228 |
| OnlyTSFM | 191 | +46 |
| OnlyNeoGS | 176 | +31 |
| OnlySounDrive | 146 | ~0 |
| Pentagon_ShipNormal (1x) | 1434 | |

So a fitted card nobody uses cost more than the whole machine. The rule "machine state must not depend on the
audio mode" is why: every card keeps its core running in turbo, and nothing told an idle core it was idle.

Profiles of the 1x row (xctrace, `tools/profiling/xctrace`) added two more items: MoonSound's render stage
(`Opl4Render::ProcessGroup` -> `PolyphaseResampler::Convolve`, ~17 %) and the TSFM (`handleStep` ~18 %: FM
clocking of silent chips, SSG generators, six FIR decimators per output sample).

## 2. Method

Three read-only research passes ran in parallel, one per card family (classic GS firmware, libopl4, TSFM / ymfm),
each asked: what changes in the state of an idle card, and can it be advanced by N ticks in closed form? Their
answers set the design for every card:

- **Detect idleness from the state itself.** No "standby" flag that could get out of step: a predicate over the
  card's own state (a firmware poll loop, every operator released to full attenuation, every slot off). Anything
  that breaks idleness (a host write, a key-on, a trace) breaks the predicate, and the card steps again; when it
  falls silent the predicate holds again. That gives "back to standby after a long idle stretch" for free.
- **Advance exactly.** While the predicate holds, only counters move. Each is advanced by the formula the stepped
  code applies N times. The result is bit for bit the stepped state, so every sync point, TTD capture, state hash
  and restore is unaffected.
- **Keep an off switch and a twin.** Each fast path can be switched off. A lockstep test drives a fast and a
  stepping copy with the same random traffic and compares the whole state after every action.

## 3. Classic GS

**Finding.** `GSCardRunner` (`core/src/emulator/sound/chips/gs/gscardrunner.h`) already had an idle-loop sleep
(added for NeoGS): it recognizes the CPU coming back to the head of a short backward loop with every register as
before (except R) and no bus side effect since, and then adds whole iterations to time, R, T-states and the step
count. The classic card simply did not implement the hooks, so `kCanSleep` was false and every instruction ran.

The first assumption was that the GS could not sleep at all, because its firmware takes a 37.5 kHz INT every
320 cycles. Disassembling `gs105a.rom` (z88dk-dis, plus the 1.04 sources under
`docs/inprogress/2026-09-19-general-sound/materials`) showed otherwise:

```
026E  IN A,(4) / RRCA / JR C,#0295 / LD A,(#4084) / OR A / JR Z,#026E
```

After boot `INITVAR` runs with `DI` and never re-enables interrupts; interrupts are only enabled in `QTPLAY` while
something plays, and `QTFAULT` leaves them off when the queue drains. The idle loop is 6 instructions, 51 T, no
memory writes, no DAC-window reads, and its only port read (FLAGS) has no side effect. The INT request is raised
every period but never accepted.

**Change** (`soundchip_gs.{h,cpp}`):

- `busEffects()`: a counter bumped by every memory write, every DAC-window read (`0x6000-0x7FFF`), every GS port
  write and every GS port read except FLAGS (port 4) and COMRG (port 1). The runner refuses to sleep if it moved
  during the candidate iteration.
- `sleepAllowed()`: no pending NMI, no port trace capturing (the "explicit GS debug"), no MultiSound bus observer,
  and `!Z80CpuIntPossible` (interrupts off).
- `sleepHardStop()`: never (the period boundary is the only event, and it is quiet while interrupts are off).
- `onStepsSkipped(n)`: `cpuSteps += n`.

**Batched quiet events.** A sleep still walked its quiet events one by one at the instruction boundary the loop
would reach them on: ~750 INT period boundaries per frame. A new optional runner hook,
`sleepQuietEventsBefore(end)`, lets a card run every quiet event due before `end` at once (false: one by one, as
before). For the GS the period boundary is pure counting:

```
periods = (end - 1 - nextEvent) / periodUnits + 1
interruptPeriods    += periods
interruptsCoalesced += _intPending ? periods : periods - 1
_intPending = true
_gsCyclesAbs += periods * periodUnits
```

`runEvents` never reads the time it runs at, so running the boundaries at the sleep's end instead of at each
boundary gives the same state.

**Result:** OnlyGS 447 -> 164 µs.

## 4. NeoGS

NeoGS already slept, but its quiet events inside a sleep were dense: a mixer side every 320 crystal clocks
(75 kHz) and the timer, each through `runEvents`. Its `sleepQuietEventsBefore(end)`
(`soundchip_neogs.cpp`) runs them at once when it can prove each is a no-op:

- nothing else is due before `end` (no reset request, no TIM_FREQ edge strobe, no DMA or ZX-DMA event);
- each mixer side recomputes the level it already holds (`mixerLevel(side) == _outL / _outR`): samples and
  volumes cannot change while the CPU sleeps;
- the output stage already carries those levels (`listeningMix()` equals `GSAudioOut::lastLeft/Right`), so
  `GSAudioOut::set` would add no step.

Then the mixer sides only move `_nextDacCrystal` by the number of sides before `end`, and the timer ticks are
counted: the first tick may follow a TIM_FREQ change off the period grid, so it is run as before; the rest are on
the grid (`n * period`), each coalescing into the request the first one raised (interrupts are off).
`dacSideEvent` was split into `mixerLevel()` and `listeningMix()` so the batch and the per-event path share them.

**Result:** OnlyNeoGS 201 -> 175 µs. The rest is its frame end with audio on (blip read and output coupling over
the frame's samples; [results.md §5](results.md#5-not-done)).

## 5. MoonSound (OPL4 core)

**Finding.** `Opl4::SyncTo` (`core/src/3rdparty/opl4/src/opl4.cpp`) walks two grids: an FM tick every 684 master
clocks (`Opl4Fm::Advance`: 44 operators, 18 channels, timers) and an output step every 768 (`Opl4Pcm::Advance`:
24 slots). In turbo synthesis cannot be skipped, because the chip state (TTD blob) must not depend on the audio
mode, and nothing short-cut silence.

**What changes in a silent chip** (every FM operator `kFmEgOff` at `envVol >= 0x3FF` with `keyReq == keyOn`, every
PCM slot `Off`), and the closed form for N ticks:

| State | Per tick | N ticks |
|:--|:--|:--|
| FM `_egCnt` | +1 | `+= N` |
| FM `_lfoPm` | `(x+1) & 0x1FFF` | `(x+N) & 0x1FFF` |
| FM `_lfoAm` | +1, 0 at 13440 | `(x + N % 13440) % 13440` (a loaded value >= 13440 wraps on the first tick) |
| FM noise LFSR (32 bits kept) | `x = x<<1 \| (b0^b2^b9)` | GF(2) jump: the step as a 32x32 bit matrix, its powers `M^(2^k)` precomputed once, applied for the set bits of N |
| operator phase (all 44, they turn while silent) | `+= PhaseStep(op)` mod 2^19 | no vibrato: `+= step * N`; vibrato: the step depends on the PM segment `(lfoPm >> 10) & 7` read after the increment, so the ticks in each of the 8 segments are counted (whole 8192 cycles + a walk of at most 9 chunks) and `Σ ticks[seg] * stepAt(seg)` is added |
| channel feedback taps | shift in the operator output (0) | N >= 2: `{0, 0}`; N == 1: `{h1, 0}`; rhythm channels 7/8 zeroed; 4-op slaves untouched |
| operator `out` | output, 0 at full attenuation | 0 for every channel operator |
| timers T1 / T2 | `if (++t >= P) { t = 0; flag unless masked }` | first overflow after `k1 = (t+1 >= P) ? 1 : P - t` ticks, then every P; `t = (N - k1) % P`, flag set if any overflow (a loaded count past P overflows on the first tick) |
| PCM `_egCnt` | +1 | `+= N` |
| PCM TL interpolation (runs on Off slots too) | up at `egCnt % 27 == 0`, down at `% 27 in {9, 18}`, toward `tlDest`, never past | up = multiples of 27 in `(e0, e0+N]`, down = multiples of 9 minus up; `tl` moved toward `tlDest`, clamped |
| PCM `lfoCnt` (active LFOs) | `(x + step) & (period-1)` | `(x + step * N) & (period-1)` |
| reducer window | per FM tick `windowTicks++`, sums, `held`; reset per output step | `held = 0`; if an output step happened: `windowTicks = fmEnd - outEnd*768/684` (FM first on a tie), sums 0; else `windowTicks += N` |
| streams / meters (render side) | zeros pushed; `p -= p >> 8` | the same number of zeros appended; the meter decay looped until `p < 256` |

**Change:** `Opl4::AdvanceIdle(fmTicks, outSteps)` checks `Opl4Pcm::Idle()` and `Opl4Fm::Idle()` first, then
applies `Opl4Fm::AdvanceIdle`, `FmBus::AdvanceTimersIdle`, `Opl4Pcm::AdvanceIdle` and the window / stream / meter
formulas. `SyncTo` calls it with the ticks and steps due before entering its loop. It declines when the reducer
still carries a nonzero tick from before the silence and an output step is due (that step must emit it); the next
sync takes the fast path. `Opl4::SetIdleFastPath(false)` switches it off, `IdleFmTicks()` counts what it stood
for.

**Result:** OnlyMoonSound 470 -> 158 µs.

## 6. MoonSound render

At 1x (or turbo with audio) the split streams go through `Opl4Render::ProcessGroup`: a polyphase resampler
(gather of the history ring, then a Kaiser-windowed kernel per output), the character chain and a DC blocker.
For silence the convolution is pure cost.

**Change** (`opl4render.cpp`): the resampler counts consecutive zero input frames (`_zeroRun`, capped at `_taps`;
`_taps` after `Configure`/`Reset`, whose history is zero). Once the whole history is zero, every product in
`Convolve` is a signed zero and the sum, started at `+0.0`, is exactly `+0.0`: `Process` writes `+0.0` outputs and
moves the phase as before, without the gather and the convolution. Bit-identical output, ~16 % of a 1x frame
gone.

## 7. TSFM in turbo: deferred sync

**Finding.** `SoundManager::handleStep` -> `SoundChip_TurboSoundFM::handleStep` ran `syncTo(nowT())` after every
Z80 instruction, in every mode. With the core's synthesis skipped (turbo or sound off, no TTD active) that is a
chunk loop over timer expiries and FM sample boundaries per instruction, ~17k times a frame, for nothing that
anyone reads before the next port access.

**Why deferral is exact.** `Ym2203Pair::advanceChip` is split-invariant: advancing by a + b clocks gives the same
state as a, then b (the busy countdown and timer expiries are per-clock exact, the clock counter is additive, the
prescaler clamp is applied per chunk either way). Every guest-visible read goes through a port, which syncs first.

**Change** (`soundchip_turbosoundfm.{h,cpp}`):

- `handleStep`: while the core's synthesis is skipped **and** the output is suppressed, note `_deferredT = nowT()`
  and return. (A test configuration with the core skipped but rendering on caught the missing "suppressed"
  condition: the golden digest changed.)
- `catchUp()` syncs to the noted position. It runs before every `syncTo` (port accesses), in `handleFrameEnd`
  (before the frame start rebases the axis: the noted position is the frame end the per-step sync reached), before
  a change of the skip flag, in `TTDRecordingStarted` (which also stops deferring until the next frame start
  re-reads the TTD state), and on `TTDLoadState` / `reset` the pending sync is dropped.
- Observers outside the frame loop: a debugger run of N frames (`Emulator::RunNFrames`) is a T-state budget and
  stops mid-frame, and `TsfmPlayer_Test.CpuObservableCoreSameInTurboAndSoundOff` read the chips there and saw them
  behind. `ITurboSoundDevice::catchUp()` (default no-op) is now called through
  `SoundManager::catchUpDeferredCores()` from `MainLoop::Park` and from the end of a direct run
  (`Emulator::DirectStepScope`), on the driving thread, so a stopped machine always shows the chips in step.

**Result:** the TSFM's per-instruction cost in turbo without audio is one store.

## 8. TSFM at 1x: ymfm idle fast path

**Finding.** At 1x the TSFM renders, so the core is clocked: `clock_fm` per FM sample (~996 per chip per frame at
prescaler /6) producing zero words while both FM halves are silent.

**What `fm_engine_base::clock()` does on an idle engine** (no channel active or modified, every operator in release
at `0x3ff`, no key, no SSG-EG, cached phase step):

- `m_total_clocks++` (u8);
- the periodic `prepare()` every 4097th clock: on an idle engine it rebuilds caches equal to the live ones (local
  patch 1), clocks key state 0 -> 0, finds every channel quiet, and only resets the counter;
- `m_env_counter` on its divide-by-3 cycle (x.2 format);
- `clock_noise_and_lfo()`: a constant on OPN;
- per channel: feedback taps shift in the unchanged input; per operator: SSG-EG off clears `m_ssg_inverted`,
  the release envelope adds an increment and clamps back to `0x3ff`, `m_phase += phase_step`.

**Change** (ymfm local patch 6, [PATCHES.md §6](../../../core/src/3rdparty/ymfm/PATCHES.md)): `idle()` and
`clock_idle(count)` on the engine, channel and operator, with the closed forms (prepare counter: from a count below
4096 walk up to it, then period 4097; envelope counter: `linear = 3q + r + count`, `q = linear / 3`, `r = linear % 3`,
q wrapping at 2^30 as the u32 does; phases `+= step * count` mod 2^32). `ym2203::fm_idle()` / `clock_fm_idle()`
wrap them in `ymfm_opn.cpp`, where the engine's template members are instantiated (the first attempt called the
engine members from the TSFM header and failed to link).

`Ym2203Pair::advanceChip` (full path) clocks an idle chip in one go up to the clock **before** the next timer
expiry: at the expiry's own clock the expiry comes first and may key on through CSM, so that clock goes the normal
way and the predicate is checked again. One zero word is still pushed per sample clock at its own time, so the
output stage is untouched. `setFmIdleFastPath(false)` switches it off.

## 9. Decimators: skipping the FIR of silence

The TSFM HQ path evaluated six FIR decimators per output sample (four SSG sides, two FM slaves), the plain AY four,
also when their input was silence. `Ym2203Pair::renderChannels` (the MultiSound path) already skipped them with a
documented rule: a decimator whose newest `window()` samples are all zero outputs exactly `+0.0`.

**Change:** `FilterDecimator::getOutputsSkippingSilent(decimators, zeroRuns, count, out)`
(`filter_decimator.h`): a decimator whose caller-counted zero run reaches its window gets `skipOutput()` (its phase
moves as `getOutput` moves it) and `0.0`; the rest share one `getOutputs` pass. Grouping does not change results:
`DecimatorDot` keeps separate accumulators per row. The TSFM counts zeros per SSG side and per FM part
(`Ym2203OutputState::zeroRun`), the plain AY per side; a decimator reset or clear only zeroes its history, so the
counts stay true.

## 10. Proving exactness

| Test | Twin | Traffic | Compared after every action |
|:--|:--|:--|:--|
| `SoundChip_GS_IdleSleep.BootAndRandomHostTrafficMatchTheSteppingCard` | `setIdleSleep(false)` | boot, random commands, data, reads, runs, frames | TTD blob (CPU incl. R, RAM, timing, mailbox), activity counters, audio |
| `SoundChip_GS_IdleSleep.ModulePlaybackAndStopMatchTheSteppingCard` | same | module upload (COM30), play (COM31), stop (COM32) and idle again | same, plus "sleeps again after the stop" |
| `SoundChip_NeoGS_IdleSleep.*` (existing) | same | boot, traffic, module playback | TTD hash, steps, audio |
| `Opl4_IdleFastPath.*` | `SetIdleFastPath(false)` | Authentic split / LinearBlend / HiFi; random FM (vibrato, rhythm, 4-op, timers) and PCM (TL ramps, LFOs on silent slots) writes, all-off releases, runs up to several frames | `SaveState`, status, rendered audio |
| `Opl4_IdleFastPath.PowerOnSilenceInOneStep` | same | timers running, one silent second in one sync | same |
| `Ym2203Pair_Test.IdleFmFastPathMatchesTheClockedPair` | `setFmIdleFastPath(false)` | random FM traffic, releases, timers with CSM, prescaler changes, frame rebases | TTD blob, every word (time, value), status of both chips |
| `SoundChip_GeneralSound_Golden.*`, `TsfmGolden_Test.*` | recorded digests | scripted sessions | trajectory, traces, blobs, audio: unchanged |
| `TsfmPlayer_Test.CpuObservableCoreSameInTurboAndSoundOff` | normal / turbo / sound-off sessions | a real TSFM player | CPU-observable core per frame |

Full `core-tests` passed before landing; after the merge with master 2185 sound and TTD tests passed; the full
build had no compiler warnings.

## 11. Measuring

- **Baseline:** a second worktree at the same commit without the change, both built Release with `-DBENCHMARKS=ON`.
- **A/B:** interleaved rounds (A B B A), minimum `cpu_time` per row and the median paired ratio; the dev Mac stayed
  at load 56-84, so rows that should not move (`NoCardsTurbo`) were re-measured alone at low load (139.3 vs
  140.7 µs: equal).
- **Profiles:** `tools/profiling/xctrace/record.sh` on single rows (`OnlyNeoGS_Turbo`, `OnlyTSFM_Turbo`,
  `ShipNormal`) to find what remained after each step.

Numbers: [results.md §4](results.md#4-results). In short: Pentagon turbo 548 -> 170 µs, 1x 1555 -> 1038 µs.

## 12. Pitfalls met on the way

- **The "GS cannot sleep" assumption.** It came from the 37.5 kHz INT; the firmware disassembly showed the idle
  loop runs with interrupts off. Read the firmware before designing around it.
- **Deferral needs every observer.** Port accesses, the frame end and TTD were obvious; a debugger run stopping
  mid-frame was not. The catch-up on park and at the end of a direct run closes it.
- **Core skipped is not output suppressed.** The TSFM golden test runs a frame with the core skipped and rendering
  on; deferring there dropped the render.
- **Template instantiation.** ymfm instantiates engine members inside `ymfm_opn.cpp`; new members called from
  outside need a non-template wrapper there.
- **zsh does not split words.** The first A/B loop iterated `for side in $order` once with `"old new"` and ran only
  the new build; arrays (`order=(old new)`, `"${order[@]}"`) fixed it.
- **Expiry clocks.** The ymfm fast path stops one clock before a timer expiry: at that clock the expiry precedes
  the sample and a CSM key-on changes the idle state.
