# OPL4 ↔ ymfm — verification findings log

**Status:** living log, created 2026-09-15 during the OPL4 conformance-suite close-out;
updated the same day after the **classic-map adoption landed** — the §2.1 divergences are
fixed in the in-tree engine (RESOLVED banners below, adoption bugs in §2.3), the
co-simulation runs accommodation-free on the FM side, and the MFM guest tunes render
healthy on both backends (§2.1 table).
updated the same day after the **classic-map adoption landed** — the §2.1 divergences are
fixed in the in-tree engine (RESOLVED banners below, adoption bugs in §2.3), the
co-simulation runs accommodation-free on the FM side, and the MFM guest tunes render
healthy on both backends (§2.1 table).
**Scope:** every issue ymfm surfaced during OPL4 verification, in **both directions** —
(a) divergences and bugs the A/B exposed in the in-tree engine, and (b) quirks and
limitations of ymfm itself that the harness had to accommodate. Each entry cites the
code or run that evidences it.
**Companions:** [`opl4-openmsx-audit-and-diff-harness.md`](opl4-openmsx-audit-and-diff-harness.md) §6 is
authoritative for the Revision-5 backend bring-up; [`opl4-core-tdd.md`](opl4-core-tdd.md) §12.2/§13.2 for
the chip-model view; [`opl4-unreal-ng-integration.md`](opl4-unreal-ng-integration.md) §12.2 for the current
suite inventory; [`opl4-fm-backend-comparison.md`](opl4-fm-backend-comparison.md) is the narrative explainer
of the two FM backends and the MFM guest-level confirmation. Nothing in this file changes
behaviour — every finding below is
already encoded as a guard, an adapter accommodation, or a documented divergence;
this document collects them in one place.

---

## 1. Where ymfm sits in the verification stack

ymfm (vendored at pinned commit 81aec25, `core/src/3rdparty/ymfm/`) is used in two
distinct roles; findings below say which one produced them.

