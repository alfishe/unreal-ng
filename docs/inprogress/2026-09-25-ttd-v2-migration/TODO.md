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
- [x] Step 4 — Live capture next to v1 (`TimeTravelManager::SetShadowEngine`), delta base for changed pieces only — capture p50 v1 → engine: ZX-Evo 355 → 18.5 µs, Pentagon game 426 → 64 µs; counted work below v1 on every measured case; `TimeTravelManager_Shadow_Test` checks every live frame against v1
- [x] Step 5 — Restore only the pieces that differ (`TimeTravelEngine::RestoreToMemory`) — memory restore p50 v1 → engine: ZX-Evo 731 → 125 µs, Pentagon 1024 174 → 27; Pentagon game 311 → 337 µs (+8%, hot pieces with long chains; p99 568 µs, within PR-5)
- [ ] Step 6 — Device memory as regions, large memories first (`ITTDRegionSource`, `TTDRegionTracker`; registered next to the device's serializer, fed by shadow mode):
  - [x] NeoGS RAM (2–4 MB) and flash (512 KB): marks in `NeoGSMemory::write` / `poke` / `powerOn` and in the flash chip's program / erase / load; A/B `BM_HostFrame_NeoGS_*` within noise (−0.6…+0.7%)
  - [x] MoonSound wave RAM (up to 1 MiB): taken from the wave memory's own dirty bitmap before each capture, no new hook
  - [x] General Sound RAM (128–512 KB): marks in `writeMem` and on a state load; the engine's GS blob holds the 95-byte registers only (`TTDStateWithoutRegions`), the v1 feeder splits v1's GS blob the same way. GS 512 upload, bytes per frame (memory pieces + device state): v1 2,304 → engine 1,624; capture p50 293 → 20 µs
  - [ ] The lightweight GS player's upload store
  - [x] Machines with large RAM — region 0 on every model, no per-model code: `TimeTravelManager_ShadowModels_Test` checks every frame against v1 on Pentagon, Scorpion, ProfScorpion, Profi, ATM710, ATM450, ZX-Evo, TS-Conf and Sprinter. Matrix (600 frames, v1 → engine): capture p50 TS-Conf 223 → 6.3 µs, ZX-Evo 273 → 16.6, ATM710 112 → 7.5, Profi 74 → 4.9; memory restore p50 TS-Conf 585 → 32 µs, ZX-Evo 728 → 124; references TS-Conf / ZX-Evo 4,103 → 87 / 111 B per frame
  - [x] Region records per checkpoint only for regions that changed (an idle card costs nothing per frame; references below v1 on every model, 48K 96 → 60 B per frame)
  - [ ] Sprinter video RAM and fast RAM, VDAC2 graphics memory (restored through the device)
  - [ ] ZX-Evo AVR and Scorpion SMUC EEPROMs
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
