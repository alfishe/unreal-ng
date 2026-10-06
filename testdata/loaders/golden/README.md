# Golden commit digests

`commit-digests.txt`: for every valid SNA / Z80 / SZX fixture under `testdata/loaders/` loaded on every creatable
machine (fresh machine, zero RAM), one row `<fixture>@<machine><TAB>result`. The result is `refused`,
`threw: <what>` or the digest of the state left behind: hashes of the RAM pages, the paging latches with the bank
mapping, the CPU, AY 0 (the machines are built with the TurboSound slot, which the test runner leaves empty otherwise) and the
border / flags.

It records what `master` does TODAY, defects included, so the snapshot pipeline (PLAN #84) can prove each step
reproduces it. Test: `core/tests/loaders/snapshot/snapshotgolden_test.cpp`; helper `core/tests/_helpers/snapshotdigest.h`;
plan and findings: `docs/inprogress/2026-10-02-snapshot-pipeline/TODO.md`.

A row may change only in a commit that says so (list the rows). To rewrite after an approved change:

    UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 core-tests --gtest_filter='SnapshotGoldenRewrite*'
