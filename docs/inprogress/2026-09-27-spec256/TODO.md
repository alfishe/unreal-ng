# TODO — Spec256 research complete, implementation not started

**Folder status:** research/investigation delivered 2026-09-27; no code.

## Delivered

- Full technical reference of the format and the Z80_GFX lockstep execution
  model ([spec256-format-and-mechanics.md](spec256-format-and-mechanics.md)).
- Resource catalog: emulators, FPGA cores, ~47-game catalog, tools, community
  links ([spec256-resources.md](spec256-resources.md)).
- Reference-implementation survey, primarily the local zxpoly tree with
  file/line evidence ([spec256-emulator-survey.md](spec256-emulator-survey.md)).
- unreal-ng integration analysis: 10 design decisions, 4-phase plan
  (R → E1 → E2 → I), extension-point checklist with file/line refs, risks,
  open questions ([unreal-ng-integration-analysis.md](unreal-ng-integration-analysis.md)).

## Not started

Everything else: no model, config, loader, renderer, TTD or automation code.

## Decisions needed before any implementation

1. **Track A vs B**: standalone `SPEC256` model now (Phase R is ~1–2 weeks,
   low risk) vs waiting for ZX-Poly Phase 1–2 and doing Spec256 as its
   Phase 4 ([2026-09-27-zxpoly](../2026-09-27-zxpoly/) §4, PLAN #43).
   The analysis recommends: Phase R + E1 can proceed independently; E2
   should ride the ZX-Poly scheduler if that program is approved.
2. **TTD v2 V1 first** (PLAN #40) if Phase I TTD capture is in scope —
   512 KB shadow RAM needs memory regions.
3. Fixture licensing (zxpoly test ZIPs are GPL-repo assets) before any are
   committed under `testdata/`.

## Next concrete steps (when picked up)

1. Fixture day: extract zxpoly's three test archives; freeze pixel bit order
   and palette triplet order by diffing against zxpoly renders.
2. Phase R slice list is §4 of the integration analysis; extension points in
   §5.
