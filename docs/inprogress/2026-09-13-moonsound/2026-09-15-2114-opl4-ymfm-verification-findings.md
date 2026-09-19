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
**Companions:** [`2026-09-14-1828-opl4-openmsx-audit-and-diff-harness.md`](2026-09-14-1828-opl4-openmsx-audit-and-diff-harness.md) §6 is
authoritative for the Revision-5 backend bring-up; [`2026-09-13-0217-opl4-core-tdd.md`](2026-09-13-0217-opl4-core-tdd.md) §12.2/§13.2 for
the chip-model view; [`2026-09-13-0217-opl4-unreal-ng-integration.md`](2026-09-13-0217-opl4-unreal-ng-integration.md) §12.2 for the current
suite inventory; [`2026-09-15-2114-opl4-fm-backend-comparison.md`](2026-09-15-2114-opl4-fm-backend-comparison.md) is the narrative explainer
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

### 2.4 Register rate 0 must freeze — the JAMMED2 pad wash (2026-09-17)

Guest-level co-simulation (three engines on one captured register stream,
`scratch/replay3way.cpp`) exposed a semantics bug the suites missed because none of
their voices sustain on a rate-0 segment: **`EgRate` applied the KSR keycode even at
register rate 0**, so AR0/DR0/RR0 segments "crept" at the keycode-scaled rate
instead of freezing. Both references freeze outright — ymfm's `effective_rate`
(`ymfm_fm.h` L145) returns 0 when `rawrate == 0` *before* adding ksrval, and
Nuked-OPL3 gates every envelope increment on `reg_rate != 0` (`opl3.c` L430); the
keycode only accelerates nonzero register rates (total rate 4..63).

The audible case: mfm_sample_3 module 5 (JAMMED2.MFM) pads — ins2 modulators are
AR15/**DR0**/EGT1, and the captured stream held every C0 at FB7-additive — an
artifact of the FM data-port read-back defect (§2.5), not the demo's intent. Under
silicon semantics the modulator holds its attack peak forever, keeping the
feedback loop at full gain: a sustained broadband wash (both references, zc ≈
0.44). In-tree, the DR0 creep decayed the modulator to SL6 — 18 dB below the
loop-gain boundary — collapsing the wash into a quiet limit-cycle sine (zc ≈
0.02) beating detuned against its neighbours: the reported "dirty, highly
quantized" chord texture. Fix: `EgRate` early-returns 0 at `regRate == 0` and
every `AdvanceEnvelope` state breaks on rate 0; NTS (0x108 bit 6 — fnum bit 8 vs
bit 9 for the keycode LSB) decoded on the way past, as both references do and the
demo's 0x108 = 0x40 requires.

Verified: `scratch/fbprobe.cpp` (isolated loop, three engines agree to 3 decimals
at the guest operating point — tree 0.4387 / ymfm 0.4382 / Nuked 0.4380 zc at
TL18/FB7, was 0.0204 in-tree), `scratch/replay3way.cpp` on the captured stream
(all five pad channels ch7/8/11/13/15 flip tonal→NOISE, tree ≈ ymfm within 2%),
guarded by `CompareDr0PadVoice` (`tests/opl4fmcompare.cpp`).

**Same class confirmed on module 7 (MATIN, 2026-09-18).** The mfm3 guest
diagnostic (`Mfm3_Module7_Matin_Diagnostic`) captures the same construction: a
six-channel auto-panned pad bank (0-based ch6/7/8 out=L, ch10/12/13 out=R)
carrying the identical Roymans-kit patch — mod M1/EGT1/KSR1/TL19/AR15/DR0/SL6/RR7,
car TL20/AR15/DR1/SL9/RR5 — found verbatim at `matin.mfm` offset 0x29 with
**file fb_con = 0x0D (FB6+ADD)**, rewritten by the per-tick stereo pan pass to
0x0F (FB7+ADD, captured `C0=3C` then `C0=3F`) — which §2.5 explains as the RMW
pan pass reading 0xFF off the then-unclaimed FM data port, not the author's
intent. Tooling caveat: the corpus `MATIN.MFM.json` instrument table is
mis-sliced by one operator (evidence: `insN.car` ≡ `insN+1.mod` byte-chain; the
captured patch appears in no decoded slot) — matin instrument fields from that
JSON must not be trusted without byte-checking the binary.

