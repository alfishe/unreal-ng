# libopl4 — FM/PCM split and FM engine re-derivation

**Target tree:** `tools/poc/015-opl4-synthesis` on branch `moonsound`
**Base reviewed:** merge `212b7098` (master → moonsound, 2026-09-15; moonsound HEAD `dc40407a`)
**Audience:** development agent working directly in the repository
**Status:** implementation pass 1 landed 2026-09-15..17 (uncommitted working tree on
`moonsound`): FM/PCM split done (`src/fm/`, `src/pcm/`, `FmBus` + `IFmSynth`), F2/F4/F5/F8
fixed and verified, F9 partially (FM-specific tables extracted, envelope calibrated against
ymfm); F1/F3/F6/F7/F10 remain open — see §3.1. Acceptance evidence: PoC suites
6142/6128 checks 0 failures (in-tree/ymfm), MFM Music sample 2 melody 7 (CRYOGENT) guest
test green on both backends, release gate at pre-existing baseline. Original pre-flight note
(2026-09-17): every defect in §3 re-confirmed present at the base commit with line
references refreshed, the Nuked-OPL3 reference pinned (§5.4), the differential-harness
bring-up made an explicit step (§6 step 4), and the acceptance criteria made measurable (§7).

---

## 1. Goal

Two outcomes, in this order of importance:

1. **Make the FM half correct**, by re-deriving it in the YMF262 silicon domain instead of the YMF278 wave-part domain it currently borrows. The present engine is not "slightly off"; it runs the OPL envelope in the wrong attenuation range, never resets operator phase on key-on, and scales sustain level at half depth. These are structural, not tuning.
2. **Split FM and PCM into two independently swappable engines behind stable interfaces**, so any of `{in-tree, ymfm, Nuked-OPL3, future}` can be plugged into either half at *runtime*, and two backends can be instantiated *in the same process* for per-sample differential testing.

Secondary but load-bearing: replace the current statistical comparator (RMS ratio within 5–25 %, zero-cross pitch ratio) with a **bit-exact per-sample diff**. Every defect listed in §3 passes the current comparator. A tolerance-based oracle cannot find them.

### Non-goals

- Changing the PCM engine's behaviour. It is openMSX-audited and the user reports it working. It moves files and gains an interface; its arithmetic is untouched.
- Changing the time model, the 49516.4 Hz FM grid (`clock/684`), the 44100 Hz output grid (`clock/768`), the reducer, the render layer, or the character chain.
- Host integration (`2026-09-13-0217-opl4-unreal-ng-integration.md`), TTD tiers (`2026-09-13-0217-opl4-ttd-integration-tdd.md`). Both are downstream and are touched only where the state-size contract changes (§6 steps 3 and 7).

---

## 2. Background

### 2.1 Where the code is today

```
tools/poc/015-opl4-synthesis/
├── include/opl4/
│   ├── opl4.h            public Opl4 class; `using FmBackend = Opl4Fm | Opl4FmYmfm` (compile-time)
│   ├── opl4config.h      Opl4Config, RenderMode, Quality, ChannelId, ChannelGroup
│   ├── iwavememory.h     wave-memory abstraction
│   └── wavememory.h      default ROM+SRAM implementation
├── src/
│   ├── opl4tables.h      SHARED tables — the root problem (§2.3)
│   ├── opl4fm.h/.cpp     FM engine: synthesis + bus semantics + timers, all in one class
│   ├── opl4pcm.h/.cpp    PCM engine (24 slots, 44100 Hz)
│   ├── opl4render.h/.cpp resampler, board analog, character chain, DC blocker
│   ├── opl4.cpp          top level: time model, bus, reducer, mix, state
│   └── ymfm/opl4fmymfm.* ymfm YMF262 backend, selected by -DOPL4_FM_BACKEND=ymfm
├── tests/                testfw.h, opl4tests.cpp, opl4vectors.cpp, opl4sweep.cpp, opl4fmcompare.cpp
└── cosim/                cosim-ymfm.cpp (vs ymfm ymf278b), cosim-oracle.cpp (self digests), fetch-refs.sh
```

Backend selection is a compile-time type alias (`opl4.h` L22-29) plus a CMake cache variable that redirects the whole binary output directory (`CMakeLists.txt`). Consequences:

- The top level can never hold two FM engines at once. The one place that does (`tests/opl4fmcompare.cpp`) reaches around the public API with its own `EngineA`/`EngineB` typedefs.
- Two build trees are required to A/B (`bin/` and `bin/ymfm/`), and the build system carries a comment explaining how that interleaving previously served the wrong backend's tests.
- Adding a third backend (Nuked) means a third cache value and a third output directory.

### 2.2 What the FM engine currently owns

`Opl4Fm` is three things in one class:

| Concern | Where |
|---|---|
| Bus semantics: bank-1 aliasing before NEW, `0x105` NEW/NEW2, register shadow `_regs[512]`, status byte | `WriteReg` L402-546, `ReadStatus` L548-551 |
| Timers T1/T2 and their status bits | `AdvanceTimers` L553-575 |
| Synthesis: phase, envelope, waveforms, connections, 4-op, rhythm, LFO, routing | rest of the file |

`Opl4FmYmfm` therefore has to **re-implement the first two field-for-field** (its header says so explicitly) so that guest-visible behaviour does not change with the backend. That duplication is the direct source of findings §3.5 (reg 0x04 RST storage differs) and part of §3.6 (address latch) in `2026-09-15-2114-opl4-ymfm-verification-findings.md`. A third backend would duplicate it a third time.

### 2.3 Root cause of the FM quality problem

`src/opl4fm.h` includes `opl4tables.h`, and the FM engine runs on the **PCM engine's envelope machinery**:

| Quantity | libopl4 FM today (YMF278 wave domain) | YMF262 silicon |
|---|---|---|
| Attenuation domain | `kMaxAttIndex = 0x280` = 640 steps × 0.09375 dB = **60 dB** | 9-bit, 0..511 × 0.1875 dB = **96 dB** |
| Envelope increment table | `kEgInc` (15 rows, YMF278 lineage) | OPL3 increment table |
| Rate cadence | `kEgRateShift` + `FmRateRow` compact shift model | 5.11 fractional counter |
| Attack ladder | `(~envVol * inc) >> 4` in the 640 domain | `(~eg_rout * inc) >> 3` in the 512 domain |
| Sine → output | linear 13-bit sine, then `VolFactor(wave << 3, index)` (exp-decode once, multiply) | log-domain: `logsin(phase) + attenuation`, single `exp()` at the end |

