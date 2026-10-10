# Phases C0-C10: one document per phase

Each phase gets its design here **before** its code, and the document becomes the as-built record when
the phase lands. The phase table is in [tdd.md](../tdd.md) §14; the tests per phase in
[test-and-benchmark-plan.md](../test-and-benchmark-plan.md) §3.

| Phase | Document | Status |
|---|---|---|
| C0 / C1 | [c1-core-and-parity.md](c1-core-and-parity.md) | done |
| C2 | [c2-composite-descriptor.md](c2-composite-descriptor.md) | done |
| C3 | [c3-image-sources.md](c3-image-sources.md) | done |
| C4 | [c4-graft.md](c4-graft.md) | done |
| C4b | [c4b-lazy-graft-base.md](c4b-lazy-graft-base.md) | done: a graft reads only the base directories its upper layers reach (25x faster at 20 000 base files); counts and provenance of the rest on demand |
| C5 | [c5-iso.md](c5-iso.md) | done (C5a ISO, C5b boot carry-over) |
| C6 | [c6-provenance-flatten.md](c6-provenance-flatten.md) | done (C6a S1, C6b attribution, C6c S2) |
| C7 | [c7-partitions.md](c7-partitions.md) | done |
| C8 | [c8-commit-writeback.md](c8-commit-writeback.md) | done (C8c Qt: owner's build pending) |
| C8d | [c8d-writeback-tails.md](c8d-writeback-tails.md) | done: attributes sidecar, the host trash, partitioned write-back, a commit without a sector list in memory |
| C9 | [c9-bulk-read.md](c9-bulk-read.md) | dropped after measuring: 14x cheaper at the device, about 2 % of a guest's per-sector cost |
| C10 | [c10-sparse-memory.md](c10-sparse-memory.md) | done: C10a zero runs + sparse memory, C10b dynamic VHD; C10c measured (images stay streamed); §8 full-disk and RAM findings |
| C10d | [c10d-session-spill.md](c10d-session-spill.md) | done: session writes bounded in RAM (128 MiB), the rest spilled to disk; streamed deltas |
| C10e | [c10e-session-journal.md](c10e-session-journal.md) | done: 1 MiB arenas, 16 MiB in memory, 30 s flush, a recoverable journal next to the medium (off by default since the master merge: §9); sweep measured in §8 |
| C11 | [../benchmarks/README.md](../benchmarks/README.md) | done: benchmarks and charts C1-C8, the NFR table; host-file memory 47.6 -> 17.4 MiB at 100 K entries, a 64-layer build 1.8 -> 1.33 s, no allocation per read |
