# 046 — proposal

**Adopt** the CPU time profile as the core `ProfileAnalyzer` (analyzer id `profile`, verb `cpuprofile` on every
plane), as built in feature [docs/inprogress/2026-11-05-profile-analyzer](../../../docs/inprogress/2026-11-05-profile-analyzer/README.md)
(§14 decisions, §15 numbers). Module doc: [cpu-profile.md](../../../docs/emulator/analysis/cpu-profile.md).

## What goes in

| Part | Shape | Why (analysis) |
|:--|:--|:--|
| cells | 16 B (`u64` count with the entry's routine id in bits 40-63, `u64` T), per physical page, lazy | 8 B cells are not cheaper; the routine bits make the entry test free |
| T-states | always on | counts alone are not an answer and save only ~10 % |
| shadow stack | on by default, fed by `Z80::FlowHook` from the CALL / RST / RET handlers | costs nothing measurable over the cells; per-step decoding costs 1-3 ns |
| RET chains | a dispatch from a dispatch frame replaces it | stacking overflowed on Zynaps |
| per-frame series | on by default | ~1 µs a frame, within the noise |
| beam bins | off by default (`beam=true`) | +2-5 %, over the 3 % budget |
| idle | idle-tolerant subscription | a HALT-paced program would pay +100 % |
| acknowledge | booked to the handler's first instruction; the kit export says `acknowledge_tstates` | L1 folds it into the interrupted instruction; the totals match |
| kit export | L1's `instructions` / `routines` / `replay` / `pages` subset | `fig_budget.py` reads it unchanged |

## What stays in the POC

- The step functions B / C / C8 / D-G and the `bench` / `replay` driver (`-DBUILD_PROFILE_ANALYZER_POC=ON`); the
  `replay --core 1` mode is the accuracy check of the core analyzer against an L1 file and should be rerun when the
  attribution rules change.
- `Settings::flowSource` 1 / 2 in the core are internal (no plane exposes them): the A/B variants K1o / K1m and the
  `FlowSourcesAgree` test, which proves the flow events give the same profile as decoding. Remove them if the test
  is replaced by a recorded fixture.

## Later (not needed to land)

- Attribution by address in `report` (today the kit export + `.ann` + `fig_budget.py` does it).
- `trace cpuprofile` alias, the `debug.cpuprofile` topic's push cadence tuning.
- Profiling a recording directly (`Settings::countReplay` on an analysis clone already does it in the POC).