The 60 dB ceiling is not a rounding difference. `OperatorOutput` (L252-279) sums `envVol + AM + (tl << 3) + KSL` and `VolFactor` (opl4tables.h L56-62) returns **hard zero** at index ≥ 640. TL alone reaches 47 dB at `0x3F`. So a moderately attenuated operator with a partly-decayed envelope is *muted*, not quiet. That is the reported "FM is quiet, hisses, drops notes" failure mode surviving underneath every register-map fix already landed.

The linear-vs-log difference matters separately: silicon adds the sine's own attenuation to the envelope attenuation *before* exponentiating and saturates the sum. Multiplying a linear sine by an exp-decoded envelope is algebraically equivalent only in infinite precision; in fixed point the quantization and the saturation corner both differ, and no amount of table tuning closes it.

### 2.4 Why co-simulation has not caught this

`tests/opl4fmcompare.cpp`:

- L225: pitch ratio within 0.5 %
- L227: level RMS ratio within 5 %
- L287: the classic-map block declares DIVERGENT only beyond **25 %** RMS

Missing phase reset, half-scale sustain level, wrong sustain-phase rate, and one-tap feedback all sit comfortably inside those bands. Additionally, ymfm's YMF262 is a careful reimplementation, not a die-derived model, and `2026-09-15-2114-opl4-ymfm-verification-findings.md` §3.1/§3.3/§3.5 already records places where ymfm is the unverified side. Arbitrating "in-tree vs ymfm" by RMS produces the open-items table in §5 of that document rather than answers.

**Nuked-OPL3 is the correct oracle for the FM half.** YMF278B's FM block is a YMF262 block; Nuked-OPL3 is derived from die analysis, is sample-exact, is two files, and is LGPL-2.1 (compatible with this GPL-3.0 tree, and in any case linked only into test harnesses). With a structurally isomorphic engine the diff becomes exact equality, and the remaining questions (rhythm B0-kon suppression, ws 3/5 table choice) resolve themselves instead of waiting on hardware recordings.

---

## 3. Defect inventory (what must end up fixed)

Line numbers are against `src/opl4fm.cpp` at the reviewed commit (`dc40407a`, re-verified 2026-09-17). If HEAD has moved since, re-locate each defect by its quoted code, not by line. Each of these is an acceptance test in §7.

| # | Defect | Location | Audible effect |
|---|---|---|---|
| F1 | **Key-on does not reset operator phase.** `KeyOn` sets `envVol`/`egState` only; there is no `op.phase = 0` anywhere in the FM engine. | L365-380 | Attack timbre varies per note; modulator/carrier relationship at note start is nondeterministic; makes exact diffing impossible by construction. |
| F2 | **Sustain level is half-scale, and SL=15 is not special-cased.** `sustainLevel = op.sl << 4` gives 1.5 dB/step in the 0.09375 dB domain; OPL is 3 dB/step (`<< 5`), and SL 0xF means full attenuation (93 dB). The PCM half already does this correctly via `kDecayLevelTable` (opl4tables.h L124-128). | L317 | Everything sustains too loud; decay shapes wrong; 0xF patches never go silent. |
| F3 | **Decay→sustain transition is a no-op ternary; non-sustaining operators then decay at DR.** `op.egState = op.egt ? kFmEgSus : kFmEgSus;` and the `kFmEgSus` branch uses `op.dr`. OPL: below SL, an EGT=0 operator continues at the **release** rate. | L319, L325-340 | Every percussive patch has the wrong tail length. |
| F4 | **Feedback uses one previous sample.** `_fbHist[ch] = o1` and modulation reads that single tap. Silicon averages the last two operator outputs. | L632, L689, and the `mod` computation in both branches | Buzzier, diverges further as FB rises. |
| F5 | **4-op channels take tuning and key-on from the slave channel.** Operators C/D come from `_ch[s]`, whose `fnum`/`block` are filled from the slave's 0xA0/0xB0, and the slave's own 0xB0 keys them. On YMF262 the slave's F-number, block and key-on are ignored: the master's 0xB0 keys and tunes all four. | L605-659, `UpdateChannelParams` L382-400, the 0xA0/0xB0 handlers L518-537 | Drivers that program only the master leave ops 3/4 unkeyed. Verify against Nuked before changing. |
| F6 | **Rhythm mode is a placeholder.** HH/SD/CY are `(_noise & 2) ? +0x1000 : -0x1000` through the envelope. Silicon derives HH/SD/CY from specific phase bits of operators 13/17 XOR-combined with the noise bit. | L660-678 | All drum content is wrong. |
| F7 | **Noise LFSR is the wrong polynomial and is unmasked.** `(n ^ n>>2 ^ n>>9)` shifted **left**, never masked to 23 bits. OPL3: 23-bit register, taps 0/14/15/22, shifted right. | L584-586 | Noise colour wrong; also feeds F6. |
| F8 | **Timer register 0x04 handling.** RST (bit 7) should be handled to the exclusion of the other bits in the same write, and it clears flags only — the current code also zeroes the counters. | L424-435 | Timer-driven tempo drift on drivers that use the combined write. |
| F9 | **Envelope domain / rate model** (§2.3). Subsumes finding §2.2 #5 in the verification log. | `opl4tables.h` + all of `AdvanceEnvelope` | 60 dB cliff: quiet operators hard-mute. Release tails truncate. |
| F10 | **Check rhythm-channel output doubling.** OPL2 sums rhythm voices twice; confirm the YMF262/OPL3 behaviour against Nuked and implement whichever it is, explicitly. | L660-678 | Drum balance. |

### 3.1 Status after implementation pass 1 (2026-09-17)

Verified against the working tree by code inspection + the verification rig below. "Fixed"
means an acceptance artifact exists; relocations refer to the new `src/fm/` files.

