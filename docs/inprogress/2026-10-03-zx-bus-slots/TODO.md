# TODO - ZX-bus slots (machine -> buses -> slots -> cards)

**Status:** design drafted 2026-10-03; owner decisions Q1-Q7 recorded; SL-0 research done; SL-1 (reference data + pure plan engine, not wired into the emulator) built 2026-10-03, uncommitted on branch `zx-bus-slots`. PLAN row #82.
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
- [x] SL-1 core types and the pure plan (2026-10-03, working tree of `zx-bus-slots`, not committed): vocabulary
  (`slotvocabulary.{h,cpp}`), aggregates (`slottypes.h`), the reference data collection (`refdata/`: 63 sources, 12
  cards of the first catalog, one `MachineDef` per creatable model - 15 -, 3 adapters, 1 exception), the plan engine
  (`slotplanner.{h,cpp}`, rules D1-D12), the matrix generator (`slotmatrix.{h,cpp}`); compatibility-matrix.md §1-§4
  now generated between `slots:generated` markers. Tests: `RefData_Test.CollectionIsConsistent`,
  `SlotPlanner_Test.*` (WorkedPlans A-H + the §2.1 plan tests), `SlotMatrix_Test.*`. As-built notes and deviations:
  [tdd.md](tdd.md) §5. Files under `core/src/emulator/slots/` and `core/tests/emulator/slots/`
  - [ ] owner review of the SL-1 data choices (tdd.md §5, deviations 3-5)
  - [ ] skipped plan tests, later phases: `RefusedWhileTtdRecords` (SL-5), `IniLoadUsesSamePlan`,
    `LegacyKeysTranslated` (SL-4)
- [ ] SL-2 port claim table, IORQGE, shadowing (A/B benchmark)
- [ ] SL-3 migrate the old dispatch rules, remove the three mechanisms one by one
- [ ] SL-4 migrate cards one by one; `[SLOTS]` + legacy key translation; `data/configs` converted
- [ ] SL-5 TTD fingerprint and session guard
- [ ] SL-6 apply by restart (model-switch path), media carried over, model switch, GS personality switch moved onto it
- [ ] SL-7 five automation surfaces + OpenAPI + Qt slot window + recipe + user doc
- [ ] SL-8 Sprinter ISA slots in the report; ZX-bus adapter as a bus host (ISA I5)
