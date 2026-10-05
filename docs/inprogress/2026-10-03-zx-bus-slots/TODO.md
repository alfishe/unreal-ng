# TODO - ZX-bus slots (machine -> buses -> slots -> cards)

**Status:** design drafted 2026-10-03; owner decisions Q1-Q8 recorded; SL-0 research done; SL-1 (reference data + pure plan engine) committed on branch `zx-bus-slots`; SL-2 (port claim table, serving the existing full-decode observers) committed; branch merged with master (TTD v2 engine) 2026-10-04; SL-3 (rule migration) built 2026-10-04; merged with master `e378c483a` and SL-4 (cards on slots, `[SLOTS]`, shipped configs converted) committed 2026-10-04; the first slot-built card (ZX-MultiSound, MS-4) committed 2026-10-04 (`cdac570ec`..`dcaf21a18`); SL-5 (TTD) committed on branch `slots-ttd` (`29250c546`) and merged into `zx-bus-slots` 2026-10-04; owner decision Q8 (conflicting configs refuse the machine), slot-built cards under TTD (MultiSound MS-5) and the quiet-machine A/B rerun built 2026-10-05; SL-6 (slot changes applied
by a restart, the model switch carrying the slot set, the General Sound personality on the plan) built 2026-10-05; SL-7
(the surfaces, Qt, recipe, user doc) built 2026-10-05 (not committed). PLAN row #82.
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
- [open-questions.md](open-questions.md): owner decisions Q1-Q11

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
  - [x] skipped plan tests, later phases: `RefusedWhileTtdRecords` (SL-5, built as `TtdSlots_Test.RefusedWhileTtdRecords`), `IniLoadUsesSamePlan`,
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
  - [x] owner decision 2026-10-04: no General Sound card in the shipped 48K / 128K / +2 / +2A / +3 / Profi v5, v3 /
    Sprinter configs (a comment says how to add one); golden regenerated for these eight only ([tdd.md](tdd.md) §8)
  - [ ] owner review: the per-slot fit override `<slot>.fit = unrealistic` in configs (shipped Profi SounDrive,
    zx-diagnostics NeoGS) and on every translated legacy key; `builtin.<id> = on | off`; the soundrive
    `mode=both` value (the emulator decode); the retrofitted 48K AY socket; the Profi board Covox declaration
  - [ ] owner review: `SlotManager` decides and reports, the managers still build the cards (CONFIG fields as the
    hand-over); `ICard` objects with the ZX-MultiSound (built 2026-10-04, below) / SL-6
  - [ ] the Profi's shipped `profi-bus.2 = soundrive` exists only to keep the shared Covox module four-channel as on
    master (the Profi decoder never routes the card's ports): drop it once a fitted-device change is acceptable
  - [ ] quiet-machine rerun of the SL-4 A/B (step 6 vs step 0 under load 17-34: -3.4 .. +3.7 %, no port path changed)
  - [ ] runtime card changes (`requestGeneralSoundCardSwitch`, `NetworkManager::RequestChange`) bypass the slot set
    and its report until SL-6 builds the restart path - the GS switch is planned and followed by the plan since SL-6
    ([tdd.md](tdd.md) §14); `NetworkManager::RequestChange` still bypasses it (SL-7 puts the network card change on
    `SlotChange::Run`)
  - [x] recipes / user docs still name the legacy keys (`[SOUND] GSType`, `TurboSound`, `[NETWORK] Card`): moved to
    `[SLOTS]` in SL-7 (the legacy keys are documented as translated)
  - [ ] side note (not slots): the old ts-conf line `CovoxFB=1 ; ... (#FB and the #FE beeper bit ...)` parsed as 0
    (IniFile strips the inline comment at the last `#`); the converted config says `builtin.covox = on`, same devices
- [x] Slot-built cards for the ZX-MultiSound (MultiSound MS-4, 2026-10-04, committed on `zx-bus-slots`; [tdd.md](tdd.md) §9): `ICard` / `CardType` (`slots/card.{h,cpp}`), `SlotManager::BuildCards` /
  `ReleaseCards`, the claim table's `Read` / `Write` in production for those cards (slots 2+ of the observers' table),
  `SoundManager` rows for card rows, a socketed chip taken out at creation when the INI leaves the socket unconfigured
  - [x] owner decision Q8 (2026-10-05): configured entries in conflict refuse the machine with every pair and its rule
    ([tdd.md](tdd.md) §11); the unconfigured-socket rule stays (Q7), an explicit `ay-socket = ay` under a card that
    needs the chip out is such a conflict; no shipped config fits the MultiSound
  - [ ] the three role tables still stand: legacy observers and slot-built cards share `_fullDecodeClaims`, a port
    both cover resolves by whichever claims it first (none today); the single table waits for the legacy cards' moves
  - [x] a TTD session recorded with a slot-built card captures it (MS-5, 2026-10-05, [tdd.md](tdd.md) §13): the
    card's devices registered by slot, a card without its devices refuses recording, the guard knows the card
  - [x] quiet-machine rerun of the MS-4 A/B (2026-10-05, [tdd.md](tdd.md) §12): everything at parity or faster but
    the 48K / 128K `OUT #00FF` (+1.5 to +2.1 %); one real layout change found and removed (`CONFIG::midiBank` moved
    the `EmulatorContext` fields: the bank path is in `Config` now), after which the path's machine code is
    byte-identical to A's and the rows still measure +1.5 / +1.9 %: function placement
  - [ ] owner question: accept function-placement noise of +-2 % on rows whose code is unchanged, or pin the hot path's
    layout for the whole build (function alignment / an order file) so A/B rows stop moving with unrelated code