| # | Status | Evidence / notes |
|---|---|---|
| F1 | **Open.** | No `phase = 0` on key-on yet. Re-verify against Nuked-OPL3 first: OPL3 phase accumulates continuously and kon-reset semantics differ from the YM21xx assumption this row was written under. |
| F2 | **Fixed.** | `sustainLevel = (op.sl \| ((op.sl + 1) & 0x10)) << 5` — 3 dB/step with the SL=0xF full-attenuation case, clamped to the engine ceiling (`fmsynthopl4.cpp` decay branch). |
| F3 | **Open.** | The no-op ternary (`op.egt ? kFmEgSus : kFmEgSus`) is still present; the `if (!op.egt)` release-rate branch handles only part of the semantics. |
| F4 | **Fixed.** | Two-tap feedback in both the 4-op stage-1 and 2-op branches: `taps = _fbHist[ch][0] + _fbHist[ch][1]` scaled by `fbShift` (`fmsynthopl4.cpp`). Verified by the guest drum chains turning tonal + rig differentials. |
| F5 | **Fixed.** | 4-op master ownership: a master channel re-points all four operators' choffs to itself — phase and key-scale read the MASTER's 0xA0/0xB0; the slave's own 0xA0/0xB0 feed nothing (`fmsynthopl4.cpp` RebuildConnections, comment cites CRYOGENT). This plus F4 was the "some channels noising" root cause: melody 7's drum chains program masters only, so the old per-slave decode left ops 3/4 unkeyed. |
| F6 | **Open.** | HH/SD/CY are still the `(_noise & 2) ? ±0x1000` placeholder (line ~629). BD key-on routing and the 0xBD bit order are correct (ymfm/Nuked-verified comment). |
| F7 | **Open.** | LFSR still taps 0/2/9 shifted left, unmasked. |
| F8 | **Fixed.** | `FmBus::Write` 0x04: RST (bit 7) exclusive — resets flags only, counters untouched; comment cites openMSX YMF278B::writeIO (`fmbus.cpp`). |
| F9 | **Partially.** | FM tables split into `src/fm/fmtables.h` (fixing an ODR violation where FM symbols resolved to the PCM copies — see §3.2), rate/shift model re-derived to half-cadence + KSR-off semantics, envelope statistics calibrated against ymfm (rig + guest, both backends green). The full 96 dB log-domain re-derivation (§2.3 table) is not done — `kFmMaxAttIndex` is still 0x280. |
| F10 | **Open.** | Blocked on F6. |

### 3.2 Lessons from pass 1 (2026-09-17)

- **ODR, the quiet one.** The PoC compiles into the core build (`core/src/CMakeLists.txt`
  "temporary home") *and* keeps its own library targets. When FM and PCM both defined
  same-named internal symbols (tables, constants), the linker silently bound FM
  references to the PCM copies — divergence appeared only as subtly wrong envelopes
  (koff phase) that passed RMS comparators. Symptom-to-cause tool: per-backend dump
  diffing, not the statistical comparator. Fixed by the `src/fm/`/`src/pcm/` split with
  `fmtables.h`/`pcmtables.h` namespaces intact.
- **Read the registry buffers as what they are.** `SoundChip_Moonsound::getFmBuffer()`
  returns interleaved stereo (`[L,R,L,R,...]`, 882 samples/frame). A guest-test harness
  that reads it linearly as mono computes a half-rate alternate-sample series — for a
  right-only channel that is literally the sample sequence with every other sample
  dropped, which *measures* as high-frequency noise (hf ≈ √2 of true) and can send the
  investigation chasing phantom engine defects. Mono-sum `(fm[2s] + fm[2s+1]) * 0.5`
  first; the interleaving lesson is baked into `moonsound_mfm2_guest_test.cpp`.
- **Statistical comparators need calibrated bands, not vibes.** FB7 patches are chaotic:
  sample-exact equality is impossible by construction, and loose RMS bands (5–25 %)
  passed every §3 defect. The rig now uses per-case calibrated bands (rms ratio, hf,
  zero-cross rate) with the case's own reference run — see `CompareLeadVoice` in
  `tests/opl4fmcompare.cpp` for the pattern that proved the ch15–17 lead brightness
  (hf ≈ 1.03) is genuine timbre, identical in both engines.

---

## 4. Target architecture

### 4.1 Principle

Three layers, with the split lines chosen so that *nothing guest-visible depends on which synthesis core is plugged in*:

```
            ┌──────────────────────────────────────────────┐
            │ Opl4 (top level)                             │
            │ time model, reducer, mix, render, state blob │
            └───────────────┬──────────────────┬───────────┘
                            │                  │
                 ┌──────────▼────────┐  ┌──────▼────────────┐
                 │ FmBus             │  │ PcmBus            │
                 │ register shadow   │  │ (thin: tone-load  │
                 │ bank aliasing     │  │  timing, LD flags)│
                 │ NEW/NEW2          │  └──────┬────────────┘
                 │ timers T1/T2      │         │
                 │ status byte       │         │
                 │ routing + mute    │         │
                 └──────────┬────────┘         │
                            │                  │
                 ┌──────────▼────────┐  ┌──────▼────────────┐
                 │ IFmSynth          │  │ IPcmSynth         │
                 ├───────────────────┤  ├───────────────────┤
                 │ FmSynthOpl4       │  │ PcmSynthOpl4      │
                 │ FmSynthYmfm       │  │ PcmSynthYmfm      │
                 │ FmSynthNuked      │  │ (future: openMSX) │
                 └───────────────────┘  └───────────────────┘
```

**Bus semantics move up, out of every backend.** A synthesis backend receives already-de-aliased register writes and emits samples. It owns no timers, no status byte, no NEW/NEW2, no routing. This deletes the duplication in `Opl4FmYmfm` and removes findings §3.5 and §3.6 as a class.

**Routing and mute move up too.** Today `Opl4::AdvanceFmToOutput` reads `_fm->Channels()[ch].route`, which forces `Opl4FmYmfm` to serve a static stub array (finding §3.9). Instead `FmBus` derives routing from its own register shadow, which every backend feeds identically. Backends that can expose per-channel taps get full mute support; backends that cannot declare it in their capability flags and `FmBus` falls back to the mixed pair.

### 4.2 Directory layout after the split

