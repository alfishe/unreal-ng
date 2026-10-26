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
| P2 | `TraceBundle`, `ReplayTracer`, L1 structures, fact sheet | done 2026-10-26 in the worktree, awaits review: module doc [analysis-layers.md](../../emulator/analysis/analysis-layers.md) "L1: code behaviour", tests `AnalysisL1_Test.*` (fixtures `testdata/analysis/fixtures/`), [replay-p2.md](baselines/replay-p2.md) (gate >= 5x real time met), Manic Miner check in replay-p2.md |
| P2 | `analysis build` / `cancel` as a job (topics `analysis.progress`, `analysis.ready`), `explain` routine sheets, `start record=true`, on every plane and MCP `analysis` | done 2026-10-26 in the worktree, awaits review: plane docs, live block `test_analysis_layers.py` |
| P2 | P1 leftover: physical mode without the 64 K Z80 fact table | done 2026-10-26 (`AnalysisL0_Test.PhysicalModeHasNoZ80View`) |
| P3 | Region store, structural detectors (signatures with the 48K ROM catalog, code map, text, init tables, pointer / handler tables, records, variables), one file each | done 2026-10-26 in the worktree, awaits review: module doc [analysis-layers.md](../../emulator/analysis/analysis-layers.md) "L2 structure and the draft", tests `AnalysisL2_Test.*` (fixtures `testdata/analysis/fixtures/`) |
| P3 | Export ctl / skool / fields / geometry / layout / symbols / map; `analysis query` / `export` on every plane and MCP | done 2026-10-26 in the worktree, awaits review: plane docs, live block `test_analysis_layers.py` |
| P3 | Manic Miner gate through zx-reaper (`mm_session.py`, `reaper_gate.py`) | done 2026-10-26: [score-p3.md](baselines/score-p3.md): categories 99.98 %, entries 31 / 33, records 5 / 5, make / make test / shift +$100 / +$101 pass; zx-reaper fixes on its branch `unreal-ng-fixes` |
| P4 | Media detectors (graphics, level maps, screens, RNG, unpackers, music), image macros and geometry in the export, `analysis gallery` on every plane and MCP | done 2026-10-26 in the worktree, awaits review: module doc [analysis-layers.md](../../emulator/analysis/analysis-layers.md) "The gallery", tests `AnalysisL2_Test.*` / `AnalysisControl*_Test.Gallery*` / `Analysis_GalleryAnswersAnImage`, live block `test_analysis_layers.py::test_gallery`, [score-p4.md](baselines/score-p4.md): geometry 106 / 106, P3 numbers kept |
| P5 | Findings register, review queue, experiment runner, directed coverage, candidate bugs; `analysis findings` / `review` / `experiment` / `cover` on every plane and MCP | done 2026-10-26 in the worktree, awaits review: module doc [analysis-layers.md](../../emulator/analysis/analysis-layers.md) "L3 review", tests `AnalysisL3_Test.*` (fixtures `poke-flip.asm`, `key-test.asm`, `read-unwritten.asm`), `Analysis_ReviewActionsForwardTheirOptions`, live block `test_analysis_layers.py::test_review_experiment_cover`, [score-p5.md](baselines/score-p5.md): queue 108 open, coverage 17 flips / 77 new instructions, 0 candidate bugs (4 false positives tuned out), P4 gate numbers kept |
| P5 | Recipe `.recipe/analysis/analysis-layers.md`, module doc, plane docs | done 2026-10-26 in the worktree, awaits review |
| P5 | A/B harness `tools/verification/analysis/run_ab.py` (gate + token ledger against run B) | done 2026-10-26: draft alone fails the verdict as expected (31 / 33 entries, no ledger); the review-only agent run by the recipe is next |
| P6 | Hold-out: Jet Set Willy + one 128K title, the plan.md targets | open |
