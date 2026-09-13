# Local patches against upstream ymfm

The canonical patch file is
`docs/inprogress/2026-09-10-turbosound-fm/verification/ymfm-ttd.patch`
(git-style, headers `a/<file>` → apply with `-p1` from this directory).
It changes three files; everything else is byte-identical to the commit in
VERSION.

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
2. From this directory apply the canonical patch:
   `patch -p1 < <repo>/docs/inprogress/2026-09-10-turbosound-fm/verification/ymfm-ttd.patch`
3. If a hunk no longer applies, re-derive it by hand — all three functions
   are small; the invariants to preserve are: save must not call
   `invalidate_caches()` / `update_prescale()`, restore must rebuild caches
   and prescale without `prepare()` side effects, and `ssg_registers`
   must start with a deterministic register file (drop the third hunk only
   if upstream itself initialises `m_regdata` at construction). Then
   refresh the canonical patch file from your tree.
4. Re-run `YmfmTtdPatch.*`; the state-size pin (494) must be re-derived
   from `stress.cpp` after any layout change — never adjust it blindly.