```
tools/poc/015-opl4-synthesis/
├── include/opl4/
│   ├── opl4.h            unchanged public surface; FmBackend alias REMOVED
│   ├── opl4config.h      + FmEngineId / PcmEngineId enums, + engine ids in Opl4Config
│   ├── ifmsynth.h        NEW — IFmSynth, FmOutput, FmCaps
│   ├── ipcmsynth.h       NEW — IPcmSynth, PcmOutput, PcmCaps
│   ├── engines.h         NEW — factory: MakeFmSynth(FmEngineId), MakePcmSynth(PcmEngineId),
│   │                            availability query, id↔name mapping
│   ├── iwavememory.h     unchanged
│   └── wavememory.h      unchanged
├── src/
│   ├── common/
│   │   ├── exptable.h    kPowerTable + the exp decode, used by BOTH halves
│   │   └── mixtables.h   kPanTable, kMixScale, block-mix constants
│   ├── fm/
│   │   ├── fmtables.h    NEW — OPL3 domain ONLY: logsin, exp, EG increments,
│   │   │                        EG rate shift, KSL, MULT, waveform selects
│   │   ├── fmbus.h/.cpp  NEW — register shadow, aliasing, NEW/NEW2, timers,
│   │   │                        status, routing, mute, channel taps
│   │   ├── fmsynthopl4.h/.cpp   (was src/opl4fm.*) — re-derived engine
│   │   ├── fmsynthymfm.h/.cpp   (was src/ymfm/opl4fmymfm.*) — synthesis only now
│   │   └── fmsynthnuked.h/.cpp  NEW — Nuked-OPL3 wrapper (test builds only)
│   ├── pcm/
│   │   ├── pcmtables.h   NEW — YMF278 wave domain ONLY: kMaxAttIndex 0x280,
│   │   │                        kEgInc, kEgRateShift, kEgRateSelect,
│   │   │                        kDecayLevelTable, LFO tables
│   │   ├── pcmbus.h/.cpp NEW — tone-header load timing, LD1/LD2, busy
│   │   ├── pcmsynthopl4.h/.cpp  (was src/opl4pcm.*) — arithmetic UNCHANGED
│   │   └── pcmsynthymfm.h/.cpp  NEW, optional — ymfm ymf278b wave half
│   ├── opl4.cpp          top level; owns FmBus + PcmBus, holds IFmSynth*/IPcmSynth*
│   ├── opl4render.h/.cpp unchanged
│   └── wavememory.cpp    unchanged
├── tests/                as today, plus tests/fm/ and tests/pcm/ subfolders
└── cosim/
    ├── diff-fm.cpp       NEW — bit-exact N-way FM differential
    ├── diff-pcm.cpp      NEW — the PCM equivalent (rename/refit of cosim-ymfm.cpp)
    ├── scripts/*.frs     NEW — register stimulus scripts (§5.3)
    ├── reports/          NEW — checked-in baseline divergence reports (§6 step 4)
    ├── cosim-oracle.cpp  unchanged in purpose; digests regenerate
    └── fetch-refs.sh     + Nuked-OPL3 pin
```

**`src/opl4tables.h` ceases to exist.** Its contents are partitioned three ways. This is the enforcement mechanism for §2.3: after the split, `src/fm/**` must not include anything from `src/pcm/**` and vice versa. Add a CI grep to that effect (§7.5) — it is the guard that prevents the FM half from silently re-acquiring wave-part semantics.

### 4.3 `IFmSynth`

```cpp
// include/opl4/ifmsynth.h
#pragma once
#include <cstddef>
#include <cstdint>

namespace opl4
{

struct FmCaps
{
    bool perChannelTaps = false; // can emit the 18 per-channel values
    bool exactStateSave = false; // save/restore is bit-exact and cheap
    const char* name = "";       // "opl4", "ymfm", "nuked"
};

struct FmOutput
{
    int32_t channel[18];         // valid only if FmCaps::perChannelTaps
    int32_t mixL = 0, mixR = 0;  // always valid; routing already applied by the
                                 // backend ONLY when perChannelTaps == false
};

class IFmSynth
{
public:
    virtual ~IFmSynth() = default;

    virtual void Reset() = 0;

    // De-aliased register write. reg is 0x000..0x1FF: bank already resolved,
    // NEW/NEW2 gating already applied by FmBus. The backend keeps whatever
    // internal shadow it needs but is never the authority on bus behaviour.
    virtual void WriteReg(uint16_t reg, uint8_t data) = 0;

    // One 684-clock FM tick.
    virtual void Advance(FmOutput& out) = 0;

    virtual FmCaps Caps() const = 0;

    // Variable-size state. LayoutTag() changes whenever the layout changes;
    // the TTD store refuses a mismatched tag (already the policy for the
    // ymfm backend, now generalised).
    virtual size_t StateSize() const = 0;
    virtual void SaveState(uint8_t* dst) const = 0;
    virtual void LoadState(const uint8_t* src) = 0;
    virtual uint32_t LayoutTag() const = 0;
};

} // namespace opl4
```

Cost: one virtual call per FM tick (49 516/s), not per operator. Immaterial. Do **not** template the top level to avoid it; runtime selection is the requirement.

`IPcmSynth` mirrors this with `PcmOutput { int32_t slot[24]; int32_t mixL, mixR; }`, plus `SetMemory(IWaveMemory*)`, `ReadReg`, and the tone-load return that `Opl4::WriteWave` already consumes.

### 4.4 Engine selection

```cpp
// include/opl4/opl4config.h  (additions)
enum class FmEngineId : uint8_t { Opl4 = 0, Ymfm = 1, Nuked = 2 };
enum class PcmEngineId : uint8_t { Opl4 = 0, Ymfm = 1 };

struct Opl4Config
{
    // ... existing fields ...
    FmEngineId  fmEngine  = FmEngineId::Opl4;
    PcmEngineId pcmEngine = PcmEngineId::Opl4;
};
```

```cpp
// include/opl4/engines.h
std::unique_ptr<IFmSynth>  MakeFmSynth(FmEngineId id);   // nullptr if not compiled in
std::unique_ptr<IPcmSynth> MakePcmSynth(PcmEngineId id);
bool        FmEngineAvailable(FmEngineId id);
const char* FmEngineName(FmEngineId id);
```

Build options become additive rather than exclusive:

| Option | Default | Effect |
|---|---|---|
| `OPL4_WITH_YMFM` | `OFF` in the shipping build, `ON` in the test build | compiles `FmSynthYmfm` + `PcmSynthYmfm` |
| `OPL4_WITH_NUKED` | `OFF` shipping, `ON` for `diff-fm` | compiles `FmSynthNuked` |

