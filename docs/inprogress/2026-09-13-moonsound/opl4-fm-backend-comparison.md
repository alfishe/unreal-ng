# OPL4 FM backends — the in-tree compact model vs ymfm

**Status:** explainer, created 2026-09-15 after the MFM Music sample guest-level confirmation
(§8) landed the first whole-disk reproducer of the classic-map divergences; updated the same
day with the full YMF262/OPL3 register map (§5–§6) and the per-reference compliance verdict (§7),
and again the same evening after the **classic-map adoption landed** — the layer-3 divergences
are resolved (§4), the in-tree engine now renders period FM repertoire correctly (§8), and the
open decision (§11) is closed.
**Scope:** what each of the two interchangeable FM implementations of the MoonSound
(YMF278B / OPL4) card is, how the swap works, where the engines agree and diverge, and what
that means for real guest music — the narrative companion to the evidence log
[`opl4-ymfm-verification-findings.md`](opl4-ymfm-verification-findings.md). Every divergence
below is reproduced by a named artifact there; nothing in this file changes behaviour.
**Companions:** [`opl4-openmsx-audit-and-diff-harness.md`](opl4-openmsx-audit-and-diff-harness.md) §6
(Revision-5 backend bring-up), [`opl4-core-tdd.md`](opl4-core-tdd.md) §12.2/§13.2 (chip-model
view), [`opl4-unreal-ng-integration.md`](opl4-unreal-ng-integration.md) §12.2 (suite inventory).

---

## 1. One chip, two implementations

Both backends model the **same silicon**: the FM half of the YMF278B, which is
register-compatible with the YMF262 (OPL3) — 18 two-op channels (any 6 linkable into 4-op
pairs), 8 sine-derived operator waveforms, per-operator ADSR, LFO vibrato/tremolo, the
5-voice rhythm mode, and timers T1/T2. On the card that half sits behind ports `#C4`–`#C7`
(plus the register-file mirror at `#7E`/`#7F`); the wave-table half plays the YRW801 2 MB
sample ROM. **Only the FM synthesis differs between backends** — the PCM half, ROM loading,
port decode, detection protocol and mixer wiring are shared.

### 1.1 The in-tree compact model (default backend)

This project's own core (`-DOPL4_FM_BACKEND=opl4`, the CMake default):

| Property | Value |
|---|---|
| Provenance | Written for this codebase; verified primarily against openMSX (de-facto OPL4 reference, GPL — diffed against, never vendored) |
| Dependencies | None (self-contained, ships in the core) |
| Register map | **Classic YMF262** (adopted 2026-09-15): operator slots at +0/+8/+0x10 with the 0x26/0x27 and 0x2E/0x2F gaps, channel *i* pairs {i, i+3}; slot-indexed operator storage (44 slots), state schema v3 |
| Output | Full 16-bit range, no scale adapter |
| Introspection | Per-channel level taps (`channelPeak()`) — what the guest metrics tests read |
| TTD | Native state serialization, one schema (`kStateVersion` 3; schema 2 = ymfm backend) |

### 1.2 The ymfm backend

Aaron Giles's Yamaha FM emulation library (MAME lineage, BSD-3, vendored at pinned commit
`81aec25`, `core/src/3rdparty/ymfm/`), wrapped by the adapter
[`Opl4FmYmfm : Opl4Fm`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h):

- **FM synthesis only.** ymfm's `ymf262` core renders phase/envelopes/connections/rhythm/
  4-op/LFO. Bus-visible semantics — bank-1 aliasing, `0x105` NEW/NEW2, timers
  T1=`(0x100−load)·4` / T2=`·16`, status bits — stay in the audited base class; ymfm's own
  timer/busy/IRQ machinery is stubbed and never advanced.
- **Clocking contract:** one `generate()` per 684-clock FM boundary — the cadence ymfm's own
  `ymf278b` uses, on the OPL4 49516.4 Hz FM grid (YMF262 silicon runs 49716 Hz, so the
  adapter's clocking, not ymfm's default rate constant, defines pitch; comparator-confirmed
  pitch ratio 1.0000).