| Role | Artifact | What runs on ymfm |
|---|---|---|
| **FM backend swap** | [`Opl4FmYmfm`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h) adapter, selected by `CMake -DOPL4_FM_BACKEND=ymfm` (defines `OPL4_FM_YMFM`) | FM synthesis only (ymf262 core): phase, envelopes, connections, rhythm, 4-op, LFO. Bus-visible semantics — bank-1 aliasing, 0x105 NEW/NEW2, timers T1/T2, status bits — stay in the audited base class; ymfm's own timer/busy/IRQ machinery is stubbed and never advanced ([`opl4fmymfm.h`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h) L10–18, L42–49). The whole PoC suite and the whole core suite re-run green on this build. |
| **Full-chip differential** | [`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) vs ymfm's own `ymf278b` (6 scenarios, 6/6) | Both halves, including ymfm's PCM wave part — the corner the core TDD §13.2 calls "the least-developed corner of the library". Drives both engines from the same WaveMemory image and register scripts. |

Adapter-level contract that made the swap trustworthy: one `generate()` call per
684-clock FM boundary — the exact cadence ymfm's own ymf278b uses (OPL4's 49516.4 Hz
FM grid; note YMF262 silicon itself runs 49716 Hz, so the adapter's clocking, not
ymfm's default rate constant, defines the pitch). The comparator confirmed pitch
ratio 1.0000 on identical voices.

---

## 2. Findings in the in-tree engine (ymfm as the reference)

### 2.1 The three classic-map divergences — `FmCompare`

Feeding one **real OPL3 driver's register bytes** into both engines (the A/B's whole
purpose) reports DIVERGENT — three stacked divergences, none of them state-visible
([`opl4fmcompare.cpp`](tools/poc/015-opl4-synthesis/tests/opl4fmcompare.cpp) L1–24, L251–302; audit doc §6.2):

1. **Operator register map.** In-tree is linear (`op = reg − 0x20`, channel *i* pairs
   operators {2i, 2i+1}); YMF262 silicon — and ymfm — use the classic layout
   (operator groups at +0/+8/+0x10 with the 0x26/0x27 and 0x2E/0x2F gaps, channel *i*
   pairs {i, i+3}; the channel-0 carrier lives at 0x23/0x43/0x63/0x83). Identical
   driver bytes program the wrong operators.
2. **0xC0 routing polarity inverted.** In-tree bits *exclude* a side when set
   (0x30 = fully silent, 0x00 = both); YMF262 CHA/CHB *include* (0x30 = both). A
   classic driver's routine `0xC0 = 0x31` mutes us, not ymfm.
3. **Unconfigured carrier has AR 0** in the in-tree engine and never attacks. This
   compounds 1: when envelope writes miss the real carrier, the voice stays silent
   rather than merely wrong.

Pre-adoption measured verdict (2026-09-15 run, script uses carrier TL 0x10): identical
bytes, rms ours = 0 vs ymfm = 5861, pitch ratio 0.0000 → DIVERGENT. The Revision-5
measurement with the original script recorded 1465 (audit doc §6.2).

**RESOLVED (2026-09-15).** The in-tree engine adopted the classic YMF262/silicon
semantics: the classic operator register map (slot-indexed operator storage — 22 slots
per bank, the 0x26/0x27 and 0x2E/0x2F gap slots stored-but-unreferenced, bank-1 rhythm
slots at indices 38–43, which is why `_ops` had to grow to 44 entries — §2.3),
include-semantics 0xC0 routing with the full CHA/CHB/CHC/CHD quad, unconfigured-carrier
attack, the ymfm/silicon ws 3/5 tables, ymfm KSL consumption semantics and 4-op
master-bit connection select. The comparator block now asserts the match: **rms ours
5844 vs ymfm 5861, pitch ratio 1.0000 → agree**. Adoption bugs found and fixed on the
way are logged in §2.3.

**Guest-level confirmation (2026-09-15, MFM Music sample disk).** The corpus disk
`MFM Music sample 1` (user symptom report: *FM plays at very low amplitude
compared to PCM, hisses, drops notes*) is a whole-disk reproducer of §2.1: its
MBPlayer writes the classic YMF262 operator-map stream. Per-melody guest tests
(`core/tests/emulator/sound/moonsound_mfm_guest_test.cpp`, harness-staged launch,
fixtures persisted at `testdata/sound/moonsound/mfm-sample/`) play both tunes
through the real player and measure the rendered FM buffer at 1x speed. Guest
execution is byte-identical on both backends (same FM/wave register-write counts,
same MBPlayer position advancement 0→1 / 1→3); only the rendering differs:

| 150-frame window | in-tree pre-adoption | in-tree post-adoption | ymfm (classic) | white-noise reference |
|---|---|---|---|---|
| melody 1 (BCAREFUL) fmRms | 66.7–351.6 | 2154 | 2635 | — |
| melody 1 HF ratio / ZC rate | 1.402 / 0.461 | 0.251 / 0.054 | 0.195 / 0.031 | √2 ≈ 1.414 / 0.5 |
| melody 2 (MELODIES) fmRms | 66.7 | 1891 | 1514 | — |
| melody 2 HF ratio / ZC rate | 1.411 / 0.495 | 0.623 / 0.146 | 0.745 / 0.174 | √2 ≈ 1.414 / 0.5 |

Pre-adoption, in-tree rendered both tunes as statistical white noise at 1/7–1/23 of
the classic-map amplitude — the reported hiss-and-quiet FM, with 2–4 of 18 FM
channels audible (missing notes). Post-adoption (measured 2026-09-15 on the default
tree) both tunes render healthy and tonal, within ±25% RMS of the ymfm reference —
independent implementations, comparator-locked at the PoC level. The ymfm melody-2
HF/ZC (0.745/0.174) are bright but tonal — its percussive content. PCM is
legitimately silent: both tunes are "MB FOR MOONSOUND FM" FM-only tunes, so the
low-FM-vs-PCM symptom is this rendering, not a mix imbalance. Fencing: since the
adoption the `MoonSoundMfmGuest_Test.*` tests hold the healthy reference on **both**
backends with identical fences (`#ifdef` removed); the per-channel activity metric
stays printout-only because the FM channelPeak taps are in-tree instrumentation
(§3.9).

Where the engines **agree** (same comparator): flags/NEW/NEW2/bank aliasing, timers
T1/T2 step-for-step, the full 512-byte register file (one exclusion, §3.5), pitch
ratio 1.0000 (zero-cross/sample 0.00600 both), level ratio 0.999 after the adapter's
×4 scale (22966 vs 22987 RMS), release-tail parity (407.2 M vs 415.1 M |sample| sum),
and adapter save/restore exactness (2000-sample lockstep, §3.8).

### 2.2 Sweep-suite divergences (post-adoption status)

From running the 13 FM sweep families on **both** backends
([`opl4sweep.cpp`](tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp)). The classic-map
adoption (§2.1) resolved the first four rows; the last two are the live set:

| # | Divergence | Status / disposition |
|---|---|---|
| 1 | **Waveforms ws 3 and ws 5** — pre-adoption the in-tree square-derived set (ws 3 = one double-frequency sine cycle then zero; ws 5 repeats ws 4) genuinely differed from ymfm's table (ws 3 = \|sin\| on quarters 1/3, zero on 2/4; ws 5 = two \|sin\| humps then zero); ws 0/1/2 and 4/6/7 always agreed. | **RESOLVED 2026-09-15** — the in-tree engine adopted the ymfm/silicon tables; the exactness checks run unguarded on both backends. Which table matches YMF278B silicon stays a hardware-tier question (§5). |
| 2 | **4-op connection-select key bit** — pre-adoption in-tree keyed on the **slave** C0 bit; ymfm algorithms 8–11 key on the **master** bit (algs 9/11 add O1). | **RESOLVED 2026-09-15** — master-bit semantics adopted; the loud/quiet row set is identical on both backends now. |
| 3 | **C0 quad bits CHC/CHD** — pre-adoption unmodelled in-tree (bits 4/5 per-side mutes, 6/7 inert); ymfm models the full OPL4 quad (CHA/CHB pair A, CHC/CHD pair B summed into L/R — a side is audible when *any* of its enable bits is set). | **RESOLVED 2026-09-15** — full quad modeled; `FmRoutingMatrix` runs a single path on both backends. |
| 4 | **KSL exact folded table** — pre-adoption in-tree-specific bands (3 dB/block, 1.5 dB per fnum bit 6, block-0 16-unit corner). | **RESOLVED 2026-09-15** — ymfm consumption semantics adopted (`<< ksl`, not `<< (3+ksl)` — the table numerals are consumed directly in 0.09375 dB index units against a TL already pre-scaled `<<3`, `ymfm_opl.cpp` L327); folded table identical on both. |
| 5 | **Envelope exact shift-ladder mid-range ratio** — compact model vs ymfm 5.11 counter. | Live by design: internal counter models differ. Guarded; superseded map-agnostically by `FmEnvelopeRatesVsYmfm` — the OPL-family ~2× half-level-time per rate index holds on **both** backends. |
| 6 | **Rhythm B0-kon suppression** — REAL divergence (measured 2026-09-15): with rhythm enabled and nothing keyed via 0xBD, ymfm still sounds a B0-keyed channel-7 voice (suppressed 0.576 ≈ keyed 0.580 RMS); the in-tree classic engine implements the YMF262 datasheet suppression — the voice only sounds once its 0xBD bit keys it. | Guarded `#if !defined(OPL4_FM_YMFM)` with the measurement in-code; the five-voice audible matrix itself runs on both. Silicon arbitration on the hardware tier (§5). |

### 2.3 Bugs the adoption itself surfaced (suites as the fence)

Adopting the slot-addressed classic map exposed three latent engine-layout bugs, each
caught by the PoC suites before anything shipped:

1. **`_ops` was sized 36, but the classic map is slot-indexed.** Bank-1 rhythm slots
   16..21 land at indices 38–43 — past a packed 36-entry array — so bank-1 register
   stores wrote out of bounds and channel reads pulled garbage: an *idle* engine
   ticked +759 per side (the channel-15 tap read `_ch` memory as an operator), which
   after the default chip mix surfaced as a constant **+0.002044678 float DC on both
   channels of every mixed frame** — corrupting even pure-PCM golden vectors (+268
   stream scale; 171 failures at the first adoption build). Fix: `kOperatorCount` 44
   with the register-slot layout; ASan-verified clean afterwards.
2. **Bank stride was 18 (usable operators), not 22 (slots).** Bank-1 channel 0's
   operators aliased bank-0 channel 8's rhythm TOM/CY — the rhythm voice bled into
   melody channel 9. Fix: `opBase = bank ? 22 : 0` in `WriteReg` and the same stride
   in `Reset`'s channel→slot map.
3. **KSL shift double-counted the TL pre-scale** (`<< (3+ksl)` — 8× too strong).
   Fix: `<< ksl`, plus the sweep/vector expectation updates that follow from the
   ymfm consumption semantics (3 dB/oct at reg 01's ×4 slope, 0.75 dB per fnum bit 6
   at that slope, block-0 table clamp).

---

## 3. Findings in ymfm itself (quirks the harness accommodates)

These are ymfm-side behaviours discovered while wiring the two roles above. None
were patched in the vendored copy (the single exception is noted in 3.8); each has
an accommodation in our adapter, comparator, or cosim.

### 3.1 PCM unity scale — quarter-size "mixing details need verification" path

ymfm's PCM unity chain is `(x*8168)>>15` — roughly **1/4 scale** — while libopl4
rounds through the triple `(x*2047)>>11` mantissa chain (envelope, TL, pan each).
ymfm's own source flags this corner as unverified. Consequence: cosim position
decoding estimates a per-engine empirical scale from frame 0 instead of assuming
one ([`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) L22–30, L191–236). On the FM side the
raw ymf262 core similarly runs ~13-bit intermediate headroom; the **adapter**
compensates with a ×4 scale so backend-swapped builds level-match (sweep constant
`kFmCarrierRmsMin`, [`opl4sweep.cpp`](tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp) L42–50).

### 3.2 No PCM interpolation — fractional position bits ignored

ymf278b's `fetch_sample` ignores the fractional position bits, so its output is a
zero-order hold at integer steps. The cosim position traces therefore run integer
steps (fnum 0); libopl4's interpolator is covered separately by `VecInterpGolden`
([`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) L28–30). Any future fractional-step differential
against ymfm needs a different oracle.

### 3.3 PCM envelope clocking — 2× faster at mid rates

libopl4 gates envelope rows every `2^(12-rate/4)` samples (Valley Bell lineage,
per the core TDD); ymfm's 5.11 fractional counter nets `2^(11-rate/4)` — measured
**exactly 2× faster at rate 32** (1024 vs 2048 samples per 6 dB). Both agree at
rate 0/15 and on sustain plateaus. The cosim envelope scenario compares each
engine against its own unity reference and accepts a `[1.6, 2.4]` band around the
documented 2× ([`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) L31–36, L539–552). Which cadence matches
silicon is unresolved until the hardware-recording tier.

### 3.4 PCM end S == 0 corner — wrap-every-step vs 64 KiB one-shot

With loop end complement S == 0 (a full 64 KiB sample), libopl4 follows openMSX's
chip comparator (commit 5426b4b1: `pos + S >= 0x10000` never trips → linear
one-shot), while ymfm decodes `end == 0` and **wraps every step** (`pos +=
increment + loop`). The cosim `end0-degenerate` case checks each engine against
its **own** model step (1 for ours, `increment + loop` for ymfm) rather than
against each other ([`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) L43–47, L275–298, L359–364). This is
the one cosim scenario that is an own-model check by design.

### 3.5 Register 0x04 storage — RST bit ORed into the stored byte

ymfm ORs the RST bit into the stored 0x04 byte; Opl4Fm stores it raw. The
comparator's 512-byte storage sweep skips 0x04 (plus 0x104/0x105 choreography
registers) for exactly this reason ([`opl4fmcompare.cpp`](tools/poc/015-opl4-synthesis/tests/opl4fmcompare.cpp) L146–171).