`OPL4_FM_BACKEND` and the `bin/ymfm` output-directory split are **deleted**. One binary, all compiled-in engines, selected at construction. The comment in `CMakeLists.txt` about interleaved builds serving the wrong backend's tests becomes obsolete.

---

## 5. The differential harness

### 5.1 What it must do

`cosim/diff-fm.cpp`, one binary:

```
diff-fm --a=opl4 --b=nuked --script=scripts/2op-basic.frs --ticks=200000 [--tolerance=0] [--trace=out.csv]
```

- Constructs **two** `IFmSynth` instances in the same process.
- Feeds the identical register script to both through the identical `FmBus` instance semantics (one bus per engine, same writes).
- Compares `mixL`/`mixR` **per sample**, default tolerance 0.
- On first mismatch: prints tick index, both sample values, and a full dump of the register shadow plus, where the backend exposes it, per-operator phase/envelope/state. Then exits non-zero.
- `--trace` writes both streams as CSV for offline plotting.

### 5.2 Output-lag alignment

Nuked-OPL3 carries a small fixed output pipeline delay relative to a naive per-tick model. Determine the constant lag **once**, at harness bring-up, with an impulse script; then assert it stays constant and shift by it. Do not make it a tunable: a lag that changes between scripts is a bug, not a calibration.

Likewise, if `FmSynthNuked` is compared against a backend with different output scaling, normalise by an exact integer shift declared in the backend, never by an empirically fitted float. The current `cosim-ymfm.cpp` per-engine empirical scale estimate (L22-30, L191-236) is acceptable for the PCM half against ymfm; it must not enter the FM path.

### 5.3 Stimulus script format

Plain text, one directive per line, so scripts are diffable and hand-writable:

```
# fmscript v1 — 2-op sine, ch0, key on, sustain, key off
w 105 01          # NEW
w 020 21          # ch0 modulator: EGT, MULT 1
w 023 21          # ch0 carrier
w 040 2A          # modulator TL
w 043 00          # carrier TL
w 060 F0          # AR 15 DR 0
w 063 F0
w 080 77
w 083 77
w 0C0 31          # CHA|CHB, FB 0, CNT 1
w 0A0 98
w 0B0 31          # block 4, key on
run 40000
w 0B0 11          # key off
run 20000
```

Directives: `w <reg:3hex> <data:2hex>`, `run <ticks>`, `#` comment. Keep the existing `cosimdrv.h` register-script helpers as the builder for programmatic cases; the file format exists so a failing case can be checked in as a regression.

**Minimum script set to author** (each becomes a permanent regression case):

| Script | Targets |
|---|---|
| `2op-basic.frs` | F1 (phase reset), baseline |
| `2op-feedback-sweep.frs` | F4, all 8 FB values |
| `eg-sustain-ladder.frs` | F2, F3, all 16 SL × EGT 0/1 |
| `eg-rate-matrix.frs` | F9, AR/DR/RR × KSR × block |
| `tl-floor.frs` | F9 60 dB cliff: TL 0x30..0x3F with a decaying envelope |
| `4op-algorithms.frs` | F5, all four algorithms, master-only and slave-also programming |
| `rhythm-all.frs` | F6, F7, F10, each of the five voices alone and combined |
| `ws-all.frs` | all 8 waveforms × 2 operators |
| `ksl-matrix.frs` | KSL 0..3 × block 0..7 × fnum MSBs |
| `lfo-am-pm.frs` | AM/PM depth bits, both 0xBD settings |
| `timers.frs` | F8, T1/T2 periods and the RST write |

### 5.4 Reference pinning

Extend `cosim/fetch-refs.sh` — the pin is **fixed here, not left as a bring-up decision**:

```sh
NUKED_REF=765ec962e473aeb767e4cba74ffdc8f588ffbfe8   # 2026-08-24 "remove NEW bit checks for 4-op channels"
NUKED_REPO=https://github.com/nukeykt/Nuked-OPL3.git
```

The pin is deliberate, not "whatever master is today": that commit removes the NEW-mode gate
around 4-op pairing, which makes the slave channel's `0xA0`/`0xB0` writes ignored and the
master key and tune all four operators — exactly the F5 target semantics. Anything older
carries the NEW-gated variant this work is arbitrating. Re-pin deliberately; a re-pin
invalidates the output-lag constant (§5.2) and both baseline reports (§6 step 4).

Same idempotent clone-and-checkout pattern as ymfm (checkout into `refs/nuked-opl3`).
`refs/` stays gitignored — no third-party source enters the repo, and `libopl4` itself stays
std-lib-only (spec R10). `FmSynthNuked` lives under `src/fm/` but is compiled only when
`OPL4_WITH_NUKED=ON`, which also requires `refs/nuked-opl3` to be present.

Integration facts at the pin (v1.8 API, verified against `opl3.h` at that commit):

- **It is C99, two files** (`opl3.c`, `opl3.h`). `cosim/CMakeLists.txt` must
  `enable_language(C)` and compile `opl3.c` with warnings suppressed (`-w` / `/w`), exactly
  like the vendored ymfm objects; the wrapper itself is warning-clean C++ under project flags.
- **Register addressing matches the bus contract directly.**
  `OPL3_WriteReg(chip, uint16_t reg, uint8_t v)` takes the 9-bit space with the bank in
  bit 8 — feed it the de-aliased `reg12` from `FmBus` unchanged. Forward **everything**,
  including `0x104` (4-op select) and `0x105` (NEW): Nuked derives its own channel topology
  and output routing from them. Its internal timers (`0x02`-`0x04`) keep running inside the
  model but only touch its own (unused) status byte; the bus owns guest-visible status.
- **One `OPL3_Generate4Ch(chip, int16_t* out4)` call per `Advance`**, then
  `mixL = out4[0] + out4[2]`, `mixR = out4[1] + out4[3]` — the same MAME convention the ymfm
  adapter already uses. Call `OPL3_Reset(chip, 49516)` in `Reset()` (the rate feeds only the
  `Resampled` variants, which are not used). Use direct `OPL3_WriteReg`, never
  `OPL3_WriteRegBuffered` — the buffered path adds `OPL_WRITEBUF_DELAY` (= 2) samples of
  write delay that would corrupt the lag measurement.
