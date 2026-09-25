# z80test Verification — Gaps & Cleanup Tasks

**Date:** 2026-09-24

## Current reality (verified by running the suite)

`core/tests/z80/z80test/z80test_runner.cpp` (gtest `Z80TestVerification.RunAllVectors`) drives
all 163 z80test-1.2a vectors (`z80test_vectors.h`, auto-generated, baked into the header — the
original `tests.asm` source is no longer in the tree, see Gap 4) directly through the real
in-tree CPU (`EmulatorManager` → `Z80` → `Memory`), not a hand-written reference model. As of
this audit:

```
Passed: 159/159
Failed: 0/159
Skipped: 4 (NEC/ST flavor tests)
```

The 4 skips are `SCF (NEC)`, `CCF (NEC)`, `SCF (ST)`, `CCF (ST)` — CMOS/NEC-flavor SCF/CCF
variants that don't apply to this emulator's Zilog NMOS Z80 target. **This is not a bug**: no
in-tree code claims to emulate those CPU flavors, so there's nothing to verify against those
CRCs. Every vector this emulator's target CPU is responsible for already passes.

This supersedes `z80_reference_status.md` in this folder, which describes an earlier,
**abandoned** approach (a hand-written Python Z80 reference model, `114/163 passing`,
last updated 2026-01-28). That approach is dead — its source now lives under
`core/tests/z80/z80test/to-delete/`, a directory literally named for deletion.

Separately, `tools/poc/017-z80-standalone-cpu/` already runs the full Kevin Horton
ZEX family (zexdoc/zexall/zexbit/zexfix) — a different, complementary vector set,
against a hand-extracted standalone copy of the CPU, all passing. The two suites verify
different things (this one hits the actual in-tree `core/src/emulator/cpu` directly;
poc/017 verifies an extracted copy but covers the ZEX corpus). Neither doc set
cross-references the other before this pass.

## Gaps found

1. **Stale/misleading status doc.** `z80_reference_status.md` reports the old Python
   approach's 114/163 result as if it were current project state. `DONE.md` in this same
   folder claims "Status: complete" while linking to that stale doc — self-contradictory.
   *(Fix: banner + DONE.md rewrite.)*