- [x] SL-5 TTD fingerprint and session guard (2026-10-04, branch `slots-ttd` `29250c546`, merged; [tdd.md](tdd.md) §10):
  `slots.<slot>` / `slots.builtin.<id>` in the configuration fingerprint (affectsRestore); slot cards named by slot in
  the engine's device table and checked against the plan at registration; one slot-set guard on load replacing the
  TurboSound / GS guards and serving the Sprinter ISA check; the registry refuses a second device under a held id;
  `ChangeRefusal()` names the recording session (R-OP-7). Corpus, bench gate, CoreGolden unchanged
  - [ ] owner: the session id in the R-OP-7 refusal is a per-instance recording number (`#n, started at frame f`):
    v1 sessions have none; switch to the v2 session UUID with Phase 5?
  - [ ] the guard is symmetric now (a session without a card no longer loads where one is fitted): owner check
  - [ ] two instances of one module (MultiSound + GS / SAA cards): needs the v1 per-id checkpoint gone (TTD Phase 5);
    until then the planner refuses them as a conflict (Q8; `TtdMultiSound_Test.TwoInstancesOfOneModuleRefusedByThePlanner`),
    and the MultiSound's own GS records under id 60, so a GS card next to a card with its GS switched off is fine
  - [x] the GS runtime switch leaves the plan (and the fingerprint) naming the configured personality until SL-6 -
    closed by SL-6: the switch moves the plan's GS slot and the fingerprint, `TtdDevicesMatchPlan` checks the personality