### 2.5 FM data-port read-back — the MFM pan-pass corruption (2026-09-18)

The YMF278B answers FM data-port reads with the last-written value of the
latched register: openMSX's `MSXMoonSound::readIO` returns
`ymf262.readReg(opl3latch)` for #C5/#C7, and MoonBlaster-class players depend
on it — the pan pass read-modify-writes each channel's C0 (`in a,(c)` /
`and 0Fh` / rewrite with the pan bits; the original MSX driver in mfm_sample_01
does exactly the same), so the feedback/connection nibble survives only if the
read returns the register.

`portDeviceClaimsRead` claimed only #C4, so a #C5 read floated to 0xFF and
every C0 became 0x0F|pan = FB7 additive for the rest of the song; DR0-
sustained pad modulators then self-oscillated into broadband noise. This
overturns the 2026-09-17 "authentic" verdicts (sample-2 hiss, sample-3
JAMMED2/MATIN): the reference engines agreed with the noise because they were
fed the same corrupted stream — register-traffic agreement cannot certify a
value the guest only ever saw wrong.

Fix: the device claims #C5/#C7 reads unconditionally (the register file exists
in every arming state) and returns the FM shadow through `FmBus::Read` /
`Opl4::ReadFm` for the latched register, with the bank taken from the most
recent address-port write — the same single-strobe rule the write decode
applies, including the bank-1 → bank-0 aliasing before NEW. The bank cannot
live in the data port itself: the card author's MBPlayer strobes registers via
#C6 and data via #C5 (`MBPlayer_out_fm2`), which a per-port bank rule would
misroute. Fenced by `MoonSoundDevice_Test.FmPorts_DataReadbackReturnsRegisterFile`.

Verified post-fix: MATIN's init pairs flip `C0=3C→3F` to `C0=1C→3C` (FB6 and
pan both preserved — the upload write keeps the pan nibble the same way), all
18 channels read tonal (mix hf 0.50 → 0.058), JAMMED2's pad bank and the four
sample-2 hiss melodies turn tonal, and `replay3way` on the corrected matin
capture (`scratch/mfm3-dirty-capture-module7-matin_78456.csv`) reads tonal on
all three engines. Open hardware question: the ZXM card's CPLD read
pass-through is inferred (the enable logic does not appear to gate on read vs
write; MoonService and openMSX both depend on it) but not traced end-to-end.

### 2.6 MFM sample 4 / HAPERT "F13" — register stream clean (2026-09-18; verdict superseded by §2.7)

Symptom: melody 1 (HAPERT, the boot tune of `mfm_sample_4.trd`) reported dirt
on F13 — either hardware channel 13 or FM step 13 (hardware channel 2); both
play the accordion (instrument 3: FB7-FM, modulator TL32 = 24 dB down, AR2
slow attack) and the FB6-additive pluck (instrument 4) with one change each
way. Operator maths was already ruled out (both instruments clean and periodic
under libopl4 and ymfm for all 16 fbconn values except instrument 4 at FB7),
leaving three register-stream suspects: bank-1 port routing for ch13, the
unanswered #C7 read-back, and a mis-landed 0x104 pairing ch13 with ch10.