- **`opl3_chip` is not memcpy-relocatable**: `opl3_slot`/`opl3_channel` are wired with
  intra-struct pointers at `OPL3_Reset` time. So `FmSynthNuked` declares
  `FmCaps::exactStateSave = false`; its `SaveState`/`LoadState` serialise
  `sizeof(opl3_chip)` verbatim and exist for the harness's on-mismatch full-state dump
  (§5.1) only — a restored blob aliases its source instance through those pointers, which
  is acceptable for diagnostics and never acceptable for TTD. `LayoutTag()` returns the
  first 4 bytes of the pin hash, so a re-pin automatically changes the tag.

License note: Nuked-OPL3 is LGPL-2.1, which is compatible with this GPL-3.0 tree. It is nevertheless a test-only dependency in the default configuration; keep it that way unless there is a deliberate decision to ship it.

---

## 6. Step-by-step work plan

Each step is a separate commit, and each ends with a green suite. Do not collapse them:
step 5 invalidates every FM golden digest while every PCM digest must stay byte-identical,
and step 3 changes the persisted state layout — one cause per commit keeps each digest and
layout change attributable. Before starting step 1, snapshot the 55 oracle digests to a file
under `scratch/` (cosim-oracle prints them; add a `--dump-digests` flag first if it does
not — trivial); every later step re-runs the oracle and diffs against that snapshot, which
is how acceptance criterion 4 is actually verified rather than asserted.

### Step 1 — Mechanical split, no behaviour change

**Goal:** identical output bit-for-bit, new file layout.

1. Create `src/common/`, `src/fm/`, `src/pcm/`.
2. Partition `src/opl4tables.h`:
   - `kPowerTable`, `VolFactor` → `src/common/exptable.h`
   - `kPanTable`, `kMixScale`, block-mix constants → `src/common/mixtables.h`
   - `kMaxAttIndex`, `kMinAttIndex`, `kEgInc`, `kEgRateShift`, `kEgRateSelect`, `RateRow`, `kDecayLevelTable`, `DecayLevel`, LFO tables → `src/pcm/pcmtables.h`
   - Create `src/fm/fmtables.h` initially containing *copies* of the entries FM uses (`kEgInc`, `kEgRateShift`, `kEgRateSelect`, `kMaxAttIndex`, `FmRateRow`, `kSineTable`, `kPmScale`, `kMultTable`, `kKslAtten`). **Copies, not includes** — step 5 replaces their contents, and the copy is what makes that possible without touching PCM.
3. Move `src/opl4fm.*` → `src/fm/fmsynthopl4.*`, `src/opl4pcm.*` → `src/pcm/pcmsynthopl4.*`, `src/ymfm/opl4fmymfm.*` → `src/fm/fmsynthymfm.*`. Use `git mv` so history follows.
4. Update includes. `tests/testfw.h` currently includes `opl4tables.h`; point it at whichever of the three it actually needs (likely `common/exptable.h` and `pcm/pcmtables.h`).
5. **Verification:** `opl4tests` passes with the identical check count (6124), `cosim-oracle` reproduces all 55 digests **byte-identical**. If any digest moves in this step, the move was not mechanical — find the difference before proceeding.

### Step 2 — Extract the bus layer

**Goal:** backends become synthesis-only; guest-visible behaviour is single-path.

1. New `src/fm/fmbus.h/.cpp` — `FmBus` owns:
   - `std::array<uint8_t, 512> _regs`
   - bank-1 → bank-0 aliasing before NEW (currently `Opl4Fm::WriteReg` L408-409)
   - `0x105` NEW/NEW2 decode and the `New2` wave-port gate
   - `_timer1/_timer2` + load/enable/mask + status bits (currently L553-575), including the F8 fix for register 0x04
   - the status byte and `ReadStatus` (bit 7 reads as 1)
   - per-channel routing decode from the `0xC0` shadow (include semantics, CHA/CHB/CHC/CHD quad)
   - mute state and the tap-sum path
2. `FmBus::Write(bank, reg, data)` resolves aliasing, updates the shadow, handles timers/NEW itself, and forwards the de-aliased `(reg12, data)` to `IFmSynth::WriteReg`.
3. `FmBus::Advance()` calls `IFmSynth::Advance(FmOutput&)`; if `Caps().perChannelTaps`, it applies routing and mute and sums L/R itself; otherwise it takes `mixL/mixR` through unchanged and reports zero taps.
4. Strip all of the above out of `FmSynthOpl4` and `FmSynthYmfm`. `FmSynthYmfm` loses its entire duplicated timer/status/NEW block — the file should shrink by roughly half. Keep `ResetAddress()` (finding §3.6) since it is a real ymfm quirk.
5. `Opl4` holds `FmBus` + `std::unique_ptr<IFmSynth>`; `Opl4::AdvanceFmToOutput` stops calling `_fm->Channels()`.
6. Mirror the minimum for PCM: `PcmBus` for tone-load timing / LD flags / busy, if that logic currently lives in `Opl4Pcm`. If it already lives in `opl4.cpp`, leave it and just note so.
7. **Verification:** oracle digests unchanged; `opl4tests` unchanged; the ymfm backend now passes the timer and status checks in `opl4fmcompare.cpp` **trivially**, because both engines share one implementation. Delete those checks from the comparator — they no longer compare anything.

### Step 3 — Interfaces and runtime selection

1. Add `include/opl4/ifmsynth.h`, `include/opl4/ipcmsynth.h`, `include/opl4/engines.h` as in §4.3-4.4.
2. Make `FmSynthOpl4`, `FmSynthYmfm` implement `IFmSynth`; `PcmSynthOpl4` implement `IPcmSynth`.
3. Replace `using FmBackend = ...` in `opl4.h` with `std::unique_ptr<IFmSynth> _fm`. Remove the forward declaration of `Opl4FmYmfm` from the public header.
4. `Opl4::Configure` constructs engines from `cfg.fmEngine` / `cfg.pcmEngine`; assert-and-fail clearly if the requested engine was not compiled in.
5. State blob: replace `FmBackend::kStateSize` (compile-time) with `_fm->StateSize()` (runtime), computed once in `Configure` so `StateSize()` stays constant afterwards as the core-TDD §9 determinism contract requires. Prepend `LayoutTag()` to the FM region; `LoadState` refuses a mismatched tag.
6. Delete `OPL4_FM_BACKEND` and the `bin/ymfm` output redirect; add `OPL4_WITH_YMFM` / `OPL4_WITH_NUKED`.
7. `Opl4::FmForTest()` returns `IFmSynth&`. Tests that need concrete-type access do their own `dynamic_cast` and skip when it fails.
8. **Verification:** build with `-DOPL4_WITH_YMFM=ON`, run the whole suite twice in one process, once per engine, from a single binary. This is the first time that is possible and it is the proof the step landed.