- **Level scale:** constant ×4 at the adapter (ymfm renders with 13-bit headroom).
- **TTD:** rides ymfm's structured `save_restore`; top-level `kStateVersion` 1/2 tags
  sessions per backend so they never cross builds.
- Selected with `-DOPL4_FM_BACKEND=ymfm`, which defines `OPL4_FM_YMFM=1` PUBLIC on the core
  target — test code can branch per backend on it.

### 1.3 ymfm's two distinct roles (where each finding comes from)

| Role | Artifact | What runs on ymfm |
|---|---|---|
| FM backend swap | [`Opl4FmYmfm`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h) | FM synthesis only; whole PoC and core suites re-run green on this build |
| Full-chip differential | [`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) | Both halves incl. ymfm's PCM wave part (6/6 scenarios) — the source of the PCM-side quirks in §9 below |

---

## 2. Selecting and building each backend

```bash
# Default (in-tree compact model)
cmake -S . -B cmake-build-release -G Ninja -DTESTS=ON
ninja -C cmake-build-release core-tests

# ymfm backend
cmake -S . -B cmake-build-ymfm -G Ninja -DOPL4_FM_BACKEND=ymfm -DTESTS=ON
ninja -C cmake-build-ymfm core-tests
```

Note: the test trees glob sources at **configure** time — a test file added after a tree was
configured is silently absent there ("Running 0 tests"); re-run the cmake line before ninja.

---

## 3. Where the engines agree (the baseline trust)

Same register stream into both engines ([`opl4fmcompare.cpp`](tools/poc/015-opl4-synthesis/tests/opl4fmcompare.cpp),
state-diff first):

- **Pitch ratio 1.0000** (zero-cross/sample 0.00600 on both).
- **Level ratio 0.999** after the adapter's ×4 scale (22966 vs 22987 RMS).
- **Release-tail parity** (407.2 M vs 415.1 M |sample| sum).
- **Timers T1/T2 step-for-step**, flags/NEW/NEW2/bank aliasing, and the **full 512-byte
  register file** (one storage exclusion, findings §3.5).
- **Adapter save/restore exactness** — 2000-sample lockstep (findings §3.8).

So the backends are not "two different sounds": the comparator isolated exactly where they
diverge, and everything else locks step.

---

## 4. Where FM rendering diverges

### 4.1 The three classic-map divergences (findings §2.1) — RESOLVED 2026-09-15

The family that decided whether **period FM drivers** — software written against real
YMF262 silicon, like the MFM disk's MBPlayer — play correctly. Pre-adoption state, kept
as the historical record:

| # | Divergence | In-tree compact model | ymfm (= YMF262 silicon) |
|---|---|---|---|
| 1 | **Operator register map** | Linear: `op = reg − 0x20`, channel *i* pairs {2i, 2i+1} | Classic groups at +0/+8/+0x10 with the 0x26/0x27 and 0x2E/0x2F gaps; channel *i* pairs {i, i+3}; channel-0 carrier at 0x23/0x43/0x63/0x83 |
| 2 | **0xC0 routing polarity** | Bits *exclude* a side when set (0x30 = fully silent, 0x00 = both) | CHA/CHB *include* (0x30 = both sides); a classic driver's routine `0xC0 = 0x31` mutes the in-tree engine, not ymfm |
| 3 | **Unconfigured carrier** | AR = 0, never attacks | Attacks — compounding #1: when envelope writes miss the real carrier, the in-tree voice stays silent rather than merely wrong |

Pre-adoption measured verdict (identical classic-driver bytes, carrier TL 0x10): **rms
ours = 0 vs ymfm = 5861, pitch ratio 0.0000 → DIVERGENT** (Revision-5 script recorded 1465).
**RESOLVED 2026-09-15**: the in-tree engine adopted all three classic semantics
(slot-indexed map, include-routing with the full CHA/CHB/CHC/CHD quad, carrier attack);
the comparator now asserts the match — rms 5844 vs 5861, pitch ratio 1.0000 — and the MFM
guest tunes render healthy on both backends (§8).

### 4.2 Sweep-suite divergences (findings §2.2)

From the 13 FM sweep families run on both backends
([`opl4sweep.cpp`](tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp)):

| # | Divergence | Disposition |
|---|---|---|
| 1 | **Waveforms ws 3 and ws 5** — pre-adoption the in-tree square-derived set differed from ymfm's \|sin\|-quarter tables (ws 0/1/2 and 4/6/7 always agreed) | **RESOLVED 2026-09-15** — ymfm/silicon tables adopted; silicon truth for YMF278B stays a hardware-tier question |
| 2 | **4-op connection-select key bit** — pre-adoption in-tree keyed on the *slave* C0 bit; ymfm algorithms 8–11 key on the *master* bit | **RESOLVED 2026-09-15** — master-bit semantics adopted; the loud/quiet row set is identical on both |
| 3 | **C0 quad bits CHC/CHD** — pre-adoption unmodelled in-tree; ymfm sums the full OPL4 quad into L/R | **RESOLVED 2026-09-15** — full quad modeled; single-path `FmRoutingMatrix` |
| 4 | **KSL exact folded table** — pre-adoption in-tree-specific bands | **RESOLVED 2026-09-15** — ymfm consumption semantics adopted (`<< ksl`, table numerals in 0.09375 dB index units) |
| 5 | **Envelope shift-ladder mid-range ratio** — compact model vs ymfm 5.11 counter | Live by design (internal counters); the cross-backend ~2× half-level-time family holds on both |
| 6 | **Rhythm B0-kon suppression** — real divergence (measured 2026-09-15): with rhythm on and nothing keyed via 0xBD, ymfm still sounds a B0-keyed channel-7 voice (suppressed 0.576 ≈ keyed 0.580 RMS); the in-tree engine implements the YMF262 datasheet suppression | Guarded with the measurement in-code; the five-voice audible matrix itself runs on both; hardware tier arbitrates |

---

## 5. The full YMF262/OPL3 register map — engine by engine

On the card, the FM register file is **two 256-byte banks**: bank 0 via ports `#C4`/`#C5`,
bank 1 via `#C6`/`#C7` — the YMF262's two register banks, 1:1 (the card's `#7E`/`#7F`
mirror addresses the same combined 512-byte file). 18 channels, 36 operators, register
semantics identical to YMF262 apart from the two OPL4 extensions flagged in the table.
Verdict legend:

