# TODO - ZX-bus slots (machine -> buses -> slots -> cards)

**Status:** design drafted 2026-10-03; owner decisions Q1-Q6 recorded. No code yet. PLAN row #82.
Prerequisite of the [ZX-MultiSound](../2026-10-03-zx-multisound/TODO.md).

## Documents

- [requirements.md](requirements.md): goals, glossary, functional and non-functional requirements
- [architecture.md](architecture.md): today's three port-claim mechanisms, the target model, declarations, the claim
  table with IORQGE and shadowing, `SlotManager` plan / apply, config, ownership, TTD, surfaces, performance
- [compatibility-matrix.md](compatibility-matrix.md): functions per card, card × card matrix, worked plans, built-ins
- [tdd.md](tdd.md): phases SL-0 to SL-8, tests, benchmarks
- [open-questions.md](open-questions.md): owner decisions Q1-Q6

## Remaining

- [ ] Owner review of the design (R-OP-8 decided: every slot change restarts the machine, Q6)
- [ ] SL-0 research (bus signals per machine from schematics, read-conflict rule, TTD registry key for two instances)
- [ ] SL-1 core types and the pure plan
- [ ] SL-2 port claim table, IORQGE, shadowing (A/B benchmark)
- [ ] SL-3 migrate the old dispatch rules, remove the three mechanisms one by one
- [ ] SL-4 migrate cards one by one; `[SLOTS]` + legacy key translation; `data/configs` converted
- [ ] SL-5 TTD fingerprint and session guard
- [ ] SL-6 apply by restart (model-switch path), media carried over, model switch, GS personality switch moved onto it
- [ ] SL-7 five automation surfaces + OpenAPI + Qt slot window + recipe + user doc
- [ ] SL-8 Sprinter ISA slots in the report; ZX-bus adapter as a bus host (ISA I5)
