# ZX DLSS GigaScreen — Requirements

**Created:** 2026-09-27
**Status:** requirements settled, ready for review (2026-09-27)
**Roadmap:** [04-zxdlss §3.1](../2026-09-21-roadmap/04-zxdlss-semantic-layer-and-multiplayer.md) (Stage 1, Z-1..Z-4)
**Design:** [design-analysis.md](design-analysis.md) · [design-mixers.md](design-mixers.md) · [test-plan.md](test-plan.md) · [prior-art.md](prior-art.md) · [rollout.md](rollout.md) · [optimization-ideas.md](optimization-ideas.md) · [reference-across-the-edge.md](reference-across-the-edge.md) · [temporal-effects-manager.md](temporal-effects-manager.md)
**Depends on:** [Look-Ahead Manager](../2026-09-27-lookahead-manager/requirements.md) · [Metadata Manager](../2026-09-27-metadata-manager/requirements.md)

## Goal

Show what GigaScreen software intended — mixed colors — instead of
flickering, while everything that is not part of color mixing stays exactly
as sharp and responsive as the raw emulator output.

## Decided

| ID | Requirement |
|---|---|
| R-1 | Scope: the standard ZX screen (256×192 picture + border). Hi-res / extended-palette clone modes are out of scope. |
| R-2 | Input is **A+B**: the RGBA frame (A) plus a per-frame meaning plane from the core renderer (B: color index, ink/paper/border role, bitmap + attribute as used by the beam, displayed screen page). B through the palette must equal A (tested invariant). |
| R-3 | Every case in the case catalog ([design §3](design-analysis.md#3-case-catalog)) has a detector: static, dither, irregular, split (moving bitmap over flickering colors), plain motion, moving + flicker, alternating objects, scroll, scene cut, all border variants. |
| R-4 | Motion compensation is part of v1: applied when a bitmap moves **and** takes part in color mixing. When those conditions do not hold, the cheaper treatment is used. |
| R-5 | Never worse than raw: any pixel the detectors are not confident about is shown unprocessed. |
| R-6 | Cover all cases first; simplify only when real material shows a case does not occur. |
| R-7 | Both a GPU (shader) implementation and a CPU + SIMD implementation, benchmarked, each with its own test suite, both checked against one scalar reference. |
| R-8 | v1 is not performance-constrained: it may use the whole GPU and several CPU cores. Optimization follows once quality criteria and tests exist. |
| R-9 | Reference material: *Across the Edge* by Demarche, recorded as a full-demo TTD; ranges exported to GIF/MP4 and frames analyzed via MCP/WebAPI. Synthetic generated fixtures cover each case in isolation. |
| R-10 | Diagnostics: class-map overlay, per-frame class statistics via WebAPI, side-by-side recording. |
| R-11 | Color mixing is not hard-coded: a **mixer store** holds several mixers (sRGB mean, linear-light mean, CRT gamma, phosphor decay, perceptual OKLab, matrix), selectable and tunable per instance. |
| R-12 | Mixers load from files and are editable in the UI, down to user-entered formulas compiled at runtime (with error reporting and live preview). |
| R-13 | Default mixer and parameters are chosen by calibration against video (preferred) or photos of a real CRT; a fitting tool derives parameters. |
| R-14 | Test inputs: TTD recordings, lossless clip files (planes A + B + frame meta + optional screen memory), lossless animation exports. |
| R-15 | Synthetic generators for every case and sub-case, with ground truth (true class, true intended color, true motion); plus synthetic Z80 programs that exercise the full chain. |
| R-16 | Golden sets for real material with human-reviewed updates only; review report with side-by-side playback. |
| R-17 | Invariant, metamorphic, equivalence (scalar vs SIMD vs threads vs GPU) and fuzz tests; zero ghosting on synthetic input. |
| R-18 | Debug tooling: per-pixel decision trace and one-command replay of any failing golden frame. |
| R-19 | v1 analysis is causal (past frames only); a new flicker may be visible for up to 2 periods before blending. The classifier is built for a two-sided window so future frames plug in without redesign. |
| R-20 | All analysis results are collected into a `gigascreen` layer of the Metadata Manager (regions, classes, periods, masks, motion, border effects, scene cuts). |
| R-21 | Look-ahead (up to 10 predicted frames) comes from the shared Look-Ahead Manager, which also serves Game Mode input-lag reduction; blending then starts on the first frame of an effect. |
| R-22 | Known software can be played from pre-recorded metadata packs (hybrid: pack rules + cheap runtime verification; or pack only: runtime analysis off, rules applied at anchored times). |
| R-23 | Negative tests: ordinary double-buffered games that flip screen pages without GigaScreen (Shadow Fields, Cubix) must stay pass-through. |
| R-24 | Cost tiers: **DLSS off → zero cost** (no plane B capture, no triggers armed, no analysis). **On, software not recognized →** full runtime analysis. **On, recognized with a pack →** pack rules + trigger checks (≈ 1 % of one core for exec triggers while armed) + cheap verification instead of full analysis. |
| R-25 | **Output contract:** DLSS writes its result into the emulator's output framebuffer as if it were the emulation result. Everything downstream (GUI, videowall, recording, screenshots, default WebAPI/MCP capture) gets the corrected picture with no changes of its own. The **raw** pre-DLSS framebuffer and plane B are kept alongside (only while DLSS is on) for analysis, tests and diagnostics, and are reachable through **separate automation methods** — never by changing the meaning of existing ones. |
| R-26 | Naive implementation first, then measure; optimizations are collected in [optimization-ideas.md](optimization-ideas.md) and scheduled only on measured benefit. |
| R-27 | Automation surface (WebAPI, MCP, CLI, Lua, Python) gets dedicated methods: capture raw frame, capture plane B, capture class-map overlay, capture side-by-side, DLSS status/statistics, mixer select/parameters. Existing `capture/screen` keeps returning what the user sees (processed). |
| R-28 | All multi-frame processing is one core **Temporal Effects Manager**, switched on by one feature and configured by mode: `off`, `blend` (today's whole-frame blend, moved from the GUI), `adaptive` (koval/Xpeccy-style baseline), `dlss` (sub-modes observe / blend / lookahead / auto). Shared history, mixer store and backends. The GUI `FrameHistory` is removed after a parity test. See [temporal-effects-manager.md](temporal-effects-manager.md). |
| R-29 | SIMD: SSE (x86) and NEON (ARM) intrinsics with a plain C++ fallback, the project's usual pattern. Order: scalar reference + threads first → measure → SIMD where measured useful; every SIMD path bit-exact vs scalar. Code that is a SIMD candidate is **marked at the time it is written** with a greppable comment tag and listed in [optimization-ideas.md §SIMD candidates](optimization-ideas.md#simd-candidates). |
| R-30 | Test quality gates: **hard** (fail on any violation from day one) — zero ghosting on synthetic input, negative material equals raw, CPU paths bit-exact vs scalar, GPU within 1/255, plane B through palette equals raw, warm-up ≤ 2 periods causal and 0 with look-ahead under constant input. **Measured** (class accuracy, ΔE OKLab of mixed colors, flicker residue, class stability) — the first run sets the baseline; afterwards any regression fails; the baseline is raised only on approved improvement. |

## Open

All initial questions settled (2026-09-27). New questions are asked one at a time and recorded here.
