# TDD: eliminate the Z80 register-file copy between the host and unreal-z80

**Audience:** an implementation agent picking up this work fresh. Read
[README.md](README.md) and [patches.md](patches.md) first for the context of
the original vendoring; this document assumes that background.

**Status:** implemented and verified 2026-09-24. See "Results" at the end of
this document. Uncommitted (per repo policy, awaiting explicit "commit"
instruction).

## Goal

`core/src/emulator/cpu/z80.cpp` currently calls `Z80CpuSetRegisters` before
and `Z80CpuGetRegisters` after every single `Z80CpuStep()` call, bulk-copying
the whole register file into and out of the engine each instruction. A
purpose-built benchmark
(`core/benchmarks/emulator/cpu/z80_engine_bus_benchmark.cpp`) decomposed the
integration's measured 1.3-1.4x CPU-bound slowdown and found this copy is
**~82% of the total per-instruction overhead** (5.81 ns of 7.08 ns), dwarfing
the callback-bus trampoline cost (1.27 ns, ~18%). Baseline numbers, Apple
M1-class machine, `-O3`, this repo's build flags (re-run these exact
benchmarks before starting — see "Performance gate" below):

| Case | ns/instruction |
|---|---|
| `BM_Z80EngineFlatBus` (ceiling: inline array, no callbacks, no register copy) | 4.42 |
| `BM_Z80EngineCallbackBus` (trampoline, no register copy) | 5.69 |
| `BM_Z80EngineCallbackBusWithRegisterSync` (today's integration shape) | 11.5 |
| `BM_Z80RealFrameCost` (real booted 48K emulator, turbo mode, mean/frame) | ~453 µs |

The goal of this work is to make the vendored `unreal-z80` engine operate
**directly on the host's `Z80Registers` memory** instead of its own private
copy, so `Z80::Step()` needs no per-instruction register marshalling at all.
Target: `BM_Z80EngineCallbackBus`-level cost (~5.69 ns/instruction) for the
integrated path, i.e. fully absorb the register-copy tax, and a measurable
improvement (not just "no worse") in `BM_Z80RealFrameCost`.

## Why this is actually feasible (verified, not assumed)

The register file inside the engine's private `Z80CPU` struct
(`core/src/3rdparty/unreal-z80/src/z80cpu-internal.h`) was compared
field-by-field against the host's `Z80Registers`
(`core/src/emulator/cpu/z80.h`) and found **byte-identical** in order and
type: `pc, sp, ir_(r_low/i), int_flags(r_hi/iff1/iff2/halted), bc, de, hl, af,
ix, iy, alt{bc,de,hl,af}, memptr, q, im, nmi_in_progress`, both
`#pragma pack(push,1)`. This is by design — the engine's header comment
states it "reproduces the register layout of the core's Z80Registers...
member-for-member". This is *not* a coincidence to rely on blindly, though:
it must become a build-time-enforced contract (see "Static layout contract"
below), not a documentation claim, before anything depends on it.

**`eipos` and `haltpos` are explicitly excluded from this correspondence.**
Both host and engine have same-named fields, but they are NOT semantically
equivalent today: `Z80::Step()` computes the host's `eipos`/`haltpos`
independently, host-side, from `Z80CpuIntPossible(engine)` and its own `t`
(see `core/src/emulator/cpu/z80.cpp` around lines 755-761, 797, 1211,
1217-1219) — it never reads the engine's internal `eipos`/`haltpos` at all,
and the existing bulk-copy struct `Z80CpuRegisters` does not include them
either. **Do not unify these as part of this change.** If the engine's
internal EI-shadow/HALT-position tracking were exposed through the same
shared memory, the host's independent computation and the engine's internal
one would both be writing/reading the same slots with different logic and
different timing — a real correctness hazard, not just a style question.
Keep `eipos`/`haltpos` exactly as they are today: private to each side,
never aliased.

## What to change, and where

### A. In `unreal-z80` (the upstream repository first, then re-vendor)

Do this in the standalone `unreal-z80` repository, verify it there with the
library's own test/benchmark suite, *then* re-vendor into
`core/src/3rdparty/unreal-z80/` — the same workflow already used for the six
patches recorded in [patches.md](patches.md). Do not develop this directly
inside the vendored copy.

1. **Extract the aliasable register block into its own type.** Pull the
   `#pragma pack(push,1) ... #pragma pack(pop)` register block out of
   `struct Z80CPU` into a standalone `struct Z80RegisterFile` (naming is the
   agent's call, but publish it in `z80cpu.h`, not `z80cpu-internal.h`, since
   hosts now need to know its layout). It must contain exactly the fields
   listed above (`pc` through `nmi_in_progress`) and nothing else —
   specifically **not** `eipos`, `haltpos`, or `t`.

2. **Make `Z80CPU` hold a pointer, not the block by value.**
   ```c
   struct Z80CPU {
       Z80RegisterFile* regs;       // points at ownedRegs unless attached
       Z80RegisterFile ownedRegs;   // default storage (standalone/demo/tests/benchmarks)
       // ... existing execution-state and bus fields, unchanged ...
   };
   ```
   `Z80CpuCreate()` sets `regs = &ownedRegs`.

3. **Add the attach/detach API**, mirroring the existing
   `Z80CpuAttachMemory`/`Z80CpuAttachPageTables` precedent in both shape and
   doc style:
   ```c
   // Host-owned register file: the engine reads/writes this memory directly,
   // no copy. Must point at a live Z80RegisterFile-shaped block that outlives
   // the attachment. Pass null to detach and fall back to owned storage
   // (current contents are copied back into ownedRegs on detach, so no state
   // is lost switching back).
   void Z80CpuAttachRegisterFile(Z80CPU* cpu, Z80RegisterFile* hostRegisterFile);
   ```

4. **Static layout contract, not a comment.** Add `static_assert`s (in
   `z80cpu.h` or a small dedicated header) that pin down `sizeof(Z80RegisterFile)`
   and the `offsetof` of every field to concrete numbers. This turns any
   future accidental layout drift — in either repository — into a compile
   error instead of silent corruption. This is the load-bearing safety net
   for the whole design; do not skip it.

5. **Mechanically rewrite every opcode body.** Every `cpu->FIELD` access
   where `FIELD` is one of the register-file members becomes
   `cpu->regs->FIELD`. This spans `opcodes-base.inc`, `opcodes-cb.inc`,
   `opcodes-dd.inc`, `opcodes-ed.inc`, `opcodes-fd.inc`, `opcodes-xxcb.inc`,
   `z80cpu-opcodes.inc`, `z80step.inc`, `z80cpu.cpp`, `z80daa.cpp` — on the
   order of 6000 lines. Do this as a scripted pass (the field names are a
   known, closed set, so a careful regex substitution is safe) followed by a
   full manual read-through of the diff, not by hand line-by-line. Fields
   that are *not* in `Z80RegisterFile` (`t`, `mem`, the callback pointers,
   `opword`/`opcode`/`prefix`, `outc0`, `halt_cycle`, `trashRegister`,
   `directRegisters`, `eipos`, `haltpos`) stay direct `cpu->` members,
   untouched.

6. **Keep the existing per-register and bulk-copy APIs working**
   (`Z80CpuGetReg`/`SetReg`, `Z80CpuGetRegisters`/`SetRegisters`) — they now
   read/write through `cpu->regs->...` instead of `cpu->...` directly,
   transparently to every existing caller (the demo, `z80test`/ZEX runners,
   the z80ex differential, and any host that doesn't attach). This must not
   change behavior for those callers at all.

### B. In the bridge (`core/src/emulator/cpu/z80.cpp`, `z80.h`)

After re-vendoring the updated library:

1. In the `Z80` constructor, right after `Z80CpuCreate()`, call
   `Z80CpuAttachRegisterFile(engine, reinterpret_cast<Z80RegisterFile*>(&this->pc))`
   (or however the agent chooses to express "the address of the host's
   register block, starting at `pc`" — `Z80Registers::tt` is the one leading
   field *not* part of the shared block; T-state stays managed exactly as
   today via `Z80CpuTstates`/`SetTstates`). Add the `static_assert` from
   step A.4 here too, checked against `Z80Registers`, so this project's build
   independently verifies the contract even if the library's own header
   checks were somehow bypassed.
2. **Delete the per-step `Z80CpuSetRegisters`/`Z80CpuGetRegisters` calls**
   from the engine-call seam inside `Z80Step()` entirely. After
   `Z80CpuStep()` returns, `this->pc`, `this->af`, etc. are already current
   — no follow-up read needed.
3. Leave `eipos`/`haltpos` handling completely untouched (lines ~755-761,
   797, 1211, 1217-1219 as they stand today).
4. Leave the decoded-instruction-field reconstruction (`prefix`/`opcode` via
   the memory-read trampoline, `Z80CpuInstructionPc`) untouched — it doesn't
   touch registers and is unaffected by this change.
5. Double check `Z80CpuAttachRegisterFile`'s effect on `Z80CpuReset()`: the
   engine must reset the *host's* memory (through `regs`), not some stale
   owned copy — verify this explicitly with a targeted test (reset while
   attached, then read host fields).

## Non-negotiable constraints

- **No regression, anywhere, full stop.** The complete `core-tests` suite
  must remain 100% green with the *exact* same pass/skip counts as the
  pre-change baseline (re-run and record the baseline the day you start —
  it was 3305 passed / 2 skipped as of this writing, but re-verify since
  other work lands on `master` independently). Any newly-failing test is a
  blocking regression. Do not skip, weaken, or delete a test to make a
  number look better — if a test's expectation is genuinely wrong given
  provably more-correct engine behavior, that is a decision for the user,
  exactly as the MEMPTR/FUSE-vector case was handled earlier in this same
  project (see [patches.md](patches.md), "Deliberately not changed" →
  now resolved in `testdata/z80/fuse/tests.expected`).
- **No performance loss, anywhere, measured, not assumed.** This is the
  explicit point of the whole exercise — a change that removes the register
  copy but introduces a new cost elsewhere (e.g. an extra indirection that
  the compiler fails to keep in a register across the whole `Step()` call,
  or a detach/re-attach happening more often than expected) is a failed
  attempt, not a partial win. See "Performance gate" below for the exact
  bar.
- **Public `Z80` API and struct layout unchanged.** Same constraint as the
  original vendoring — ~260 dependents across `core/`, `core/tests/`,
  `core/automation/`, `unreal-qt/`, `tools/` reference `Z80`/`Z80State`
  directly, including by raw field access (TTD's
  `CaptureCpuState`/`RestoreCpuState`). Nothing about `Z80Registers`'s field
  names, order, or size may change.
- **Zero compiler warnings**, in both the standalone `unreal-z80` repository
  and this project (`-Wall -Wextra -Werror`).
- **Upstream-contributable.** Every library-side change must be
  general-purpose (useful to any host of the library), with no
  unreal-ng-specific behavior baked into `unreal-z80` itself — the same bar
  the original six patches were held to, and the reason they were
  successfully upstreamed into library 0.2.0.
- **Do not commit anything.** Report results and stop; commit only on
  explicit instruction, per this repo's `AGENTS.md`/`CLAUDE.md`.

## Explicitly out of scope (do not do these as part of this change)

- Unifying `eipos`/`haltpos` between host and engine. If the agent believes
  this is also worth doing, write it up as a separate proposal — do not fold
  it into this change.
- Any change to INT/NMI acceptance (stays host-side, as today, per
  [patches.md](patches.md) "How the host uses this").
- Any change to the callback-bus trampolines (`Z80::rd/wd/in/out`,
  `UlaContention` wiring) — this change is about registers only.
- Chasing the remaining ~1.27 ns/instruction trampoline cost (that would be
  a much larger, separate redesign — templated/inlined bus access — with a
  far worse cost/benefit ratio than this change; not part of this TDD).

## How to control quality

### Correctness gate

1. **Standalone, in the `unreal-z80` repository, before touching this
   project at all:** the library's own full suite — unit tests, `z80test`,
   ZEX, the T-trace differential against the existing golden file, the
   z80ex lockstep differential, demo parity. Ideally diff the T-trace output
   byte-for-byte against the pre-change engine on a representative run, to
   prove zero behavioral change, not just "tests still pass."
2. **Integrated, in this project, after re-vendoring:** full `core-tests`,
   with explicit pass/fail reporting (not just a final tally) for:
   - `FusePhase_Test.AllOpcodes` (must stay 1356/1356)
   - `core/tests/z80/z80test/z80test_runner.cpp`, `opcode_test.cpp`,
     `z80_test.cpp`, `z80_logic_jump_test.cpp`
   - `core/tests/emulator/cpu/{bus_phase_test,io_phase_test,int_test,nmi_test,opcode_profiler_test}.cpp`
   - `core/tests/emulator/video/{contention_test,int_timing_test,io_contention_test,memory_contention_test,pentagon_border_timing_test,scorpionraster_test}.cpp`
   - `core/tests/debugger/ttd/*` (all files — TTD checkpoint/restore is the
     most sensitive consumer of register-state semantics in the whole
     codebase)
   - `core/tests/debugger/breakpoints_test.cpp`
   - Add a new, targeted test for the reset-while-attached case from step
     B.5 if one doesn't already exist that would catch it.

### Performance gate (explicit numbers, not vibes)

Re-run `./cmake-build-agent-release/bin/core-benchmarks --benchmark_filter="Z80Engine.*|Z80RealFrame.*"` before making any change (record the baseline fresh — numbers drift machine to machine) and again after, same machine, same build flags, same `--benchmark_repetitions`. Report a before/after table. Required bars:

- The new zero-copy integrated path's per-instruction cost (extend
  `z80_engine_bus_benchmark.cpp` with a new case exercising the attached
  path, e.g. `BM_Z80EngineAttachedRegisterFile`, alongside the existing
  four) must be **≤** the current `BM_Z80EngineCallbackBus` baseline
  (5.69 ns/instruction on the reference machine) — i.e. it must fully
  absorb the register-copy tax, not merely shrink it.
- `BM_Z80RealFrameCost` must be **≤** the current baseline (~453 µs/frame on
  the reference machine), ideally visibly lower.
- Run the *entire* `core-benchmarks` suite before/after (not just the Z80
  ones) and diff every benchmark that touches CPU execution
  (`turbo_frame_benchmark.cpp`, any TTD frame-overhead/capture benchmarks
  that drive real emulation) — flag any regression, however small, for a
  decision rather than silently accepting it.

### Report format

A single before/after report: the numeric table above, full test
pass/fail/skip counts (old vs new), the list of files changed in both
repositories, and any open questions or judgment calls the agent had to
make (e.g. exact naming of `Z80RegisterFile`/`Z80CpuAttachRegisterFile`).
No summary that omits a number that didn't improve — if something is flat
or worse, say so plainly and stop for a decision rather than proceeding.

## Results (2026-09-24)

Implementation matched this design closely, and in one respect exceeded it:
`reservedEipos`/`reservedHaltpos` were kept as contiguous reserved slots in
the published `Z80CpuRegisterFile` (never read/written by the engine, but
present so the struct overlays the *whole* tail of `Z80Registers` from `pc`
onward in one piece) rather than excluded from the struct entirely - cleaner
for the host than the split-pointer approach this document originally
sketched. Independently re-verified (not just trusted from the upstream
report) before integrating:

- Library's own suite, rebuilt and rerun fresh: 27/27 unit tests (including
  two new ones, `RegisterFileAttach` and `RegisterFileRefreshBit7`, covering
  exactly the risk areas this document flagged: reset-while-attached,
  detach-copies-state-back, DDCB direct-register repointing after attach,
  reserved slots never touched, R-register bit-7 stability, switching
  between two attached blocks), z80test 6/6 (doc/docflags/flags/full/ccf/
  memptr), ZEXALL-family 4/4 (zexdoc/zexall/zexbit/zexfix), the dedicated
  IMx/NMI/HALT/contention harness 24/24 - all zero-warning builds.
