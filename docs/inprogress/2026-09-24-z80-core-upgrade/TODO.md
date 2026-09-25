# TODO — Z80 core upgrade

## Done
- `unreal-z80` vendored into `core/src/3rdparty/unreal-z80/` (0.2.0 initially,
  then 0.3.0, MIT), `Z80::Step()`'s opcode-execution seam delegates to it,
  public `Z80` API/struct layout unchanged.
- `testdata/z80/fuse/tests.expected` corrected for the four
  INIR/INDR/OTIR/OTDR repeat-MEMPTR cases (silicon-verified value); documented
  in `core/tests/emulator/cpu/fuse_phase_test.cpp`.
- [register-zero-copy-tdd.md](register-zero-copy-tdd.md): re-vendored
  `unreal-z80` 0.3.0 (zero-copy `Z80CpuAttachRegisterFile`, independently
  re-verified against its own suite), rewired `z80.cpp`/`z80.h` to attach
  once instead of copying registers every step. `BM_Z80RealFrameCost`
  453 µs → 387 µs (-14.6%). **Committed** `a608154f`, merged with upstream
  (`5755803b`), pushed to `github/master-z80lib` at `85f061c4`.
- [performance-investigation.md](performance-investigation.md): profiled the
  real integrated frame cost (macOS `sample`, `xctrace` was blocked by a
  permissions dialog in this headless environment), found LTO was never
  enabled anywhere in this project. Added opt-in `ENABLE_LTO` CMake option
  scoped to `core`/`core-tests`/`core-benchmarks`. `BM_Z80RealFrameCost`
  387 µs → 342 µs (-11.6%), zero regressions. Cumulative: **1.43x slower
  than the original interpreter, down from 1.90x at the non-zero-copy
  starting point.** Currently uncommitted.

## Remaining
- Push to `origin` from a machine that can reach `172.16.21.25` — still
  pending since the first commit (`d4ee4539`); every subsequent commit
  needs the same push once reachable.
- **`MemIf` double-indirection** (documented in
  [performance-investigation.md](performance-investigation.md) "Finding 2",
  not yet fixed): `Memory::MemoryReadFast`/`MemoryReadDebug` are `virtual`
  (needed for `ScorpionMemory`'s override), so the member-function-pointer
  dispatch through `MemIf` pays a member-pointer call *and* a vtable lookup
  on every memory access, for every model — even ones that never need the
  override. A devirtualized fast path for the common (non-Scorpion) case is
  the next concrete optimization target; real correctness risk (must not
  break Scorpion or the existing Fast/Debug interface selection), needs
  careful scoping.
- Re-profile with LTO active to see the new self-time shape (the picture in
  `performance-investigation.md` "Finding 1" predates the LTO fix) before
  deciding what to attack next.
- Minor, not yet acted on: `busTraceHook` unconditionally checked on every
  memory access in production (could gate behind a cached bool, matching
  the existing `_feature_calltrace_enabled` pattern);
  `DebugManager::GetAnalyzerManager()` per-instruction lookup cost even with
  no analyzers active.
- Optional cleanup: the old interpreter files (`op_*.cpp`, `cpulogic`,
  `cputables`, `daa_tabs`) are still compiled but no longer called from
  `Z80Step()`. Deleting them is a separate, deliberate follow-up, not bundled
  into this change.

This folder stays `TODO.md` until the performance work is either concluded
or explicitly deferred, and `origin` is pushed; flip to `DONE.md` at that
point.
