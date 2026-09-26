# Local patches against upstream ymfm

Two canonical patch files live in
`docs/inprogress/2026-09-10-turbosound-fm/verification/` (git-style,
headers `a/<file>` → apply with `-p1` from this directory):

* `ymfm-ttd.patch` — patches 1–3 below (time-travel debugging invariants),
* `ymfm-furnace-csm.patch` — patch 4 below (CSM key-on pulse, from Furnace).

Patch 5 (reset clears the running counters) is small and documented inline
below; it has no separate patch file.

Together they change three files; everything else is byte-identical to the
commit in VERSION.txt.

The emulator's time-travel debugging (TTD) serializes devices at
arbitrary T-state boundaries and requires two invariants that upstream
`save_restore` does not provide: a save is a **pure serialization** (no
observable effect on the running chip), and a restore is an **exact
continuation** (the restored chip is indistinguishable from the original
from the next clock onward).

## 1. `ymfm_fm.ipp` — `fm_engine_base::save_restore`

**What:** the post-restore `invalidate_caches()` call is replaced by

* `state.save_restore()` of `m_active_channels`, `m_modified_channels`
  and `m_prepare_count` (the prepare scheduling), and
* on restore only, a cache rebuild loop calling
  `m_regs.cache_operator_data(...)` for every operator.

**Why:** upstream `invalidate_caches()` sets `m_modified_channels = ~0`,
which forces a full `prepare()` on the next clock — and `prepare()` is
not idempotent (it clocks key state and clears the CSM key-on latch).
Consequences on upstream:

* a chip that merely *saved* diverges from an identical chip that never
  saved (prepare runs one extra time), and
* a chip *restored* from a checkpoint does not continue bit-exactly.

Persisting the three scheduling members makes save side-effect free and
restore exact. The explicit cache rebuild from registers avoids the
`prepare()` side effects: every register write marks all channels
modified, so with `m_modified_channels == 0` the live caches equal a
fresh computation.

**State size:** 482 → 494 bytes (three extra words; pinned by the
`YmfmTtdPatch` tests).

## 2. `ymfm_opn.cpp` — `ym2203::save_restore`

**What:** the trailing `update_prescale(m_fm.clock_prescale())` is
guarded with `if (!state.saving())` — restore only.

**Why:** `update_prescale()` mutates live chip state: it re-writes the
FM engine prescale, calls `m_ssg.prescale_changed()` and reconfigures
the SSG resampler. On restore that is required to rebuild the derived
resampler configuration; on save it violates the pure-serialization
contract (and, once the TSFM device routes SSG through its override
adapter, would call back into host code mid-serialization).

## 3. `ymfm_ssg.h` — `ssg_registers` constructor

**What:** the empty constructor becomes `ssg_registers() { reset(); }`
(zero-filling the 16-byte register file).

**Why:** upstream initialises `ssg_registers::m_regdata` only inside
`ssg_engine::reset()`. The TSFM device installs an SSG override adapter
(design §9.3) **before** the chip is reset, and `ssg_engine::reset()`
delegates the whole reset to the override — which intentionally no-ops —
so `m_regdata` is never written and stays indeterminate. The `ym2203`
constructor itself never resets the SSG. `save_restore()` then serialises
16 garbage bytes into the 494-byte state (found during the P4 gate:
two chips fed identical traffic produced byte-different saves; the
indeterminate window sat at offsets 422–437, i.e. exactly `m_regdata`).
With the constructor init the saves are deterministic and the cross-mode
core-hash gate holds.

**State size:** unchanged (494) — the registers were always part of the
payload; only their values were garbage.

## 4. `ymfm_fm.ipp` — `fm_operator::prepare` — CSM key-on is a pulse

**Origin:** Furnace (tildearrow) commit `26739bba`, 2023-02-05, "YM2612:
fix CSM on ymfm". Furnace's bundled ymfm (`src/engine/platform/sound/ymfm/`)
carries it; upstream ymfm (`81aec25c`, 2026-07) does not.

**What:** after `clock_keystate(m_keyon_live != 0)`, if the CSM bit of
`m_keyon_live` is set and the normal key bit is not, call
`clock_keystate(0)` before the CSM bit is cleared:

