# TTD v1 → v2 migration

Planning folder for the next generation of the time-travel debugger (TTD) and
for landing the `profi`, `generalsound` and `moonsound` branches around it.
Status: see [TODO.md](TODO.md).

## 1. Documents

| Document | What it answers |
|---|---|
| [current-state.md](current-state.md) | What TTD does today (code, costs, gaps, integrity, suspected bugs, dependents) |
| [requirements.md](requirements.md) | What v2 must achieve: functional, performance, quality and benchmark requirements, acceptance criteria |
| [integrity-and-versioning.md](integrity-and-versioning.md) | Open investigation: checksum coverage, granularity, failure behaviour, streaming; file versioning and compatibility |
| [target-architecture.md](target-architecture.md) | What TTD should become, what is dropped from earlier designs, and the checksum decision |
| [branch-merge-strategy.md](branch-merge-strategy.md) | Where the three branches stand, what to fix on master first, per-branch checklists, merge order |
| [migration-trajectory.md](migration-trajectory.md) | The ordered steps (V0–V6 interleaved with the merges), exit criteria, open decisions |

Reading order: this page → current-state → requirements → migration-trajectory →
the others as reference.

## 2. What "v1" and "v2" mean here

The existing documents use "v1"/"v2" in at least six different senses (file
schema number, the phase-5 codec, POC 011's comparison baseline, design-doc
revisions, first-delivery scope, device blob layouts). In this folder:

- **v1** = TTD as shipped on master today, including its file format (header
  `schema_version = 1`). It already has page-granular capture and the XOR +
  zstd codec.
- **v2** = the parts designed but not built: memory regions for device-owned
  RAM, cost proportional to change (no whole-RAM key frames or copies),
  device-state table with versions and "unchanged" sharing, determinism inputs
  in the file, a memory budget, disk streaming, and the chunked, checksummed,
  versioned file container.

POC 011 (`tools/poc/011-ttd-v2-capture-analysis/`) compares its "v2" against a
hypothetical format that stores the full machine every frame; that "v1" is
not the shipped engine, so its 30×/199× headline gains do not apply to this
migration. Its codec and index-overhead measurements remain useful.

## 3. Summary

- **TTD is half-way to v2 already.** The codec landed in phase 5. What is
  missing is everything about *scale* (memory bounds, disk, device RAM) and
  *trust* (versioning, checksums, reporting incomplete restores).
- **Correctness first.** Reading the code turned up a capture gap (RAM
  page 255 on 4 MB machines) and several suspected bugs (stale cache after "resume from here", stale journal after load). Step V0 fixes and
  tests these before anything else builds on them.
- **Merge order: profi → generalsound → moonsound**, with the *memory region*
  part of v2 landed on master before GS, because GS (512 KB card RAM) and
  MoonSound (1 MiB sample RAM) both need it and would otherwise ship TTD code
  that has to be rewritten. The rest of v2 comes after all three merges.
- **Fix before any merge:** all three branches use `PeripheralId` 9, which is
  stored in files — allocate one table on master first.
- **Checksums and versioning: needed, mechanism open.** See
  [integrity-and-versioning.md](integrity-and-versioning.md).
  Today most random corruption goes unnoticed (57–100% of flips per section in
  a measured experiment). Checksums would tell a damaged file from an emulator
  bug and make crash recovery of streamed files possible; what they cover,
  when they are checked and what a failure does has many nuances and is being
  investigated separately. It does not catch determinism or emulator
  bugs — the kind of TTD defect actually seen so far — which stay the job of
  the corpus and divergence tests.

## 4. Glossary

| Term | Meaning |
|---|---|
| Checkpoint | Saved machine state at a frame boundary; one per frame |
| Piece | A 4 KB block of memory as stored in the page store. The code calls it a *slot* (`TTDCodecPageStore`); these documents say *piece* throughout |
| XorPrev | A piece stored as the difference (XOR) from its previous version, compressed |
| Chain | The sequence of XorPrev pieces that must be decoded to rebuild one piece |
| Key frame (v1) | Every 50th checkpoint, where all non-zero RAM is stored in full to cap chain length |
| Chain cap (v2) | Per-piece limit on chain length; replaces whole-RAM key frames |
| Memory region | A block of emulated memory tracked by pieces: machine RAM, or RAM owned by a device (GS, MoonSound) |
| Device blob | A device's small state (registers, latches) saved by the device registry |
| Restore report | The registry's list of devices it could not restore (missing, wrong size, unknown) |
| Write journal | Log of every memory/port write, used to answer "who last wrote this" |
| Coverage index | Per-frame record of which addresses were executed / written / read, used to skip frames in reverse search |
| Replay | Re-running the emulator from a checkpoint to reach a point inside a frame |
| Chunk | Unit of the v2 file: a header with stream id and CRC32C, then a payload |
| Cue table | Index at the end of a v2 file mapping frames to chunk positions |
| Configuration fingerprint | The emulator settings exact replay depends on (frame length, clock, audio rate, …) |

## 5. Sources consulted

- Code on master `d8be186d` (`core/src/debugger/ttd/`, device serializers,
  automation, Qt, analyzer, fixtures) and a bit-flip experiment on
  `testdata/ttd/tsfm_tech_support.ttd`.
- Parent design `docs/emulator/design/debugger/time-travel-debug/`
  (`time-travel-debugging-tdd.md`, `ttd-container-format.md`,
  `overhead-and-gating.md`, `ttd-use-cases.md`, `implementation-plan.md`).
- [2026-07-19-time-travel](../2026-07-19-time-travel/) (all phase documents,
  decisions, phase-5 codec results),
  [2026-08-20-ttd-reverse-search-index](../2026-08-20-ttd-reverse-search-index/),
  [2026-09-10-ttd-registry-integration](../2026-09-10-ttd-registry-integration/),
  [2026-09-23-ttd-qt-toolbar-widget](../2026-09-23-ttd-qt-toolbar-widget/).
- `tools/poc/011-ttd-v2-capture-analysis/` (README, AUDIT, knowledge),
  `tools/poc/01-ttd-compression` (byte-identical to `010-ttd-compression`),
  `tools/poc/010-ttd-gui`.
- [2026-09-13-moonsound](../2026-09-13-moonsound/) (`opl4-ttd-integration-tdd.md`),
  [2026-09-19-general-sound](../2026-09-19-general-sound/),
  [2026-09-21-profi](../2026-09-21-profi/),
  [2026-09-24-core-performance](../2026-09-24-core-performance/) (TTD findings F2, G6, M7).
- The `profi`, `generalsound`, `moonsound` branches (remote tips), read-only.
