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

## 4. Open

- The v3 DS80 floating bus: which screen page each of the two latches holds (O).
- The v5 DS80 wait rule is a model (M); the forum's speed-test figures fit it but do not prove it.
- 5.0-5.02 unmodified boards select ZQ3 with the CP/M button, not with DS80; two-crystal 5.0/5.01 builds stay at
  3.5 MHz in DS80. Board variants for these come later if anyone needs them.