```cpp
clock_keystate(uint32_t(m_keyon_live != 0));
if (m_keyon_live & (1 << KEYON_CSM))
    if (!(m_keyon_live & (1 << KEYON_NORMAL)))
        clock_keystate(0);
m_keyon_live &= ~(1 << KEYON_CSM);
```

**Why:** in CSM mode (register `0x27` bits 7–6 = `10`) every timer A
overflow keys channel 3's four operators on. On hardware (Nuked-OPN2,
die-level: `mode_kon_csm` is asserted for one sample per overflow) that is
a *pulse*: phase reset, envelope attack restart, then release — a
retrigger on every overflow, which is what makes the mode usable for
formant/speech synthesis. Upstream ymfm raises `KEYON_CSM`, the next
`prepare()` keys the operator **on** and clears the bit, and nothing keys
it **off** again until the next `prepare()` — normally 4096 samples later.
So the channel sustained exactly like a manually keyed note and later
overflows found `m_key_state` already 1, so `clock_keystate()` saw no
edge and did not retrigger. Measured on our copy (2026-09-13, standalone
probe, one carrier AR 31 / RR 15, timer A every 40 samples): output
byte-identical to a manual key-on, full level, no pulses. With the patch:
a restart from phase 0 every 40 samples with the envelope decaying in
between. Manual key-on output is unchanged.

**Interaction with patch 1:** none new — `prepare()` was already
non-idempotent and is still never forced by save/restore. `m_keyon_live`
and `m_key_state` were already serialized, so the TTD state size stays
494 and the `YmfmTtdPatch` stress (which exercises CSM writes) is
unaffected.

**Test:** `TsfmTimer_Test.CsmRetriggersEveryTimerATick`
(`core/tests/emulator/sound/tsfm/tsfm_core_test.cpp`): timer A every
400 FM samples, no manual key-on, fast release — one burst per overflow,
each starting on the overflow sample; on pristine upstream the test sees
a single sustained burst and fails.

## 5. `ymfm_fm.ipp` — `fm_engine_base::reset` — clear the running counters

**What:** `reset()` also zeroes `m_env_counter` (the envelope generator
clock) and `m_total_clocks` (the low 8 bits of the clock count), and returns
the prepare scheduling added by patch 1 (`m_active_channels`,
`m_modified_channels`, `m_prepare_count`) to its constructor values, so
every channel is re-prepared on the next clock.

**Why:** upstream `reset()` restores registers, channels and operators but
leaves both counters running from construction. After a machine reset the
envelope clock phase - and the timer B first-load offset, which is taken
from the low clock bits (`-(m_total_clocks & 15)`) - therefore depended on
how long the emulator instance had been up: two recordings from the same
reset + snapshot differed in exactly these bytes of the TTD payload
(found 2026-09-25 re-recording `testdata/ttd/`). A real chip's /IC reset
clears its internal state. The YM2203 has no LFO, so the LFO counter
(`reset_lfo()`, OPNA only) is not involved.

**Interaction with patch 1:** the scheduling fields it serializes are now
also reset; all five fields were already serialized, so the TTD payload
stays 494 bytes.

**Test:** `TsfmCore_Test.ResetClearsEngineCounters`
(`core/tests/emulator/sound/tsfm/tsfm_core_test.cpp`): two devices, one run
for a while first, give byte-identical ymfm state after a reset.

## Furnace changes reviewed and NOT ported (2026-09-13)

Furnace's ymfm copy (`5be1d6b`, 2026-09-13) differs from upstream in nine
OPN-related files. Everything except patch 4 falls into these groups:

