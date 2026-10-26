# TODO: analysis layers

**Status:** proposal, no code. PLAN.md row **#111** (T2). It is the umbrella for #29 (2026-01-14 analyzers) and
#75 (font finder). Owner decisions of 2026-10-25 in [plan.md](plan.md#owner-decisions-2026-10-25): built in a worktree, landed when the A/B run proves it.

## Documents

| File | Topic |
|:--|:--|
| [README.md](README.md) | **Start here.** The problem, the four layers, what each module gives and why, savings |
| [design.md](design.md) | How each module is built, with diagrams and data structures |
| [plan.md](plan.md) | Order of work, how we measure, tests, risks, open questions |

## Tasks

| Phase | Task | Status |
|:--|:--|:--|
| - | Owner review of the proposal and the open questions | done 2026-10-25 |
| P0 | `memorytracker_frame_benchmark` baseline (off / counters) on 48K, 128K and Pentagon | done 2026-10-25 |
| P0 | `tools/verification/analysis/score_draft.py` against mm.skool; baseline from `sna2ctl -m` | done 2026-10-25 |
| P0 | Run B token ledger checked in as the baseline | done 2026-10-25 |
| P1 | L0 level `facts` + tests + A/B | done 2026-10-26 in the worktree, awaits review: [memorytracker-p1.md](baselines/memorytracker-p1.md) |
| P1 | `analysis start/stop/status/explain` on every plane, MCP `analysis` | done 2026-10-26 in the worktree, awaits review: module doc [analysis-layers.md](../../emulator/analysis/analysis-layers.md), live block `tools/verification/mcp/test_analysis_layers.py`, MCP calls per action in `host.clients.mcp.tools` |
| P2 | `TraceBundle`, `ReplayTracer`, L1 structures, fact sheet | open |
| P3 | Structural detectors, ROM signatures, ctl / skool export, `query` | open |
| P4 | Media detectors, gallery | open |
| P5 | Findings register, review queue, experiment runner, directed coverage, candidate bugs | open |
| P5 | Recipe `.recipe/analysis/analysis-layers.md`, module doc, plane docs | module doc and plane docs started in P1 (L0 + the verbs); recipe open |
| P6 | Hold-out: Jet Set Willy + one 128K title, the plan.md targets | open |
