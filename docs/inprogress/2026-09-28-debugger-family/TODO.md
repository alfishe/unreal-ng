# TODO — unreal-ng debugger family (2026-09-28)

**Status:** proposal drafted, awaiting review. No code.

Product and strategy layer over the [debugger model](../2026-09-28-debugger-model/README.md)
(one model, one protocol, many skins), built from the
[emulator debugger survey](../2026-09-28-emulator-debugger-survey/README.md).

## Documents
1. [use-cases.md](use-cases.md) — roles, operations, flows; today vs target; what is specified.
   [use-cases-extended.md](use-cases-extended.md) — 15 roles beyond debugging (party organizers, streamers, composers, archivists, …) with their essential needs.
2. [comparative-analysis.md](comparative-analysis.md) — best / most convenient / most striking per category; scoreboard.
3. [proposition.md](proposition.md) — delivery options with pros/cons; one engine, one app with workspaces, satellites; MVP; north star.
4. [roadmap.md](roadmap.md) — prioritization rules, phases 0-4, deliverables, proposed PLAN.md rows #62-#71.
5. [workbench-framework.md](workbench-framework.md) — the shell between an IDE and a video editor: panels, workspaces, timeline, monitors and scopes, windows on several monitors, modular MVP, Lua/Python plug-ins, toolkit comparison, delivery tiers, mobile companions.

6. [ttd-offline-analysis.md](ttd-offline-analysis.md) — record cheap, analyze without limits: what exists, requirements OR-1…OR-21, constraints, architecture (recording library, replay workers, analyzer host, result cache), tracking O-1…O-21 with priorities.
7. [rzx-ttd.md](rzx-ttd.md) — research article: the RZX format (verified), its semantics, security, implementations in 12 emulators, ecosystem and SZX; side-by-side with TTD; export, import, what TTD borrows; full references.

Knowledge base: separate project [ZX-meta-db](../2026-09-28-zx-meta-db/TODO.md).

## Next
- [x] Delivery, MVP scope and knowledge-base decisions taken (2026-09-28, proposition §12).
- [ ] Decide the tier-1 toolkit, docking library and plug-in UI forms (workbench-framework §13).
- [ ] Merge the Spectaculator, ZXSpin and unreal-ng self-survey results into the comparative analysis (§20).
- [ ] Apply the approved PLAN.md rows.
- [ ] Turn MVP items into requirement lists and designs (one folder each, or sections of the debugger model).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — rows proposed in roadmap §8; related #6, #9, #23, #42, #45, #46, #49, #51.
