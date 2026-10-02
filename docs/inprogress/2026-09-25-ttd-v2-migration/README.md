# TTD v1 → v2 migration

This folder plans the move of the time-travel debugger (TTD) from the engine on master today (**v1**) to the next one (**v2**). This page is the roadmap: the phases in order, and for each phase what it does, why, how we know it is complete, and how we know it broke nothing. The details sit one level down, in the [step notes](migration-trajectory.md) and in each phase's technical design (TDD). Progress is tracked in [TODO.md](TODO.md).

## 1. Roadmap

| Phase | Name | Status | Size | Design |
|---|---|---|---|---|
| [Phase 0](#phase-0--preparation) | Preparation | **done** (2026-09-29) | — | [step notes](migration-trajectory.md#phase-0--preparation) |
| [Phase 1](#phase-1--memory-that-costs-only-what-changes) | Memory that costs only what changes | next | M | [TDD](phase-1-memory-regions-tdd.md) |
| [Phase 2](#phase-2--device-state-with-versions) | Device state with versions | planned | M | TDD before the phase starts |
| [Phase 3](#phase-3--everything-a-replay-needs-in-the-file) | Everything a replay needs, in the file | planned (Step 1 pulled forward) | M | TDD before the phase starts |
| [Phase 4](#phase-4--memory-budget) | Memory budget | planned | M | TDD before the phase starts |
| [Phase 5](#phase-5--versioned-file-and-disk-mode) | Versioned file and disk mode | planned | L | TDD before the phase starts |
| [Phase 6](#phase-6--cleanup) | Cleanup | planned | S | — |

Size: S = a focused day or two of agent work, M = several days, L = a week or more.

Phases are done in order. A step inside a phase is referred to as "Phase 1, Step 2 — Chain length limit per piece".

## 2. Phases

### Phase 0 — Preparation

**Status: done.** Make today's engine correct and measurable before changing it.

| Step | Name | Result |
|---|---|---|
| Step 1 | Make v1 honest | Done 2026-09-28: the RAM page 255 capture gap, suspected bugs B1–B10 fixed with tests, the state-completeness test, the feature flag no longer disables fast loading |
| Step 2 | Benchmark harness | Done 2026-09-29: the benchmark matrix with v1 as the first engine, a CI gate, stored baselines ([results](v0b-benchmark-results.md)) |
| Step 3 | Merge the feature branches | Done: `profi`, `generalsound`, `moonsound` are on master ([merge notes](branch-merge-strategy.md)) |
| Step 4 | Checkpoints inside a frame | **Dropped**: Step 2 measured seek p99 ≤ 3.8 ms on every configuration (limit 5 ms), so it is not needed |

### Phase 1 — Memory that costs only what changes

**What.** All emulated memory goes through one mechanism that stores only what changed since the previous frame:

| Step | Name |
|---|---|
| Step 1 | Memory regions: machine RAM becomes region 0, devices register their own memory as further regions |
| Step 2 | Chain length limit per piece, instead of storing all RAM again every 50 frames |
| Step 3 | Delta base kept for changed pieces only, instead of a full copy of RAM every frame |
| Step 4 | Copy-on-write page reference table, instead of a full table in every checkpoint |
| Step 5 | Device memory as regions: General Sound RAM, MoonSound wave memory, NeoGS memory, the ZX-Evo AVR EEPROM, the Scorpion SMUC EEPROM with its serial-link state |
| Step 6 | Restore only the pieces that differ, instead of decoding all memory on every seek |
| Step 7 | Encode a changed piece once, instead of compressing it both as a difference and as a full copy |

**Why.** Measured on v1 (Phase 0, Step 2):
- capture costs 190 µs per frame on ZX-Evo with nothing written, against 7–60 µs on 128K-class machines: the cost follows installed RAM, not change;
- every 50th frame stores all RAM again, so capture p99 is 2.9× the median on ZX-Evo (limit PR-3: 3×);
- the page reference table costs 4 KB per frame on a 4 MB machine even when nothing changes (~720 MB per hour);
- device memory is copied whole into every checkpoint (General Sound: 300–420 µs per capture), or not captured at all (MoonSound wave memory, both EEPROMs);
- every seek decodes all memory: memory restore takes 765–785 µs on ZX-Evo, the largest part of a seek;
- every changed piece is compressed twice, and the full compression (8.6 µs, against 0.96 µs for the difference) almost never wins.

**Done when.** Requirements FR-5, PR-3, PR-4, FR-22 and the memory part of PR-9 / PR-10 hold, shown by:
- capture with no memory written costs the same on ZX-Evo as on Pentagon (benchmark BM-8), and capture p99 ≤ 3× the median on every configuration of the matrix;
- memory restore follows the difference between two positions: a seek one frame back on ZX-Evo decodes about as many pieces as that frame changed (BM-6);
- an unchanged frame costs a constant few bytes of reference table, whatever the installed memory (PR-10);
- every device region restores exactly: a seek to before a write brings back the old contents (a test per region);
- the full benchmark matrix compared against the Phase 0 baseline: bytes per frame and capture time go down on ZX-Evo and on the configurations with device memory.

**Does not break.**
- On configurations without device memory, no byte or time metric of the matrix gets worse by more than 5%.
- The CI gate (`TTDBench_Test`) baseline changes only where the step explains why.
- The fixture corpus (`TTD_Corpus_Test`) restores and replays every fixture exactly, comparing RAM; the fixtures are re-recorded once.
- The state-completeness test and the full `core-tests` suite pass.
- Seek p99 stays within 5 ms on the matrix (PR-5).

**Design.** [Phase 1 TDD](phase-1-memory-regions-tdd.md).

### Phase 2 — Device state with versions

**What.**

| Step | Name |
|---|---|
| Step 1 | Device table in the session header, with a layout version per device, checked in release builds |
| Step 2 | Unchanged device state shared between checkpoints and stored once in the file ("same as previous") |
| Step 3 | A restore that could not be exact is reported on every surface (WebAPI, MCP, CLI, Python, Lua, DeZog, Qt) |
| Step 4 | Sound devices registered through the same declare / implement contract as the rest |

**Why.** Every device writes its whole state into every checkpoint (1.1 KB per frame on ZX-Evo, restore 115–230 µs with a General Sound card), the layouts carry no version, and a device that could not be restored is only logged, so a seek can land on a wrong state without anyone noticing.

**Done when.** FR-2, FR-4, FR-7, FR-19, PR-10: the contract test covers every device on every creatable model; a deliberately corrupted device state produces a "degraded" result on every surface; an idle frame costs (almost) nothing for device state.

**Does not break.** The same guards as Phase 1 (matrix comparison, CI gate, corpus, state-completeness test); the public API keeps every v1 route and command (QR-8).

**Design.** TDD written before the phase starts; input: [target architecture §4](target-architecture.md).

### Phase 3 — Everything a replay needs, in the file

**What.**

| Step | Name |
|---|---|
| Step 1 | Input events and external events saved in the session file (**pulled forward** for the offline-analysis program, [O-1](../2026-09-28-debugger-family/ttd-offline-analysis.md)) |
| Step 2 | Configuration fingerprint in the file (frame length, clock and turbo, audio rate, decimator, HQ settings, ROM signature, device table); a mismatch is reported as "not bit-exact" |
| Step 3 | Hardware-reset and debugger-edit markers actually written |
| Step 4 | RTC / CMOS reads served from an emulated clock recorded in the session |
| Step 5 | No writes outside the session while history is replayed (disk, SD, HDD, flash, CMOS files) |
| Step 6 | Isolation beyond port reads: DMA transfers into RAM and device-supplied interrupt vectors journaled |
| Step 7 | Media identity per session, through the unified media manager ([PLAN #58](../2026-09-28-storage-manager/technical-design.md), phase M7) |

**Why.** A saved session today holds neither the keyboard and mouse input nor the settings exact replay depends on (verified 2026-09-29), so a loaded session cannot replay a position inside a frame exactly; software that reads the clock sees the host's time; and a replay can write to a disk image.

**Done when.** FR-10, FR-14, FR-20, FR-21: a loaded session replays inside a frame with the recorded input; a session loaded with a different audio rate says so; a disk image is byte-identical after replaying writes to it.

**Does not break.** Phase 1 guards; live runs (not replays) behave as before - only the recording and replay paths use the recorded clock and inputs.

**Design.** TDD written before the phase starts; input: [target architecture §5](target-architecture.md).

### Phase 4 — Memory budget

**What.**

| Step | Name |
|---|---|
| Step 1 | Integrity and versioning decision written down ([investigation](integrity-and-versioning.md)) - a prerequisite, since crash safety and streaming shape how a budget releases memory |
| Step 2 | Real memory accounting (pieces, regions, journal, coverage, caches) in the status and the Qt panel |
| Step 3 | Configurable budget: when it is reached, the oldest frames are released and the earliest reachable frame is reported everywhere |
| Step 4 | Switching TTD or debug mode off during a recording stops it cleanly |

**Why.** Nothing bounds TTD's memory except the 64 MB write journal: a 10-minute ZX-Evo session already holds 0.86 GB.

**Done when.** FR-15, FR-16, FR-17: a one-hour ZX-Evo recording stays within the budget; a seek to a released frame fails with a clear message; the reported memory matches the real total.

**Does not break.** Phase 1 guards, plus a long soak test that checks reference counts while frames are released.

**Design.** TDD written before the phase starts; input: [target architecture §6](target-architecture.md).

### Phase 5 — Versioned file and disk mode

**What.**

| Step | Name |
|---|---|
| Step 1 | Chunked file container with the integrity mechanism decided in Phase 4, an index of frames and a footer; crash recovery |
| Step 2 | Disk mode: sealed chunks written in the background, released pieces fetched back on seek; "save" means finalize |
| Step 3 | Format description (`ttd.ksy`) and the Python analyzer rewritten for chunks |
| Step 4 | Write-journal coverage window, instead of the all-or-nothing "journal complete" flag |
| Step 5 | Stream ids reserved for branched histories (FR-23, [PLAN #76](../2026-09-29-model-what-if/design.md)) |

From this phase on the format is versioned; until then it is amended in place.

**Why.** Today the file has no version, most random damage goes unnoticed (57–100% of bit flips per section in a measured experiment), nothing survives a crash, and the whole session must fit in memory.

**Done when.** FR-11, FR-12, FR-13, FR-23, PR-8, PR-12, QR-2, QR-5: killing the emulator during a disk-mode recording leaves a file that loads up to the last complete unit; the bit-flip experiment reports every flip the chosen mechanism covers; a file from the first v2 release still loads; files are byte-identical across platforms.

**Does not break.** Phase 1 guards; the fixture corpus re-recorded once in the new format.

**Design.** TDD written before the phase starts; inputs: [target architecture §7–§8](target-architecture.md), [integrity and versioning](integrity-and-versioning.md).

### Phase 6 — Cleanup

**What.** Retire the old proof-of-concept readers, bring the parent TDD in line with what was built, re-enable the long seek test, move this folder to `DONE.md`.

**Done when.** QR-9 and the acceptance criteria of [requirements §6](requirements.md) hold, with the evidence linked from `DONE.md`.

## 3. Rules for every phase

- **The same gate at the end of every step:** full build with zero warnings, the full `core-tests` suite, and the fixture corpus re-recorded whenever the file contents change ([testdata/ttd/README.md](../../../testdata/ttd/README.md)).
- **The benchmark matrix is the regression guard:** each phase compares the full matrix against the stored baseline ([tools/verification/ttd-bench](../../../tools/verification/ttd-bench/README.md)) and stores its own result as the next baseline.
- **All or nothing:** a phase lands on master as a series of commits; if a problem shows up later, the phase is reverted as a whole, not left half applied. Until Phase 5 the file format is amended in place, so a revert needs no compatibility work.
- **No linear-timeline assumption** (FR-22, FR-24): nothing in Phases 1–4 may assume that history is one straight line or add a path that truncates it, so branched histories ([PLAN #76](../2026-09-29-model-what-if/design.md)) stay possible.
- **A TDD before the code:** each phase starts with its technical design in this folder, linked from the roadmap above.

## 4. Documents

| Document | What it answers |
|---|---|
| This page | The roadmap: phases, why, how each is checked |
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

Until 2026-10-01 these documents numbered the steps V0…V6. Older documents elsewhere may still use those names:

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
- **v2** = what this migration builds: memory regions for device-owned memory, cost proportional to change, device state with versions, the inputs exact replay needs in the file, a memory budget, disk streaming, and a versioned, checksummed file.

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
