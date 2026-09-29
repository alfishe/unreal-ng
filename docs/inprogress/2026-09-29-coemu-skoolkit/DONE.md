# DONE: SkoolKit co-emulation runner

**Plan row:** [#74](../PLAN.md) (retired) · [requirements.md](requirements.md) · [tdd.md](tdd.md)

## What landed

- `tools/verification/coemu/skoolkit/`: `run.sh` (harness contract, discovery), `skoolkit-run.py` (simulated
  `LOAD ""`, then SkoolKit's contention simulator until `DONE`; dump, screen as text, log), `README.md`.
- The harness README lists the runner; the contention probe's "Results so far" has a SkoolKit row and the
  P-05 note counts SkoolKit with FUSE and unreal-ng.

Permanent documentation: [tools/verification/coemu/skoolkit/README.md](../../../tools/verification/coemu/skoolkit/README.md).

## Status

| Step | Status |
|:--|:--|
| Prototype: load and run ctprobe on SkoolKit 10.1 (48K, 128K) | done 2026-09-29 |
| Requirements, design | done 2026-09-29 |
| Runner (`run.sh`, driver, README) | done 2026-09-29 |
| Harness and probe READMEs, results | done 2026-09-29 |
| Acceptance AC1-AC8 | done 2026-09-29 (below) |

## Acceptance evidence (SkoolKit 10.1, 2026-09-29)

| Criterion | Result |
|:--|:--|
| AC1 | `run.sh 48k 128k`: both `wrong: 10 values wrong in 1 checks` (P-02 only), dumps compared by `ctprobe-compare.py` |
| AC2 | `SKOOLKIT_PYTHON=/nonexistent run.sh 48k`: `skipped`, exit 3 |
| AC3 | `run.sh plus3 pentagon`: both `skipped` ("SkoolKit simulates only the 48K and the 128K"); `run.sh` reports all eleven machines |
| AC4 | `run-all.sh 48k 128k`: the summary has a `skoolkit` column |
| AC5 | two runs: `cmp` of both dumps identical |
| AC6 | `run.sh 48k 128k`: 3.6 s wall time |
| AC7 | the probe README explains P-02 (no floating bus in SkoolKit) and adds SkoolKit to the P-05 note |
| AC8 | `fix-absolute-paths.py`: 0 in every changed file; links resolve |

Also checked: discovery through `tap2sna.py` on `PATH` (a virtual environment's `bin` on `PATH`, no
`SKOOLKIT_PYTHON`).

## Findings

- SkoolKit has no floating bus: unused ports read `#FF`. That is P-02's 10 values on both machines.
- On P-05 (a port whose high byte points at an odd page at `#C000` on the 128K) SkoolKit contends, as FUSE and
  unreal-ng do; xpeccy-plus does not. Still open until real hardware decides.
