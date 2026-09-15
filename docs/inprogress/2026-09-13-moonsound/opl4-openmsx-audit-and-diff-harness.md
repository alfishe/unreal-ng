# libopl4 vs the reference cores — audit results and the differential harness plan

**Date:** 2026-09-14
**Scope:** a suspect-by-suspect diff of our YMF278B model against openMSX
(the de-facto MoonSound reference), the fixes that fell out of it, and the
design of the register-stream differential harness that keeps us honest
from here on.
**Companion:** `opl4-core-tdd.md` (chip library), `opl4-unreal-ng-integration.md`
Revision 4 (device wiring).
**Revision 5** (2026-09-14): §6 records the ymfm OPL3 verification backend —
the license-clean in-tree comparator §4.4 called for — and the three FM
divergences it exposed.
**Post-fix verification (2026-09-14):** with the audit fixes and the ymfm
comparator in place, the card author's complete 26-disk corpus plays FM +
PCM end to end in-app, and the audio path (gain staging, no clipping) is
test-pinned — see `opl4-unreal-ng-integration.md` §12.6 and §12.7
(Revision 5). The one remaining failure is a disk-subsystem loader stall
unrelated to the chip (integration §12.8).

---

## 1. The reference landscape

| Core | License | Role for us |
|------|---------|-------------|
| **openMSX** `YMF278.cc` / `YMF278B.cc` / `MSXMoonSound.cc` | GPL | **The reference.** Correct ROM+RAM map, MoonSound port protocol, busy/status timing, wave-header fetch. Everyone else validates against it; MSX1_MiSTer's PCM golden harness is derived from it. Diff against it, never vendor it. |
| **ymfm** (Aaron Giles) | BSD-3 | Vendorable (already vendored for TSFM — OPN/SSG/ADPCM subset only; its OPL4 PCM core is not in our tree). Sample-generation call shape familiar from TSFM. PCM side less battle-tested on real MoonSound repertoire. |
| **MAME** `ymf278b.cpp` | LGPL (dual, for openMSX) | Historical ancestor of most cores. Its bug comments are a checklist of the classic failure modes; openMSX contributed the looped-sample addressing fix that caused pitch fluctuation. Third opinion. |
| **libvgm** (ValleyBell) | GPL/LGPL | Easiest way to drive a register stream headlessly (VGM replay). |
| blueMSX / Furnace / WebMSX | various | Older or partial OPL4s; Furnace carries both its own and an openMSX-derived core. Background only. |
| **MSX1_MiSTer** fork (muhanpong) | — | RTL to Verilate against, the same way jt03 is planned for TSFM. |

**No Nuked-style decap OPL4 exists** (nukeykt has OPL3/OPN2/OPM/SC-55, not
YMF278B). For the PCM half, openMSX is as close to ground truth as available.

## 2. The symptom triage that motivated the audit

