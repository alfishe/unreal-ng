# TODO — TTD v1 → v2 migration

Status: **Phase 0 done, Phase 1 next.** v2 is a new engine, `ttd::TimeTravelEngine`, built next to v1 and checked against it ([engine-approach-and-naming.md](engine-approach-and-naming.md)); decisions D1–D33 in [engine-decisions.md](engine-decisions.md). Roadmap: [README.md](README.md); where the earlier steps went: [README §5](README.md#5-former-step-names).

After every phase: the quality bar of D33 on the whole benchmark matrix (identical restores; file size, memory and capture work not larger than v1's in any case, for the same history kept; seek time within PR-5).

Still open for the user: the default memory budget (Phase 4) and the integrity and versioning mechanism ([integrity-and-versioning.md](integrity-and-versioning.md), Phase 4, Step 1). Neither blocks Phase 1.

## Phase 0 — Preparation (done)

- [x] Step 0 of the merge strategy: `PeripheralId` table and notification enum on master
- [x] Step 1 — Make v1 honest (done 2026-09-28)
- [x] Step 2 — Benchmark harness (done 2026-09-29, [results](v0b-benchmark-results.md))
- [x] Step 3 — Merge the feature branches
- [x] ~~Step 4 — Checkpoints inside a frame~~ (dropped 2026-09-29)
- [x] Since: experiments E1–E6 ([POC 011](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/README.md)); v1 stores compressed data at its exact size (`67e5aff28`)

## Phase 1 — Engine core: memory that costs only what changes

Design: [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md).

- [x] Step 1 — Engine skeleton and verification: `TimeTravelEngine`, machine time, frame table, positions with a branch, optional streams; v1 file reader (`bench/ttdv1feeder`); the oracle against v1 (`TTDV1Feeder_Test`: every corpus checkpoint identical); both engines in the benchmark (`UNREAL_TTD_BENCH_ENGINE=all`) — branch `ttd-engine`, 2026-10-02
- [x] Step 2 — Piece store: change stored once, encoded once (T = 128 B), chain limit per piece (K = 50), arena with exact sizes, dependencies, shareable by sessions — RAM payload already below v1 on every `ci` case (up to −28%)
- [x] Step 3 — Regions and the reference table: per-checkpoint change records (8 B per changed piece) + a full copy-on-write table every 64 checkpoints; parent link — references below v1 on every measured case (ZX-Evo 4,103 → 78 B per frame, Pentagon game 128 → 62); copy-on-write blocks alone lost to v1 on small busy machines (TDD §4.4)
- [ ] Step 4 — Live capture next to v1, delta base for changed pieces only
- [ ] Step 5 — Restore only the pieces that differ
- [ ] Step 6 — Device memory as regions, large memories first: NeoGS RAM and flash, MoonSound wave memory, General Sound RAM and upload store, Sprinter video RAM, VDAC2 graphics memory; then the ZX-Evo AVR and Scorpion SMUC EEPROMs
- [ ] Phase check: D33 on the matrix, bytes per stream against the E6 model

## Phase 2 — Device state with versions

Design: [phase-2-device-state-tdd.md](phase-2-device-state-tdd.md).

- [ ] Step 1 — Device registry: type id u16 + instance name, layout version, restore order, firmware fingerprint
- [ ] Step 2 — Unchanged state shared, changed fields only, time-derived counters
- [ ] Step 3 — Degraded restores reported on every surface
- [ ] Step 4 — Sound devices on the contract; device-set changes as timeline events

## Phase 3 — Everything a replay needs

Design: [phase-3-replay-inputs-tdd.md](phase-3-replay-inputs-tdd.md).

- [ ] Step 1 — One event stream (input, external events, markers, port reads, bus data, DMA, network)
- [ ] Step 2 — Replay modes: input events, `IN` values (RZX)
- [ ] Step 3 — Several CPUs: own cycle counters, clock-change events, positions on any CPU
- [ ] Step 4 — Configuration fingerprint and media versions
- [ ] Step 5 — Emulated real-time clocks
- [ ] Step 6 — No writes outside the session during replay
- [ ] Step 7 — Write journal as a derived index with a retention policy (experiment E7 first)

## Phase 4 — The session file

Design: [phase-4-session-file-tdd.md](phase-4-session-file-tdd.md).

- [ ] Step 1 — Integrity and versioning decision
- [ ] Step 2 — Written as it records: append-only, background writer, crash-safe
- [ ] Step 3 — Memory as a cache: budget, eviction with rebasing, read-back on seek, accounting
- [ ] Step 4 — Optional frame-boundary streams in the file (screenshot first)
- [ ] Step 5 — v1 files read into the engine's format
- [ ] Step 6 — `ttd.ksy` and the Python analyzer

## Phase 5 — Switch the emulator to the engine

Design: [phase-5-switchover-tdd.md](phase-5-switchover-tdd.md).

- [ ] Step 1 — The emulator and every surface on the engine; clean stop on TTD / debug mode off
- [ ] Step 2 — History never cut short: branches on resume and edit in the past, seek while recording, loads as events
- [ ] Step 3 — Black-box setting in unreal-qt (off for automation); session file location in the UI, default `scratch/ttd/`
- [ ] Step 4 — v1 only in the verification tools

## Phase 6 — Cleanup

- [ ] Delete `TimeTravelManager` (keep the v1 file reader for verification), retire the proof-of-concept readers, TDD truth pass, move this folder to DONE

## Later

- [ ] Branch operations in the UI: PLAN #76 ([design](../2026-09-29-model-what-if/design.md))
- [ ] Groups of machines (ZX-Poly) over a shared piece store (D22)
