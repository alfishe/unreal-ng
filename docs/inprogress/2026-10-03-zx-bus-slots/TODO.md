# TODO - ZX-bus slots (machine -> buses -> slots -> cards)

**Status:** design drafted 2026-10-03; owner decisions Q1-Q7 recorded; SL-0 research done; SL-1 (reference data + pure plan engine) committed on branch `zx-bus-slots`; SL-2 (port claim table, serving the existing full-decode observers) built 2026-10-04, uncommitted. PLAN row #82.
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
- [x] SL-2 port claim table, IORQGE, shadowing (2026-10-04, working tree of `zx-bus-slots`, not committed):
  `slots/portclaimtable.{h,cpp}` (claimed-port bitmap, per-low-byte buckets in slot order, board-port bitmap,
  cycle resolution with CardWins / BoardWins / UlaOnly / None, Iorq / RdWr detection, IORQGE hiding by slot order,
  read rule, ROM-fetch lock and DOS gates through `IClaimSignals`, shadow flags for built-ins); PortDecoder's
  full-decode observers (MoonSound, ZXNETUSB, ComPort / ZX-WiFi) served from it with the old semantics (exact before
  low byte, R6, `OverrideDecodeForFullDecodeClaim` unchanged); the map + low-byte array removed; unclaimed port = one
  inline bit test; `PeripheralPortIn/Out` one `find`. Tests `PortClaimTable_Test.*` (12); full `core-tests` green.
  Benchmarks `BM_PortOut`, `BM_PortCard` added. Review round (2026-10-04): no instrumentation counter on the access
  path (a test-only scan trap proves "unclaimed never scans"), the claimed port looked up once per cycle (the Z80
  tap's lookup is memoized for the decoder's override). A/B vs SL-1, 8 interleaved rounds with the load below 12 for
  each whole round: unclaimed ports 3-14 % faster; a claimed card port still +3.7 to +5.3 % (1.2-1.9 ns per
  access). Table, loads and the variants tried: [tdd.md](tdd.md) §6
  - [ ] claimed-port residue (+3.7 to +5.3 % on the MoonSound's own port, 1.2-1.9 ns per access; < 0.05 % of a frame):
    not from the counter or the second lookup (both removed, no change); the profile puts it in the decoders'
    and `Z80::inFromBus`'s own code around the inline taps, and variants move it about as much as its size. To be
    removed with SL-3's single resolution pass (tap + R6 + override in one), re-measured with `BM_PortCard`
  - [ ] `Write` / `Read` (the full resolution) unused in production until SL-3
- [ ] SL-3 migrate the old dispatch rules, remove the three mechanisms one by one
- [ ] SL-4 migrate cards one by one; `[SLOTS]` + legacy key translation; `data/configs` converted
- [ ] SL-5 TTD fingerprint and session guard
- [ ] SL-6 apply by restart (model-switch path), media carried over, model switch, GS personality switch moved onto it
- [ ] SL-7 five automation surfaces + OpenAPI + Qt slot window + recipe + user doc
- [ ] SL-8 Sprinter ISA slots in the report; ZX-bus adapter as a bus host (ISA I5)
