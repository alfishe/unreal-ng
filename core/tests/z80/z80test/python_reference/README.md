# Independent Python Z80 Reference Model

A from-scratch, independently-written Z80 instruction interpreter that
cross-verifies `core/src/emulator/cpu` against the same 163 z80test-1.2a CRC
vectors the C++ suite (`Z80TestVerification.*`, one directory up) uses —
except this one doesn't touch the C++ core at all. It's a second, separate
implementation of Z80 flag semantics, written in a different language from
different published sources, so a bug shared by both would have to be a
genuine spec ambiguity, not a copy-paste artifact.

**Current result: 159/159 passing**, 4 skipped (matches the C++ suite
exactly — non-Zilog CPU flavors, see below).

```bash
python3 core/tests/z80/z80test/python_reference/run_verification.py
```

## Why this exists

`core/tests/z80/z80test/z80test_runner.cpp` already verifies the real CPU
against real Zilog hardware CRCs, which is the strongest ground truth
there is. But when a CRC test fails, it only tells you *that* something's
wrong, not *why* — you get a 32-bit hash mismatch, not a register trace.
A second, independent implementation gives you something to single-step
side-by-side against a divergence, and catches the class of bug a
mechanical port (see `tools/poc/017-z80-standalone-cpu/`, which extracts
a *copy* of the same C++ source) structurally can't: a logic error that
made it into the original code and was then faithfully copied.

## Files

| File | Purpose |
|------|---------|
| `z80_reference.py` | The interpreter: ALU/rotate/BIT/block-op/etc. flag semantics, executor registry keyed by z80test instruction name. |
| `vectors_loader.py` | Parses `../z80test_vectors.h` directly (same source of truth as the C++ runner) — no dependency on the original `tests.asm`, which is no longer in the tree. |
| `iterator.py` | Python port of `z80test_iterator.h`'s counter/shifter combinatorial expansion, plus the matching CRC-32. |
| `run_verification.py` | Drives all three against the vectors and reports pass/fail; `--diff` pinpoints the first mismatching iteration against a C++ ground-truth CSV. |

## The memory model — and why a full run is order-dependent

z80test's vector format places a 16-byte "data record" (F,A,BC,DE,HL,IX,IY,
MEM,SP) contiguously in memory, anchored so `(HL)` points at the MEM field.
The counter/shifter can perturb HL's low byte — or, for indexed tests, the
*displacement byte itself* (a real opcode byte) — so the effective address
sometimes lands on an adjacent field of that same record, or beyond it.

The C++ harness reproduces this faithfully because it runs **all 163
vectors through one persistent emulator/memory instance**, in a fixed
order, and only ever explicitly initializes a handful of bytes per test.
A few tests read memory that was never written *for them* — its content is
whatever an earlier test in the sequence left behind (`POP+PUSH AF`'s stack
read, and indexed tests hit by a displacement-shift iteration reading past
the single byte the harness deliberately writes at the base displacement).

This model reproduces that exactly: `run_verification.py`'s default (no
`--test`) run threads one `z80_reference.SharedMemory` through every vector
in file order, mirroring the C++ side byte-for-byte. Running a single test
via `--test "NAME"` uses a private, empty memory instead — correct for the
vast majority of tests, but it will disagree with the full-run CRC for the
handful of order-dependent ones. That's expected, not a bug; it's why
`--test` is billed as isolation for debugging, not a substitute for a full
run.

## The 4 skipped vectors

`SCF (NEC)`, `CCF (NEC)`, `SCF (ST)`, `CCF (ST)` — CMOS/NEC-flavor SCF/CCF
variants no in-tree code claims to emulate. Same exclusion, same reasoning,
as the C++ suite (see `core/tests/z80/z80test/README.md`).

## Debugging a future failure

```bash
# Ground truth from the *verified* C++ emulator (already checked against
# real Zilog hardware CRCs) — regenerate whenever core/src/emulator/cpu
# changes and you need to re-diff:
./cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests \
    --gtest_filter="Z80TestVerification.DISABLED_GenerateReferenceCSVs"

# Then:
python3 core/tests/z80/z80test/python_reference/run_verification.py --diff
```

`--diff` replays the failing test's iterations against its ground-truth CSV
(`tools/verification/z80/reference/<index>_<name>.csv`, gitignored, created
by the command above) and prints the first mismatching iteration's opcode
and the exact flag-bit diff.

## Provenance

Undocumented flag algorithms for block I/O (`INI`/`IND`/`OUTI`/`OUTD` and
their repeating forms) and block compare (`CPI`/`CPD`/`CPIR`/`CPDR`) are
transcribed from the same published research the C++ core itself is built
from — David Banks' Z80 undocumented-flags analysis and the Xpeccy
emulator, both cited in
[`../../../../docs/inprogress/2026-01-18-z80-tests/z80_block_io_fixes.md`](../../../../docs/inprogress/2026-01-18-z80-tests/z80_block_io_fixes.md).
Everything else (ALU, rotates, INC/DEC, 16-bit arithmetic, BIT, DAA, block
LD/CP, the Q-register SCF/CCF model) is written directly from the Zilog Z80
undocumented-behavior literature this project already relies on elsewhere.

The `PORT_IDLE_VALUE = 0xFF` constant used for `IN R,(C)`/`IN (C)`/`INI`/
`IND` (and their repeating forms) was reverse-derived from the C++ runner's
ground-truth CSVs rather than looked up: those tests' golden CRCs were
regenerated against this emulator's own tape/EAR pull-up idle-port model
(documented inline in `../z80test_vectors.h`), not original hardware CRCs,
so 0xFF is this model's best faithful match to that emulator-specific
default, not a general Z80 fact.