The working hypothesis for "wrong notes, wrong instruments, wrong rhythm
simultaneously" is that it is rarely three bugs — usually one or two upstream
ones (the classic mapping, from MAME's changelog and opl4tech.txt):

- **Wrong instrument** → wave-header fetch off (pointer table at ROM start,
  12-byte headers, base-address computation).
- **Wrong notes** → the octave field treated unsigned; it is signed, negative
  octaves are legal, combined with the sample's base pitch from the header.
- **Wrong rhythm** → not the audio path: either the PCM engine stepping rate
  (24 slots on a 44.1 kHz frame from the 33.8688 MHz clock) or the FM
  timers/IRQ and busy-status bits — a driver polling busy incorrectly also
  writes registers into the wrong window, which corrupts instrument
  selection too. Two symptoms from one root cause.
- **Plausible-but-wrong samples** → ROM/RAM split or mirroring wrong.

## 3. Audit results

Method: line-level diff of our engine against
`openMSX/src/sound/YMF278.cc` (engine), `YMF278B.cc` (chip I/O wrapper —
this is where LD/busy timing lives, real-HW-measured constants), and
`MSXMoonSound.cc` (board wiring).

### 3.1 Already correct (byte-for-byte or algebraically identical)

| Suspect | Our code | openMSX evidence | Verdict |
|---------|----------|------------------|---------|
| Wave-header base | `Opl4Pcm::WriteRegDirect` case 0: `wave < 384 \|\| hdr==0 ? wave*12 : hdr*0x80000 + (wave-384)*12`, `hdr = (regs[2] >> 2) & 7` | `YMF278.cc:596-599` identical, including the reg-2 bit extraction (bits **4..2**) | Match. Our old *comment* ("bits 2..1") was wrong — fixed. |
| 12-byte header decode | bits/start/loop from buf[0..4]; bytes 7..11 rewrite banks 5..9 observably; keyon retriggers | `YMF278.cc:606-621` identical | Match |
| Signed octave | `SignExtend4` into `int8_t oct`; `CalcStep` guards `oct == -8` | `YMF278.cc:215-220, 633` — byte-for-byte, including the historical "shifted 3 positions too far" fix | Match |
| PCM stepping rate | 24 slots advance once per 768-master-clock output step (44100 exact) | same model | Match |
| FM timers | T1 = `(0x100-load)*4`, T2 = `*16`, ticked at the 684-clock FM grid (49516.4 Hz) → 0.08–20.5 ms / 0.32–82 ms | OPL3 datasheet periods; timer flags at status bits 6/5 | Match |
| Memory map | linear ROM `0x000000-0x1FFFFF` + SRAM `0x200000+`, float-high `0xFF` beyond, ROM writes discarded | `YMF278.cc:850-894`: 10 chip-selects `/MCS0../MCS9`, mode bit = **reg 2 bit 1**; for a 2 MiB ROM + 1 MiB SRAM population both modes map identically (SRAM on `/MCS6`+`/MCS7` at `0x200000-0x2FFFFF`) | Equivalent for our board; mode bit left unmodelled (documented) |
| Reg readback quirks | reg 2 `(v & 0x1F) \| 0x20`, reg 6 MA-gated with auto-increment, `0xFF` when MA=0, regs 3/4 mask `0x3F` | `YMF278.cc:774-791` identical | Match |
| Writes during LD | applied immediately, LD only reported in status | `YMF278B.cc` sets no write guard; loadTime only drives status | Match |
| Bus busy constants | 56 (FM write) / 88 (wave write) / 28 (mem write) / 38 (mem read) | `YMF278B.cc` constants identical (real-HW-measured) | Match |

### 3.2 Divergences found and fixed (this revision)

All four were in the "busy flag timing" bucket — exactly the bucket the
triage predicts can produce two of the three symptoms from one cause.

1. **LD status bit: `0x80` → `0x02`.**
   `Opl4::ReadStatus` reported LD on bit 7; the chip reports it on **bit 1**
   (`YMF278B::readYMF278Status`: busy → `0x01`, LD → `0x02`, OR'd onto the
   YMF262 status whose timer flags live at bits 6/5 — no collisions).
   A driver polling `bit 1` for LD never saw our LD window.

2. **LD scope: only tone-load writes open it.**
   We opened a 9600-clock LD window for *any* write to regs `0x02-0x06`;
   the chip opens LD **only** for writes to regs `0x08-0x1F` (the wave-number
   registers; `YMF278B::writeIO` gates `LOAD_DELAY` on exactly that range).
   Register and memory writes extend BUSY only (88/28 clocks).

3. **NEW2 write gate added.**
   The YMF278B **ignores wave register writes entirely — select and data —
   while NEW2 (FM bank-1 reg `0x105` bit 1) is clear** (verified on real
   YMF278 per openMSX; reads still work). We applied everything unconditionally
   and used the OPL3 `NEW` bit (bit 0) for the card's `#7F` claim. Now:
   `_new2` state in `Opl4Fm` (serialized in the flags byte, bit 64),
   `Opl4::New2Mode()`, the write gate in `SoundChip_Moonsound`'s wave ports,
   and the claim gate retargeted from `NewMode()` to `New2Mode()`.
   Detection software always writes `0x105 = NEW2|NEW` first, so behavior
   for well-behaved drivers is unchanged.

4. **PCM loop end switched to the chip's stored-complement form.**
   Headers store the end point as its complement S (`= 0x10000 - trueEnd`).
   We pre-negated to E and tested `pos >= E`; openMSX keeps S raw and tests
   `pos + S >= 0x10000`, wrapping by `pos += S + loopAddr`. Algebraically
   identical — **except S = 0** (a full 64 KiB sample), where `E = 0x10000`
   truncated to 0 and our test `pos >= 0` wrapped *every step*, teleporting
   the slot through memory whenever `loopAddr != 0`. The raw form never trips
   for S = 0: linear one-shot with the natural 16-bit wrap. Fixed in
   `NextPos`; the old behavior was even pinned by a vector test, now
   corrected. (MAME's historical pitch-fluctuation fix is this exact
   overrun-carrying wrap — preserved.)

Minor: wave-register *reads* now apply the 38-clock memory busy for latch
`0x03-0x06` (was: only `0x06`), matching `YMF278B::readIO`.

### 3.3 Accepted deviations

- **Register-select busy** (56 FM / 88 wave clocks on the *latch* write) is
  not modelled: our I/O layer folds select+write into one atomic event, so
  there is no observable window between them for a register-stream replay.
  openMSX itself notes these are "so small that on a MSX you can never see
  BUSY=1" for the memory regs, and "only very briefly and only on R800"
  otherwise.
- **LD window length 9600 clocks** (≈283.5 µs) vs openMSX's 10000: openMSX's
  own comment says 10000 is "slightly too high but within 2-4%" of the real
  value — i.e. the truth is ≈9600–9800. We keep 9600.

### 3.4 Verification

- `opl4tests`: 1414/1414 (bus-timing vectors rewritten for the LD bit/scope).
- `core-tests --gtest_filter=*MoonSound*`: 13/13 (device tests now arm NEW2
  before wave access — the real-silicon sequence).
- Full suite, 20 GTest shards: 0 failures.
- Zero compiler warnings (ninja, `-Werror`).

## 4. The differential harness (design)

Goal: prove PCM-engine equivalence on real repertoire, not on synthetic
vectors. Drive **both cores from one register stream**, never from the
emulator — the emulator adds bus timing and arbitration that would make
diffs ambiguous.

### 4.1 Capture

- openMSX ships `vgm_rec.tcl` (since 0.14.0): exports YMF278B/MoonSound
  register writes to **VGM** while running any MSX software. Capture a set
  from the same 26-disk MoonSound demo repertoire catalogued in
  `testdata/sound/moonsound/SOURCES.md` (MoonBlaster/demos that exercise
  FM+PCM, SRAM uploads, and looping samples).

### 4.2 Replay

- One VGM parser feeding two engines with identical event ordering:
  1. our `opl4::Opl4` headless (write register events through `WriteWave` /
     `WriteFm`, clock from the VGM timeline);
  2. openMSX's `YMF278` compiled standalone (its engine API is separable
     from the MSX machine; that is exactly how its own test harness drives it).
