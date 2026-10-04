# TODO - ZX-bus slots (machine -> buses -> slots -> cards)

**Status:** design drafted 2026-10-03; owner decisions Q1-Q7 recorded; SL-0 research done (branch `zx-bus-slots`). No code yet. PLAN row #82.
Prerequisite of the [ZX-MultiSound](../2026-10-03-zx-multisound/TODO.md).

## Documents

- [requirements.md](requirements.md): goals, glossary, functional and non-functional requirements
- [architecture.md](architecture.md): today's three port-claim mechanisms, the target model, declarations, the claim
  table with IORQGE and shadowing, `SlotManager` plan / apply, config, ownership, TTD, surfaces, performance
- [compatibility-matrix.md](compatibility-matrix.md): compatibility and displacement: outcome codes, functions, first
  card catalog, card × card, card × machine, worked plans
- [reference-data.md](reference-data.md): the matrix as a reference data collection in the code (`core/src/emulator/slots/refdata/`)
- [research.md](research.md) (+ machines, cards): SL-0 findings and code inventory
- [tdd.md](tdd.md): phases SL-0 to SL-8, tests, benchmarks
- [open-questions.md](open-questions.md): owner decisions Q1-Q7

## Remaining

- [ ] Owner review of the design (R-OP-8 decided: every slot change restarts the machine, Q6)
- [x] SL-0 research (2026-10-03): [research.md](research.md), [research-machines.md](research-machines.md),
  [research-cards.md](research-cards.md); matrix rewritten; owner decision Q7 (MultiSound on ZX-Evo: empty socket);
  reference data design [reference-data.md](reference-data.md)
- [ ] SL-1 core types and the pure plan
- [ ] SL-2 port claim table, IORQGE, shadowing (A/B benchmark)
- [ ] SL-3 migrate the old dispatch rules, remove the three mechanisms one by one
- [ ] SL-4 migrate cards one by one; `[SLOTS]` + legacy key translation; `data/configs` converted
- [ ] SL-5 TTD fingerprint and session guard
- [ ] SL-6 apply by restart (model-switch path), media carried over, model switch, GS personality switch moved onto it
- [ ] SL-7 five automation surfaces + OpenAPI + Qt slot window + recipe + user doc
- [ ] SL-8 Sprinter ISA slots in the report; ZX-bus adapter as a bus host (ISA I5)
