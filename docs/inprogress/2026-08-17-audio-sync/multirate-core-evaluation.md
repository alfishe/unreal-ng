# Multi-Rate Audio Core — Evaluation

**Status:** evaluation (companion to `audio-sync-design.md`; implementation plan in `multirate-core-implementation-plan.md`)
**Question:** the AY/beeper synthesis chains are designed for 44 100 Hz only. What does it take to run the core at 44 100 / 48 000 / 88 200 / 96 000 / 176 400 / 192 000 Hz, and what is it worth?

---

## 1. Component inventory

### Already rate-agnostic (no work)

| Component | Why |
|---|---|
| Beeper / Covox / tape (blip_buf) | `blip_set_rates(clock, rate)` synthesizes band-limited output at any rate with T-state edge precision; only ctor args change |
| Integer sample accumulator (Fix 1) | Exact at every rate. At 48k-family rates Pentagon's period even improves: 25 frames instead of 125 |
| DRC resampler + PI controller (Fix 2/3) | Rate-parametric by design; base ratio dev/core already handles arbitrary pairs |
| AY non-HQ PLL | `AUDIO_SAMPLE_TSTATE_INCREMENT = rate / 3.5MHz` — a ratio, recomputed per rate |
| `FilterInterpolate` | Already has `setRates(psgClock, rate)`; 44100 is only its default |
| Recording | `_audioSampleRate` is a parameter |

### Rate-locked (the actual work)

| Component | Lock | Severity |
|---|---|---|
| `FilterDecimator` (AY HQ) | `OUTPUT_RATE = 44100.0` baked into the 4.96:1 phase step. Coefficients themselves are **not** rate-locked (input side is fixed 218.75 kHz) | Small — phase step becomes a parameter |
| `FilterUnreal` (AY legacy) | 128-tap Hamming FIR for fs = 44100×64, fc = 11025 | Per-rate tables or runtime designer |
| `AudioCharacterChain` | Envelope attack/release are raw per-sample coefficients (0.998 = 11 ms *only* at 44.1k → 2.6 ms at 192k, audibly different punch). `MAX_DELAY = 512` **overflows at ≥176.4 kHz** (0.003×192000 = 576) — a live bug regardless of this project | Medium — τ-based conversion |
| Buffer sizing | `MAX_SAMPLES_PER_FRAME = 2048` fits 44.1k×2-multiplier; 192k Pentagon frame = 3932 samples (7865 at 2×) | Small — raise ceiling to 8192 (~100 KB total) |
| `PSG_CLOCKS_PER_AUDIO_SAMPLE` | Truncating integer division (1.75M/44100 = 39) | Audit consumers |

---

## 2. Which rates, and what they buy

Two families cover every modern standard: **44100 → 88200 → 176400** and
**48000 → 96000 → 192000**. Excluded deliberately: 32000 (broadcast legacy,
no value here), 352.8k/384k DXD (the codebase already has the better archival
answer — the `_nativeTap` captures AY at raw 218.75 kHz pre-decimation for
DSD workflows).

| Rate | Practical value |
|---|---|
| **48000** | **The one that matters.** Nearly every modern device is 48k-native; a 48k core makes the DRC base ratio unity → resampler in pure ±0.5% trim mode → best possible playback chain. Today's 44.1→48 Hermite hop is already ~−90 dB clean, so this is refinement, not rescue |
| 88200 / 176400 | Cheap to include (same family as 44100); niche |
| 96000 / 192000 | Near-zero *playback* value — beeper/AY content below 20 kHz is already band-limited-perfect at 44.1k. Real value: archival capture, analysis FFTs, headroom for future analog-modeling DSP (speaker/tape sims) |

Physics check: the AY's HQ path renders internally at 218.75 kHz and the
beeper at T-state (3.5 MHz) resolution — **no audio information is created by
raising the core rate**; only the final decimation/band-limiting changes. At
96/192k the decimation ratio drops to 2.28:1 / 1.14:1, allowing gentler
filters and optionally extended bandwidth (>20 kHz "air") for archival modes.

---

## 3. Two design decisions

1. **`FilterUnreal` cutoff: absolute, not relative.** The shipped fc is
   exactly rate/4 (11025 = 44100/4, i.e. fc/fs = 1/256), which would make the
   coefficient table rate-invariant — but the audible tone would brighten as
   the rate rises. Keeping fc = 11 025 Hz absolute preserves the shipped
   tonal character at every rate, at the cost of per-rate tables.
2. **`FilterDecimator` cutoff: same 20 kHz at every rate by default**
   (identical character), with optional extended-bandwidth tables (40 kHz @
   96k, 80 kHz @ 192k) for archival capture.

---

## 4. Verification cornerstone

Both shipped filter tables were **reproduced bit-exactly** by a
first-principles windowed-sinc generator (Kaiser β=5 for the decimator,
Hamming for FilterUnreal, both DC-normalized) — validated 2026-08-17. All
recalculated tables in the implementation plan come from the same generator
and are therefore authoritative, and the generator itself can live in the
codebase as a runtime designer, eliminating baked tables entirely.

---

## 5. Recommendation

Make `CORE_SAMPLING_RATE` an init-time per-emulator parameter
(`[SOUND] CoreRate=auto|44100|48000|88200|96000|176400|192000`, `auto` =
match device family; change requires sound reset, no hot-switch). Implement
via the runtime filter designer rather than six baked tables. Priority order:
character-chain τ conversion + `MAX_DELAY` fix (live bug), then the designer,
then plumbing, then the rate-matrix tests with FFT pitch invariance as the
acceptance criterion.
