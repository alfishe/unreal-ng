# Phases C0-C9: one document per phase

Each phase gets its design here **before** its code, and the document becomes the as-built record when
the phase lands. The phase table is in [tdd.md](../tdd.md) §13; the tests per phase in
[test-and-benchmark-plan.md](../test-and-benchmark-plan.md) §3.

| Phase | Document | Status |
|---|---|---|
| C0 / C1 | [c1-core-and-parity.md](c1-core-and-parity.md) | done |
| C2 | [c2-composite-descriptor.md](c2-composite-descriptor.md) | done |
| C3 | [c3-image-sources.md](c3-image-sources.md) | done |
| C4 | [c4-graft.md](c4-graft.md) | done |
| C5 | [c5-iso.md](c5-iso.md) | done (C5a ISO, C5b boot carry-over) |
| C6 | [c6-provenance-flatten.md](c6-provenance-flatten.md) | done (C6a S1, C6b attribution, C6c S2) |
| C7 | [c7-partitions.md](c7-partitions.md) | done |
| C8 | [c8-commit-writeback.md](c8-commit-writeback.md) | C8a, C8b done; C8c design |
| C9 | bulk `ReadSectors` | — |
| C10 | sparse and in-memory images, efficient packing on save / flatten | — (added 2026-10-05) |
