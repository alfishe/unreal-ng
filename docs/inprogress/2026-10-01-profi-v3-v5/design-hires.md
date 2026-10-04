# Profi hi-res (DS80) timing: design

**Date:** 2026-10-03 · part of [README.md](README.md) · research in
[research-profi-hires-timing.md](research-profi-hires-timing.md) · branch `profi-hires-xt`

## 1. What changes when a program sets `#DFFD` bit 7

Today the emulator keeps the Spectrum-mode timing in hi-res: 3.5 MHz, 312 lines of 224 T, INT from the lower half
of the sync PROM. The boards do this instead (research, "Emulator rules"):

| | v3.2 | v5.06 |
|:--|:--|:--|
| CPU clock | 3 MHz (12 MHz / 4), turbo 6 MHz | ZQ3 / 4, turbo ZQ3 / 2; ZQ3 = 20 MHz on the 5.06 (16-24 MHz possible) |
| Line | 64 us | 64 us |
| Frame | 320 lines (PROM `0a1d`), 20.48 ms | 312 lines, 19.968 ms |
| INT | from the PROM's upper half | from the PROM's upper half; ends at the acknowledge or after 10 ticks |
| Waits | none at 3 MHz; turbo as in Spectrum mode | around the video requests (model, M) |
| Floating bus | the hi-res bytes | none |
| AY clock | 1.5 MHz | 1.5 MHz (jumper SB7 "old", default) or 1.75 MHz ("new") |

## 2. The time base stays; the CPU clock becomes a fraction of it

Both rasters keep a 64 us line. In the emulator's base unit (one T at 3.5 MHz) that is 224 T in either mode, so the
raster, the sound, the floppy controller, the tape and every other device keep counting base T as they do now. Only
the number of CPU clocks per base T changes, and it is no longer an integer:

| Board, mode | CPU clock | CPU T per base T | Frame in base T | Frame in CPU T |
|:--|:--|:--|:--|:--|
| any, Spectrum | 3.5 MHz | 1 | 69888 | 69888 |
| any, Spectrum, turbo | 7 MHz | 2 | 69888 | 139776 |
| v3, DS80 | 3 MHz | 6/7 | 71680 (320 x 224) | 61440 |
| v3, DS80, turbo | 6 MHz | 12/7 | 71680 | 122880 |
| v5, DS80, ZQ3 20 MHz | 5 MHz | 10/7 | 69888 | 99840 |
| v5, DS80, turbo | 10 MHz | 20/7 | 69888 | 199680 |
| v5, DS80, ZQ3 16 / 24 MHz | 4 / 6 MHz | 8/7, 12/7 | 69888 | 79872 / 119808 |

Every frame comes out a whole number of CPU T (69888 and 71680 are multiples of 7).

**Mechanism.** The hardware clock ratio `EmulatorState::hw_turbo_ratio_applied` (an integer today: 2 for the
Scorpion, 4 for the ZX-Evo, 6 for the Sprinter) gets a denominator, `hw_clock_den` (1 for every other machine and
for the Profi in Spectrum mode). CPU T per base T = host speed x `hw_turbo_ratio_applied` / `hw_clock_den`. The
places that convert CPU T to base T already go through the ratio (`AudioTstate`, `HostSpeedMultiplier`,
`TtdUnitsPerTState`, `Z80::RecomputeFrameTiming`, `Z80::ApplyHardwareTurboNow`, `Screen` descaling, the tape and
WD1793 frame time, the TTD checkpoint); each takes the denominator, behind a `den == 1` fast path so the other
machines run exactly as before (A/B, phase H1). The TTD time unit grid is the least common multiple of the
numerators the model selects (v5 with ZQ3 20 MHz: 1, 2, 10, 20 -> 20 units per base T; v3: 1, 2, 6, 12 -> 12).

## 3. Phases