- Layout: independently re-derived the field-by-field byte offsets by hand
  and confirmed `sizeof(Z80CpuRegisterFile) == 41` bytes matches exactly the
  span of `Z80Registers` from `pc` through `nmi_in_progress`, before trusting
  the library's own `static_assert`s.

Integration (`core/src/emulator/cpu/z80.cpp`, `z80.h`):
- Added a matching `static_assert` layout contract on this project's side
  (offsets of every `Z80Registers` field checked against
  `Z80CpuRegisterFile`), independent of the library's own checks.
- One `Z80CpuAttachRegisterFile(_engine, reinterpret_cast<Z80CpuRegisterFile*>(&pc))`
  call added to the constructor.
- `ExecuteEngineInstruction()`'s ~80-line Load/Store region (bulk
  `Z80CpuSetRegisters`/`GetRegisters`, the dirty-check comparison, the
  `_engineWroteR` R-register merge logic) deleted entirely. What's left:
  capture `wasHalted` and clear `halted` before the step (unchanged HALT
  re-execution model, now a plain field write since `halted` is shared
  memory), call `Z80CpuStep`, then derive `haltpos`/`eipos` (the two fields
  deliberately excluded from engine ownership, exactly as planned) from the
  now-current shared state. `outc0`/`t` sync unchanged (never part of the
  register file).
