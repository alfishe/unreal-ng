# libopl4 vs the reference cores — audit results and the differential harness plan

**Date:** 2026-09-14
**Scope:** a suspect-by-suspect diff of our YMF278B model against openMSX
(the de-facto MoonSound reference), the fixes that fell out of it, and the
design of the register-stream differential harness that keeps us honest
from here on.
**Companion:** `opl4-core-tdd.md` (chip library), `opl4-unreal-ng-integration.md`
Revision 4 (device wiring).

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