| Phase | Work | Check |
|:--|:--|:--|
| H1 | The clock ratio denominator through every conversion; no machine changes. Unit tests on a synthetic 10/7 and 6/7 ratio: frame limit, rescale on a mid-frame switch, audio / screen / TTD unit conversions | full suite unchanged, golden rows unchanged, A/B of the classic machines |
| H2 | Profi DS80: the CPU clock (`[PROFI] ZQ3MHz=16..24`, default 20, v5; v3 fixed 3 MHz), the frame (v3 320 lines with `0a1d`) and INT from the PROM's upper half (a `ProfiSyncPromFrame` row per half), the AY clock (`[PROFI] AyClock=old|new`, v5; v3 always 1.5 MHz in DS80), switching at the `#DFFD` write | TEST 4.30 / the BIOS hi-res speed test against the forum's figures (5.06: 1.50 and 2.45 at ZQ3 20 MHz; v3.2: "1 к 1"); Tact Meter in hi-res; the CP/M disk boots on both boards |
| H3 | Waits in DS80 (v5 model rule; v3 turbo keeps its 2/3 rule at 6 MHz), the v3 DS80 floating bus | `ProfiWaitOverlay` tests for DS80; the speed-test figures |
| H4 | Automation (the ZQ3 and AY clock options on every surface, the DS80 clock in state reports), recipes, docs, TTD round trip of a DS80 switch | the parity checklist |

### 3.1 H2b: the AY clock (built, branch `profi-ay-clock`)

The AY clock used to be a compile-time constant (`PSG_CLOCK_RATE`, 1.75 MHz). It is now a property of the
TurboSound device that the machine sets at run time:

- **Request.** `SoundManager::SetPsgClock(hz)` -> `ITurboSoundDevice::SetPsgClock`. `PortDecoder_Profi::SyncTurbo`
  calls it right after `SyncFrame`, with `ProfiAyClockHz(v5, AyClock=new, hires)` (`profiboard.h`): 1.5 MHz in
  hi-res on v3, and on v5 unless `[PROFI] AyClock=new`; 1.75 MHz in Spectrum mode. Turbo does not touch it.
  SyncTurbo also runs on reset and after a TTD restore (`UpdateModelMemoryBanks`), so the clock always follows
  `#DFFD`.
- **Timing.** A request is queued as a *clock marker* on chip 0's SSG write queue (`reg` bit 7 set, the clock in
  15 bits of 100 Hz), at the CPU T-state of the request. The render loop applies it when its cursor reaches that
  T-state, exactly like a register write: writes before the switch render at the old clock, the switch itself lands
  on the generator tick it falls in.
- **What follows the clock.** One generator tick (8 AY clocks) is `8 x 3.5 MHz / clock` base T: 16 at 1.75 MHz,
  18 2/3 at 1.5 MHz. The render cursor keeps a fraction in 1/120 T (`_renderSub`), exact for 1.5 MHz; at
  1.75 MHz the fraction is 0 and its code is skipped (the fast path). The LQ boxcar ratio and the HQ FIR decimators'
  input rate (`FilterDecimator::setInputRate`: redesigned for the new rate, history and phase kept, so no click)
  follow too. The pitch therefore drops by 6/7 in hi-res.
- **TTD.** No new bytes: the blob's i64 render-cursor offset always fits an i32, so its upper half was pure sign
  extension; the clock (100 Hz units, 0 = default) and the cursor fraction now ride there, XORed onto the sign
  extension. At 1.75 MHz both are 0, so every existing capture keeps its bytes and loads as the default clock. A
  switch still queued at a checkpoint is a queue entry and restores with its T-state. After the restore the Profi
  re-derives the same clock from `#DFFD` and finds nothing to do.
- **TSFM** keeps 1.75 MHz (`SetPsgClock` returns false): its YM2203 core, FM timers and FM decimators (437.5 kHz,
  slaves of the SSG decimator) are built around the fixed socket clock. See Open.
- **Automation.** `psg_clock_hz` in the AY overview report (`DeviceState::Ay`: WebAPI `/state/audio/ay`, MCP
  `audio_ay`, Lua / Python `audio_ay_state()`), `AY Clock:` in the CLI `state audio ay`.