- Removed now-dead bridge fields: `_engineRegsStored`, `_engineRegsValid`,
  `_engineWroteR`, `_engineLastM1Byte` (the last one only became dead as a
  side effect of removing the R-register merge that was its sole reader).
- Confirmed no other file in the codebase referenced the removed
  `Z80CpuRegisters`/`Z80CpuGetRegisters`/`Z80CpuSetRegisters` API.

Correctness gate: full `core-tests`, **exact same pass/skip counts as the
pre-change baseline** - 3305 passed / 3307 run / 2 skipped
(`TsfmRenderDiag.RenderMoebiusOpening`, `TsfmVolumeReplay_Test.ReplayTsfmVolumeTune`,
both pre-existing and unrelated), zero build warnings. `FusePhase_Test.AllOpcodes`
1356/1356. Zero regressions.

Performance gate — before/after, same machine, `--benchmark_repetitions=5`:

| Benchmark | Before | After | Δ |
|---|---|---|---|
| `BM_Z80EngineFlatBus` (ceiling, unaffected) | 4.42 ns | 4.42 ns | 0% |
| `BM_Z80EngineCallbackBus` (no register API at all) | 5.69 ns | 5.92 ns | **+4%** |
| `BM_Z80EngineCallbackBusAttached` (what `z80.cpp` uses now) | — | 5.93 ns | — |
| `BM_Z80EngineCallbackBusWithRegisterSync` (old bulk-copy shape, kept only as a reference point) | 11.5 ns | 14.1 ns | +23% (unused path, not a regression in anything we ship) |
| `BM_Z80RealFrameCost` (real booted 48K, turbo mode — **the number that matters**) | 453 µs | 387 µs | **-14.6%** |

