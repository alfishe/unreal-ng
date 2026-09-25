# Z80 core upgrade: vendor unreal-z80 as the execution engine

## Goal

Replace the hand-written opcode-execution engine inside `Z80::Step()`
(`core/src/emulator/cpu/z80.cpp`, previously `op_noprefix.cpp` + the other
`op_*.cpp` prefix families) with `unreal-z80` — a standalone Z80 core
extracted from this same codebase and since developed independently —
vendored into `core/src/3rdparty/unreal-z80/`, following the same
one-library-per-directory pattern already used for `ymfm`, `z80ex`, `zstd`,
etc.

The `Z80` class's public shape (struct layout, method signatures) had to stay
unchanged: ~260 files across `core/`, `core/tests/`, `core/automation/`,
`unreal-qt/` and `tools/` reference `Z80`/`Z80State` directly, many via direct
field access on the public register structs (TTD's
`CaptureCpuState`/`RestoreCpuState` in `core/src/debugger/ttd/ttdcheckpoint.cpp`
being the sharpest example). Only the opcode-execution seam itself was to move.

## Why

`unreal-z80` is a cleaner, actively-benchmarked, callback-driven (z80ex-style)
implementation of the same CPU, with its own verification suite (z80test,
ZEX, FUSE-style differential vs z80ex) and ongoing performance work. Moving
to it centralizes the opcode-correctness and timing work in one place shared
with a project that has its own test/benchmark harness, instead of
maintaining a second hand-written interpreter here indefinitely.

## Methodology

1. **Survey both sides first** (two parallel research passes): the current
   `Z80` class's public API, its integration points (breakpoints, TTD
   checkpoint/coverage, the opcode profiler, fast-tape/disk-autostart traps,
   ROM paging, frequency/turbo rescaling — all driven inline from
   `Z80Step()`), and its ~260 dependents; and `unreal-z80`'s public API,
   feature coverage and known gaps.
2. **Identify the gaps before committing to the approach**: at the time
   vendoring started, `unreal-z80` had no breakpoint or save-state API (not
   needed — those stay host-side), and — the load-bearing findings — INT/NMI
   acknowledge cycles bypassed the memory/contend callbacks entirely, the
   halted CPU's M1 cycles were never reported to the contend hook, and the
   memory-read callback's `m1State` flag was documented as "M1 fetch only"
   but actually fires on every instruction-stream byte.
3. **Plan the integration as a wrapper, not a rewrite**: keep `Z80Step()`'s
   surrounding orchestration (ROM paging, breakpoints, analyzers, fast-load
   traps, TTD coverage, the profiler) exactly as-is, and replace only the
   inner
   ```cpp
   cpu.prefix = 0x0000;
   cpu.opcode = m1_cycle();
   (normal_opcode[opcode])(&cpu);
   ```
   seam with a call into the vendored engine. Sync the register file in and
   out around every step (bulk accessors, added to the library for this);
   wire the engine's memory/port/contend/IM2-vector/RETI/RETN callbacks to
   the existing `Z80::rd/wd/in/out` and `UlaContention` logic so bus tracing,
   floating-bus fallback and contention timing are unchanged; reconstruct the
   decoded-instruction fields (`prefix`/`opcode`) from the instruction
   structure rather than trusting `m1State`; keep INT/NMI acceptance
   host-side for exact timing/state parity.
4. **Fix the identified gaps upstream, in the vendored library itself**
   (`core/src/3rdparty/unreal-z80/`), not by working around them in the
   wrapper: this way the fixes are general-purpose and reusable rather than
   emulator-specific hacks. All six were later contributed back and merged
   upstream (library version 0.2.0), at which point the local patch here was
   dropped entirely — see [patches.md](patches.md) for the six changes and
   their rationale, and [version.txt](version.txt) for the exact commit this
   copy is pinned to.
5. **Verify exhaustively before considering it done**: a throwaway spike
   harness against a slice of this repo's own FUSE/z80test data first, then
   the full mandatory gate from `AGENTS.md` — build with zero compiler
   warnings, full `core-tests` suite green, with explicit attention to
   `FusePhase_Test` (exhaustive documented+undocumented opcode bus-phase
   traces), the z80test CRC-vector runner, bus/IO-phase timing,
   interrupt/NMI timing, ULA contention across every affected model, and the
   full TTD checkpoint/restore and breakpoint suites.
6. **Treat every genuine behavioral discrepancy as a decision, not a
   silent override.** One surfaced: `FusePhase_Test.AllOpcodes` failed on the
   MEMPTR value produced by the *repeat* iteration of `INIR`/`INDR`/`OTIR`/
   `OTDR` (4 of 1356 cases). Investigation traced it to a genuine correction
   in `unreal-z80` verified against real NMOS Z80 silicon via z80test's
   `z80memptr` suite: the classic FUSE vectors here (~2003-2004) predate that
   research and encode the historically-assumed-but-wrong formula. Decision:
   fix `testdata/z80/fuse/tests.expected` to the silicon-verified value
   rather than reverting the engine or silently skipping the check — see the
   comment above `skipAF` in `core/tests/emulator/cpu/fuse_phase_test.cpp`
   for the corrected cases and citation.

## Outcome (as of 2026-09-24)

- `core/src/3rdparty/unreal-z80/` vendored at commit `b7451bd` (library
  0.2.0, MIT-licensed, no local patch — see [patches.md](patches.md)).
- `Z80::Step()`'s opcode-execution seam now delegates to the vendored
  engine; everything else in `Z80` is unchanged.
- Full verification gate green: zero build warnings, full `core-tests` suite
  passing (3296/3296, same 2 pre-existing skips as baseline).
- Known trade-off: CPU-bound workloads run ~1.3-1.4x slower than the former
  in-class interpreter, from copying the register file in and out of the
  engine around every instruction (see "Performance" in
  [patches.md](patches.md)). Recovering that needs the engine to become the
  sole owner of the register file — a follow-up, not required for
  correctness.
- Not yet committed — see this folder's status marker.
