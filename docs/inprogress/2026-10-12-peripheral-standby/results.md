# Peripheral standby: idle sound cards at ~zero cost, machine state unchanged

**Task (owner, 2026-10-12):** from the frame profiles of branch `perf/frame-profile-benchmarks`
([core/benchmarks/emulator/profiles/README.md](../../../core/benchmarks/emulator/profiles/README.md), section 7):
the shipped cards running idle cost ~540 µs of a 735 µs Pentagon turbo frame. Cards must go to standby after start
and cost nothing until used; GS like NeoGS stays in standby until the host starts a command exchange or the GS
debug trace is switched on; a card idle for a long time goes back to standby; synchronisation, TTD rewind and the
state must be exactly the same with and without standby.

## Contents

- [1. Approach](#1-approach)
- [2. Per card](#2-per-card)
- [3. Exactness](#3-exactness)
- [4. Results](#4-results)
- [5. Not done](#5-not-done)

## 1. Approach

Standby is not a mode with its own state: every card detects from its own state that nothing can change except
counters, and advances those counters in closed form. So:

- there is nothing to enter or leave: a host access, a key-on or a debug trace makes the state non-idle and the
  card runs every step again; once it falls silent the detection finds the idle state again, by itself;
- the result is the state the stepped card reaches, bit for bit (counters, phases, timers, R, T, step counts), so
  sync points, TTD captures, hashes and restores see no difference;
- each fast path has an off switch, and a lockstep test runs a fast and a stepping copy side by side with random
  traffic and compares the whole state after every action.

## 2. Per card

| Card | Idle state | What advances in one go | Code |
|:--|:--|:--|:--|
| Classic GS | gs105a polls FLAGS with interrupts off (`COMINT_` at #026E, 6 instructions, no writes) | the poll loop by the existing `GSCardRunner` sleep (now with the card's hooks), and the 37.5 kHz INT period boundaries of the whole sleep at once | `soundchip_gs.{h,cpp}`: `busEffects`, `sleepAllowed`, `sleepQuietEventsBefore` |
| NeoGS | poll loop already slept | the timer ticks and the mixer sides inside a sleep at once, while each side recomputes the level it holds | `soundchip_neogs.cpp`: `sleepQuietEventsBefore` |
| `GSCardRunner` | - | optional card hook `sleepQuietEventsBefore(end)`: quiet events before `end` in one call instead of one by one | `gscardrunner.h` |
| MoonSound (OPL4) | every FM operator off at full attenuation with no key edge, every PCM slot off | FM: phases (vibrato by PM segment), LFOs, envelope counter, the 32-bit noise LFSR (GF(2) matrix jump), timers with status flags, feedback taps; PCM: envelope counter, TL interpolation, LFO counters; the reducer window, streams and meters | `opl4.cpp` `AdvanceIdle`, `fmsynthopl4.cpp`, `fmbus.cpp`, `pcmsynthopl4.cpp` |
| MoonSound render | silent streams | the polyphase resampler outputs exactly +0.0 once its whole history is zero (no convolution) | `opl4render.cpp` |
| TSFM, turbo / sound off | core synthesis skipped (nothing renders) | per-step sync deferred: the step notes the CPU position, the chips catch up at a port access, the frame end, a TTD recording start, a park of the main loop or the end of a direct run | `soundchip_turbosoundfm.{h,cpp}`, `SoundManager::catchUpDeferredCores`, `MainLoop::Park`, `Emulator::DirectStepScope` |
| TSFM, 1x | FM half released to `0x3ff`, nothing keyed or modified | ymfm patch 6: engine counters, the prepare sweep, phases, feedback taps up to the clock before the next timer expiry; one zero word per sample clock as before | `ymfm_fm.{h,ipp}`, `ymfm_opn.{h,cpp}` ([PATCHES.md §6](../../../core/src/3rdparty/ymfm/PATCHES.md)), `ym2203pair.cpp` |
| TSFM / AY HQ output | silent SSG side or FM part | decimators skip the FIR once their window is zero (`FilterDecimator::getOutputsSkippingSilent`, the rule `Ym2203Pair::renderChannels` already used) | `filter_decimator.h`, `soundchip_turbosoundfm.cpp`, `soundchip_turbosound.cpp` |

"Explicit GS debug": the port trace (`startPortTrace`) disables the GS / NeoGS sleep, so a trace sees every
instruction and event. The MultiSound GS with a bus observer never sleeps.

## 3. Exactness

| Test | What it compares |
|:--|:--|
| `SoundChip_GS_IdleSleep.*` (new) | sleeping vs stepping classic GS: boot, random commands / data / reads / runs, module upload, play, stop and idle again; TTD blob, counters, audio after every action |
| `SoundChip_NeoGS_IdleSleep.*` | the same for NeoGS (now through the batched events) |
| `Opl4_IdleFastPath.*` (new) | fast vs stepping OPL4, Authentic split / LinearBlend / HiFi: random FM (vibrato, rhythm, 4-op, timers) and PCM (TL ramps, LFOs on silent slots) traffic, runs up to several frames; SaveState, status, rendered audio after every action; one silent second in one sync |
| `Ym2203Pair_Test.IdleFmFastPathMatchesTheClockedPair` (new) | fast vs clocked pair: random FM traffic with timers, CSM and prescaler changes; TTD blobs, words (time and value), status |
| `SoundChip_GeneralSound_Golden.*`, `TsfmGolden_Test.*` | recorded digests of the full trajectory and audio: unchanged |
| `TsfmPlayer_Test.CpuObservableCoreSameInTurboAndSoundOff` | the CPU-observable TSFM core equal in normal, turbo and sound-off sessions (with the deferred sync) |

Full `core-tests` passed (every shard).

## 4. Results

`BM_Prof_*` (core/benchmarks/emulator/profiles), Release, Apple Silicon dev Mac, base = the same tree without the
change. Two interleaved rounds (A B B A), minimum `cpu_time` per frame, load average 56-84 during the run (the
machine is shared; the median paired ratio is the more robust column).

| Row | before µs | after µs | x | median paired ratio |
|:--|--:|--:|--:|--:|
| Pentagon_ShipNormal | 1555 | 1038 | 0.67 | 0.66 |
| Pentagon_ShipTurbo | 548 | 170 | 0.31 | 0.31 |
| Pentagon_BareTurbo | 554 | 165 | 0.30 | 0.30 |
| Pentagon_ShipTurboDebugHud | 688 | 254 | 0.37 | 0.36 |
| Pentagon_WorstNormal | 1721 | 1203 | 0.70 | 0.69 |
| Pentagon_OnlyGS_Turbo | 447 | 164 | 0.37 | 0.38 |
| Pentagon_OnlyMoonSound_Turbo | 470 | 158 | 0.34 | 0.33 |
| Pentagon_OnlyTSFM_Turbo | 221 | 184 | 0.83 | 0.82 |
| Pentagon_OnlyNeoGS_Turbo | 201 | 175 | 0.87 | 0.91 |
| Pentagon_OnlySounDrive_Turbo | 154 | 152 | 0.99 | 1.02 |
| Pentagon_NoCardsTurbo | 155 | 170 | (noise) | |
| Pentagon_MaxTurbo | 152 | 155 | 1.02 | 0.98 |
| 48K_ShipTurbo | 335 | 246 | 0.73 | 0.76 |
| 128K_ShipTurbo | 245 | 203 | 0.83 | 0.81 |
| Scorpion_ShipTurbo | 935 | 548 | 0.59 | 0.55 |
| TSConf_ShipTurbo | 6103 | 5124 | 0.84 | 0.86 |
| TSConf_MaxTurbo | 3121 | 2949 | 0.94 | 0.95 |
| ATM3_ShipTurbo | 1102 | 827 | 0.75 | 0.75 |
| ATM3_MaxTurbo | 675 | 687 | 1.02 | 1.00 |
| ATM710_ShipTurbo | 1280 | 949 | 0.74 | 0.73 |
| Profi_ShipTurbo | 490 | 476 | 0.97 | 0.91 |

`NoCardsTurbo` re-measured alone, five interleaved pairs at load 7-16: minimum 139.3 µs before, 140.7 µs after,
equal within noise (as `MaxTurbo`: no card is fitted, nothing changed there).

What it says:

- **Pentagon turbo 548 -> 170 µs** (3.2x, ~5,900 fps): the shipped cards together now cost ~15 µs over
  `MaxTurbo`, against ~400 µs before. Classic GS 447 -> 164, MoonSound 470 -> 158: both near `NoCardsTurbo`.
- **1x frame 1555 -> 1038 µs:** MoonSound's render of silence (resampler) and the TSFM's silent FM clocking and
  FIRs are gone.
- What is left of the cards in turbo: NeoGS ~20 µs (its frame end: blip read and the output coupling filter over
  the frame's samples), TSFM ~30 µs with audio on (SSG generators and the output stage, see section 5).

## 5. Not done

Possible next steps, none needed for the task:

- TSFM at 1x / turbo with audio: the per-step `syncTo` could be deferred to the moment a sample is due (the
  analysis found it exact); the SSG generators of a silent chip could advance in closed form (tone counters, the
  17-bit noise LFSR, the envelope).
- NeoGS / GS frame end with audio on: skip the blip read and the coupling filter while the output is a settled
  constant.
- The plain AY's per-step render at 1x is a machine cost, not a card's (48K / 128K rows).
