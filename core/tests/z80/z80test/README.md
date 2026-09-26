# z80test Verification Suite

Native C++ port of Patrik Rak's [z80test-1.2a](https://github.com/redcode/Z80) CRC-based CPU
verification suite. Unlike a hand-written reference model, it drives all 163 hardware-captured
test vectors directly through the real in-tree emulator (`EmulatorManager` → `Z80` →
`Memory`), so a pass here means the actual `core/src/emulator/cpu` code — not a copy or a
reimplementation — matches genuine Zilog Z80 silicon.

## Files

| File | Purpose |
|------|---------|
| `z80test_vectors.h` | All 163 test vectors (opcode, base state, counter/shifter masks, expected CRC-32 + iteration count), baked in from the original `tests.asm`. |
| `z80test_iterator.h` | Port of the counter/shifter combinatorial-expansion algorithm from `idea.asm`, plus the CRC-32 accumulator. |
| `z80test_runner.cpp` | The gtest fixture (`Z80TestVerification`) that sets Z80/memory state per iteration, executes, and checks the accumulated CRC + iteration count against the vector. |

Run with:

```bash
./cmake-build-agent-release/bin/core-tests --gtest_filter="Z80TestVerification.*"
```

Current result: **159/159 passing**, 4 intentionally skipped (see below). Full detail on how
this suite reached this state, and what's still open, is tracked in
[`../../../../docs/inprogress/2026-01-18-z80-tests/`](../../../../docs/inprogress/2026-01-18-z80-tests/)
(`z80test-verification-gaps.md` is the most current doc there; `z80_reference_status.md` is
superseded — see the banner at its top).

## The 4 skipped vectors

`SCF (NEC)`, `CCF (NEC)`, `SCF (ST)`, `CCF (ST)` are excluded via an explicit blacklist in
`RunAllVectors`. These check CMOS/NEC-specific SCF/CCF flag quirks that don't apply to this
emulator's Zilog NMOS Z80 target — there is no in-tree code path that claims to emulate those
flavors, so there's nothing meaningful to assert. This is a deliberate scope decision, not a
known failure.

## Relationship to the ZEX suite and the Python reference model

`tools/poc/017-z80-standalone-cpu/` separately runs the Kevin Horton ZEX family
(zexdoc/zexall/zexbit/zexfix) — a different vector corpus, particularly strong on undocumented
opcode coverage — against a hand-extracted standalone copy of the CPU core. Both suites
currently pass 100% of what they assert. They're complementary: this suite exercises the
actual in-tree code directly; poc/017 covers the ZEX corpus against an extracted copy (chosen
for build isolation, not because a hand-extraction was strictly required for this class of
test — the two projects simply predate a decision to converge on one approach).

[`python_reference/`](python_reference/) is a third, genuinely independent leg: a from-scratch
Z80 interpreter written in Python against the same 163 vectors, with no dependency on the C++
core at all — **159/159 passing**, same 4 skips. Where poc/017 verifies a mechanical *copy* of
the C++ source (so it shares any bug that copy inherited), the Python model is a second
implementation from the same published hardware research, useful for single-stepping a
divergence rather than just hashing one.

## Debugging a future failure

If `RunAllVectors` ever fails, it automatically falls back to a per-iteration diff against a
CSV reference file at `tools/verification/z80/reference/<index>_<test-name>.csv`, printing the
first mismatching iteration's opcode and F register diff. Those CSVs aren't checked in — they
don't exist until you generate them by manually enabling and running the (normally `DISABLED_`)
`Z80TestVerification.DISABLED_GenerateReferenceCSVs` test:

```bash
./cmake-build-agent-release/bin/core-tests \
  --gtest_also_run_disabled_tests \
  --gtest_filter="Z80TestVerification.DISABLED_GenerateReferenceCSVs"
```

That writes one CSV per vector (current-emulator ground truth, not an external reference) plus
a `reference_crcs.md` summary table, all under `tools/verification/z80/reference/` (gitignored
— treat as scratch, regenerate on demand).

## Known limitation

The original `z80test-1.2a` source archive (`testdata/z80/z80test-1.2a/`, MIT-licensed) and the
script that generated `z80test_vectors.h` from it are no longer in the repository — only the
generated header survives. If a vector is ever suspected to carry a transcription error, it
can't currently be regenerated or diffed against upstream; re-vendoring the source archive is
the fix, tracked in the gaps doc referenced above.