- [x] SL-6 apply by restart (model-switch path), media carried over, model switch, GS personality switch moved onto it
  (2026-10-05, working tree of `zx-bus-slots`, not committed; [tdd.md](tdd.md) §14): `SlotManager::PlanChange` (the plan
  engine over the current set, dirty media, the TTD guard of R-OP-7 wired, the new `[SLOTS]` checked as creation checks
  it) and `SlotChange::Run` (`slots/slotchange.{h,cpp}`: refused / dry run / the same model rebuilt through
  `ModelSwitch` with the planned set and the instance's own create override; the old machine keeps running if the new
  one fails, e.g. a card that cannot be built - `BuildCards` refuses the machine now); `SlotManager::Carry` (a model
  switch carries the cards, re-places them behind an adapter where the bus is missing, drops and reports what the new
  machine cannot take, `ModelSwitchResult::slotCarry`); `GeneralSoundRequest` (the personality as a slot replace) and the
  running machine's frame-boundary switch planned and followed (plan + TTD fingerprint). Tests `SlotChange_Test.*` (8),
  `SlotManager_Test.Carry*` (2), `ModelSwitch_Test.CarriesTheSlotSet`,
  `TtdSlots_Test.RuntimePersonalitySwitchMovesThePlanAndTheFingerprint`; full `core-tests` green
  - [x] Q9 decided 2026-10-05 ([open-questions.md](open-questions.md)): a model switch merges the carried cards with the
    new machine's own configured cards (as built); removals are not carried
  - [x] Q10 decided 2026-10-05: the explicit personality switch on every surface moves to the restart in SL-7 (done:
    WebAPI, CLI, MCP, Lua, Python, Qt); the in-place switch stays only for the `gs_lightweight` feature
  - [ ] side note (not slots): `EmulatorStepOverObserver_Test.DestroyedEmulatorLeavesNoHandlerBehind` segfaults alone
    within 40 repeats (`BreakpointManager::GetBreakpointById` on the emulation thread while the test stops the machine);
    it cost one `core-tests` shard once during SL-6
- [x] SL-7 five automation surfaces + OpenAPI + Qt slot window + recipe + user doc (2026-10-05, working tree of
  `zx-bus-slots`, not committed; [tdd.md](tdd.md) §15): `SlotControl` behind WebAPI `/slots` (+ OpenAPI tag `Slots`),
  CLI `slots`, MCP `slots_*` / aspect `slots`, Lua / Python `slots_*`, create-time `"slots"`, the model switch's carry
  on every reply; the GS personality switch on every surface is a slot replace applied by a restart (Q10); Qt Machine >
  Slots with plan preview, confirmation, removed cards named with Undo; recipe [.recipe/machines/slots.md](../../../.recipe/machines/slots.md),
  user doc [docs/features/slots.md](../../features/slots.md); recipes and docs naming the legacy keys moved to `[SLOTS]`
  - [x] Q11 decided 2026-10-05 (A): the network card change (`network set card=`, the Network window's card boxes)
    becomes a slot change applied by a restart - done 2026-10-05 (working tree of `zx-bus-slots`, not committed;
    [tdd.md](tdd.md) §16): SlotControl verb `network` on CLI, WebAPI + OpenAPI, MCP `network_configure`, Lua, Python,
    Qt (`SlotChangeController::ApplyNetworkCards`); `NetworkManager::RequestChange` refuses a ZX-bus card change
  - [x] Python bindings verified live 2026-10-05 (separate build with `ENABLE_PYTHON_AUTOMATION=ON`, 0 warnings, 25 of
    25 checks; [tdd.md](tdd.md) §16)
  - [x] side note (not SL-7): the plan of a MultiSound removal listed `ay-socket` among the lost functions although the
    board AY comes back un-shadowed - fixed 2026-10-05 in the planner (D10 lists only what nothing offers afterwards)
  - [ ] owner question: a restart (every slot change, also the network card change) starts the machine from its
    configuration, so network settings changed at run time and not repeated in the request are lost (hosts, com_port,
    ...). Carry the running machine's `[NETWORK]` settings across every slot restart? Recommendation: yes, in
    `SlotChange::Run` for every restart (the settings are configuration, not machine state)
  - [ ] owner question: the runtime feature `network` (on / off) still unplugs and plugs the fitted cards in place,
    while the slot report keeps naming them. Keep it as a power switch (the report shows the feature state), or make it a
    slot change too? Recommendation: keep it a power switch and show "feature network off" in the slot report's state
- [ ] SL-8 Sprinter ISA slots in the report; ZX-bus adapter as a bus host (ISA I5)
