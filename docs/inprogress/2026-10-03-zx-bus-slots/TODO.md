# TODO - ZX-bus slots (machine -> buses -> slots -> cards)

**Status:** design drafted 2026-10-03; owner decisions Q1-Q7 recorded; SL-0 research done; SL-1 (reference data + pure plan engine) committed on branch `zx-bus-slots`; SL-2 (port claim table, serving the existing full-decode observers) committed; branch merged with master (TTD v2 engine) 2026-10-04; SL-3 (rule migration) built 2026-10-04; merged with master `e378c483a` and SL-4 (cards on slots, `[SLOTS]`, shipped configs converted) built 2026-10-04, uncommitted. PLAN row #82.
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

## Pause (owner decision 2026-10-04) - lifted

SL-3 and later waited until the `ttd-engine` branch landed on master. Done 2026-10-04: `zx-bus-slots` merged with
master (one conflict, `portin_benchmark.cpp`, both sides kept; [tdd.md](tdd.md) §7), SL-3 continued.

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
  - [x] claimed-port residue: closed by SL-3's single resolution pass (MoonSound `#C4` rows -7.0 to +0.6 % vs SL-1,
    [tdd.md](tdd.md) §7)
  - [ ] `Write` / `Read` (the full resolution) still unused in production: the legacy observers' semantics (R6, the
    dynamic read claim, the Beta-128 exception) move onto it with the card declarations in SL-4
- [x] SL-3 migrate the old dispatch rules, remove the three mechanisms one by one (2026-10-04, working tree of
  `zx-bus-slots`, not committed; [tdd.md](tdd.md) §7): (1) the card tap and R6 out of the Z80 funnel into
  `PortDecoder::ReadCycle` / `WriteCycle`; (2) the claim override's decision made once per cycle by the resolution
  (SL-2 memo removed); (3) the self-decoding devices as declared claims (`PortDevice::selfDecodingClaims`, Covox per
  fitment) in a claim table (the vector removed); (4) the exact peripheral port map as exact claims on the decoded
  port (the `std::map` removed). Each step a full green `core-tests` run; new `PortDecoder_Test.*` (4); A/B vs SL-1
  under heavy load (24-72): unclaimed ports faster on every row but the border write (parity), the card's own port
  -7.0 to +0.6 %
  - [ ] owner review: three role tables (`_fullDecodeClaims` raw / bus side, `_selfDecodingClaims` raw / after the
    board, `_peripheralClaims` decoded port) instead of one; collapse into one table as SL-4 moves the cards
  - [ ] quiet-machine rerun of the SL-3 A/B (load below 12) before the SL-4 card measurements
- [x] SL-4 migrate cards one by one; `[SLOTS]` + legacy key translation; `data/configs` converted (2026-10-04,
  working tree of `zx-bus-slots`, not committed; [tdd.md](tdd.md) §8): (0) merge of master `e378c483a`; (1)
  `SlotManager` plans the slot set at creation (`[SLOTS]`, legacy keys translated with a deprecation line, first wins,
  `DeviceState::Slots`); (2) AY socket (`ay` / `ts` / `tsfm` / `none`; the 48K's AY socket retrofitted); (3) `gs` /
  `gs-lw` / `neogs` + `ram`; (4) `moonsound` (refused on the Profi: `#7E` palette); (5) `covox-fb` / `soundrive`
  (`mode` 1 / 2 / `both` reaches the Covox decode; the board Covox switchable, `builtin.covox`); (6) `zxnetusb` /
  `zx-wifi`; (7) the 17 shipped configs carry `[SLOTS]`. Every step a full green `core-tests` run; the shipped
  machines fit exactly master's devices (`SlotManagerShipped_Test`, golden `testdata/slots/fitted-devices.txt`)
  - [ ] owner review: the per-slot fit override `<slot>.fit = unrealistic` in configs (shipped Sinclair / Profi /
    Sprinter NeoGS, Profi SounDrive) and on every translated legacy key; `builtin.<id> = on | off`; the soundrive
    `mode=both` value (the emulator decode); the retrofitted 48K AY socket; the Profi board Covox declaration
  - [ ] owner review: `SlotManager` decides and reports, the managers still build the cards (CONFIG fields as the
    hand-over); `ICard` objects with the ZX-MultiSound / SL-6
  - [ ] the Profi's shipped `profi-bus.2 = soundrive` exists only to keep the shared Covox module four-channel as on
    master (the Profi decoder never routes the card's ports): drop it once a fitted-device change is acceptable
  - [ ] quiet-machine rerun of the SL-4 A/B (step 6 vs step 0 under load 17-34: -3.4 .. +3.7 %, no port path changed)
  - [ ] runtime card changes (`requestGeneralSoundCardSwitch`, `NetworkManager::RequestChange`) bypass the slot set
    and its report until SL-6 builds the restart path
  - [ ] recipes / user docs still name the legacy keys (`[SOUND] GSType`, `TurboSound`, `[NETWORK] Card`): SL-7
  - [ ] side note (not slots): the old ts-conf line `CovoxFB=1 ; ... (#FB and the #FE beeper bit ...)` parsed as 0
    (IniFile strips the inline comment at the last `#`); the converted config says `builtin.covox = on`, same devices
- [ ] SL-5 TTD fingerprint and session guard
- [ ] SL-6 apply by restart (model-switch path), media carried over, model switch, GS personality switch moved onto it
- [ ] SL-7 five automation surfaces + OpenAPI + Qt slot window + recipe + user doc
- [ ] SL-8 Sprinter ISA slots in the report; ZX-bus adapter as a bus host (ISA I5)
