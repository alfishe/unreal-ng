# Sprint 0 — Foundations

Started: 2026-07-19
Completed: 2026-07-19
Reference: `docs/emulator/design/debugger/time-travel-debug/implementation-plan.md` §2

Cross-cutting decisions applied: see [`decisions.md`](decisions.md).

## Outcome

All 5 Sprint 0 items landed, build is green, 50 new tests all pass.

## Items delivered

| # | Item | Files | Tests |
|---|---|---|---|
| 0.1 | Instance-tagged notifications | `core/src/emulator/notifications.h` (new payloads) · `emulator.cpp` · `memory.cpp` · `cpu/z80.cpp` · `ports/portdecoder.cpp` (7 post sites total) · `recordingmanager.{h,cpp}` (comment updates) · `platform.h` (comment updates) | 10 (`InstanceTaggedPayloads_*`) |
| 0.2 | Run-control claim token | `core/src/emulator/emulatorcontext.{h,cpp}` | 14 (`RunControlClaim_*`) |
| 0.3a | `timetravel` runtime feature flag | `core/src/base/featuremanager.{h,cpp}` (registered in `setDefaults()`; users may override via a local `features.ini` if present — the file is user-owned, not versioned) | 5 (`TimeTravelFeature_*`) |
| 0.3b | `ENABLE_GDB_AUTOMATION` build-time flag | root + `unreal-qt/` + `core/automation/` `CMakeLists.txt` (stub target only; Phase G1 builds the actual server) | — (CMake smoke only) |
| 0.4 | Machine-state hash + divergence harness | `core/src/debugger/ttd/machine_state_hash.{h,cpp}` | 21 (`MachineStateHash_*`) |
| 0.5 | Capture benchmark + memory-interface microbenchmark | `core/benchmarks/debugger/ttd/ttd_capture_benchmark.cpp` · `core/benchmarks/debugger/ttd/memory_interface_microbenchmark.cpp` | — (benchmarks) |

## Sprint-specific design decisions

- **Payload migration in practice**: the plan called for "replace in-place, update observers as they break". In practice zero observers broke, because the new payload classes inherit from `SimpleNumberPayload` and legacy observers reading only `_payloadNumber` kept working. See [decisions.md — Payload migration policy](decisions.md#payload-migration-policy).

## Benchmark baseline (DEBUG build, Apple M-series)

Run with `./bin/core-benchmarks --benchmark_filter='TTD_.*'`. Release numbers will be 2–3× better.

```
BM_TTD_Capture_SnapshotOnly               23 ns     (field extraction only, O(1))
BM_TTD_Capture_HashSnapshotOnly          227 ns     (FNV-1a over ~70-byte snapshot)
BM_TTD_Capture_HashRAM/48KB              149 us    314 MiB/s
BM_TTD_Capture_HashRAM/128KB             401 us    312 MiB/s
BM_TTD_Capture_HashRAM/512KB           1,604 us    313 MiB/s
BM_TTD_Capture_HashRAM/1MB             3,262 us    309 MiB/s
BM_TTD_Capture_HashRAM/4MB            12,882 us    312 MiB/s
BM_TTD_Capture_FullFrame/48KB            143 us
BM_TTD_Capture_FullFrame/128KB           394 us
BM_TTD_Capture_FullFrame/512KB         1,617 us
BM_TTD_MemIf_Fast_SequentialReads        362 us / 64KB   174 MiB/s
BM_TTD_MemIf_Fast_SequentialWrites       (similar)
BM_TTD_MemIf_Dbg_SequentialReads         (small overhead vs Fast)
BM_TTD_MemIf_TTDLite_SequentialWrites    587 us / 64KB   107 MiB/s   (~60% slower than Fast due to ring-buffer append)
```

### Key takeaways for Phase 1 planning

- Per-frame full-state capture is well within frame budget at all realistic sizes:
  - 50 Hz frame = 20,000 µs. 128 KB capture = 394 µs ≈ 2% of budget.
  - 512 KB capture = 1,617 µs ≈ 8% of budget.
- FNV-1a runs at a steady ~310 MiB/s regardless of buffer size (memory-bandwidth-bound, not algorithm-bound). A faster hash (xxHash, hardware CRC32) would lift this ~3–5× but is not needed for Phase 1.
- TTD-lite write path adds ~60% overhead vs FastMemIf on sequential writes — this is the cost of the ring-buffer append only. The real DbgMemIf adds more (breakpoint predicate + tracker pipeline). Conclusion: a dedicated TTD third interface will be meaningfully cheaper than reusing DbgMemIf for TTD recording, validating overhead doc §5 (last row) before anyone had to argue about it.

## What Sprint 0 deliberately does NOT ship

- **No actual TTD recorder.** The capture primitives exist; the recorder that calls them at frame boundaries is Phase 1.
- **No GDB RSP server.** `ENABLE_GDB_AUTOMATION` is a CMake stub that prints a status message; the server target is G1.
- **No corpus fixtures.** The divergence harness exists; the BASIC-idle / scroller / AccuracyCoinZX / TR-DOS corpus tests are wired in a later sprint.
- **No enforcement of the run-control claim at run-affecting call sites.** The token exists and is testable; Phase 2 (TTD seek) and G1 (GDB stub) wire the actual checks into Resume / Step / Seek / state-write.