- **Tests.** `soundchip_turbosound_test.cpp` (pitch ratio 6/7 in HQ and LQ, the switch landing on its T-state with a
  constant level unbroken, default clock bit-identical and the capture layout unchanged, TTD round trip with a
  queued switch), `portdecoder_profi_test.cpp` (v5 old / new, v3), `ttdayserializer_test.cpp` (a Profi v5 program
  flipping hi-res several times a frame replays byte for byte from three restore points).

## 4. Open

- Fixed after landing (2026-10-03): the VG93 and the tape now count time in base T on both boards
  (`SetBaseClockTimeBase`, set in the `PortDecoder_Profi` constructor). Before, their time was `t_states` plus the
  CPU T inside the frame, so when the CPU ran faster than 3.5 MHz (v5 hi-res at 10/7, turbo x2) it stepped back by
  the difference at every frame boundary, and forward when slower (v3 hi-res at 6/7). The SP-DOS boot loader, which
  reads in hi-res polling `#BF`, got Lost Data on every sector on a v5 (`ProfiBoot_Test.SpDosBootsToItsShell`
  catches it). The disk and the tape now run in real time whatever the CPU clock, as on the boards.

- The v3 DS80 floating bus: which screen page each of the two latches holds (O).
- The v5 DS80 wait rule is a model of the 5.06 netlist; BIOS 2.0's speed test reproduces a real 5.06's figures exactly
  (1.50 / 2.45 with the waits; 1.65 / 3.35 without them), see test-programs.md.
- 5.0-5.02 unmodified boards select ZQ3 with the CP/M button, not with DS80; two-crystal 5.0/5.01 builds stay at
  3.5 MHz in DS80. Board variants for these come later if anyone needs them.
- The AY clock reaches the AY / TurboSound slot. The shipped Profi configs fit `TurboSound=Single`, the boards' one
  AY-3-8910 / 8912 (they had TSFM before); TSFM, if chosen, stays at 1.75 MHz in hi-res until it follows the socket
  clock (its YM2203 timers would change with it, and those are CPU-visible).
- The native-rate DSD capture tap assumes 218.75 kHz; in hi-res the AY feeds it at 187.5 kHz.
- TTD across a v3 hi-res switch replays exactly (`V3HiresFrameSwitchesReplayExactly`). What first looked like a
  divergence was the test stopping on z80.t, which a v3 entering hi-res rescales down by 6/7: positions inside a
  frame compare in TTD units (`EmulatorState::TtdTInFrame`), never in CPU T.

## 5. Cost (A/B)

`BM_HostFrame_*` and `BM_TurboSoundFrame_*`, A = master ecb6e0608, B = the branch at 5a6b0af96 (H1-H4, the AY
clock, the XT keyboard), Release, 2026-10-03, interleaved rounds. The machine was loaded (1-minute load 10-25), so
only paired differences count; round-to-round noise was about +-2 %.

| Benchmark | Paired differences B / A | Mean |
|:--|:--|:--|
| 48K fast (12 rounds) | +0.9 +1.9 +0.8 +4.8 -3.1 -3.2 | +0.4 % |
| 48K debug (12 rounds) | +0.2 +0.4 +0.3 +0.7 -2.1 -2.4 | -0.5 % |
| Pentagon fast (12 rounds) | -0.2 +0.5 +0.6 -1.9 +1.0 -0.4 | -0.1 % |
| Scorpion fast (10 rounds) | -0.3 -1.7 -0.1 +0.1 +0.6 | -0.3 % |
| TurboSound frame, player load (10 rounds) | -1.7 -1.6 -5.2 -0.9 -0.6 | -2.0 % |

No measurable cost on the other machines: the clock denominator and the AY clock sit behind `den == 1` / default
clock fast paths. The Profi itself got 12-21 % faster in the same run, because its shipped config moved from TSFM to
one AY (`TurboSound=Single`).