The guest suite (`moonsound_mfm4_guest_test.cpp`, fixtures staged from the
author's build tree — the TRD's raw-sector player and HAPERT.MFM are
byte-identical to `moonsound.bin`/`HAPERT.MFM` there) settles all three on
the fixed build: 0x104 is written exactly twice in the whole tune (init and
cleanup, both 0 — zero 4-op chains), the #C6/#C7 bank-1 lanes deliver the
authored bytes (ch13's register line is the quoted instrument 3 verbatim),
and the pan-pass RMW preserves the feedback nibble (`3E→1E`/`3E→2E` at f14;
pre-fix these collapsed to `0x0F|pan` = FB7 additive). Authored C0 restores
land at every section change (f3138/f4489/f5826/f7177 — the predicted
instrument-change restore), so the pre-fix corruption window on the F13
voices was f3152..f4489 and the pluck bytes from f4489 were authored-clean
even then.

Verdict: **no reproducible dirt anywhere in HAPERT post-fix.** ch13 and ch2
first sound together at f3152 (the F13 section entry) and measure tonal
through attack and sustain (hf 0.10/0.33 and 0.09/0.09); the burst-aligned
sweep finds all 18 channels tonal or silent; the whole ~3-minute tune walks
clean under per-round mix metrics (dense-final-section hf 0.45-0.67 at
zc ≤ 0.18 — bright tonal, vs the 1.4/0.5 white-noise band); the ymfm backend
replays byte-identical register traffic (55294 writes, same KON frames); and
the offline three-engine replay of the deep capture
(`scratch/replay3way-mfm4-hapert.log`, stream
`scratch/mfm4-hapert-capture-melody1-hapert-deep_50906.csv`) is tonal in all
54 engine × channel combinations. Whatever the audible F13 dirt was, it lived
on the pre-fix build.

**Superseded (same day):** the register stream is clean, but the dirt was real
and lived in the in-tree FM synthesis — see §2.7. The hf/zc noise metrics used
here cannot see it (a click followed by a smooth fade reads "tonal"); only a
per-channel waveform-shape comparison against ymfm/Nuked exposes it.

### 2.7 FM key-on, envelope floor and NTS — the HAPERT F13 root cause (2026-09-18)

Method: every captured MFM stream (`scratch/mfm{2,3,4}-*capture*.csv`, 7
captures) replayed through in-tree, ymfm and Nuked with each FM channel
soloed at register level (other channels' C0 output bits cleared), compared
per 2-frame window by best normalised cross-correlation (lag ±8) and level
ratio. A window counts as an in-tree divergence when in-tree ≁ ymfm while
Nuked ~ ymfm (≥ 0.95). Three defects, all in `fm/fmsynthopl4.cpp`:

1. **Key-on reset the envelope to silence** (`KeyOn`: `envVol = kFmMaxAttIndex`,
   the PCM `KeyOnHelper` rule applied to FM — openMSX's own comment reads
   "Unlike FM, the envelope level is reset"). OPL attack starts from the
   CURRENT level (ymfm `start_attack`, Nuked); the operator **phase restarts**
   (ymfm `m_phase = 0`, Nuked `pg_reset`), which the engine never did; and the
   key is **sampled once per clock** (ymfm `clock_keystate`, Nuked's per-sample
   key edge) — a KOFF/KON pair written between two clocks is no transition.
   MoonBlaster re-keys every note as KOFF, A0, KON in one burst, so each note
   dropped to 0 (click) and re-attacked from silence; HAPERT's accordion
   (AR2/AR3) faded in over ~0.4 s on every one of its 64 notes. Fix:
   `KeyOn` latches `FmOperator::keyReq`; `ClockKeyState` applies it at the
   start of each `Advance` (phase reset, level kept, instant attack only at
   rate ≥ 63). `keyReq` fills the padding byte after `ksl` (layout size
   unchanged); in-tree `kStateVersion` 4 → 5.
2. **FM envelope floor at −60 dB** — `kFmMaxAttIndex = 0x280` and the shared
   PCM `VolFactor` −60 dB clip (the pending "step 5 re-domain" of
   `fmtables.h`). The OPL3 ceiling is 96 dB (ymfm 0x3FF). Attacks started
   20 dB up (AR1–9 reached −6 dB 17–20 % early vs ymfm; Nuked agrees with
   ymfm to ≤ 5 %), releases/decays and SL15 stopped at 60 dB, and quiet
   modulators (total attenuation past 60 dB) contributed no modulation. Fix:
   `kFmMaxAttIndex = 0x3FF`; operator output through `FmVolFactor`, which
   saturates at the 96 dB ceiling. PCM keeps its own −60 dB domain.
3. **NTS read from bank 1** — `EgRate` read `_regs[0x108]`; NTS is bank-0
   register 0x08 bit 6 (ymfm `note_select() = byte(0x08, 6, 1)`, Nuked
   `nts` on the high == 0 path). Songs that set NTS (FOUNTAIN writes 0x08 =
   0x40) ran every KSR envelope on the wrong keycode bit whenever F-number
   bits 8 and 9 differ: +6 to +8 dB after 1 s on FOUNTAIN ch15's DR3 carrier.

| Shape sweep, 39 442 audible windows | shape divergences | level > 1.4× / < 0.7× | level > 1.18× / < 0.85× | mean corr vs ymfm |
|---|---|---|---|---|
| before | 20 187 | 4 475 | 7 837 | 0.408 |
| + key-on semantics | 5 | 348 | 940 | 0.9945 |
| + 96 dB envelope domain | 3 | 334 | 914 | 0.9948 |
| + NTS bank 0 | 2 | 5 | 16 | 0.9950 |

The two remaining shape windows (HAPERT ch8 f2202/f2216) are an FB7 vibrato
crossing an octave boundary every frame, where ymfm and Nuked also disagree
in phase. Envelope timing per rate (AR/DR/RR 1–15, KSR 0/1, two keycodes)
now matches ymfm everywhere except rate 1 with KSR 0, where in-tree agrees
with Nuked (0.80 vs 0.77) and ymfm is the outlier.

Fences (`tests/opl4fmcompare.cpp`, all four fail on the old engine):
`CompareRetriggerKeepsLevel` (level after/before 1.02, shape 1.000 vs ymfm;
old 0.036), `CompareRekeyAfterGapStartsFromLevel` (0.993× ymfm; old 0.002×),
`CompareSlowAttackTiming` (AR2 1.000× ymfm; old 0.833×), `CompareNtsKsrDecay`
(−0.01 dB; old +6.3 dB). Golden self-oracle regenerated: 25 FM/mix digests
changed, no PCM-only digest; cosim-ymfm 6/6.

Harness note: `scratch/replay3way.cpp` passes a single `int16_t` to
`OPL3_Generate`, which writes a stereo pair — a stack overwrite in its Nuked
lane. Treat earlier Nuked numbers from that tool with caution.

### 2.8 Live core-rate renegotiation — MoonSound stayed at 44.1 kHz (2026-09-18)

Symptom: after the host audio device renegotiated (for example 44100 → 48000),
AY/TS/TSFM followed and MoonSound did not. `SoundChip_Moonsound::setCoreRate`
only recorded the rate: libopl4's output rate was fixed at `Configure()`. The
device kept rendering 44.1 kHz audio into frames sized for the new rate. At
48 kHz the tone played 8.8 % sharp (633.7 Hz instead of 582.5 Hz); at 88.2 kHz
and above half of every frame was silent and the tone was 2–4.4× too high.

Fix: `Opl4::SetOutputRate(rate)` (44100..192000) re-runs the render layer's
`Configure` at the new rate. That layer is the only rate-dependent part: the
chip, FM and PCM streams run on chip grids, so chip state and pending audio are
kept. User settings (board, punch, room) survive. `setCoreRate` calls it, as
the AY path re-derives its PLL and decimators.

The switching test found a second, older bug: **both render paths dropped
resampler output when upsampling.** One input frame emits 2–4 output frames at
88.2–192 kHz, and when the caller's frame cap landed mid-input
(`ProcessGroup`, `ProcessChip`) the rest were discarded. That cost 15–44
samples per second and cut the waveform (−15 dB THD+N on a pure sine). A
per-stage `ResampleCarry` now emits them first on the next call.

Fences: PoC `TestLiveOutputRateSwitch` switches a sustained FM sine through
48 / 88.2 / 96 / 176.4 / 192 / 44.1 kHz in both modes on both render APIs. It
asserts exactly `rate` frames per second and the tone's pitch and purity at
the new rate: HiFi −55…−57 dB, Authentic −36…−50 dB (HoldDrop). Device
`CoreRateRenegotiation_AllStandardRatesRenderFullFramesInTune` goes through
`SoundManager::requestCoreRate`: every rate reaches `Opl4::OutputRate()`, frame
tails carry the tone, and the pitch is within 1 %. On the old `setCoreRate` it
fails at every rate except 44100.

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

Current cost of the accommodations: **6152 checks in-tree vs 6138 on ymfm** (14
checks guarded: tap-based instrumentation plus the two live §2.2 rows), both zero
failures, whole run < 1 s; cosim 6/6 scenarios, cosim-oracle 55/55 — the golden
was regenerated twice on purpose: 2026-09-15 for the classic-map adoption (12 FM
digests) and 2026-09-18 for the rate-0 freeze + NTS semantics of §2.4 (25 FM
digests; every PCM-only digest byte-identical both times — the PCM half is
untouched by FM-side changes).

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
| Register rate 0 freeze (§2.4) | **RESOLVED 2026-09-17** — `EgRate`/`AdvanceEnvelope` freeze at rate 0 + NTS decode; three-engine co-sim agrees; `CompareDr0PadVoice` is the fence | Done — differential + co-sim replay stay the fence |
| FM data-port read-back (§2.5) | **RESOLVED 2026-09-18** — #C5/#C7 read claims returning the `FmBus` shadow (`Opl4::ReadFm`); module-5/7 and the mfm2 hiss melodies render tonal, three-engine replay of the corrected stream agrees; `FmPorts_DataReadbackReturnsRegisterFile` is the fence | Done — device fence + guest reruns stay |
| MFM sample 4 / HAPERT "F13" (§2.6, §2.7) | **RESOLVED 2026-09-18** — root cause in-tree FM synthesis (§2.7: key-on envelope reset / no phase reset / unclocked key, −60 dB FM floor, NTS bank); register stream itself clean: 0x104 always 0, bank-1 lanes authored, pan RMW preserves fbconn; both F13 candidates tonal on their own note-ons (first KON f3152), whole tune clean on both backends + three-engine replay; pre-fix corruption window was f3152..f4489 | Done — `MoonSoundMfm4Guest_Test.*` stays (battery + DeepScan) |
| Nuked ch2/4/5 near-silent on the JAMMED2 stream (tree ≈ ymfm loud) | Observed 2026-09-17 in `scratch/replay3way` — possibly that tool's `OPL3_Generate` stack overwrite (§2.7 harness note); not yet re-measured with a correct buffer | Re-check with a fixed harness |
| Output stage harshness (2026-09-18) | **RESOLVED** — the synthesis matched ymfm/Nuked; the output stage added the harshness: HoldDrop jitter (D2, now opt-in), a broken `PolyphaseResampler`, a high-pass board filter never configured on the split path, a mixed-`Render()` staging bug, and a whole-step sine table. See [2026-09-18-2045-opl4-output-stage-harshness.md](2026-09-18-2045-opl4-output-stage-harshness.md) | Hardware recording for D2 |
| Live core-rate renegotiation (§2.8) | **RESOLVED 2026-09-18** — `Opl4::SetOutputRate` wired from `setCoreRate`; the upsampling output-drop bug fixed (`ResampleCarry`) | Done — rate-switch fences stay |

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