### 3.6 `reset()` does not clear the address latch

`ymf262::reset()` leaves `m_address` untouched; the adapter's `Reset()` explicitly
clears it via the exposed `ResetAddress()` ([`opl4fmymfm.h`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h) L52–64). Without
this, a save/restore or chip-reset sequence could pair a stale latch with a fresh
register file.

### 3.7 `regs()` accessor is mutable-only

`opl_registers_base::read()` is const, but the `regs()` accessor is not — the
adapter's `RegByte()` (used by the comparator and diagnostics) performs a
`const_cast` ([`opl4fmymfm.h`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h) L74–78).

### 3.8 `save_restore` — single non-const entry point for both directions

ymfm exposes one non-const `save_restore` for save **and** load. The local TTD
patch (PATCHES.md) pins the saving direction pure, so the adapter's `SaveTo()`
const_cast is safe — saving only reads. This is the **only local modification** to
the vendored ymfm sources. Structured save/restore also costs **~15× the in-tree
memcpy** for TTD captures: measured 78.3 ms recorded vs 30.7 ms baseline over 30
frames → 60.8% time share vs the 50% budget (audit doc §6.4). Consequences: the
backend can never be the default while that stands, TTD sessions are per-backend
(the store refuses mismatched layout tags by design), and the capture-cost gate
assertion is guarded for that build. Adapter save/restore correctness itself is
exact — 2000-sample lockstep in `FmCompare`.