- GPL note: the comparator links openMSX code and lives in `scratch/` or a
  research target, never in the shipped tree; our core never links it.

### 4.3 Diff at three levels (state first)

1. **Register/state snapshot after each write**: register files, `memAdr`,
   decoded slot parameters. Catches decode divergences before they become
   audio.
2. **Per-slot PCM address and envelope state** per frame: `pos`, `stepPtr`,
   `envVol`, `EgPhase` for all 24 slots. Catches stepping, loop-address and
   envelope-rate divergences.
3. **Mixed sample stream** last: `chipStream` frames. An audio-only diff
   tells you *that* you are wrong but not *where* — with multiple symptoms
   present the state diff must fire first.

Tolerance policy: level 1 exact; level 2 exact; level 3 exact on
Authentic-mode int16 output where feasible, else a bounded error metric
documented alongside.

### 4.4 Future anchors

- libvgm's YMF278B as a second replay consumer for the same VGM set.
- MSX1_MiSTer (muhanpong) RTL Verilated for bit-exact PCM comparisons —
  the jt03-equivalent anchor for TSFM.
- ymfm's OPL4 core as a BSD-licensed in-tree sanity engine if a
  license-clean comparator is ever needed in CI.

## 5. Next steps

- [ ] Record 3–5 representative VGMs via openMSX + `vgm_rec.tcl` from the
      catalogued demo disks.
- [ ] Stand up the two-engine comparator in `scratch/` with the three-level
      diff; run on the captures.
- [ ] Triage any level-1/2 diffs (the §3 table is the map of suspects).
- [ ] If clean to level 3: promote a nightly capture-diff set into
      `core/tests` as golden PCM state vectors (level 2 snapshots — compact
      and license-safe).
- [ ] Decide the FM operator-map question §6.2 raises (linear vs classic
      OPL3 map) before investing in FM repertoire testing.

## 6. The ymfm OPL3 verification backend (Revision 5)

§4.4 anticipated "ymfm's OPL4 core as a BSD-licensed in-tree sanity engine
if a license-clean comparator is ever needed in CI." This revision built
exactly that, for the **FM half only** (the PCM half stays on the
openMSX-audited in-tree model — §3.1 already cleared it suspect-by-suspect).

### 6.1 What was built

- **Vendored:** `ymfm_opl.{cpp,h}` and `ymfm_pcm.{cpp,h}` at
  `core/src/3rdparty/ymfm/` (repo already vendored the OPN subset for TSFM
  at pinned commit 81aec25; these two are byte-identical to that commit,
  `VERSION.txt` updated). The ymf262 (OPL3) class is the synthesis engine;
  `ymfm_pcm.cpp` is required because `ymfm_opl.cpp`'s y8950 references the
  adpcm_b engine.
- **Adapter:** `Opl4FmYmfm : Opl4Fm` (`tools/poc/015-opl4-synthesis/src/`),
  same pattern as the TSFM device: the base class keeps every audited bus
  semantic (bank-1 aliasing until NEW, timer periods/enable/mask/RST bits,
  status bits 6/5), the subclass replaces synthesis with
  `ymf262::generate(&out, 1)` per FM boundary (L = data[0] + data[2],
  R = data[1] + data[3]) and shuttles TTD state through ymfm's structured
  `save_restore` (the PATCHES.md engine-level TTD patch — now proven exact
  for OPL, previously only for OPN).
