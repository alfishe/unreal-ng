# DONE — Z80 reference test integration & block I/O fixes (2026-01-18, revised 2026-09-24)

**Status:** complete.

## What landed

- Native C++ gtest port of Patrik Rak's z80test-1.2a suite
  (`core/tests/z80/z80test/`, gtest `Z80TestVerification.*`): all 163 hardware-captured CRC
  vectors driven directly through the real in-tree `core/src/emulator/cpu` code (not a
  reference-model reimplementation). **159/159 passing**, 4 intentionally out of scope
  (`SCF`/`CCF` NEC and ST CMOS flavors — not applicable to this emulator's Zilog NMOS Z80
  target). See `core/tests/z80/z80test/README.md` for the suite's purpose, the NEC/ST scope
  decision, and how to debug a future failure.
- Block I/O instruction flag fixes (INI/IND/OUTI/OUTD + repeating variants) — see
  `z80_block_io_fixes.md`; these are what took the suite from partial to full pass.
- 2026-09-24 cleanup pass (`z80test-verification-gaps.md`): removed the abandoned, broken Python
  reference-model approach (`core/tests/z80/z80test/to-delete/`), added the README, banner-marked
  `z80_reference_status.md` as historical (it described the abandoned approach's 114/163
  result, which is no longer representative), and cross-linked this suite with the separate
  ZEXALL/zexdoc/zexbit/zexfix coverage in `tools/poc/017-z80-standalone-cpu/`.
- 2026-09-24, same day: rewrote the Python reference model from scratch
  (`core/tests/z80/z80test/python_reference/`) as a genuinely independent cross-check — no
  dependency on the C++ core, an independently-transcribed instruction interpreter, driven by
  the same `z80test_vectors.h`. **159/159 passing**, same 4 skips as the C++ suite. Root cause
  of the old 114/163 ceiling was a wrong memory-addressing model (see
  `python_reference/README.md`); fixed by replaying every vector through one persistent shared
  memory image in file order, matching `z80test_runner.cpp`'s single-shared-emulator-instance
  semantics exactly.

## Evidence

- `./cmake-build-agent-release/bin/core-tests --gtest_filter="Z80TestVerification.*"` →
  159/159 passed, 4 skipped, 0 failed (verified 2026-09-24).
- Full `core-tests` suite green after the cleanup (see verification run in the same session).
- `python3 core/tests/z80/z80test/python_reference/run_verification.py` → 159/159 passed, 4
  skipped, 0 failed (verified 2026-09-24) — independent of the C++ core.

## Known limitation (not blocking)

The original `z80test-1.2a` source archive and the script that generated
`core/tests/z80/z80test/z80test_vectors.h` from it are no longer in the repo (source is
MIT-licensed; loss looks incidental, not policy-driven). If a vector is ever suspected wrong,
re-vendoring the archive is the fix — tracked in `z80test-verification-gaps.md`, not currently
blocking since all in-scope vectors pass.

## Follow-ups

- None required. Optional, low-priority: re-vendor `testdata/z80/z80test-1.2a/` if a future
  vector audit needs it.