### Step 4 — Bring up the differential harness (before any synthesis change)

**Goal:** the bit-exact oracle exists, and its divergences against the *current* engine are
recorded as a baseline, so step 5 has a measurable progress metric from its first compiling
build instead of waiting until the end.

1. Extend `cosim/fetch-refs.sh` with the pinned Nuked clone (§5.4).
2. `cosim/CMakeLists.txt`: build `diff-fm` (§5.1) gated on `OPL4_WITH_NUKED=ON` and the
   presence of `refs/nuked-opl3`; compile `opl3.c` as C with warnings off (§5.4 facts).
3. `src/fm/fmsynthnuked.h/.cpp` per §5.4: `Reset`/`WriteReg`/`Advance`/state with
   `exactStateSave = false`, `perChannelTaps = false`.
4. Author the eleven `.frs` scripts of §5.3 into `cosim/scripts/` — they are permanent
   regression cases from this step on.
5. Determine the output-lag constant once, with an impulse script (§5.2); hard-code it in
   `FmSynthNuked` with a test asserting it stays constant across every script. A lag that
   differs between scripts is a bug in the wrapper, not a calibration.
6. Baseline runs — divergence here is **expected and is the point** (this is the pre-fix
   measurement of every §3 defect):
   - `diff-fm --a=opl4 --b=nuked` over every script → check the report in as
     `cosim/reports/diff-fm-baseline-opl4.txt` (per script: first-mismatch tick and
     divergent-sample count).
   - `diff-fm --a=ymfm --b=nuked` → `cosim/reports/diff-fm-baseline-ymfm.txt`
     (the calibration data behind acceptance criterion 2).
   If any script already matches bit-exactly, the corresponding §3 defect claim is wrong —
   stop and re-derive before writing any engine code.
7. **Verification:** whole suite green from ONE build tree configured
   `-DOPL4_WITH_YMFM=ON -DOPL4_WITH_NUKED=ON`; zero warnings; oracle digests still
   byte-identical against the pre-work snapshot (nothing synthesised has changed); both
   baseline reports checked in; the lag constant recorded in-code.

### Step 5 — Re-derive the FM engine (the actual fix)

This is the substantive step. Work inside `src/fm/fmsynthopl4.cpp` + `src/fm/fmtables.h` only; nothing else in the tree changes. The step-4 harness is the progress metric: re-run the full script set after every sub-item and watch the divergence set shrink monotonically to zero. Do not tune constants by ear or by RMS — the only acceptable end state is exact equality.

1. **Re-domain.** Replace the borrowed copies in `fmtables.h` with the OPL3 domain:
   - attenuation domain 0..511 at 0.1875 dB (or 0..1023 at 0.09375 dB if you prefer one exp table with the PCM half — but then every constant below scales accordingly and the saturation point must still be 96 dB, not 60 dB)
   - log-sin table + exp table, so operator output is `exp(logsin(phase) + attenuation)` with the sum **saturated**, not `linear_sine * exp(-attenuation)`
   - OPL3 EG increment table and the 5.11 fractional counter cadence, replacing `kEgRateShift`/`FmRateRow`
   - attack ladder in the new domain
2. **F2:** sustain level `sl << 5` equivalent in the chosen domain, with SL 0xF → full attenuation. Use a table, mirroring `kDecayLevelTable`, not a shift.
3. **F3:** decay → sustain transition; sustain phase uses RR when EGT=0, holds when EGT=1.
4. **F1:** `op.phase = 0` in `KeyOn` on the off→on edge. Confirm against Nuked whether key-on also resets the envelope counter phase.
5. **F4:** two-tap feedback history; `mod = (fb[0] + fb[1]) >> shift` with the OPL shift convention.
6. **F5:** in 4-op mode, all four operators take fnum/block from the master channel's `0xA0/0xB0`, and the master's key-on bit keys all four; the slave's key-on is ignored. Verify with `4op-algorithms.frs` before and after.
7. **F7:** correct 23-bit LFSR, right-shifting, taps 0/14/15/22, masked.
8. **F6/F10:** rhythm mode from the phase-bit algorithm; settle the output-doubling question against Nuked and comment the answer in-code.
9. **F8:** already handled in step 2 if the bus extraction was done correctly; verify.
10. **Verification:** `diff-fm --a=opl4 --b=nuked` exact on every script in §5.3. Golden digests regenerate — expect all FM digests to move and **every PCM-only digest to stay byte-identical**. If a PCM digest moves, the table split in step 1 leaked.

### Step 6 — Retire the statistical comparator

1. `tests/opl4fmcompare.cpp`: delete the RMS-ratio and zero-cross-ratio assertions (L217-292 — the `ZeroCrossRate`/`Rms` block through the classic-map DIVERGENT check). They are superseded and they will now fail or pass for the wrong reasons.
2. Keep from that file: save/restore lockstep, the register-file storage sweep (with the 0x04 exclusion now unnecessary — see step 2), NEW/NEW2 and bank aliasing (now bus-level, so move these into a `tests/fm/fmbus_test.cpp`).
3. Remove the `#if !defined(OPL4_FM_YMFM)` guard inventory documented in §4 of the verification log. With runtime selection there is no build-time backend macro to guard on. Tests that only apply to tap-capable backends check `Caps().perChannelTaps` at runtime and skip.
4. Keep `diff-fm` in the standard run (wired in step 4) and promote its result to an assertion: the suite fails if any §5.3 script diverges on `--a=opl4 --b=nuked`; `--a=ymfm --b=nuked` stays report-only.

### Step 7 — Documentation and log reconciliation