- **Switch:** CMake option `OPL4_FM_BACKEND` (`intree` default | `ymfm`),
  compile-time only — the TTD state layout differs between backends, so a
  runtime switch would make layouts ambiguous. `core/src/CMakeLists.txt`
  mirrors the selection as PUBLIC `OPL4_FM_YMFM=1` because opl4 is linked
  PRIVATE and core-tests compiles the core sources (and FM-programming test
  helpers) directly.
- **Comparator:** `tests/opl4fmcompare.cpp` in the PoC suite — one register
  stream into both engines, state diff first, samples last (§4.3's policy,
  realized in-tree).

### 6.2 Comparator findings

Where the engines agree on interpretation, they agree on output:

- **State:** flags, both timers, and the shared register file track exactly
  (bank aliasing, T1 = (0x100−load)·4, T2 = ·16, RST choreography).
- **Pitch:** ratio ours/ymfm = **1.0000** — equivalent phase mathematics.
- **Level:** ratio ≈ **4.0** — ymfm's 13-bit intermediate headroom vs our
  scaling; a constant, not a fidelity gap.
- **TTD:** adapter save/restore exact — 4000 lockstep checks, restored twin
  bit-identical to the source engine from the capture point on.

And where a **classic OPL3 driver's register bytes** meet our engine, the
comparator reports DIVERGENT — three stacked divergences, none of them
state-visible (which is why the openMSX audit never saw them; openMSX uses
the classic map and our PCM comparison was unaffected):

1. **Operator register map.** Ours is linear (`op = reg − 0x20`, channel
   *i* pairs operators {2i, 2i+1}); YMF262 is classic (operator groups at
   +0/+8/+0x10, channel *i* pairs {i, i+3}; channel-0 carrier lives at
   0x23/0x43/0x63/0x83). Identical driver bytes therefore program the
   wrong operators.
2. **0xC0 routing polarity inverted.** Our bits *exclude* sides (0x30 =
   fully silent, 0x00 = both); YMF262 CHA/CHB *include* (0x30 = both).
   A classic driver's routine `0xC0 = 0x31` mutes us, not ymfm.
3. **Unconfigured carrier has AR 0** in our engine and never attacks —
   compounding 1: when the envelope writes miss the real carrier, the
   voice stays silent rather than merely wrong.

Measured verdict line: identical bytes, rms ours = 0 vs ymfm = 1465.
These three are the prime "wrong instruments / silent FM" suspects for
any real MoonSound FM repertoire — the actionable outcome of the A/B.

### 6.3 Verification matrix

| Configuration | Result |
|---|---|
| PoC default (`opl4tests`, bin/) | 5452 checks / 0 failures |
| PoC ymfm (`bin-ymfm/`) | 5440 checks / 0 failures (12 pinning checks guarded, §6.4) |
| core default (`cmake-build-release`, 20 shards) | 0 failures |
| core ymfm (`cmake-build-ymfm`, 20 shards) | 0 failures |
| `*MoonSound*` filter, both backends | 11/11, incl. all three TTD round-trip tests |
| Compiler warnings | zero, both backends (−Werror) |

### 6.4 Guarded under `OPL4_FM_YMFM` (with rationale in-code)

- PoC `VecFm4Op` / `VecFmRhythm` / `VecFmKsl` bodies and the `TestTaps` FM
  peak assertion — they pin the in-tree synthesis model / linear map / tap
  semantics the backend swap intentionally replaces.
- `KeyOnFmCh0` (PoC testfw) and `KeyOnFmCh0ThroughPorts` (core MoonSound
  test) are not guarded but **backend-aware**: under the flag they program
  the classic-map carrier addresses and 0xC0 = 0x30 (CHA+CHB), so the
  device tests exercise the ymfm path meaningfully instead of silently.
- `TTD_Capture_Cost_Gate_Test` time-share assertion: ymfm's structured
  `save_restore` costs ~15x the in-tree memcpy (measured 78.3 ms recorded
  vs 30.7 ms baseline over 30 frames → 60.8% share vs the 50% budget).
  The byte-volume gate stays active for that build; correctness is covered
  by the TTD round-trip tests.

### 6.5 Known limitations of the backend

- Rhythm-mode channel taps are zeroed (the base class API exposes them;
  ymfm mixes them internally). No shipped code path reads taps.
- Not shippable as default while the TTD capture-cost regression stands —
  it is a verification instrument, per its charter.
- The option must never be combined with a pre-existing TTD recording from
  the other backend (layout tag differs; the store refuses mismatched
  tags by design).
