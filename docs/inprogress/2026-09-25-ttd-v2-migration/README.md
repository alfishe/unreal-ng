# TTD v1 → v2 migration

This folder plans the move of the time-travel debugger (TTD) from the engine on master today (**v1**) to the next one (**v2**). This page is the roadmap: the phases in order, and for each phase what it does, why, and how we know it is complete. The details sit one level down, in the [step notes](migration-trajectory.md) and in each phase's technical design (TDD). Progress is tracked in [TODO.md](TODO.md).

## 1. Roadmap

v2 is a new engine, `ttd::TimeTravelEngine`, built next to v1 and checked against it byte for byte ([engine-approach-and-naming.md](engine-approach-and-naming.md)). Users keep v1 until Phase 5 switches the emulator to the engine. What the engine supports from its first commit, and the conflicts settled before coding, are in [engine-decisions.md](engine-decisions.md) (decisions 1–38, cited below as D1…D38). Every item of state time travel records, rebuilds or leaves out, with its class (required, derived, telemetry, host-facing) and the known gaps: [state-registry.md](state-registry.md). Phase 1 results against the quality bar: [phase-1-results.md](phase-1-results.md). Phase 2 results: [phase-2-results.md](phase-2-results.md).

| Phase | Name | Status | Size | Design |
|---|---|---|---|---|
| [Phase 0](#phase-0--preparation) | Preparation | **done** (2026-09-29) | — | [step notes](migration-trajectory.md#phase-0--preparation) |
| [Phase 1](#phase-1--engine-core-memory-that-costs-only-what-changes) | Engine core: memory that costs only what changes | next | L | [TDD](phase-1-memory-regions-tdd.md) |
| [Phase 2](#phase-2--device-state-with-versions) | Device state with versions | planned | M | [TDD](phase-2-device-state-tdd.md) |
| [Phase 3](#phase-3--everything-a-replay-needs) | Everything a replay needs | planned | M | [TDD](phase-3-replay-inputs-tdd.md) |
| [Phase 4](#phase-4--the-session-file) | The session file: written as it records | planned | L | [TDD](phase-4-session-file-tdd.md) |
| [Phase 5](#phase-5--switch-the-emulator-to-the-engine) | Switch the emulator to the engine | planned | M | [TDD](phase-5-switchover-tdd.md) |
| [Phase 6](#phase-6--cleanup) | Cleanup | planned | S | — |

Size: S = a focused day or two of agent work, M = several days, L = a week or more.

Phases are done in order. A step inside a phase is referred to as "Phase 1, Step 2 — Piece store".

**How v2 is built** (decided 2026-10-02): as a new engine, `ttd::TimeTravelEngine`, next to v1's `ttd::TimeTravelManager`. v1 runs the emulator until the engine matches it on every check, then it is deleted. Names carry no version, so nothing is renamed afterwards. Rationale and the naming table: [engine-approach-and-naming.md](engine-approach-and-naming.md). What it supports from the start, and the conflicts settled before coding: [engine-decisions.md](engine-decisions.md).

**After every phase** the engine is compared with v1 on the whole benchmark matrix and must hold the quality bar of D33: identical restores; file size, memory and counted capture work not larger than v1's in any case, for the same history kept; seek time within PR-5.

## 2. Phases

### Phase 0 — Preparation

**Status: done.** Make today's engine correct and measurable before building the next one.

| Step | Name | Result |
|---|---|---|
| Step 1 | Make v1 honest | Done 2026-09-28: the RAM page 255 capture gap, suspected bugs B1–B10 fixed with tests, the state-completeness test, the feature flag no longer disables fast loading |
| Step 2 | Benchmark harness | Done 2026-09-29: the benchmark matrix with v1 as the first engine, a CI gate, stored baselines ([results](v0b-benchmark-results.md)) |
| Step 3 | Merge the feature branches | Done: `profi`, `generalsound`, `moonsound` are on master ([merge notes](branch-merge-strategy.md)) |
| Step 4 | Checkpoints inside a frame | **Dropped**: Step 2 measured seek p99 ≤ 3.8 ms on every configuration (limit 5 ms), so it is not needed |

Done since, outside the plan: experiments E1–E6 on real recordings ([POC 011 experiments](../../../tools/poc/011-ttd-v2-capture-analysis/experiments/README.md)); v1 stores compressed data at its exact size (`67e5aff28`, 15–68% less recording memory).

### Phase 1 — Engine core: memory that costs only what changes

**What.**

| Step | Name |
|---|---|
| Step 1 | Engine skeleton and verification: `TimeTravelEngine`, machine time and the frame table, positions with a branch, optional streams; a v1 file reader feeds the engine frame by frame; the oracle compares every restored frame with v1; the benchmark runs both engines |
| Step 2 | Piece store: 4 KB pieces, each change stored once, encoded once (T = 128 B), a chain length limit per piece (K = 50), exact-size allocation through an arena, dependencies recorded, shareable by several sessions |
| Step 3 | Regions and the copy-on-write reference table: machine RAM is region 0; blocks of 8 pages, two levels; every checkpoint links its parent |
| Step 4 | Live capture next to v1: the engine records the running emulator in parallel with v1, with the delta base kept for changed pieces only |
| Step 5 | Restore only the pieces that differ |
| Step 6 | Device memory as regions, large memories first: NeoGS RAM and flash (4.5 MB), MoonSound wave memory (1 MiB), General Sound RAM and upload store, Sprinter video RAM, VDAC2 graphics memory (restored through the device); then the ZX-Evo AVR and Scorpion SMUC EEPROMs |

**Why.** Measured on v1 (Phase 0, Step 2; E1–E6):
- capture cost follows installed RAM, not change: 190 µs per frame on ZX-Evo with nothing written, against 7–60 µs on 128K-class machines;
- every 50th frame stores all RAM again, the page reference table costs 4 KB per frame on a 4 MB machine, and every seek decodes all memory (765–785 µs on ZX-Evo);
- device memory is copied whole into every checkpoint (General Sound) or not captured at all (NeoGS, MoonSound wave memory, both EEPROMs);
- modeled on real sessions (E6), this design keeps a recording in 3–16× less memory than v1 (before v1's exact-size fix).

**Done when.** FR-5, FR-22, PR-3, PR-4, the memory part of PR-9 / PR-10, and D33:
- every frame of the fixture corpus and of the real-use sessions restores from the engine byte for byte as from v1;
- the engine's bytes per stream match the E6 model; file size, memory and capture work are not larger than v1's on any matrix case;
- capture with nothing written costs the same on ZX-Evo as on Pentagon (BM-8); an unchanged frame costs a constant few bytes (PR-10);
- every device region restores exactly: a seek to before a write brings back the old contents (a test per region).

**Does not break.** v1 is not changed, so nothing users see can break; the full `core-tests` suite and the CI gate run at every step.

### Phase 2 — Device state with versions

**What.**

| Step | Name |
|---|---|
| Step 1 | Device registry: a stable type id (u16) plus an instance name, a layout version, restore-order dependencies, a firmware fingerprint, an after-restore call (D23) |
| Step 2 | Unchanged device state shared between checkpoints; changed state stored as the fields that changed; counters that advance with time derived from time, not stored (D18) |
| Step 3 | A restore that could not be exact is reported with its reason, on every surface (FR-7) |
| Step 4 | Sound devices on the same contract, synced at every frame boundary; the device set fixed for a session (D38, FR-4) |

**Why.** v1 writes every device's whole state into every checkpoint: 2–7 MB per minute even at an idle prompt (E5), most of it the timers of cards that play nothing (E6). The layouts carry no version, the one-byte device id is nearly full and already collides between designs, and a device that could not be restored is only logged.

**Done when.** FR-2, FR-4, FR-7, FR-19, PR-10, D33: an idle frame costs (almost) nothing for device state; the contract test covers every device on every creatable model; a corrupted device state produces a "degraded" result on every surface.

### Phase 3 — Everything a replay needs

**What.**

| Step | Name |
|---|---|
| Step 1 | One event stream with a kind per event, its point of application and a payload: input, external events, reset and debugger-edit markers, port reads, bus data such as interrupt vectors, DMA into memory, network data (D24) |
| Step 2 | Two replay modes: recorded input events, and recorded `IN` values for RZX (D14) |
| Step 3 | Several CPUs: each keeps its own cycle counter, clock changes are events, a position can name any CPU (D20, D21) |
| Step 4 | Configuration fingerprint and media versions per checkpoint; a mismatch on load is reported as "not bit-exact" (D25, FR-14) |
| Step 5 | Real-time clocks run from an emulated time base kept in the session (D25) |
| Step 6 | No writes outside the session while history is replayed (FR-20) |
| Step 7 | The write journal as a derived index with a retention policy (ring, whole history, or a window regenerated by replay), the default chosen by experiment E7 (D17) |

**Why.** v1 files already hold the input journal, external events, port journals and network input, but not the settings exact replay depends on; one session cannot name a position on a card CPU; the clock's time base is not kept in the session; a replay can write to a disk image; and the Sprinter's replay reads interrupt vectors from live devices. The write journal is the largest stream on any active recording (E6).

**Done when.** FR-10, FR-14, FR-20, FR-21, D33: a loaded session replays inside a frame exactly; a session loaded with different settings says so; a disk image is byte-identical after replaying writes to it; a card CPU position can be the target of a seek.

### Phase 4 — The session file

**What.**

| Step | Name |
|---|---|
| Step 1 | Integrity and versioning decided ([investigation](integrity-and-versioning.md)) |
| Step 2 | The session is written to a file as it records (D28): append-only, self-contained parts cut by dependencies (D6), written by a background thread; it survives a crash of the emulator |
| Step 3 | Memory as a cache of the file: a budget, eviction with rebasing of chains that reach past the new start (D5), evicted data read back on a seek; real memory accounting (FR-16) |
| Step 4 | Optional frame-boundary streams in the file, the first one a screenshot per frame for quick debugging (D19) |
| Step 5 | A v1 file is read into the engine's format, or into memory when no file is wanted (D31) |
| Step 6 | Format description (`ttd.ksy`) and the Python analyzer for the new format |

**Why.** A whole session must fit in memory today, nothing survives a crash, and saving is an explicit step. With the file written as it records, "save" is a rename and an unwanted recording is deleted.

**Done when.** FR-11, FR-12, FR-13, FR-15, FR-16, PR-8, PR-12, QR-2, QR-5, D33: a one-hour ZX-Evo recording stays within its memory budget; killing the emulator leaves a file that loads up to the last complete part; recording to the file costs no more frame time than recording to memory (capture never waits for the disk); files are byte-identical across platforms.

### Phase 5 — Switch the emulator to the engine

**What.**

| Step | Name |
|---|---|
| Step 1 | The emulator runs on the engine; every surface (WebAPI, MCP, CLI, Lua, Python, Qt, DeZog) switches inside, with the same routes and commands (QR-8); switching TTD or debug mode off stops a recording cleanly (FR-17) |
| Step 2 | History is never cut short: resuming from the past or editing it starts a branch, seeking while recording pauses it, loading a snapshot is a timeline event (D7–D10, D12, D13) |
| Step 3 | Always-on recording (black box) as a global persisted setting in unreal-qt, off for automation (D29); the session file's location chosen in the UI, by default `scratch/ttd/<date-time>-<name>.ttd` (D30) |
| Step 4 | v1 leaves the emulator: it stays only in the verification tools (tests, benchmark, comparison) (D32) |

**Why.** Users get the engine only when it is better than v1 on every check (D33).

**Done when.** The acceptance criteria of [requirements §6](requirements.md) hold through every surface; the recipes in `.recipe/` work unchanged or are updated with the new behavior (branches instead of truncation).

### Phase 6 — Cleanup

**What.** Delete `TimeTravelManager` and what only it used, keep the v1 file reader for verification, retire the old proof-of-concept readers, bring the parent TDD in line with what was built, move this folder to `DONE.md`.

**Done when.** QR-9 and the acceptance criteria of [requirements §6](requirements.md) hold, with the evidence linked from `DONE.md`.

### Later, outside these phases

- Branch operations in the UI (list, switch, rename, compare): [PLAN #76](../2026-09-29-model-what-if/design.md). The data model carries branches from Phase 1.
- Groups of machines (ZX-Poly): one group seek and one group file over a shared piece store (D22).

## 3. Rules for every phase

- **The same gate at the end of every step:** full build with zero warnings, the full `core-tests` suite, and the fixture corpus re-recorded whenever the file contents change ([testdata/ttd/README.md](../../../testdata/ttd/README.md)).
- **The benchmark matrix is the regression guard:** each phase compares the full matrix against the stored baseline ([tools/verification/ttd-bench](../../../tools/verification/ttd-bench/README.md)) and stores its own result as the next baseline.
- **All or nothing:** a phase lands on master as a series of commits; if a problem shows up later, the phase is reverted as a whole, not left half applied. v1 and its file format are not changed by Phases 1–4, so a revert never affects users.
- **No linear-timeline assumption** (FR-22, FR-24): nothing in Phases 1–4 may assume that history is one straight line or add a path that truncates it, so branched histories ([PLAN #76](../2026-09-29-model-what-if/design.md)) stay possible.
- **The engine beats v1 after every phase** ([engine-decisions.md](engine-decisions.md#g-quality-bar-after-every-phase), decision 33): every frame restores byte for byte as in v1; file size, memory and counted capture work are not larger than v1's in any case of the matrix, for the same history kept; seek time may be slower, within PR-5 and, from Phase 1, Step 5 (restore only the pieces that differ) on, within 25% of E4's estimates. Ratios fixed by design are listed there and are not regressions.
- **A TDD before the code:** each phase starts with its technical design in this folder, linked from the roadmap above.

## 4. Documents

| Document | What it answers |
|---|---|
| This page | The roadmap: phases, why, how each is checked |
| [engine-approach-and-naming.md](engine-approach-and-naming.md) | How v2 is built (a new engine next to v1) and what its parts are called |
| [engine-decisions.md](engine-decisions.md) | The design decisions taken before the engine's code: what it supports from the start, and the conflicts between documents they settle |
| [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md) | Phase 1 technical design |
| [migration-trajectory.md](migration-trajectory.md) | Step notes: what each step contains in detail, the history of decisions, risks per phase |
| [current-state.md](current-state.md) | What TTD does today: code, costs, gaps, suspected bugs |
| [requirements.md](requirements.md) | What v2 must achieve: functional (FR), performance (PR), quality (QR) and benchmark (BR) requirements, acceptance criteria |
| [target-architecture.md](target-architecture.md) | What TTD should become as a whole |
| [integrity-and-versioning.md](integrity-and-versioning.md) | Open investigation: checksums and file versioning (decided in Phase 4, Step 1) |
| [v0b-benchmark-results.md](v0b-benchmark-results.md) | Phase 0, Step 2 results: v1's numbers, the decision to drop checkpoints inside a frame |
| [branch-merge-strategy.md](branch-merge-strategy.md) | Phase 0, Step 3: how the three feature branches were merged (history) |

Reading order: this page → [current-state](current-state.md) → [requirements](requirements.md) → the TDD of the current phase.

## 5. Former step names

**Phases before the engine decision (2026-10-02).** The phases used to change v1 in place. Where their steps went:

| Before 2026-10-02 | Now |
|---|---|
| Phase 1, Step 1 — Memory regions | Phase 1, Step 3 |
| Phase 1, Step 2 — Chain length limit per piece | Phase 1, Step 2 (piece store) |
| Phase 1, Step 3 — Delta base for changed pieces only | Phase 1, Step 4 (live capture) |
| Phase 1, Step 4 — Copy-on-write page reference table | Phase 1, Step 3 |
| Phase 1, Step 5 — Device memory as regions | Phase 1, Step 6 |
| Phase 1, Step 6 — Restore only the pieces that differ | Phase 1, Step 5 |
| Phase 1, Step 7 — Encode a changed piece once | Phase 1, Step 2 (piece store) |
| Phase 2, Steps 1–4 | Phase 2, Steps 1–4 (Step 2 widened: changed fields, time-derived counters) |
| Phase 3, Step 1 — Input and external events in the file | Phase 3, Step 1 (one event stream) |
| Phase 3, Steps 2, 7 — Configuration fingerprint, media identity | Phase 3, Step 4 |
| Phase 3, Step 3 — Reset and debugger-edit markers | Phase 3, Step 1 |
| Phase 3, Step 4 — Emulated clock | Phase 3, Step 5 |
| Phase 3, Step 5 — No writes outside the session | Phase 3, Step 6 |
| Phase 3, Step 6 — DMA, interrupt vectors | Phase 3, Step 1 |
| Phase 4, Step 1 — Integrity and versioning | Phase 4, Step 1 |
| Phase 4, Steps 2, 3 — Accounting, budget | Phase 4, Step 3 |
| Phase 4, Step 4 — Clean stop on TTD / debug mode off | Phase 5, Step 1 |
| Phase 5, Steps 1, 2 — Chunked container, disk mode | Phase 4, Step 2 (written as it records) |
| Phase 5, Step 3 — Format description and analyzer | Phase 4, Step 6 |
| Phase 5, Step 4 — Write-journal coverage window | Phase 3, Step 7 |
| Phase 5, Step 5 — Stream ids for branches | Phase 1, Step 3 (parent link) and Phase 4, Step 2 |

**Names before 2026-10-01.** Until then these documents numbered the steps V0…V6. Older documents elsewhere may still use those names:

| Former name | Now |
|---|---|
| V0 make v1 honest | Phase 0, Step 1 |
| V0b benchmark harness | Phase 0, Step 2 |
| profi / generalsound / moonsound merges | Phase 0, Step 3 |
| V1b checkpoints inside a frame | Phase 0, Step 4 (dropped) |
| V1 memory regions | Phase 1 |
| V2 device state | Phase 2 |
| V3 determinism inputs | Phase 3 |
| V4 memory budget | Phase 4 |
| V5 container v2 + disk mode | Phase 5 |
| V6 cleanup | Phase 6 |

## 6. What "v1" and "v2" mean here

The existing documents use "v1"/"v2" in at least six different senses (file schema number, the phase-5 codec, POC 011's comparison baseline, design-doc revisions, first-delivery scope, device blob layouts). In this folder:

- **v1** = TTD as shipped on master today, including its file format (header `schema_version = 1`). It already stores memory as 4 KB pieces, each either a full compressed copy or a compressed difference (XOR) from its previous version.
- **v2** = what this migration builds: memory regions for device-owned memory, cost proportional to change, device state with versions, the inputs exact replay needs in the file, a memory budget, disk streaming, and a versioned, checksummed file. In code it is the engine `ttd::TimeTravelEngine`; the name carries no version ([engine-approach-and-naming.md](engine-approach-and-naming.md)).

POC 011 (`tools/poc/011-ttd-v2-capture-analysis/`) compares its "v2" against a hypothetical format that stores the full machine every frame; that "v1" is not the shipped engine, so its 30×/199× headline gains do not apply to this migration. Its codec and index-overhead measurements remain useful.

## 7. Glossary

| Term | Meaning |
|---|---|
| Checkpoint | Saved machine state at a frame boundary; one per frame |
| Piece | A 4 KB block of memory as stored in the page store. The code calls it a *slot* (`TTDCodecPageStore`); these documents say *piece* throughout |
| XorPrev | A piece stored as the difference (XOR) from its previous version, compressed |
| Chain | The sequence of XorPrev pieces that must be decoded to rebuild one piece |
| Key frame (v1) | Every 50th checkpoint, where all non-zero RAM is stored in full to cap chain length |
| Chain length limit (v2) | Per-piece limit on chain length; replaces whole-RAM key frames |
| Memory region | A block of emulated memory tracked by pieces: machine RAM, or memory owned by a device (General Sound, MoonSound, an EEPROM) |
| Device blob | A device's small state (registers, latches) saved by the device registry |
| Restore report | The registry's list of devices it could not restore (missing, wrong size, unknown) |
| Write journal | Log of every memory/port write, used to answer "who last wrote this" |
| Coverage index | Per-frame record of which addresses were executed / written / read, used to skip frames in reverse search |
| Replay | Re-running the emulator from a checkpoint to reach a point inside a frame |
| Chunk | Unit of the v2 file: a header with stream id and checksum, then a payload |
| Configuration fingerprint | The emulator settings exact replay depends on (frame length, clock, audio rate, …) |

## 8. Sources consulted

- Code on master `d8be186d` (`core/src/debugger/ttd/`, device serializers, automation, Qt, analyzer, fixtures) and a bit-flip experiment on `testdata/ttd/tsfm_tech_support.ttd`.
- Parent design `docs/emulator/design/debugger/time-travel-debug/` (`time-travel-debugging-tdd.md`, `ttd-container-format.md`, `overhead-and-gating.md`, `ttd-use-cases.md`, `implementation-plan.md`).
- [2026-07-19-time-travel](../2026-07-19-time-travel/) (all phase documents, decisions, phase-5 codec results), [2026-08-20-ttd-reverse-search-index](../2026-08-20-ttd-reverse-search-index/), [2026-09-10-ttd-registry-integration](../2026-09-10-ttd-registry-integration/), [2026-09-23-ttd-qt-toolbar-widget](../2026-09-23-ttd-qt-toolbar-widget/).
- `tools/poc/011-ttd-v2-capture-analysis/` (README, AUDIT, knowledge), `tools/poc/01-ttd-compression` (byte-identical to `010-ttd-compression`), `tools/poc/010-ttd-gui`.
- [2026-09-13-moonsound](../2026-09-13-moonsound/) (`opl4-ttd-integration-tdd.md`), [2026-09-19-general-sound](../2026-09-19-general-sound/), [2026-09-21-profi](../2026-09-21-profi/), [2026-09-24-core-performance](../2026-09-24-core-performance/) (TTD findings F2, G6, M7).