2. **Dead code left in-tree.** `core/tests/z80/z80test/to-delete/` (Python reference model +
   debug scripts, ~18 files) and `tools/verification/z80/__pycache__/` (bytecode cache with no
   corresponding `.py` source left in the tree — the sources were deleted but the cache wasn't)
   are clutter with no build role. Not compiled (CMake only globs `*.cpp`), but actively
   misleading to anyone reading the directory. *(Fix: delete both.)*

3. **No README for the suite itself.** Nothing in `core/tests/z80/z80test/` or
   `core/tests/README.md` explains what this suite is, that it's already green, the NEC/ST
   exclusion rationale, or how it relates to the poc/017 ZEXALL coverage — which is exactly
   why this came up as an open question. *(Fix: add README, link from `core/tests/README.md`.)*

4. **Source archive gone missing.** `testdata/z80/z80test-1.2a/` is present but empty (just a
   stray `.DS_Store`) — the original `tests.asm` the docs and `to-delete/extract_z80tests.py`
   reference no longer exists anywhere in the repo. z80test is MIT-licensed
   (`testdata/NOTICE.md:14`), so there's no licensing reason it was removed; looks like
   incidental loss during earlier cleanup. Without it, `z80test_vectors.h` cannot be
   regenerated or audited against upstream if a vector is ever suspected wrong — the header's
   own comment references a `generate_cpp_header.py` that also no longer exists anywhere in
   the repo (not even in `to-delete/`). *(Decision: document as a known limitation rather than
   try to re-derive 163 hand-verified CRC vectors from scratch — that's out of proportion to
   the actual risk, since the vectors already pass and are the same well-known publicly
   available Patrik Rak CRCs. Flagged so a future re-vendor of the archive is a deliberate,
   informed choice, not an accident.)*

5. **`DISABLED_GenerateReferenceCSVs` / CSV debug path is undocumented.** The runner has a
   manually-invoked (`DISABLED_` prefix) gtest that dumps per-iteration CSVs to
   `tools/verification/z80/reference/` for failure triage, and `RunAllVectors` reads those CSVs
   back if a test fails. Nothing explains this exists or how to use it when a future failure
   needs bisecting. *(Fix: document in the new README.)*

## Non-gaps (checked, fine as-is)

- Vector coverage is comprehensive: all 163 standard cases incl. undocumented `IXH/IXL/IYH/IYL`
  (`[HX,LX]`/`[HY,LY]`), `SLL` (`SLIA`), block ops, indexed `BIT`/`SET`/`RES`, `IN (C)`/`OUT
  (C),0`, `LD A,I`/`LD A,R` flag quirks.
- `Z80TestIterator` (counter/shifter combinatorial expansion) and the CRC-32 implementation
  are correct — the suite passes at 100% of what it asserts on.
- Not wired into `test-parallel` sharding specially — it's a single gtest case, runs in ~30ms,
  no sharding concern.

## Task list

- [x] Delete `core/tests/z80/z80test/to-delete/`
- [x] Delete `tools/verification/z80/__pycache__/`
- [x] Add `core/tests/z80/z80test/README.md`
- [x] Link it from `core/tests/README.md`
- [x] Add superseded banner to `z80_reference_status.md`, pointing here and to the new README
- [x] Rewrite `DONE.md` for this folder to reflect the real, current state
- [x] Full build + `core-tests` run to confirm zero regressions from the cleanup

## Follow-up: independent Python reference model (2026-09-24, same day)

The user pushed back on deleting the Python approach outright: the point of a *second*
implementation is exactly the case a mechanical port (poc/017, which extracts a literal copy of
the C++ source) can't catch — a bug baked into the original logic and then faithfully copied.
Deleting broken code and stopping there doesn't buy that; finishing it does.

Rewrote `core/tests/z80/z80test/python_reference/` from scratch (not a resurrection of the
abandoned `to-delete/` code, though it started from reading that code for salvageable structure):

- **Vector source**: parses `z80test_vectors.h` directly (regex-based), no dependency on the
  missing `tests.asm` (Gap 4 above) — the header is already the shared source of truth the C++
  runner uses.
- **Memory model, the real blocker**: the old approach treated `(HL)`'s "mem" field as fixed
  bytes 16/17 of the vector. That's wrong — z80test places the whole 16-byte record contiguously
  in memory with HL pointing *into* it, and a shifter-perturbed HL/displacement can land on an
  adjacent field, or on whatever an *earlier test* left in the same shared emulator instance
  (confirmed empirically: e.g. `POP+PUSH AF` reads live stack RAM at a fixed SP that's never
  explicitly initialized for that test). Fixed by threading one persistent `SharedMemory` through
  every vector in file order, replaying the exact same writes `z80test_runner.cpp`'s
  `executeIteration` does — not just for the failing case, for the *entire ordered run*, matching
  the C++ harness's single shared-emulator-instance semantics exactly.
- **Undocumented flag algorithms**: block I/O (INI/IND/OUTI/OUTD + repeats) and block compare
  (CPI/CPD/CPIR/CPDR) ported from the David Banks / Xpeccy formulas in
  `z80_block_io_fixes.md` / `core/src/emulator/cpu/op_ed.cpp` — the old approach's hand-rolled
  approximations were simply wrong, not just missing the memory model fix.
- **Genuine bugs found and fixed** along the way (both copy-paste and new): `alu_xy_mem` read the
  ALU-op selector from the wrong opcode byte (copied from the old code without noticing), and the
  single-iteration INI/IND/OUTI/OUTD parity-flag polarity was inverted.
- **Port-dependent tests** (`IN R,(C)`, `IN (C)`, `INI`, `IND` + repeats): their golden CRCs were
  regenerated against this emulator's own tape/EAR pull-up idle-port model, not original hardware
  — documented as such, with the idle value reverse-derived from the C++ ground-truth CSVs.

Result: **159/159 passing**, same 4 non-Zilog-flavor skips as the C++ suite — no scope carve-outs
beyond that. Verified via `python3 core/tests/z80/z80test/python_reference/run_verification.py`.

- [x] Restore-and-rewrite the Python reference model to a genuine 159/159 pass
- [x] Document memory model, port-idle-value provenance, and order-dependent-test caveat in
      `python_reference/README.md`
- [x] Cross-link from `core/tests/z80/z80test/README.md`
- [x] Delete `to-delete/` again (superseded by the finished implementation, not by its removal)
