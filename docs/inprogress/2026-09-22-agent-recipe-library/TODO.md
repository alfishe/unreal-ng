# TODO — Agent recipe library (2026-09-22)

**Status:** content complete and verified, **not yet committed**.

## Done

- 31 files / 4,196 lines under `.recipe/` (index + 3 common + 5 media +
  3 run + 6 analysis + 5 machines + 4 peripherals + 4 articles) — structure
  and conventions in [plan.md](plan.md). Includes the 2026-09-22 follow-up
  coverage: non-standard loader detection/tracing, physical copy-protection
  forensics, UDI image authoring, agent screenshot viewing, TTD visual
  inspection; and the 2026-09-23 growth pass: `_common/machines.md` model
  reference, per-model recipes (pentagon/scorpion/profi/atm/spectrum) and
  per-peripheral sound-card recipes (generalsound/moonsound/turbosound/
  covox-sounddrive) with branch-aware accuracy (profi/generalsound/moonsound).
- MCP-first two-section structure applied to all recipes (header note +
  `## MCP (preferred)` + `## WebAPI`); MCP call shapes re-verified against
  `mcp-tools.cpp` / `mcp-analysis.cpp`.
- Screenshot viewing enabler in code (staged): `capture_screen`/`capture_media`
  accept a save path — binary written server-side, base64 omitted; recipes
  `media/agent-screenshot-view.md` + `analysis/ttd-visual-inspection.md`
  document it.
- Endpoint/body/response accuracy verified against source
  (`emulator_api.h`, `api/*.cpp`, MCP schemas); 3 shape defects fixed in
  self-review.
- Cross-link check across the library: zero broken relative links.

## Remaining

- [ ] Review + commit `.recipe/` and this folder (one-time commit
  permission per AGENTS.md; quality gates: docs-only change — verify all
  cross-references and links are valid).
- [ ] Rescope decision: PLAN.md T3 #15 (P3-3 triage recipes) — the recipes
  half now exists here (`articles/bug-hunt-ttd.md`); the item's unique
  remainder is the tape-load ordering rule doc (G-7) and optional
  `machine_selftest`.
- [ ] Opportunistic growth per [plan.md](plan.md) "Future work":
  symbols/debugger recipes, capture/recording recipes.

## Pointers

- Plan: [plan.md](plan.md)
- Artifact: [`.recipe/README.md`](../../../.recipe/README.md) at repository root