### 3.9 Per-channel taps not available

ymfm mixes channels internally; the adapter zeroes per-channel taps (rhythm-mode
taps included) and serves a static `Channels()` stub. Host FM peak meters and
per-channel mute have no effect on the ymfm build's FM output
([`opl4fmymfm.h`](tools/poc/015-opl4-synthesis/src/ymfm/opl4fmymfm.h) L20–26, L123–128).

### 3.10 ymf278b output routing — the PCM primary pair is not exposed

`ymf278b::generate()` produces six outputs: DO0 = FM channels 2+3, DO1 = wavetable
channels 2+3 (the 0x68+s bit-4 output select), DO2 = `(fmout*fmMix +
pcmout*pcmMix) >> 11`. The PCM engine's **primary** stereo pair (normal voices,
pan applied) is not exposed directly — with FM silent and mix code 0, DO2-left is
the PCM left channel scaled by 2042/2048. The cosim taps DO2 accordingly
([`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp) L100–107).

---

## 4. Test-infrastructure accommodations (post-adoption inventory)

The classic-map adoption removed every *map-aware* accommodation: register
addressing, routing polarity, key-on helpers and cosim register scripts are
single-path classic on both backends. What remains is there for one of three
reasons — ymfm-side quirks (§3), engine-internal differences, or genuine live
divergences (§2.2 #5/#6):

- **Single-path shared conventions** ([`opl4sweep.cpp`](tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp) header note):
  `FmOpAddr()` (classic `(c%3)+8*(c/3)`/`+3`) and the include-semantics `kRoute*`
  constants — identical on both backends.
- **Scale constant** (`kFmCarrierRmsMin`): in-tree `16000·kNormScale` vs ymfm
  `4000·4·kNormScale` — the adapter's ×4 level fold, by design (§3.1 headroom).
- **Guard inventory** (`#if !defined(OPL4_FM_YMFM)`), each with in-code rationale:
  - [`opl4sweep.cpp`](tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp): rhythm B0-kon suppression (real divergence,
    §2.2 #6) and envelope shift-ladder exactness (internal counter models differ,
    §2.2 #5 — superseded map-agnostically by `FmEnvelopeRatesVsYmfm`).
  - [`opl4vectors.cpp`](tools/poc/015-opl4-synthesis/tests/opl4vectors.cpp) `VecFm4Op`/`VecFmRhythm`/`VecFmKsl`
    per-channel-peak blocks and [`opl4tests.cpp`](tools/poc/015-opl4-synthesis/tests/opl4tests.cpp) `TestTaps` — the
    ymfm adapter zeroes per-channel taps (§3.9); the assertions pin in-tree
    instrumentation, not map semantics.
  - Core: the `TTD_Capture_Cost_Gate_Test` time-share assertion (§3.8 cost).
- **Backend-aware key-on helpers are gone**: the PoC `KeyOnFmCh0`
  ([`testfw.h`](tools/poc/015-opl4-synthesis/tests/testfw.h)) and the core-side
  `KeyOnFmChannelThroughPorts` are single-path classic now.
- **Cosim accommodations** ([`cosim-ymfm.cpp`](tools/poc/015-opl4-synthesis/cosim/cosim-ymfm.cpp)): FM-side register
  scripts are single-path classic (the `KeyOnFm` `ours` flag is vestigial); the
  per-engine empirical decode scale with a 4% tolerance plus a 2-LSB absolute
  floor, integer-step traces (§3.2), the [1.6, 2.4] decay band (§3.3), the
  own-model S == 0 check (§3.4) and per-engine TL ladders all remain — they cover
  ymfm PCM-side quirks and the by-design output-scale difference, not FM register
  semantics.
- **Vector-suite fallout fixed during backend bring-up** (historical, unchanged):
  `VecFm4Op` moved its level check to **EGT = 1 sustaining voices**, made carriers
  audible at fnum 0x200 / block 4 (~390 Hz) and sets **0xF8 unity FM mix** — see
  the git history / the pre-adoption revision of this file for the full account.

Current cost of the accommodations: **6124 checks in-tree vs 6110 on ymfm** (14
checks guarded: tap-based instrumentation plus the two live §2.2 rows), both zero
failures, whole run < 1 s; cosim 6/6 scenarios, cosim-oracle 55/55 with the golden
regenerated (12 FM digests changed, every PCM-only digest byte-identical — the PCM
half untouched by the adoption).

---

## 5. Open items

| Item | Status | Arbitrated by |
|---|---|---|
| The three classic-map divergences (§2.1) | **RESOLVED 2026-09-15** — classic semantics adopted in-tree (§2.1 RESOLVED banner; adoption bugs §2.3); `FmCompare` asserts the match (5844 vs 5861, pitch 1.0000); the MFM guest tests `MoonSoundMfmGuest_Test.*` hold the healthy reference on **both** backends with identical fences; both core trees green (sole failure: the backend-independent, disk-load-stage `AuthorDisk_Moonsound2`) | Done — cosim + comparator stay the fence |
| ws 3/5 waveform table choice (§2.2 #1) | **RESOLVED at engine level** — in-tree adopted the ymfm/silicon tables; the engines agree. Silicon truth for YMF278B remains unverified | Hardware recordings / §12.4 HDL tier |
| Rhythm B0-kon suppression (§2.2 #6) | Real divergence, measured 2026-09-15: ymfm keys rhythm-mode channels from B0 kon too; the in-tree engine implements the YMF262 datasheet suppression | Hardware recordings; the datasheet reading stands until then |
| PCM envelope mid-rate cadence 2× (§3.3) | Banded, not asserted equal | Hardware recordings |
| S == 0 loop corner (§3.4) | Own-model checks on both sides; openMSX lineage favoured | Hardware recordings |
| ymfm-side quirks (§3.1–§3.10) | Accommodated in-tree; **none upstreamed** — they are accommodations, not fixes we own; the TTD purity pin (§3.8) is local-only by design | — |

---

## 6. Reproducing the numbers

```bash
# PoC suites (from tools/poc/015-opl4-synthesis)
./bin/opl4tests        # in-tree backend: 6124 checks / 0 failures
./bin/ymfm/opl4tests   # ymfm backend:    6110 checks / 0 failures
# FmCompare now ASSERTS the classic-map match (rms 5844 vs 5861, pitch 1.0000)

# Full-chip differential + self-oracle (from tools/poc/015-opl4-synthesis;
# both run from cosim/ — the golden path is relative)
cd cosim && ./bin/cosim-ymfm   # 6/6 scenarios
cd cosim && ./bin/cosim-oracle # 55/55 digests

# Core builds, both backends (MFM guest A/B — the §2.1 table)
cmake -S . -B cmake-build-ymfm -G Ninja -DOPL4_FM_BACKEND=ymfm -DTESTS=ON
./cmake-build-release/bin/core-tests --gtest_filter='MoonSoundMfmGuest_Test.*'
./cmake-build-ymfm/bin/core-tests  --gtest_filter='MoonSoundMfmGuest_Test.*'
```

2026-09-15 post-adoption measured: classic-map voice `rms ours=5844 ymfm=5861,
pitch ratio 1.0000 -> agree`; per-engine voice `pitch ratio 1.0000`, `level ratio
0.999` (22966 vs 22987 RMS), release tails 407.2 M vs 415.1 M |sample| sum; MFM
guest melodies healthy on both backends — in-tree 2154/1891 fmRms vs ymfm
2635/1514, all HF ratios ≤ 0.75.