1. Update `docs/inprogress/2026-09-13-moonsound/2026-09-13-0217-opl4-core-tdd.md` §4 to describe the OPL3 domain, and §12.2 to describe exact diffing.
2. In `2026-09-15-2114-opl4-ymfm-verification-findings.md`: mark §2.2 #5 (envelope shift ladder) as resolved by re-domaining; move §2.2 #6 (rhythm B0-kon) and §2.2 #1 (ws 3/5) from "await hardware recordings" to "arbitrated by Nuked-OPL3" and record the answers; mark §3.9 (per-channel taps) as an engine capability rather than an accommodation; mark §3.5/§3.6 as eliminated by the bus extraction.
3. Update `README.md` layout and build sections.
4. Mark `2026-09-15-2114-opl4-fm-backend-comparison.md` superseded: its two-build-tree A/B procedure dies with `OPL4_FM_BACKEND`. Keep the historical measurements, prepend a pointer to the runtime-selection workflow.
5. Add a short `cosim/README.md` section on the script format and on adding a fourth backend — the whole point of the interface is that the next one costs one file.

---

## 7. Acceptance criteria

1. `diff-fm --a=opl4 --b=nuked` reports **zero divergent samples, tolerance 0**, on every script in §5.3, for at least 200 000 ticks each.
2. `diff-fm --a=ymfm --b=nuked` runs and reports its divergences without failing the build — it is the calibration of how far a good reimplementation sits from the die model, and it is the sanity check that the harness itself is not trivially passing. Its per-script divergences are frozen in `cosim/reports/diff-fm-baseline-ymfm.txt` (step 4).
3. One binary runs the full suite under each compiled-in FM engine, selected at runtime, with no `#ifdef` guards on backend identity.
4. Every PCM golden digest is byte-identical to the pre-work values, at every step.
5. `grep -r "pcm/" src/fm/` and `grep -r "fm/" src/pcm/` both return nothing. Wire this into the test target as a hard failure.
6. `MoonSoundMfmGuest_Test.*` passes on all engines, and the FM RMS figures for both melodies converge across engines to **≤ 5 % ratio deviation** (for scale: the classic-map scenario already sits at 0.1 % post-adoption; the melodies are the fence that must close from today's ±25 % band).
7. The 60 dB cliff is gone: `tl-floor.frs` shows a smooth decay to the noise floor, not a step to digital zero.
8. **Zero compiler warnings** in all three configurations — default, `-DOPL4_WITH_YMFM=ON`, and `-DOPL4_WITH_YMFM=ON -DOPL4_WITH_NUKED=ON` — under project flags (`-Wall -Wextra -Wpedantic` on gcc/clang, the MSVC equivalent on Windows). Third-party objects (`ymfm_*.cpp`, `opl3.c`) are exempt, suppressed at the target level only.
9. **Every commit boundary passes the repository gates:** `ninja -C cmake-build-release` clean, `./cmake-build-release/bin/core-tests` green, PoC `opl4tests` green, cosim 6/6, oracle 55/55 — except step 5, where every FM digest moves **by design** and is re-pinned in the same commit while every PCM-only digest stays byte-identical against the pre-work snapshot. The `opl4tests` check count may grow but never shrink silently — a drop means checks were deleted, which belongs in step 6 where it is deliberate.
10. The step-4 baseline reports exist under `cosim/reports/`, and the final standard-suite run includes bit-exact `diff-fm --a=opl4 --b=nuked` over all §5.3 scripts (zero divergent samples, tolerance 0, ≥ 200 000 ticks each) as a hard assertion.
11. **The shipping default links no LGPL code:** with `OPL4_WITH_YMFM` and `OPL4_WITH_NUKED` both `OFF`, the default build produces binaries with no ymfm and no Nuked objects, and the full suite passes in that configuration.

---

## 8. Risks and notes for the implementing agent

- **Step 5 is where the engine is rewritten, and it will not be a patch.** `AdvanceEnvelope`, `OperatorOutput`, `WaveSample` and the tables are all replaced together — a half-migrated engine (new domain, old sustain scaling) will produce nonsense that is harder to debug than either endpoint. Do it as one commit, guided by the step-4 harness from the first compiling build.
- **The harness precedes the fix (step 4).** Its baseline reports are the pre-fix enumeration of every §3 defect; during step 5, disappearance-of-divergence against that baseline is the only progress metric. If the step-4 baseline shows a script already matching bit-exactly, the corresponding defect claim in §3 is wrong — stop and re-derive before writing engine code.
- **Nuked-OPL3 emulates YMF262, not YMF278B's FM block.** They are believed to be the same block; where a difference is suspected (the rhythm B0-kon suppression question), record it as a known deviation with the reasoning, rather than bending the engine to match.
- **Do not "fix" the PCM half while passing through.** Its documented deviations (finding §3.3 envelope cadence, §3.4 S==0 corner) are deliberate openMSX-lineage choices. Moving files must not change one arithmetic operation.
- **`Opl4Config` grows two fields**, which is an ABI change for the host integration doc. Both default to the in-tree engine, so host code compiles unchanged, but the integration doc should be updated in step 7.
- **Nuked master moves rarely but abruptly** (the 2026-08-24 4-op NEW-gate removal changed behaviour mid-2026). The pin in §5.4 is part of the spec: a re-pin is a behavioural change that re-opens the lag constant, both baselines and any recorded arbitration answers.
- **Nuked's internal routing stays live even though the bus owns routing.** For `perChannelTaps = false` backends `FmBus` passes `mixL`/`mixR` through, so Nuked's own CHA/CHB/CHC/CHD decode (fed by the forwarded `0x105`) is the output path — it agrees with the bus decode by construction post-step-2, but a mismatch there would appear as a uniform, constant-factor divergence across *every* script rather than a per-defect signature. When the whole baseline is uniformly off, diagnose routing before synthesis.
- **TTD state layout changes** in step 3 (runtime size + layout tag). Sessions recorded before the change will not load; that is already the policy for backend-specific state and should be called out in the TTD doc.
- **ymfm's structured save/restore is ~15× the cost of the in-tree memcpy** (finding §3.8). With runtime selection this stops being a build-wide constraint and becomes a per-engine capability — surface it through `FmCaps::exactStateSave` so the capture-cost gate can assert only against engines that claim cheap state.