| Change | Files | Decision and reason |
|---|---|---|
| Frequency latch "armed" semantics: a high-byte write (`0xA4–0xA6`, `0xAC–0xAE`) arms the latch, a low-byte write commits only when armed and then clears the latch | `ymfm_opn.cpp` `opn_registers_base::write` | **Not ported.** Introduced with Furnace's initial ymfm import (`2879b5e4`, 2021-12-15, "arcade: add ymfm-based core") with no hardware rationale. Nuked-OPN2's die-level model keeps `reg_a4` as a persistent latch and every `0xA0` write commits with it, which is what upstream ymfm does. Porting would change pitch for software that writes low bytes without re-writing the high byte. |
| TL ramp (`tl_ramp`, `m_actual_level`, `m_ramp_counter`) | `ymfm_fm.h`, `ymfm_fm.ipp` | Not ported. OPP/OPZ feature; for OPN `tl_ramp` is always 0 so it is behaviour-neutral, but it adds two per-operator members that TTD would have to reason about. |
| `compute_volume` early-out also requires `eg_shift == 0` | `ymfm_fm.ipp` | Not ported. `eg_shift` is only non-zero on OPZ; no effect on OPN. |
| `m_timer_running[4]`, `assert` removed in `engine_timer_expired` | `ymfm_fm.h`, `ymfm_fm.ipp` | Not ported. Four-timer chips (OPX/OPQ) only. |
| Per-channel/per-operator output capture and debug accessors (`m_output`, `m_special1/2`, `debug_fm_engine`, SSG/ADPCM `get_last_out`) | `ymfm_fm.h/.ipp`, `ymfm_opn.h`, `ymfm_ssg.*`, `ymfm_adpcm.*` | Not ported. Furnace UI plumbing, no emulation effect. If per-channel FM capture is ever wanted, take `m_output` from here. |
| `array_size`, `std::array` → C array, virtual destructors removed, `snprintf` rewrites, `roundtrip_fp` rewrite | `ymfm.h`, `ymfm_fm.h/.ipp`, `ymfm_opn.cpp` | Not ported. Style/portability; `roundtrip_fp` is arithmetically identical. |
| `if (index >= REGISTERS) return false;` after the assert in `write` | `ymfm_opn.cpp` | Not ported. Our callers mask the address to 8 bits before the write. |

Furnace's own YM2203 platform (`src/engine/platform/ym2203.cpp`) also
offers Nuked-OPN2 and a cycle-level LLE core; both retrigger CSM natively.

## Evidence

* `verification/stress.cpp` in the TSFM design folder — 6 seeds × 4M
  steps of randomized YM2203 traffic, lockstep A (never saves) / B
  (saves) / C (restored at checkpoints) chips: PASS with the patch,
  FAIL on upstream 81aec25c. Details:
  `verification/verification-report.md` in the same folder.
* gtest port at CI size:
  `core/tests/emulator/sound/tsfm/ymfm_ttd_patch_test.cpp`
  (`YmfmTtdPatch.*`) — 1 seed × 400k steps with 5k-step checkpoints,
  plus a 50k-step run checkpointing **every** step.
* In-tree negative control (2026-09-12, plan P1 gate): after
  `patch -R -p1` of this patch (back to pristine upstream), both
  `YmfmTtdPatch` tests **fail** (`sideEffectFree = false`,
  `restoreMismatches > 0`, `stateSize != 494`); re-applying the patch
  makes both pass again.
* Patch 3 evidence (2026-09-12, plan P4 gate): with the constructor init
  removed, `TsfmPlayer_Test.CoreHashSameInTurboAndSoundOff` fails with
  byte-different ymfm saves across identical sessions (stale heap in the
  SSG register window); with it, normal/turbo/sound-off saves are
  byte-identical for every frame.

## Re-applying on upgrade

1. Copy the new upstream files over this directory (file list in
   VERSION.txt).
   NOTE: do not create a file named exactly `VERSION` in this directory —
   this dir is a SYSTEM include path, and on case-insensitive filesystems
   (macOS, Windows) it would shadow the C++ standard header `<version>`.
   That is why the provenance file is `VERSION.txt`.
2. From this directory apply both canonical patches:
   `patch -p1 < <repo>/docs/inprogress/2026-09-10-turbosound-fm/verification/ymfm-ttd.patch`
   `patch -p1 < <repo>/docs/inprogress/2026-09-10-turbosound-fm/verification/ymfm-furnace-csm.patch`
3. If a hunk no longer applies, re-derive it by hand — all four functions
   are small; the invariants to preserve are: save must not call
   `invalidate_caches()` / `update_prescale()`, restore must rebuild caches
   and prescale without `prepare()` side effects, `ssg_registers` must
   start with a deterministic register file (drop the third hunk only if
   upstream itself initialises `m_regdata` at construction), and a CSM
   key-on must be released in the same `prepare()` unless the operator is
   keyed normally (drop patch 4 only if upstream fixes CSM itself). Then
   refresh the canonical patch files from your tree.
4. Re-run `YmfmTtdPatch.*` and `TsfmTimer.*`; the state-size pin (494)
   must be re-derived from `stress.cpp` after any layout change — never
   adjust it blindly.
