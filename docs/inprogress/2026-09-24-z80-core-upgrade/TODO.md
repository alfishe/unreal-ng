# TODO — Z80 core upgrade

## Done
- `unreal-z80` vendored into `core/src/3rdparty/unreal-z80/` (0.2.0, MIT, no
  local patch), `Z80::Step()`'s opcode-execution seam delegates to it, public
  `Z80` API/struct layout unchanged.
- `testdata/z80/fuse/tests.expected` corrected for the four
  INIR/INDR/OTIR/OTDR repeat-MEMPTR cases (silicon-verified value); documented
  in `core/tests/emulator/cpu/fuse_phase_test.cpp`.
- Full verification gate green: zero build warnings, full `core-tests` suite
  passing. **Committed** (`d4ee4539`) and merged with two subsequent upstream
  syncs (`20efff9c`, latest merge pulling `9918884b`'s `core-benchmarks` fix
  among others); pushed to `github` remote. `origin` (internal server) was
  unreachable from this environment — still needs a push from a machine that
  can reach it.
- Added `core/benchmarks/emulator/cpu/z80_engine_bus_benchmark.cpp`,
  decomposing the integration's ~1.3-1.4x CPU-bound slowdown: the bulk
  register-file copy around every `Z80Step()` is ~82% of the overhead
  (5.81 ns/instruction), the callback-bus trampoline only ~18% (1.27 ns).
  Currently uncommitted.
- Wrote [register-zero-copy-tdd.md](register-zero-copy-tdd.md), then
  **implemented and verified it** (2026-09-24): re-vendored `unreal-z80`
  0.3.0 (zero-copy `Z80CpuAttachRegisterFile`, independently re-verified
  against its own suite — 27/27 unit tests incl. two new register-file
  tests, z80test 6/6, ZEXALL-family 4/4, IMx/NMI/HALT/contention harness
  24/24), rewired `z80.cpp`/`z80.h` to attach once instead of copying every
  step, added a matching layout `static_assert` contract on this side.
  Result: full `core-tests` **exact same pass/skip counts as baseline**
  (3305/3307, 2 pre-existing skips) — zero regressions — and
  `BM_Z80RealFrameCost` (the number that matters) improved **453 µs → 387 µs
  (-14.6%)**. One honest caveat: the microbenchmark-only
  `BM_Z80EngineCallbackBusAttached` is 5.93 ns vs the old
  `BM_Z80EngineCallbackBus` 5.69 ns baseline (+4%) — a small universal cost
  the library now pays for exposing the attach capability at all, even for
  callers that don't attach; disclosed upfront, not hidden. Full numbers and
  analysis in register-zero-copy-tdd.md's "Results" section. Currently
  uncommitted.

## Remaining
- **Review and commit** the re-vendored `unreal-z80` 0.3.0, the `z80.cpp`/
  `z80.h` rewrite, and the updated benchmark. Nothing committed yet — the
  library source in the upstream repo is itself still an uncommitted working
  tree there too (see [version.txt](version.txt)); re-pin once upstream
  commits it.
- Push to `origin` from a machine that can reach `172.16.21.25` (also still
  pending from the prior commit `d4ee4539` / merge `20efff9c`).
- Optional cleanup: the old interpreter files (`op_*.cpp`, `cpulogic`,
  `cputables`, `daa_tabs`) are still compiled but no longer called from
  `Z80Step()`. Deleting them is a separate, deliberate follow-up, not bundled
  into this change.

This folder stays `TODO.md` until the above is committed and `origin` is
pushed; flip to `DONE.md` at that point.