- **AGREE** — differential artifact exists, no divergence found
- **DIVERGE** — documented and guarded divergence (finding # in parentheses)
- **OPEN** — engines differ *and* silicon truth unknown (parked on the hardware tier)
- **n/a** — not part of the OPL4 FM map

### 5.1 Bank / global registers

| Register | Silicon function | In-tree compact | ymfm (`ymf262`) | Verdict |
|---|---|---|---|---|
| `0x01` | Test register. On OPL2 bit 5 gates wave select; OPL3+ has wave select always on | ignored as test; ws always available | same — OPL3+ returns constant 1 ([`ymfm_opl.h`](../../../core/src/3rdparty/ymfm/ymfm_opl.h) L203) | AGREE (register-file diff; `FmWaveformSweep` green on both with no `0x01` write) |
| `0x02` / `0x03` | Timer 1/2 preset load | audited timer block: T1=`(0x100−load)·4`, T2=`·16` | ymfm timers stubbed; adapter owns the audited timers | AGREE — `FmTimerSweep`, step-for-step, openMSX-audited formulas |
| `0x04` bank 1 | Timer control: ST1/ST2, masks MT1/MT2, RST | audited | stubbed in adapter (base class owns it) | AGREE (bus layer, both backends) |
| `0x04` bank 0 | 4-op connection mask (6 links; requires NEW) | classic | classic | **AGREE** since the master-bit adoption (§4.2 #2 resolved) |
| `0x05` bank 1, b0 | **NEW** — OPL3 mode: bank-1 registers, 4-op, 4-channel output | audited | audited | AGREE — NEW/bank-1 aliasing differential |
| `0x05` bank 1, b1 | **NEW2 — OPL4-only**: gates wave-register access | audited base class | n/a — YMF262 has no NEW2 | n/a for YMF262; AGREE vs openMSX |
| `0x08` | CSM / NOTE-SEL | not modeled | not modeled — OPL/OPL2-only ([`ymfm_opl.h`](../../../core/src/3rdparty/ymfm/ymfm_opl.h) L63) | n/a — not in the YMF262/OPL4 FM map |
| `0xBD` | b7/b6 AM/VIB depth, b5 RHY, b4-0 rhythm key-ons (BD/SD/TOM/TC/HH) | full | full | AGREE on the AM/VIB depth matrix and the five-voice audible matrix; **B0-kon suppression DIVERGES** (§4.2 #6 — ymfm keys rhythm channels from B0 too) |

### 5.2 Per-operator registers (2 × 18 operator slots)

Addressing note — this is where the classic map lives, on **both** engines since the
adoption. Within each bank, operators of channel *c* are addressed as
`mod = base + (c%3) + 8·(c/3)`, `car = mod + 3` (bases 0x20/0x40/0x60/0x80/0xE0; the
sweep helper `FmOpAddr()` carries the single shared convention).

| Register | Silicon function | Verdict |
|---|---|---|
| `0x20`–`0x35` | b7 AM, b6 VIB, b5 EGT (sustain), b4 KSR, b3-0 MULT (×½…×15) | **AGREE** — addressing and semantics (MULT/pitch sweeps, envelope-stage sweeps, AM/VIB matrix) since the §4.1 #1 adoption |
| `0x40`–`0x55` | b7-6 KSL, b5-0 TL | **AGREE** — TL ladder and KSL folded table (§4.2 #4 resolved: ymfm consumption semantics adopted) |
| `0x60`–`0x75` | b7-4 AR, b3-0 DR | Map-agnostic behaviour **AGREE**; mid-range shift-ladder ratio remains engine-internal (§4.2 #5) — the cross-backend ~2× half-level family holds on both |
| `0x80`–`0x95` | b7-4 SL, b3-0 RR | **AGREE** — release-tail parity (407.2 M vs 415.1 M \|sample\|) |
| `0xE0`–`0xF5` | b2-0 WS — 8 sine-derived waveforms | **AGREE** since the §4.2 #1 table adoption; silicon truth for YMF278B still hardware-tier |
| (unwritten) | Power-on / reset defaults | **AGREE** since the §4.1 #3 adoption — unconfigured carriers attack |

### 5.3 Per-channel registers (9 channels per bank)

| Register | Silicon function | Verdict |
|---|---|---|
| `0xA0`–`0xA8` | F-Number LSB | **AGREE** — pitch ratio 1.0000 (zero-cross/sample 0.00600 on both) |
| `0xB0`–`0xB8` | b5 KON, b4-2 BLOCK, b1-0 F-Number MSB | **AGREE** — pitch + `FmKonMomentary` key-on edge behaviour |
| `0xC0`–`0xC8` | b1-0 ALG (CNT), b3-2 FL (feedback), b4-7 CHA/CHB/CHC/CHD | **AGREE** — FL (`FmFeedbackSweep`), include-semantics CHA/CHB with the full CHC/CHD quad (all resolved 2026-09-15, §4.1 #2 / §4.2 #2–#3) |

### 5.4 Chip-level features (not single registers)

| Feature | Silicon behaviour | Verdict |
|---|---|---|
| Composite status read at `#C4` | YMF262 timer flags b6/b5 OR'd with OPL4 BUSY (b0) / LD (b1) | **AGREE** — audited bus layer, identical on both backends (ymfm's status machinery stubbed) |
| Write busy timing | An FM write occupies the bus ~56 master clocks | Emulated in the I/O layer; **n/a** for the ymfm swap (stubbed by design — adapter contract) |
| 2-op algorithm tree | FM (modulator → carrier) or additive | **AGREE** — modulator-domain + additive voices verified on both |
| 4-op connections | 8 algorithms (3 operators into carrier), enabled by `0x04` mask + NEW | **AGREE** — master-bit connection select adopted (§4.2 #2 resolved); the loud/quiet row set is identical on both |
| Rhythm mode | 5 percussion voices from channels 7/8 operators, `0xBD` key-ons | Five-voice audible matrix **AGREE** on both; **B0-kon suppression DIVERGES** (§4.2 #6 — ymfm keys rhythm channels from B0 kon too; in-tree implements the datasheet suppression) |
| LFO | Tremolo/vibrato, depths via `0xBD` b7/b6 | **AGREE** — `FmAmVibDepthMatrix` |
| 4-channel output | CHA-CHD routed to L/R pairs | **AGREE** — single-path routing matrix since the quad adoption (§4.1 #2, §4.2 #3 resolved) |
| Introspection taps | (not silicon — emulation feature) | In-tree exposes per-channel `channelPeak()` taps; **absent on ymfm** (findings §3.9) |

Evidence density note: the 13 FM sweep families
([`opl4sweep.cpp`](../../../tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp)) — TL ladder,
MULT/pitch, KSL, envelope stages, feedback/algorithm, rhythm, waveforms, AM/VIB depth,
routing matrix, timers, KON momentary, cross-backend envelope rates, and the register-stream
compare — are what back the AGREE rows above; every AGREE means "differentially exercised,
no divergence found", not "silicon-proven".

---

## 6. Reading the table: three layers of "the same chip"

- **Layer 1 — bus & register file.** Bank decode, aliasing, NEW/NEW2, timers, status OR,
  busy. 100% AGREE, audited against openMSX. This is why detection and init code behaves
  byte-identically on both backends (the MFM guest streams match write-for-write).
- **Layer 2 — synthesis semantics.** Pitch, envelope-family behaviour, TL, release, LFO,
  feedback, rhythm voices, KON edges. AGREE at the map-agnostic level, with per-backend
  exactness bands (the 14 guarded checks) where the engines' internals differ.
- **Layer 3 — identity: the operator map, routing polarity, defaults.** RESOLVED
  2026-09-15 — the in-tree engine adopted the classic/silicon map (§4.1). Judged strictly
  against a YMF262 register map, **both engines are compliant now**; the MFM white-noise
  story is history (§8).

---

## 7. The "fully comply" assumption — status per reference

The assumption: *OPL4's FM half is YMF262-compatible, so it should fully comply and pass
co-simulation against ymfm, openMSX and HDL.* The premise is right — the register file and
feature set of §5 are the YMF262's, plus two OPL4 extensions — but "fully comply" has to be
asked per reference, and today the answer differs:

1. **vs ymfm** — co-simulation and the comparator/sweeps pass **accommodation-free on the
   FM side** since the adoption: 6124 in-tree vs 6110 ymfm checks, zero failures, cosim
   6/6 scenarios with single-path classic register scripts, self-oracle 55/55 with the
   golden regenerated (12 FM digests changed, every PCM-only digest byte-identical). The
   14 remaining guarded checks cover tap instrumentation (ymfm zeroes per-channel taps),
   the adapter's ×4 output scale, engine-internal envelope counters and the one live
   divergence (rhythm B0-kon suppression, §4.2 #6). On the ymfm backend a co-sim against
   ymfm is circular (same engine); meaningful arbitration needs an independent reference.
2. **vs openMSX** — the de-facto OPL4 reference, but GPL: diffed against, never vendored.
   Bus semantics (banks, aliasing, NEW/NEW2, timers, status, LD window) are verified
   against it — fully compliant. openMSX's FM half is itself a software model (an OPL3
   engine embedded in its `YMF278`), so it is a strong second opinion, not ground truth;
   the open corners below would need arbitrating against it too.
3. **vs HDL** — the co-simulation tier ([`opl4-core-tdd.md`](opl4-core-tdd.md) §12.4,
   MSX1_MiSTer OPL4 core) is **designed but not executed**. Caveat on the reference chain:
   that core's FM was itself validated against Nuked-OPL3 (core-tdd §13 reference table) —
   so even the HDL tier ultimately chains back to software references. Nuked-OPL3
   (decap-derived YMF262) is the strongest FM ground truth available, but YMF278B-specific
   corners are not decap-verified anywhere.

**Bottom line.** Compliance is currently provable only *relative to a chosen reference*:

| Claim | Status |
|---|---|
| Bus / register-file compliance vs openMSX | **proven** (both backends) |
| Pitch/level/timer/envelope-family parity vs ymfm | **proven** (map-agnostic checks) |
| Full classic-stream rendering compliance vs ymfm | **true on both backends** (adoption landed 2026-09-15; §4.1, §8) |
| Absolute silicon compliance (ws 3/5, PCM cadence, S == 0) | **unproven for everyone** — parked on the HDL + hardware-recording tier |

What remains to make the assumption hold end-to-end: execute the §12.4 HDL tier and
hardware recordings to arbitrate the open corners — ws 3/5 silicon truth, the rhythm
B0-kon suppression, and the PCM-side quirks (§9).

---

## 8. Guest-level impact: the MFM Music sample case (2026-09-15)

The corpus disk `MFM Music sample 1` (`testdata/sound/moonsound/mfm_sample.trd`) is the
first **whole-disk reproducer** of the classic-map divergences. Symptom report: *FM plays at
very low amplitude compared to PCM, hisses, drops notes.* Per-melody guest tests
([`moonsound_mfm_guest_test.cpp`](../../../core/tests/emulator/sound/moonsound_mfm_guest_test.cpp),
fixtures persisted at `testdata/sound/moonsound/mfm-sample/`) play both tunes through the
real MBPlayer code and measure the rendered FM buffer at 1x speed. **Guest execution is
byte-identical on both backends** (same card-port write counts, same MBPlayer position
advancement); only the rendering differs:

| 150-frame window | in-tree pre-adoption | in-tree post-adoption | ymfm (classic) | white-noise reference |
|---|---|---|---|---|
| melody 1 (BCAREFUL) fmRms | 66.7–351.6 | 2154 | 2635 | — |
| melody 1 HF ratio / ZC rate | 1.402 / 0.461 | 0.251 / 0.054 | 0.195 / 0.031 | √2 ≈ 1.414 / 0.5 |
| melody 2 (MELODIES) fmRms | 66.7 | 1891 | 1514 | — |
| melody 2 HF ratio / ZC rate | 1.411 / 0.495 | 0.623 / 0.146 | 0.745 / 0.174 | √2 ≈ 1.414 / 0.5 |

Pre-adoption, in-tree rendered both tunes as statistical white noise at 1/7–1/23 of the
classic-map amplitude, with 2–4 of 18 FM channels audible — the reported hiss-and-quiet FM,
exactly what divergences 4.1 #1–#3 predict for a classic-driver register stream. Post-
adoption (2026-09-15) both tunes render healthy and tonal on the default tree, within ±25%
RMS of the ymfm reference. Both tunes are "MB FOR MOONSOUND FM" FM-only tunes: PCM silence
is correct guest behaviour, so the low-FM-vs-PCM symptom is this rendering, not a mix
imbalance. The ymfm melody-2 HF/ZC (0.745/0.174) are bright but tonal — percussive
content, well clear of the white-noise mark.

Fencing since the adoption: the `MoonSoundMfmGuest_Test.*` tests hold the healthy
reference on **both** backends with identical fences (`#ifdef OPL4_FM_YMFM` branches
removed); the per-channel activity metric stays printout-only (FM channelPeak taps are
in-tree instrumentation, findings §3.9).

---

## 9. ymfm-side quirks (what adopting it costs)

From the full-chip differential role (findings §3) — none patched in the vendored copy (one
local exception, 3.8); each has an accommodation in the adapter, comparator or cosim:

| # | Quirk | One-liner |
|---|---|---|
| 3.1 | PCM unity scale | Quarter-size output on the "mixing details need verification" path |
| 3.2 | No PCM interpolation | Fractional position bits ignored |
| 3.3 | PCM envelope clocking | 2× faster at mid rates |
| 3.4 | PCM end S == 0 corner | Wrap-every-step vs 64 KiB one-shot |
| 3.5 | Register 0x04 storage | RST bit ORed into the stored byte |
| 3.6 | `reset()` | Does not clear the address latch |
| 3.7 | `regs()` accessor | Mutable-only |
| 3.8 | `save_restore` | Single non-const entry point for both directions (the one local patch — TTD purity pin, §3.8) |
| 3.9 | Per-channel taps | **Not available** — why the guest metrics tests read `fmChannelsAudible = 0` on the ymfm tree |
| 3.10 | `ymf278b` output routing | PCM primary pair not exposed |

Which behaviour matches YMF278B **silicon** for ws 3/5 (§2.2 #1), the PCM mid-rate cadence
(3.3) and the S == 0 corner (3.4) is unresolved — those need hardware recordings (the HDL /
hardware tier, [`opl4-core-tdd.md`](opl4-core-tdd.md) §12.4);
the log keeps them banded, not asserted equal.

---

## 10. Practical guidance

- **Default tree** (compact model): everyday development; self-contained, full 16-bit
  output, per-channel taps, native TTD schema. Since the classic-map adoption it renders
  period FM drivers correctly (§8) — same register semantics as the ymfm reference.
- **ymfm tree**: the second implementation of the same classic map — use it for A/B
  measurements of any FM rendering question. Costs: the ×4 adapter scale, the ymfm quirks
  (§9 below; findings §3), no per-channel taps, PCM primary pair unexposed, and 14
  checks guarded out of the PoC suites (6124 in-tree vs 6110 ymfm checks, both zero
  failures).
- **TTD sessions never cross backends** (`kStateVersion` 3 in-tree / 2 ymfm).

---

## 11. The open decision — CLOSED 2026-09-15

The in-tree compact model **adopted the classic operator map** (plus include-routing with
the CHC/CHD quad, carrier attack, the ymfm ws 3/5 tables, KSL consumption semantics and
the 4-op master bit). The adoption bugs found on the way are logged in the findings doc
§2.3; verification: PoC suites 6124/6110 zero-failure, cosim 6/6 accommodation-free on the
FM side, self-oracle 55/55 (golden regenerated, PCM untouched), both core trees green, MFM
guest tunes healthy on both backends. What remains open is hardware-tier arbitration of
the surviving engine-level questions (findings §5): ws 3/5 silicon truth, rhythm B0-kon
suppression, and the PCM-side quirks.

---

## 12. Reproducing the numbers

```bash
# PoC suites (from tools/poc/015-opl4-synthesis)
./bin/opl4tests        # in-tree backend: 6124 checks / 0 failures
./bin/ymfm/opl4tests   # ymfm backend:    6110 checks / 0 failures
# FmCompare asserts the classic-map match (rms 5844 vs 5861, pitch 1.0000)

# Full-chip differential (from tools/poc/015-opl4-synthesis; run from cosim/)
cd cosim && ./bin/cosim-ymfm   # 6/6 scenarios

# Guest-level A/B (the §8 table)
./cmake-build-release/bin/core-tests --gtest_filter='MoonSoundMfmGuest_Test.*'
./cmake-build-ymfm/bin/core-tests  --gtest_filter='MoonSoundMfmGuest_Test.*'
```