Two things worth being direct about, per this document's own "no summary
that omits a number that didn't improve" rule:

1. **The microbenchmark's literal numeric bar ("≤ 5.69 ns/instruction") is
   not met** - `BM_Z80EngineCallbackBusAttached` measures 5.93 ns, about 4%
   over that specific old number. This is because library 0.3.0 introduces a
   small universal cost (one dependent load per instruction to fetch the
   register-file pointer, reloaded after every host callback) that applies
   to *every* caller, attached or not - confirmed by `BM_Z80EngineCallbackBus`
   itself (no register API touched at all) moving from 5.69 to 5.92 ns. This
   was disclosed upfront by whoever did the library work, not discovered
   after the fact, and a build-time option to compile the indirection out
   for hosts that never attach was offered but not implemented (not needed
   here, since this project always attaches).
2. **The number that actually matters improved substantially.**
   `BM_Z80RealFrameCost` - the real, booted-ROM, full-instruction-mix,
   turbo-mode frame cost - dropped from 453 µs to 387 µs, a 14.6% improvement
   (roughly 1.17x faster), consistent with an independent cross-check
   (`turbo_frame_benchmark.cpp`'s `BM_FrameCostTurbo`, 390 µs on the same
   run). The old integration's bulk-copy tax (previously ~1.3-1.4x slower
   than the pre-vendoring interpreter) is now largely gone in practice; the
   microbenchmark-only ~4% floor is real but small next to that win.

No open naming/judgment-call questions - `Z80RegisterFile` was named
`Z80CpuRegisterFile` (consistent with the library's existing `Z80Cpu*`
naming) and `Z80CpuAttachRegisterFile` matches exactly what this document
proposed.

Files changed, this repository: `core/src/3rdparty/unreal-z80/` re-vendored
in full (library 0.3.0, currently sourced from an uncommitted upstream
working tree - see [version.txt](version.txt)); `core/src/emulator/cpu/z80.cpp`,
`z80.h`; `core/benchmarks/emulator/cpu/z80_engine_bus_benchmark.cpp` (new
`BM_Z80EngineCallbackBusAttached` case). Nothing committed.
