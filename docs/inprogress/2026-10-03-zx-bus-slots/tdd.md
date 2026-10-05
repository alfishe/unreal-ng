# ZX-bus slots: test-driven plan

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft for owner review |
| **Design** | [requirements.md](requirements.md), [architecture.md](architecture.md), [compatibility-matrix.md](compatibility-matrix.md) |
| **Decisions** | [open-questions.md](open-questions.md) Q1-Q7 |
| **Order** | owner decision Q3: core first, then existing cards one by one, then the ZX-MultiSound |
| **Effort scale** | S < 1 week, M 1-2 weeks, L 2-4 weeks |

## 1. Phases

| Phase | Content | Exit criterion | Size |
|---|---|---|---|
| **SL-0** | Research: bus kinds and signals per machine from schematics (IORQGE, /IODOS, +12 V, edge connector variants, Scorpion / Profi buses), read-conflict rule per bus, TTD registry key for two instances of one module (engine branch), the list of every place a card is chosen today | `research.md` with a sourced table per machine; open points listed in open-questions | M |
| **SL-1** | Core types and the reference data collection ([reference-data.md](reference-data.md)): vocabulary, `CardDef` / `BusDef` / `BuiltInDef` / `MachineDef` / `AdapterDef` / `ExceptionDef`, the first card catalog, one `MachineDef` per creatable model, the pure plan engine `SlotPlanner::Plan`, the matrix generator (data only, no behavior change). **Done 2026-10-03**, see §5 | plan tests §2.1 green; no production path uses it yet | M |
| **SL-2** | `PortClaimTable` with IORQGE and passive claims, shadowing, read-conflict rule, ROM-fetch lock; wired into the Z80 funnel behind the existing mechanisms (they register into it) | claim tests §2.2 green; all of `core-tests` green; A/B benchmark: no regression on the port hot path, the no-card case at least as fast |L; **built 2026-10-04**, see §6 |
| **SL-3** | Rule migration: R6, `OverrideDecodeForFullDecodeClaim`, self-decoding dispatch, the exact port map's peripheral entries become claims; the three old mechanisms removed one at a time | each removal its own merge with the TTD corpus and machine boot tests green | L; **built 2026-10-04**, see §7 |
| **SL-4** | Card migration, one per merge: `ay`/`ts`/`tsfm` (socket), `gs`/`gs-lw`/`neogs`, `moonsound`, `covox-fb`/`soundrive`, `zxnetusb`/`zx-wifi`; `[SLOTS]` config + legacy key translation; `data/configs` converted | per card: its existing tests unchanged and green, A/B on its port path, TTD fixtures unchanged | L; **built 2026-10-04**, see §8 |
| **SL-5** | TTD: slot set in the configuration fingerprint, `SlotManager` as the source of fitted devices in `RegisterMachinePeripherals`, the session population guard generalized (from the Sprinter's `TtdSessionMatches`) | TTD tests §2.4 green; corpus unchanged | M; **built 2026-10-04**, see §10 |
| **SL-6** | Apply by restart: write the slot set into the configuration, restart through the model-switch path, media carried over with the stranded-media rules, restore of the previous configuration if the start fails; model switch carrying the slot set; the GS personality switch moved onto it | tests §2.3 green | M; **built 2026-10-05**, see §14 |
| **SL-7** | Surfaces: `SlotControl`, `DeviceState::Slots`, WebAPI + OpenAPI, CLI, MCP, Lua, Python, Qt slot window, recipe `.recipe/machines/slots.md`, user doc `docs/features/slots.md` | automation parity tests §2.5 green; recipe verified against a running emulator | M; **built 2026-10-05**, see §15 |
| **SL-8** | Sprinter: `isa1` / `isa2` in the slot report, the ZX-bus adapter as a `zxbus` host (ISA phase I5 folded in) | Sprinter ISA tests unchanged; a GS card behind the adapter listed as `zxbus` in the report | M |

The ZX-MultiSound ([integration TDD](../2026-10-03-zx-multisound/tdd-integration.md)) starts after SL-4 (it needs the
migrated TSFM, GS and SounDrive) and SL-5 for its TTD step.

## 2. Tests

All in `core-tests`, under 50 ms each, files named after the source file under test.

### 2.1 Plan (`slotmanager_test.cpp`)

As built in SL-1 the pure plan lives in `slotplanner.{h,cpp}`, so its tests are `SlotPlanner_Test.*` in
`core/tests/emulator/slots/slotplanner_test.cpp` (test file named after the file under test); the names below map
one to one, except as noted in §5.

| Test | Checks |
|---|---|
| `SlotManager_Test.PlugIntoEmptySlot` | compatible card: allowed, nothing removed |
| `SlotManager_Test.FunctionClashRefusedByDefault` | GS present, plug NeoGS without the flag: refused, plan lists `gs` |
| `SlotManager_Test.FunctionClashReplacedWithFlag` | same with `replaceIfIncompatible`: GS removed, options returned |
| `SlotManager_Test.OneCardRemovesSeveral` | matrix example A: three removals, one shadowing, lost `covox-fb` |
| `SlotManager_Test.OptionsAvoidClash` | example B |
| `SlotManager_Test.SetOptionsRunsSamePlan` | example C, incl. the dirty `sd.ngs` refusal |
| `SlotManager_Test.ReplacingWithLessReportsLostFunctions` | example D |
| `SlotManager_Test.BusFitNeedsAdapterOrOverride` | example E: refused / adapter / `unrealistic` |
| `SlotManager_Test.FixedBuiltInBlocksEvenWithFlag` | `moonsound` on Profi: refused with the palette reason |
| `SlotManager_Test.SwitchableBuiltInSwitchedOff` | a switchable built-in is reported as switched off |
| `SlotManager_Test.AccidentalPortClashDisablesLater` | two cards with overlapping ports not covered by functions: the later one disabled, plan allowed |
| `SlotManager_Test.DryRunChangesNothing` | `dryRun` returns the plan, state unchanged |
| `SlotManager_Test.RefusedWhileTtdRecords` | recording: refused with the session id |
| `SlotManager_Test.IniConflictRefusesCreation` | an INI with a clash: the machine is not created, the reason lists every conflicting pair and its rule (Q8; was `IniLoadUsesSamePlan`, first wins) |
| `SlotManager_Test.LegacyKeysTranslated` | `[SOUND] GSType=NGS`, `TurboSound=FM`, `[NETWORK] Card=ZXNETUSB` -> `[SLOTS]` entries + deprecation log |
| `SlotManager_Test.MatrixGeneratedFromDeclarations` | the generated matrix equals the reviewed table in compatibility-matrix.md §2 (the table is kept as test data) |

### 2.2 Claim table (`portclaimtable_test.cpp`)

| Test | Checks |
|---|---|
| `PortClaimTable_Test.UnclaimedPortSkipsTable` | no claim: machine decode only, one bit test (instrumented counter) |
| `PortClaimTable_Test.IorqgeSuppressesMachineDecode` | IN and OUT on an IORQGE port never reach the machine |
| `PortClaimTable_Test.PassiveWriteReachesBoth` | `#DFFD` write: card and machine paging both latch |
| `PortClaimTable_Test.PassiveReadWiredAnd` | two drivers on a read combine by the bus rule |
| `PortClaimTable_Test.ShadowedBuiltInSilent` | a shadowed built-in AY gets no writes and its row reports `shadowed` |
| `PortClaimTable_Test.RomFetchLock` | locked claims ignored after an M1 in `#0000-#3FFF` |
| `PortClaimTable_Test.RebuildOnlyOnSlotChange` | no allocation on the access path (as built: `RebuildOnlyOnStart`) |
| Existing `fulldecodeclaim_test.cpp`, `portdecoder_*_test.cpp` | unchanged and green through SL-3 |

### 2.3 Apply (`slotmanager_apply_test.cpp`)

As built in SL-6 (§14) the tests are named after the files under test: `SlotChange_Test.*` in
`core/tests/emulator/slots/slotchange_test.cpp`, the model switch in `modelswitch_test.cpp` and `slotmanager_test.cpp`,
the running machine's personality switch in `slotttd_test.cpp`.

| Test | Checks |
|---|---|
| `SlotManagerApply_Test.ChangeRestartsMachine` | an allowed plug restarts the instance with the new slot set; no card object is created in the running machine (built: `SlotChange_Test.PlugRestartsTheMachineWithTheNewSet`, `DisplacementNeedsTheReplaceFlag`, `DryRunChangesNothingAndOptionsRestartToo`, `RequestsThatCannotApplyAreRefused`) |
| `SlotManagerApply_Test.FailedStartRestoresPreviousConfig` | a card factory throws at start: the previous configuration is started again, the reply carries the error (built: `SlotChange_Test.FailedStartKeepsTheMachine`; the new machine is created next to the old one, so the old one never stops) |
| `SlotManagerApply_Test.MediaCarriedAcrossRestart` | media in kept cards survive the restart; removed NeoGS: `sd.ngs` reported as stranded, dirty medium refused without a disposition (built: `SlotChange_Test.MediaCarriedAcrossTheRestart`) |
| `SlotManagerApply_Test.GsPersonalitySwitchIsSlotReplace` | the GS personality switch goes through the plan and a restart (built: `SlotChange_Test.GsPersonalitySwitchIsSlotReplace`, and for the running machine's frame-boundary switch `TtdSlots_Test.RuntimePersonalitySwitchMovesThePlanAndTheFingerprint`) |
| `SlotManagerApply_Test.ModelSwitchCarriesSlots` | Pentagon -> ZX-Evo keeps fitting cards; ZX-Evo -> 128K reports the MultiSound as not fitting (built: `ModelSwitch_Test.CarriesTheSlotSet`, `SlotManager_Test.CarryKeepsTheCardsTheNewMachineTakes`, `CarryReportsTheCardsTheNewMachineCannotTake`) |
| `SlotManagerApply_Test.RefusedWhileTtdRecords` | added in SL-6: the change API refuses while a user recording runs, naming the session (built: `SlotChange_Test.RefusedWhileTtdRecords`) |

### 2.4 TTD (`slotttd_test.cpp`, after `slots/slotttd.cpp`)

| Test | Checks |
|---|---|
| `TtdSlots_Test.FingerprintHasSlotSet` | slot set and options in the session fingerprint |
| `TtdSlots_Test.SessionMismatchRefused` | load a session into a machine with another slot set: refused, difference listed |
| `TtdSlots_Test.MigratedCardsKeepBlobIds` | blob ids and layouts unchanged after SL-4 (corpus round-trip) |
| `TtdSlots_Test.SessionMismatchListsEveryDifference` | the guard's decision per position (socket kinds both ways, empty socket, both socket ids, GS personality incl. the not-recorded lightweight card, MoonSound on one side) |
| `TtdSlots_Test.RegistryFollowsThePlan` | the registered slot-card devices against the plan |
| `TtdSlots_Test.TwoInstancesOfOneModuleAreRefusedByName` | two devices under one blob id: the second refused and named, recording refused |
| `TtdSlots_Test.RefusedWhileTtdRecords` | R-OP-7: a slot change refused while recording, the reason names the session (§2.1's `SlotManager_Test.RefusedWhileTtdRecords`) |

### 2.5 Surfaces

One parity test per surface (WebAPI, CLI, Lua, Python, MCP) running the same script: list, catalog, matrix, refused
plug (409 / error with plan), plug with the flag, options change, remove; replies compared field by field with the core
plan. Qt: a widget test for the plan preview and the Undo action (`unreal-qt-tests`).

## 3. Benchmarks

`core-benchmarks`: `BM_PortIn_NoCard`, `BM_PortOut_NoCard`, `BM_PortIn_ClaimedIorqge`, `BM_PortOut_Passive` on
Pentagon and ZX-Evo, before (master) and after each of SL-2, SL-3 and every SL-4 card; interleaved runs with the load
check (`vm.loadavg` below 12, two runs), results in the folder TODO.

## 4. Documentation kept current

Each phase updates [TODO.md](TODO.md), PLAN row #82, the machine folders' TODOs where a machine declaration lands,
and the user doc and recipe from SL-7 on.

## 5. SL-1 as built (2026-10-03)

**Files** (no production code path includes them yet):

| File | Content |
|---|---|
| `core/src/emulator/slots/slotvocabulary.{h,cpp}` | the enums and one id + description per value |
| `core/src/emulator/slots/slottypes.h` | the aggregates, `Src` ids, `Collection` |
| `core/src/emulator/slots/refdata/{sources,cards,machines,adapters,exceptions,refdata}.cpp`, `refdata.h` | the collection: 63 sources, 12 cards, 15 machines, 3 adapters, 1 exception |
| `core/src/emulator/slots/slotplanner.{h,cpp}` | `SlotPlanner::Plan`, option parse / format helpers, `ToText` |
| `core/src/emulator/slots/slotmatrix.{h,cpp}` | `RenderMatrixTable()`: compatibility-matrix.md §1-§4 |
| `core/tests/emulator/slots/refdata/refdata_test.cpp` | `RefData_Test.CollectionIsConsistent` |
| `core/tests/emulator/slots/slotplanner_test.cpp` | `SlotPlanner_Test.*`: `WorkedPlans` (matrix §6 A-H) and the §2.1 plan tests |
| `core/tests/emulator/slots/slotmatrix_test.cpp` | `SlotMatrix_Test.MatrixMatchesDocs`, `EveryTableIsWellFormed` |
| `sprinterisolation_test.cpp`, `tsconfisolation_test.cpp` | `refdata/machines.cpp` added to the model-id registration surface (it names `MM_SPRINTER` / `MM_TSL`) |

**Tests of §2.1:** implemented `PlugIntoEmptySlot`, `FunctionClashRefusedByDefault`, `FunctionClashReplacedWithFlag`,
`OneCardRemovesSeveral`, `OptionsAvoidClash`, `SetOptionsRunsSamePlan` (incl. the dirty `sd.ngs` refusal),
`ReplacingWithLessReportsLostFunctions`, `BusFitNeedsAdapterOrOverride` (example G plus 48K adapter, ATM CPU-socket
adapter, Scorpion without +12 V), `FixedBuiltInBlocksEvenWithFlag`, `SwitchableBuiltInSwitchedOff`,
`AccidentalPortClashDisablesLater`, `DryRunChangesNothing`; `MatrixGeneratedFromDeclarations` is
`SlotMatrix_Test.MatrixMatchesDocs` (all of §1-§4, not only §2). Added: `WorkedPlans`,
`ResultDoesNotDependOnInsertionOrder`, `DisplacementIsOneStep`, `SocketBoardOnBoardWinsMachine`,
`BoardPortsAreDeadForIorqCards`, `RemoveReleasesMedia`, `MalformedRequestsRefused`, `OptionsParseAndFormat`.
**Skipped (later phases):** `RefusedWhileTtdRecords` (SL-5: the TTD session guard; built in SL-5 as `TtdSlots_Test.RefusedWhileTtdRecords`, §10), `IniLoadUsesSamePlan` and
`LegacyKeysTranslated` (SL-4: `[SLOTS]` and legacy key translation; built in SL-4 as `SlotManager_Test.*` in
`core/tests/emulator/slots/slotmanager_test.cpp`, §8).

**Deviations from the design, with the reason:**

1. **No `DescribeBuses()` on the decoders**: machine declarations live in `refdata/machines.cpp` keyed by `MEM_MODEL`
   (architecture.md §3.1 updated). One source, usable before a decoder exists (model switch), and the docs are
   generated from it.
2. **`SlotPlanner` instead of `SlotManager::Plan`**: the pure engine is its own class over a `Collection` (the real
   one by default; tests pass their own cards and machines). `SlotManager` (SL-6) will own the running slot set and
   call it. `ICard`, `CardType::Create` and the card catalog's code side come with the cards (SL-4); SL-1 is data only.
3. **Rules made precise** (reference-data.md §5 updated): D4 refuses only when *every* claim of an `Iorq` card is a
   hidden board port, otherwise the card is allowed and the dead ports are reported (`partly dead`, as the matrix §4
   always showed for SounDrive on the ZX-Evo); D6 / D2 shadow a built-in only when a card's IORQGE claim covers the
   built-in's documented port (`PortClaim::port`), so a mirror (MoonSound `#7E` vs the ULA's A0 decode) is not a
   shadow; D7 counts only read overlaps (a write both receive is co-reception); D12 is generalized to every case where
   nothing silences either side of a read (a socketed chip is taken out, a socket board is displaced as pointless,
   otherwise a bus fight with fit `unrealistic`); D8 is computed before D4's board-port check because an adapter
   decides the arbitration.
4. **The Evo / TS-Conf board Covox and the Sprinter built-ins hold no exclusive function**: on the Baseconf a Covox
   card and the board Covox both play; on TS-Conf the card's `#FB` is dead by the board-port rule (D4), which also
   gives SounDrive mode 2 its `partly dead` instead of a refusal.
5. **Machine declarations follow one real board per model**: `SCORPION` = the yellow ZS-256 (no +12 V on the slot),
   `PROFSCORP` = Turbo+ (2 slots, +12 V); `PENTAGON` = the 1024SL v2.2 class; `PLUS2` = `UlaOnly` (research §1);
   `PROFI3` has no palette.

## 6. SL-2 as built (2026-10-04)

**Files:**

| File | Content |
|---|---|
| `core/src/emulator/slots/portclaimtable.{h,cpp}` | `PortClaimTable`: `claimedBits` (one bit per port), per-low-byte buckets of `ClaimEntry {mask, match, dir, iorqge, gate, lockedOnRomFetch, detection, slot, owner}` sorted by slot order, board-port bitmap (`BoardWins`), the cycle resolution of architecture.md §4.3 (`Write` / `Read` with a board callback, `BoardCycle {Full, UlaSilenced, Hidden}`, `ReadResult` with the read rule and the bus-fight flag), shadow flags for built-ins, ROM-fetch lock and DOS gates read through `IClaimSignals` only when an entry needs them, `BuildCount()` and the test-only scan trap (`SetScanTrapForTests`: a claim of a trap device at the head of every bucket, bitmap untouched, so a test sees any bucket scan; no counter on the access path) |
| `core/src/emulator/ports/portdecoder.{h,cpp}` | the full-decode observers live in the table (`_fullDecodeClaims`; the `_fullDecodeDevices` map and the low-byte array are gone): exact observers in slot 0, low-byte observers in slot 1, so `FirstMatch` keeps the old lookup order; `NotifyFullDecodeIn/Out` and `OverrideDecodeForFullDecodeClaim` are inline with one bit test for an unclaimed port, the claimed half out of line; the Z80 tap's lookup of a claimed port is memoized for the override of the same cycle (one lookup per claimed cycle, the memo dropped when the claims change); R6 (in `Z80::inFromBus`) and the override rule unchanged; `PeripheralPortIn/Out` do one `find` instead of `key_exists` + `at` |
| `core/src/emulator/cpu/z80.cpp` | comments only (the taps are inline now) |
| `core/src/emulator/slots/{slotvocabulary,slottypes}.h` | guard the Qt `slots` / `signals` macros (`push_macro` / `pop_macro`): the headers now reach Qt translation units through `portdecoder.h` |
| `core/tests/emulator/slots/portclaimtable_test.cpp` | `PortClaimTable_Test.*` (12 tests) |
| `core/benchmarks/emulator/portin_benchmark.cpp` | `BM_PortOut` (48K / 128K / Pentagon x #00FF / #40FF / #00FE) and `BM_PortCard` (Pentagon / ATM3 x no card / MoonSound x IN / OUT x #00FD unclaimed / #00C4 card port) |

**Tests of §2.2:** `UnclaimedPortSkipsTable` (+ `UnclaimedPortSkipsTableInPortDecoder`: the Z80 taps of a real
decoder never scan for an unclaimed port), `IorqgeSuppressesMachineDecode` (CardWins hides, None leaves the board,
UlaOnly silences the ULA), `PassiveWriteReachesBoth` (#DFFD), `PassiveReadWiredAnd` (WiredAnd, CardOverUla, SlotOrder,
nobody -> floating bus), `ShadowedBuiltInSilent` (Pentagon AY shadowed by the MultiSound, not on the ZX-Evo),
`RomFetchLock`, `BoardWinsHidesBoardPortsFromIorqCards` (incl. a DOS-gated board port), `RdWrCardSeesBoardPorts`,
`RebuildOnlyOnStart` (no rebuild and no storage move on the access path); added `IorqgeHidesLaterSlots` and
`LegacyObserverOrderAndRegistration`. The tests drive the table with the SL-1 reference data (Pentagon / ATM3 / 48K /
128K buses, the `multisound`, `zxnetusb` and `soundrive` cards).

**Not in SL-2 (by design):** no machine uses `Write` / `Read` yet; the exact port map, the self-decoding dispatch,
R6 and `OverrideDecodeForFullDecodeClaim` move into the resolution in SL-3; no card is slot-created (SL-4).

**Deviations** (architecture.md §4.2 updated): buckets sorted by slot order only (not "IORQGE first"); owners are
`PortDevice*` until `ICard` exists; claims carry their `Gate`; `SlotOrder` reads put the board first; the table is
rebuilt on every observer registration change (control path), not only at machine start, because the existing
observers register at attach and at a network refit.

**Benchmarks, final code** (review round 2026-10-04: the instrumentation counter is off the access path, the
claimed port is looked up once per cycle). `BM_PortCard` and four unclaimed `BM_PortIn` / `BM_PortOut` rows; A = SL-1
`186f10087` (detached worktree, Release, `core-benchmarks` target built explicitly), B = the SL-2 working tree;
`UNREAL_NICE=0`, `--benchmark_min_time=0.3s`; eight interleaved rounds A B A B B A B A. A round counts only if the
1-minute load stayed below 12 for the whole round (sampled every 5 s; a round that crossed it was discarded and run
again - one was). Load per counted round, start / max: 1 A 11.96 / 11.96, 2 B 10.45 / 10.73, 3 A 10.08 / 11.00,
4 B 11.17 / 11.17, 5 B 9.80 / 11.74, 6 A 11.50 / 11.50, 7 B 11.04 / 11.11, 8 A 10.25 / 11.03. CPU time per block of
1000 `IN A,(C)` / `OUT (C),A`, median of four rounds per side, spread = (max - min) / median:

| Benchmark | A median us | B median us | B/A | A spread | B spread |
|---|--:|--:|--:|--:|--:|
| 48K IN #00FF | 48.0 | 41.5 | -13.7 % | 1 % | 1 % |
| PENTAGON IN #00FF | 49.4 | 44.5 | -10.0 % | 1 % | 2 % |
| 48K OUT #00FF | 32.8 | 30.8 | -6.2 % | 1 % | 1 % |
| PENTAGON OUT #00FF | 33.0 | 31.0 | -6.0 % | 1 % | 1 % |
| PENTAGON no card IN #00FD | 30.9 | 28.8 | -6.8 % | 2 % | 1 % |
| ATM3 no card IN #00FD | 32.3 | 30.1 | -6.8 % | 3 % | 1 % |
| PENTAGON MoonSound IN #00FD | 30.8 | 28.7 | -6.7 % | 1 % | 1 % |
| ATM3 MoonSound IN #00FD | 32.4 | 30.0 | -7.4 % | 1 % | 0 % |
| PENTAGON no card OUT #00FD | 43.6 | 40.1 | -8.0 % | 1 % | 0 % |
| ATM3 no card OUT #00FD | 46.9 | 44.8 | -4.6 % | 1 % | 1 % |
| PENTAGON MoonSound OUT #00FD | 43.7 | 39.9 | -8.7 % | 4 % | 2 % |
| ATM3 MoonSound OUT #00FD | 46.3 | 44.8 | -3.3 % | 2 % | 3 % |
| PENTAGON no card IN #00C4 | 30.8 | 29.1 | -5.2 % | 3 % | 0 % |
| ATM3 no card IN #00C4 | 33.4 | 31.4 | -6.1 % | 1 % | 1 % |
| PENTAGON MoonSound IN #00C4 | 38.5 | 40.3 | +4.7 % | 2 % | 1 % |
| ATM3 MoonSound IN #00C4 | 36.9 | 38.8 | +5.3 % | 2 % | 1 % |
| PENTAGON no card OUT #00C4 | 43.4 | 40.1 | -7.5 % | 1 % | 1 % |
| ATM3 no card OUT #00C4 | 33.9 | 31.4 | -7.4 % | 0 % | 1 % |
| PENTAGON MoonSound OUT #00C4 | 32.8 | 34.0 | +3.7 % | 0 % | 1 % |
| ATM3 MoonSound OUT #00C4 | 32.3 | 33.6 | +3.8 % | 1 % | 1 % |

- **No card claims the port:** B is faster everywhere, 3-14 %: the always-empty `std::map::find` and the
  out-of-line call are gone (one inline bit test).
- **A claimed port** (the MoonSound's own `#C4`): **+3.7 % to +5.3 %, about 1.2-1.9 ns per access - not closed.** The
  counter and the second lookup were not its cause: the first measurement (counter in the lookup, two lookups) gave
  +1.7 to +5.7 %, the final one +3.7 to +5.3 %. Further variants, each measured as a probe, moved it by about as much
  as the residue itself in either direction: two near-identical forms of the out-of-line memo check (a valid flag vs a sentinel port:
  Pentagon OUT #C4 34.0 vs 36.6 us), the lookup arrays on the heap so the decoder object stays compact (a strict 8-round run: claimed +3.7 to
  +5.4 %, unclaimed ATM3 rows only -0.5 to -3 %, so reverted). A sample profile of the claimed IN on the Pentagon
  puts the extra time in `PortDecoder_Pentagon128::DecodePortIn` and `Z80::inFromBus` themselves (the code
  around the inline taps), not in the table lookup. The per-cycle claimed path does the same work as before: one
  out-of-line tap, one lookup, two virtual calls, one out-of-line override. A machine pays this only on its card's
  ports, at most hundreds of accesses per frame (< 0.05 % of a 1.1-1.9 ms host frame). SL-3 replaces the tap, R6 and
  the override with one resolution pass, which is the place to remove it.

The first SL-2 measurement (before the review; load 8.9-11.6 at the start of each round, up to 15.8 at the end; all
34 rows):

| Benchmark | A median us | B median us | B/A | A spread | B spread |
|---|--:|--:|--:|--:|--:|
| 48K IN #00FF | 47.9 | 41.2 | -14.0 % | 1 % | 1 % |
| 128k IN #00FF | 55.3 | 48.8 | -11.6 % | 0 % | 1 % |
| PENTAGON IN #00FF | 49.2 | 44.2 | -10.2 % | 1 % | 1 % |
| 48K IN #40FF | 56.4 | 54.3 | -3.7 % | 1 % | 3 % |
| 128k IN #40FF | 58.8 | 56.1 | -4.6 % | 1 % | 3 % |
| PENTAGON IN #40FF | 49.5 | 44.4 | -10.2 % | 1 % | 3 % |
| 48K IN #00FE | 55.8 | 49.9 | -10.6 % | 3 % | 3 % |
| 128k IN #00FE | 61.9 | 52.4 | -15.5 % | 2 % | 1 % |
| PENTAGON IN #00FE | 47.3 | 42.9 | -9.4 % | 0 % | 0 % |
| 48K OUT #00FF | 32.5 | 30.6 | -5.9 % | 1 % | 1 % |
| 128k OUT #00FF | 31.7 | 29.8 | -6.0 % | 2 % | 1 % |
| PENTAGON OUT #00FF | 33.0 | 31.0 | -6.1 % | 1 % | 0 % |
| 48K OUT #40FF | 41.4 | 40.1 | -3.0 % | 1 % | 0 % |
| 128k OUT #40FF | 40.0 | 39.0 | -2.6 % | 0 % | 0 % |
| PENTAGON OUT #40FF | 33.0 | 31.0 | -5.9 % | 0 % | 1 % |
| 48K OUT #00FE | 70.3 | 69.7 | -0.8 % | 1 % | 2 % |
| 128k OUT #00FE | 73.4 | 72.1 | -1.7 % | 1 % | 3 % |
| PENTAGON OUT #00FE | 43.0 | 41.9 | -2.6 % | 1 % | 4 % |
| PENTAGON no card IN #00FD | 30.7 | 28.4 | -7.6 % | 0 % | 1 % |
| ATM3 no card IN #00FD | 32.3 | 30.1 | -7.0 % | 1 % | 2 % |
| PENTAGON MoonSound IN #00FD | 30.8 | 28.5 | -7.4 % | 0 % | 0 % |
| ATM3 MoonSound IN #00FD | 32.4 | 30.0 | -7.3 % | 1 % | 0 % |
| PENTAGON no card OUT #00FD | 43.4 | 40.1 | -7.8 % | 1 % | 0 % |
| ATM3 no card OUT #00FD | 46.7 | 45.9 | -1.9 % | 0 % | 0 % |
| PENTAGON MoonSound OUT #00FD | 43.4 | 40.0 | -7.8 % | 0 % | 1 % |
| ATM3 MoonSound OUT #00FD | 46.9 | 45.6 | -2.7 % | 0 % | 1 % |
| PENTAGON no card IN #00C4 | 31.0 | 28.8 | -7.2 % | 0 % | 0 % |
| ATM3 no card IN #00C4 | 33.7 | 31.0 | -8.0 % | 0 % | 0 % |
| PENTAGON MoonSound IN #00C4 | 38.5 | 39.8 | +3.4 % | 0 % | 0 % |
| ATM3 MoonSound IN #00C4 | 36.8 | 38.1 | +3.7 % | 0 % | 0 % |
| PENTAGON no card OUT #00C4 | 43.5 | 40.1 | -7.8 % | 1 % | 1 % |
| ATM3 no card OUT #00C4 | 34.0 | 32.6 | -4.2 % | 1 % | 1 % |
| PENTAGON MoonSound OUT #00C4 | 32.7 | 33.2 | +1.7 % | 1 % | 0 % |
| ATM3 MoonSound OUT #00C4 | 32.3 | 34.2 | +5.7 % | 0 % | 0 % |

## 7. SL-3 as built (2026-10-04)

**Before SL-3:** `zx-bus-slots` was brought up to master (which carries the TTD v2 engine) by a merge. One conflict:
`core/benchmarks/emulator/portin_benchmark.cpp` (master added the `#FFFD` row to `BM_PortIn`, the branch added
`BM_PortOut` / `BM_PortCard`; both kept, `BM_PortOut` stays on the first three ports). `z80.cpp`, `portdecoder.{h,cpp}`
and `sprinterisolation_test.cpp` merged cleanly (master's changes there are TTD hooks outside the port funnel). Full
build without warnings, `core-tests` 7574 tests green.

**Steps** (each its own full `core-tests` run, all green; the existing tests unchanged):

| Step | What moved | Old mechanism removed |
|---|---|---|
| 1 | The card tap and **R6** leave `Z80::inFromBus` / `Z80::out`: the Z80 runs one bus cycle, `PortDecoder::ReadCycle(port, pc, cardDrove)` / `WriteCycle(port, value, pc)`. Inline: a port no card claims is one bit test, then the board's `DecodePortIn/Out`; a claimed port goes to the out-of-line `ReadClaimedCycle` / `WriteClaimedCycle` (card first, then the board decode, then R6) | the tap + R6 code in the Z80 funnel; the unclaimed IN no longer writes the observer-value cache |
| 2 | **`OverrideDecodeForFullDecodeClaim`'s decision** moves into the resolution: `ReadClaimedCycle` / `WriteClaimedCycle` look the port up once and decide whether a low-byte card stands the board down (write: always; read: the card's `portDeviceClaimsRead`, asked once per cycle instead of twice) into `_claimCycle`; the decoders' override calls only apply it where their decode reaches the card's port space (their machine conditions - Scorpion's Beta gate, TS-Conf's register arm, Pentagon 1024's early `#7FFD` check - stay board rules); a direct `DecodePortIn/Out` outside a bus cycle (tests, tools) decides it in the override | the SL-2 per-port memo (`_claimMemo*`, `MemoizeClaim`) |
| 3 | **Self-decoding dispatch** (Covox / SounDrive) into a claim table: a device declares its raw-port claims (`PortDevice::selfDecodingClaims()`, default: every port, so `tryClaimOut/In` alone decide); `Covox` declares its fitment's mask / match (Quad: `#F1/#F3/#F9/#FB` + `#0F/#1F/#4F/#5F` families, Mono: `#FB`). `DispatchSelfDecodingOut/In` are inline: an uncovered port is one bit test, a covered one asks the covering devices in registration order, each once (`PortClaimTable::ForEachMatch`) | the `_selfDecodingDevices` vector and its loop of virtual calls over every undecoded port |
| 4 | **The exact peripheral port map** (`RegisterPortHandler`: WD1793, AY / TS / TSFM, GS) into exact claims (mask `#FFFF`) on the decoded port: `PeripheralPortIn/Out` = one bit test + a scan of the low byte's bucket; the port map report sorts the registered ports as the map did | the `std::map<uint16_t, PortDevice*> _portDevices` |

**Tables, not one table (deviation from architecture.md §4, for owner review):** the three mechanisms now live in
three `PortClaimTable` instances of `PortDecoder`, each with its own role: `_fullDecodeClaims` (bus cards on the raw
address, seen before the board), `_selfDecodingClaims` (raw address, offered only after the board's decode declined the
port) and `_peripheralClaims` (the board's devices, keyed by the *decoded* port the model decoders produce). One
instance would mix two address spaces (raw vs decoded) and would mark every Covox / FDC / AY port as claimed for the
bus-card bit test, putting those ports on the claimed path. The single table of architecture.md §4 needs the board
decode expressed as claims on raw ports (each device's ports from the reference data, the decoders reduced to the
board callback of `Read` / `Write`): that is SL-4's per-card work, card by card, and the three tables shrink into one
as the cards move. `PortClaimTable::Write` / `Read` (the IORQGE / arbitration resolution) are still unused in
production: the legacy observers keep their exact semantics (R6, the dynamic read claim, the Beta-128 exception),
which `Read`'s static claims cannot express; they move onto it with the card declarations.

**Kept for tests and tools:** `NotifyFullDecodeIn/Out` (split-phase taps; `fulldecodeclaim_test.cpp` and
`portclaimtable_test.cpp` emulate the funnel with them and stay unchanged), `RegisterSelfDecodingDevice`,
`RegisterPortHandler` and the other registration calls (the cards still register themselves until SL-4).

**Tests:** `core/tests/emulator/ports/portdecoder_test.cpp`, `PortDecoder_Test.*`: `ReadCycleAppliesSharedBusRule`
(R6 in `ReadCycle`: silent card, claiming card, undecoded port, unclaimed port), `ClaimDecidedOncePerCycle` (one
`portDeviceClaimsRead` per claimed read, none per write; the override's own decision outside a cycle),
`SelfDecodingDevicesOfferedTheirClaims` (uncovered ports never offered, registration order, one question per device,
the catch-all default), `PeripheralPortsAreExactClaims`. Full `core-tests` 7578 green after steps 3 and 4 (7574 after
steps 1 and 2); TTD corpus and CoreGolden part of it, unchanged; full build without warnings; MinGW `-fsyntax-only
-Wall -Wextra -Werror` clean on `portdecoder.cpp`, `z80.cpp`, `covox.cpp` and the new test.

**Benchmarks.** `BM_PortIn` / `BM_PortOut` / `BM_PortCard`, every row. A = SL-1 `186f10087` (`scratch/wt-slots-before`,
not rebuilt), M = the merge (SL-2 + master, before SL-3), S2 = after step 2 (tap + R6 + override in one pass), F = SL-3
final; Release, `UNREAL_NICE=0`, `--benchmark_min_time=0.3s`; six rounds, the order rotated and reversed every
round. **The machine was heavily loaded** (1-minute load 24-72 on 20 cores, owner decision: run anyway, numbers are
relative): load per round start / max: 1: A 70.1/71.9, M 70.7/72.2, S2 65.2/66.9, F 66.7/66.7; 2: A 63.4/63.4, F
60.3/62.5, S2 60.4/60.4, M 54.3/54.3; 3: S2 48.8/48.8, F 43.6/44.2, A 43.3/45.6, M 43.3/43.3; 4: S2 39.7/39.7, M
34.1/34.9, A 34.9/36.8, F 32.5/32.5; 5: A 24.6/38.2, M 38.2/38.2, S2 37.2/37.2, F 34.3/49.0; 6: A 46.4/46.4, F
40.1/47.2, S2 44.3/44.3, M 36.4/44.4. CPU time per block of 1000 `IN A,(C)` / `OUT (C),A`, median of six rounds;
spread = (max - min) / median, the largest of the four binaries (A has no `#FFFD` row: master added it):

| Benchmark | A us | M us | S2 us | F us | M/A | S2/A | F/A | spread max |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| PortIn 48K IN #00FF | 49.3 | 41.8 | 41.7 | 38.8 | -15.3 % | -15.6 % | -21.4 % | 8 % |
| PortIn 128k IN #00FF | 57.0 | 50.2 | 50.1 | 43.4 | -11.8 % | -12.0 % | -23.8 % | 7 % |
| PortIn PENTAGON IN #00FF | 50.8 | 44.3 | 43.1 | 39.6 | -12.9 % | -15.2 % | -22.1 % | 8 % |
| PortIn 48K IN #40FF | 58.3 | 55.8 | 55.8 | 51.6 | -4.4 % | -4.4 % | -11.5 % | 11 % |
| PortIn 128k IN #40FF | 60.7 | 57.6 | 57.5 | 53.2 | -5.0 % | -5.3 % | -12.4 % | 9 % |
| PortIn PENTAGON IN #40FF | 51.1 | 45.0 | 43.5 | 39.8 | -11.9 % | -14.9 % | -22.2 % | 9 % |
| PortIn 48K IN #00FE | 58.4 | 50.9 | 50.7 | 50.0 | -12.8 % | -13.2 % | -14.5 % | 13 % |
| PortIn 128k IN #00FE | 62.4 | 53.4 | 53.8 | 52.8 | -14.4 % | -13.8 % | -15.4 % | 10 % |
| PortIn PENTAGON IN #00FE | 48.5 | 43.4 | 42.7 | 42.7 | -10.5 % | -12.0 % | -12.0 % | 10 % |
| PortOut 48K OUT #00FF | 33.8 | 30.3 | 30.4 | 30.9 | -10.3 % | -10.1 % | -8.7 % | 10 % |
| PortOut 128k OUT #00FF | 32.8 | 30.9 | 30.6 | 30.2 | -5.8 % | -6.7 % | -8.0 % | 7 % |
| PortOut PENTAGON OUT #00FF | 34.0 | 32.0 | 30.3 | 28.3 | -6.0 % | -10.8 % | -16.9 % | 7 % |
| PortOut 48K OUT #40FF | 42.6 | 40.8 | 40.4 | 40.9 | -4.2 % | -5.1 % | -4.1 % | 7 % |
| PortOut 128k OUT #40FF | 41.5 | 40.5 | 40.2 | 41.1 | -2.5 % | -3.2 % | -1.0 % | 7 % |
| PortOut PENTAGON OUT #40FF | 34.0 | 31.9 | 30.3 | 28.6 | -6.1 % | -11.0 % | -16.0 % | 11 % |
| PortOut 48K OUT #00FE | 72.7 | 72.3 | 71.8 | 72.7 | -0.6 % | -1.2 % | +0.0 % | 10 % |
| PortOut 128k OUT #00FE | 74.9 | 74.4 | 73.4 | 73.7 | -0.7 % | -2.1 % | -1.7 % | 8 % |
| PortOut PENTAGON OUT #00FE | 44.5 | 42.6 | 41.7 | 42.3 | -4.3 % | -6.4 % | -4.9 % | 8 % |
| PortCard PENTAGON no card IN #00FD | 31.8 | 28.5 | 27.9 | 28.5 | -10.2 % | -12.2 % | -10.3 % | 9 % |
| PortCard ATM3 no card IN #00FD | 33.4 | 29.9 | 29.6 | 29.9 | -10.5 % | -11.4 % | -10.5 % | 10 % |
| PortCard PENTAGON MoonSound IN #00FD | 32.0 | 28.4 | 28.2 | 28.3 | -11.2 % | -11.9 % | -11.6 % | 14 % |
| PortCard ATM3 MoonSound IN #00FD | 33.4 | 30.1 | 29.7 | 30.2 | -9.7 % | -10.9 % | -9.6 % | 10 % |
| PortCard PENTAGON no card OUT #00FD | 44.7 | 40.7 | 39.5 | 41.0 | -9.1 % | -11.7 % | -8.2 % | 11 % |
| PortCard ATM3 no card OUT #00FD | 48.2 | 45.2 | 44.9 | 45.8 | -6.4 % | -7.0 % | -5.1 % | 11 % |
| PortCard PENTAGON MoonSound OUT #00FD | 44.9 | 40.7 | 39.5 | 40.5 | -9.3 % | -12.0 % | -9.8 % | 11 % |
| PortCard ATM3 MoonSound OUT #00FD | 48.4 | 44.8 | 44.8 | 45.1 | -7.5 % | -7.5 % | -6.9 % | 18 % |
| PortCard PENTAGON no card IN #00C4 | 32.2 | 28.5 | 28.5 | 28.7 | -11.5 % | -11.5 % | -10.8 % | 12 % |
| PortCard ATM3 no card IN #00C4 | 34.8 | 30.9 | 30.9 | 30.6 | -11.2 % | -11.2 % | -12.0 % | 11 % |
| PortCard PENTAGON MoonSound IN #00C4 | 39.8 | 40.1 | 39.4 | 37.8 | +0.7 % | -1.1 % | -5.0 % | 10 % |
| PortCard ATM3 MoonSound IN #00C4 | 37.9 | 38.2 | 37.1 | 37.2 | +1.0 % | -1.9 % | -1.6 % | 9 % |
| PortCard PENTAGON no card OUT #00C4 | 44.8 | 40.7 | 39.6 | 39.9 | -9.0 % | -11.6 % | -10.9 % | 13 % |
| PortCard ATM3 no card OUT #00C4 | 35.0 | 30.7 | 30.6 | 31.2 | -12.2 % | -12.6 % | -10.9 % | 9 % |
| PortCard PENTAGON MoonSound OUT #00C4 | 33.7 | 34.5 | 33.7 | 31.4 | +2.5 % | -0.0 % | -7.0 % | 10 % |
| PortCard ATM3 MoonSound OUT #00C4 | 33.3 | 33.1 | 33.4 | 33.5 | -0.7 % | +0.1 % | +0.6 % | 17 % |
| PortIn 48K IN #FFFD | - | 37.5 | 38.6 | 35.6 | - | - | - | 12 % |
| PortIn 128k IN #FFFD | - | 41.2 | 42.2 | 39.7 | - | - | - | 7 % |
| PortIn PENTAGON IN #FFFD | - | 36.3 | 35.5 | 35.1 | - | - | - | 10 % |

- **Exit criterion (a card's own port no slower than at SL-1):** the MoonSound's `#C4` rows at F vs A: Pentagon IN
  -5.0 %, ATM3 IN -1.6 %, Pentagon OUT -7.0 %, ATM3 OUT +0.6 % (at parity: that row's spread is 17 %). Met. In this
  run the SL-2 code (M) shows only -0.7 to +2.5 % on these rows (SL-2's quiet-machine measurement: +3.7 to +5.3 %),
  S2 -1.9 to +0.1 %, F -7.0 to +0.6 %: the single pass (one lookup and one read-claim question per cycle, no memo, no
  tap locals or R6 branch in the Z80 funnel) moves the claimed rows in the right direction at every step.
- **No card claims the port:** F is faster than SL-1 on every row except the ULA border write (`OUT #00FE`: 48K
  +0.0 %, 128K -1.7 %, dominated by the border latch). The unclaimed `IN #00FF` / `#40FF` rows moved a further 7-13 %
  between S2 and F, also on the 48K whose decoder has no self-decoding device and no peripheral on those ports: that
  part is code layout and load, not a measured gain of steps 3-4. `#FFFD` (the AY through the peripheral claims) is
  2-5 % faster than M.
- Under this load the spreads are 7-18 %; rows within a few percent of zero are noise. A quiet-machine rerun (load
  below 12) is worth doing before the SL-4 card measurements, which use F as their baseline.

## 8. SL-4 as built (2026-10-04)

**Before SL-4:** `zx-bus-slots` merged with master `e378c483a` (N0 remote access, network SN6b, TS-Conf WAIT fixes,
PQ-DOS). Clean auto-merge (`z80.cpp`: master's `_waitObserver` in `SetInterruptSource`); full build without warnings,
`core-tests` 7599 green.

**Steps** (each a full `core-tests` run, all green, the full build without compiler warnings, the existing tests
unchanged except where named):

| Step | What moved | Tests |
|---|---|---|
| 1 | `SlotManager` (`slots/slotmanager.{h,cpp}`): the slot set of an instance, planned in `Core::Init` before any card is built, from `[SLOTS]` (`slots/slotconfig.{h,cpp}`, `CONFIG::slotConfig`) or from the legacy card fields translated into slots; entries in slot order through `SlotPlanner::Plan` without the replace flag (first wins, a later card that would displace it is disabled with the reasons; since Q8, §11, such a conflict refuses the machine); deprecation log per legacy key present in the INI; `DeviceState::Slots` (`slots/slotreport.cpp`) | `SlotManager_Test.*` (`ParsesTheSlotsSection`, `LegacyKeysTranslated`, `IniLoadUsesSamePlan`, `FitOverrideNeverDisplaces`, `HardRefusalKeptEvenWithTheOverride`, `NotEmulatedCardsAreDisabled`, `AFieldChangedAfterTheIniWins`, `ReportListsSlotsAndBuiltIns`), `SlotManagerShipped_Test.FitsTheDevicesOfMaster/*` |
| 2 | The AY socket's board (`ay` = Single, `ts` = the two-AY TurboSound, the legacy `AY` kind, `tsfm`, `none`); the 48K gets a **retrofitted** AY socket (R-BUS-1a: the emulator's 48K decoder routes the 128K AY decode to an AY interface and the shipped 48K config fits a TSFM there) | `SocketBoardComesFromTheSlot`; changed: `RefData_Test.CollectionIsConsistent` (a retrofitted socket has no chip of its own), `SlotPlanner_Test.MalformedRequestsRefused` (`ts` on the 48K is now planned) |
| 3 | The General Sound personality (`gs` / `gs-lw` / `neogs`) and the card's `ram` option (`gs` 128k-512k -> `[SOUND] GSRamSize`, larger sizes clamp to 512k as before; `neogs` -> `[NGS] RamSize`); the runtime personality switch is unchanged until SL-6 | `GeneralSoundComesFromTheSlot` |
| 4 | MoonSound; on the Profi the card is refused with the reason (`#7E` is the palette, a fixed built-in the board wins), also from a legacy `MoonSound=1` - **the one intended behavior change** (the shipped Profi config has `MoonSound=0`) | `MoonSoundComesFromTheSlot` |
| 5 | `covox-fb` and `soundrive`: the SounDrive `mode` option reaches the Covox module (`Covox::Fitment` gains `Mode1`, `Mode2`, `Mode1Mono`; `CONFIG::sound.sdMode`; port map rows per mode); `mode` gains the value `both` (the emulator's decode of both port sets, what the legacy `SD=1` fits). The board Covox of ATM450 / ATM710 / ATM3 / TSL and a newly declared one on PROFI / PROFI3 (`#5F` / `#3F`) are switchable built-ins: `builtin.covox = on | off` (default on with `[SLOTS]`; the legacy `CovoxFB` switches it where the machine has one, else it is a `covox-fb` card) | `CovoxCardsAndTheBoardCovoxComeFromTheSlots`, `SoundriveModeReachesTheCovox` |
| 6 | `zxnetusb` / `zx-wifi` (`port=ef`); `NetworkManager` keeps the virtual network and its own checks for the runtime `RequestChange` path (SL-6), ATM2IOESP stays a `[NETWORK] Card` value (the INTERNAL connector, a later bus) | `NetworkCardsComeFromTheSlots` |
| 7 | The 17 shipped `data/configs/*/unreal.ini` carry `[SLOTS]`; the legacy card keys are gone from them (`[NETWORK] Card=NONE` replaced by a comment) | changed (they rewrote the old keys): `emulatortesthelper.cpp` `StageTurboSoundKindIni` (rewrites `ay-socket`), `Config_Test.EveryShippedConfigFitsNeoGSWithGSTypeNGS` (checks the `[SLOTS]` `neogs`; since 2026-10-04 `ShippedConfigsFitTheirGeneralSoundCard`), the TSFM suites' `CreateFmEmulator` (accept `ay-socket = tsfm`) |

**How the decision reaches the devices.** The cards are still built by their owners: `SoundManager` (mixer, AY
socket, GS, MoonSound, Covox) and `NetworkManager` (virtual network). `SlotManager::Apply` writes the fitted set into
the CONFIG card fields they read (`sound.turboSoundKind`, `gsTypeKind`, `moonsound`, `covoxFB`, `sd`, `sdMode`,
`network.card`'s ZX-bus bits), group by group (`kSlotDecidedGroups`, one group per step). With `[SLOTS]` the parser
writes the same fields as the section's projection (`SlotManager::Project`), so code that reads a loaded config keeps
working. A field that code changes after the INI was read (the test runner's sound policy, a snapshot transfer's
`FitSourceDevices`, the TTD bench) is honored: at creation that group's slots are translated from the field again.
Deviation from architecture.md §5/§7 ("SlotManager owns the fitted cards"): it owns the *decision* and the slot report;
`ICard` objects arrive with the ZX-MultiSound (the first card written for slots) and SL-6's restart path.

**The fit override in a config** (`<slot>.fit = unrealistic`, new key): R-CFG-3 plans an INI without the replace
flag, but the shipped and old configs fit cards the reference data calls unrealistic (NeoGS on the Sinclair edge, on
the Profi bus, behind the Sprinter ISA). Every translated legacy key carries the override, so an old INI keeps its
devices; the override accepts the bus fit only - a plan that would displace a card, switch a built-in off or take a
chip out of its socket is still refused, and hard refusals stay hard. Cards the emulator cannot build are disabled
with the reason (`multisound`, `covox-fb decode=a2`, `zx-wifi port=ee`).

**Same devices per shipped model.** `SlotManagerShipped_Test.FitsTheDevicesOfMaster/<folder>` creates each shipped
config's machine (all sound devices as configured) and compares mixer rows, the AY socket device, the GS card, the
Covox fitment, network bits, every port map row, the full-decode claims and the TTD device set with each blob's size
against `testdata/slots/fitted-devices.txt`, captured from the merge binary before step 1 (master's device code; the
Ethernet NIC blob's size is per-process state, only its presence is compared). Green after every step, and after the
configs were converted. `zx-diagnostics` (a 48K config for a ROM the user supplies) is compared at the config level.
TTD corpus, CoreGolden and the bench-gate byte counts are part of `core-tests` and unchanged.

**Shipped `[SLOTS]` per model:**

| Configs | AY socket | Cards | Built-in |
|---|---|---|---|
| pentagon128k, pentagon512k, scorpion, profscorp, ts-conf | `tsfm` | `zxbus.1 neogs`, `zxbus.2 moonsound`, `zxbus.3 soundrive mode=both` | ts-conf: `builtin.covox = on` |
| atm3 | `ts` | as above | `builtin.covox = on` |
| atm450, atm710 | `ts` | the same three on `cpu-socket.1-3` behind `atm-cpu-socket-zxbus` (fit `adapter`) | `builtin.covox = on` |
| spectrum48 (retrofitted socket), spectrum128, spectrum2, spectrum2a, spectrum3 | `tsfm` | none (no GS since 2026-10-04, see below) | - |
| zx-diagnostics (48K) | `tsfm` | `edge.1 neogs` behind `zxbus-to-sinclair-edge` (`fit = unrealistic`), `edge.2 moonsound`, `edge.3 soundrive mode=both` (fit `adapter`) | - |
| profi, profi3 | `ay` | `profi-bus.2 soundrive mode=both`, `fit = unrealistic` (no GS since 2026-10-04; the SounDrive keeps the shared Covox module in its four-channel form, as before; the Profi decoder does not route the card's ports) | `builtin.covox = on` |
| sprinter | `ay` | none (no GS since 2026-10-04; `[ISA] Slot1=ZXBUS` keeps the empty ISA ZX-bus adapter card, SL-8) | - |

**No General Sound on the Sinclair, Profi and Sprinter configs (owner decision 2026-10-04).** After step 7 the
shipped 48K, 128K, +2, +2A, +3, Profi v5 / v3 and Sprinter configs dropped their NeoGS: on real hardware none of these
buses takes a ZX-bus card without an adapter, so the card was fitted only as `fit = unrealistic`. Each config keeps a
comment on how to add one in `[SLOTS]` (with the fit note) and keeps its `[NGS]` section, which `Config` still reads
for a `neogs` slot. The golden `fitted-devices.txt` was regenerated: on these eight folders only the NeoGS mixer rows
(`[NeoGS]`, `[NeoGS MP3]`), its ports (`#0033`, `#00B3`, `#00BB`) and its TTD blob (id 12) are gone; every other line
and every other folder is unchanged. `Config_Test.ShippedConfigsFitNeoGSExceptTheMachinesShippedWithout` and
`ShippedConfigsFitTheirGeneralSoundCard` (formerly `ShippedConfigsFitNeoGS` / `EveryShippedConfigFitsNeoGSWithGSTypeNGS`)
list the eight folders.

**Benchmarks.** `BM_PortIn` / `BM_PortOut` / `BM_PortCard`; A = SL-3 final (`bin/core-benchmarks-final`), B = after
step 2 / after step 6; `UNREAL_NICE=0`, `--benchmark_min_time=0.3s`, six interleaved rounds (A B, B A, ...). The
machine was loaded (owner decision: run anyway, relative numbers): 1-minute load per run start -> end, step 2: 37-64,
step 6: 26-61. SL-4 changes no port code path (the cards are decided at creation), so the expectation is parity.
After step 2: every row within -2.3 .. +0.8 % except Pentagon `OUT #00FD` (+3.5 %, +4.2 %) and `OUT #00C4` no card
(+4.0 %), spreads 2-8 %. After step 6 the same Pentagon OUT rows +3.2 .. +4.1 %, the rest -1.6 .. +2.9 % (ATM3
MoonSound `IN #00C4` +3.8 %), spreads 1-4 %. To separate the master merge from SL-4, a third binary was built from the
merge tree (step 0, `scratch/wt-slots-sl4base`) and run against step 6 (load 17-34): the Pentagon OUT rows are at
parity there (-0.7 .. +0.3 %), so their +4 % against the SL-3 binary comes with the master merge (it adds a branch to
`Z80::AddWaitStates` / `AddWaitTicks`), not with SL-4. Step 6 vs step 0, every row: -3.4 .. +3.7 %, spreads 0-4 %;
the largest, ATM3 `IN #00C4` with the MoonSound +3.7 % and the ATM3 `IN` rows +2.1 .. +2.6 %, are on a path SL-4 does
not touch (the ATM3 decoder has no self-decoding IN; the cards on the ATM3 machine are the same objects) - code
layout under load; a quiet-machine rerun is listed in TODO.md.

Columns "final" / "step 2" come from the step-2 run, "step 0" / "step 6" from the step-0-versus-6 run (different
loads: compare within a pair).

| Benchmark (us per 1000 accesses, median of 6) | final | step 2 | step 0 | step 6 | step 6 / step 0 |
|---|--:|--:|--:|--:|--:|
| PortIn 48K IN #00FF | 39.0 | 39.0 | 37.6 | 37.7 | +0.1 % |
| PortIn 128k IN #00FF | 43.6 | 43.8 | 41.9 | 42.4 | +1.2 % |
| PortIn PENTAGON IN #00FF | 39.6 | 39.6 | 38.2 | 38.5 | +0.8 % |
| PortIn 48K IN #40FF | 51.2 | 51.1 | 49.3 | 49.6 | +0.6 % |
| PortIn 128k IN #40FF | 53.1 | 53.2 | 51.1 | 51.5 | +0.7 % |
| PortIn PENTAGON IN #40FF | 39.7 | 39.6 | 38.3 | 38.7 | +1.0 % |
| PortIn 48K IN #00FE | 50.1 | 50.1 | 48.1 | 48.5 | +0.7 % |
| PortIn 128k IN #00FE | 53.0 | 52.7 | 50.8 | 51.5 | +1.2 % |
| PortIn PENTAGON IN #00FE | 42.6 | 42.5 | 40.9 | 41.5 | +1.4 % |
| PortIn 48K IN #FFFD | 35.7 | 35.6 | 35.6 | 34.7 | -2.6 % |
| PortIn 128k IN #FFFD | 39.6 | 39.5 | 39.7 | 38.4 | -3.4 % |
| PortIn PENTAGON IN #FFFD | 34.9 | 35.0 | 33.9 | 34.1 | +0.4 % |
| PortOut 48K OUT #00FF | 30.8 | 30.4 | 30.1 | 29.9 | -0.7 % |
| PortOut 128k OUT #00FF | 30.0 | 29.4 | 28.6 | 28.9 | +1.0 % |
| PortOut PENTAGON OUT #00FF | 28.2 | 28.1 | 27.3 | 27.3 | +0.0 % |
| PortOut 48K OUT #40FF | 40.6 | 40.3 | 39.3 | 39.4 | +0.2 % |
| PortOut 128k OUT #40FF | 40.3 | 39.4 | 38.0 | 38.2 | +0.5 % |
| PortOut PENTAGON OUT #40FF | 28.2 | 28.1 | 27.2 | 27.1 | -0.2 % |
| PortOut 48K OUT #00FE | 72.3 | 72.1 | 70.2 | 69.6 | -0.9 % |
| PortOut 128k OUT #00FE | 73.1 | 72.8 | 70.6 | 70.8 | +0.2 % |
| PortOut PENTAGON OUT #00FE | 41.6 | 41.4 | 40.1 | 39.9 | -0.3 % |
| PortCard PENTAGON no card IN #00FD | 28.2 | 28.4 | 27.3 | 27.6 | +1.1 % |
| PortCard ATM3 no card IN #00FD | 29.5 | 29.4 | 28.6 | 29.2 | +2.1 % |
| PortCard PENTAGON MoonSound IN #00FD | 28.2 | 28.4 | 27.4 | 27.5 | +0.3 % |
| PortCard ATM3 MoonSound IN #00FD | 29.5 | 29.5 | 28.5 | 29.2 | +2.6 % |
| PortCard PENTAGON no card OUT #00FD | 39.7 | 41.1 | 39.6 | 39.7 | +0.3 % |
| PortCard ATM3 no card OUT #00FD | 44.8 | 45.0 | 43.4 | 43.4 | -0.1 % |
| PortCard PENTAGON MoonSound OUT #00FD | 39.8 | 41.4 | 39.6 | 39.6 | +0.0 % |
| PortCard ATM3 MoonSound OUT #00FD | 44.9 | 45.2 | 43.7 | 43.4 | -0.8 % |
| PortCard PENTAGON no card IN #00C4 | 28.4 | 28.9 | 27.7 | 27.9 | +0.8 % |
| PortCard ATM3 no card IN #00C4 | 30.5 | 30.9 | 29.4 | 30.2 | +2.5 % |
| PortCard PENTAGON MoonSound IN #00C4 | 37.6 | 36.9 | 35.3 | 35.8 | +1.5 % |
| PortCard ATM3 MoonSound IN #00C4 | 37.0 | 37.6 | 35.9 | 37.2 | +3.7 % |
| PortCard PENTAGON no card OUT #00C4 | 39.6 | 41.2 | 39.9 | 39.6 | -0.7 % |
| PortCard ATM3 no card OUT #00C4 | 30.7 | 30.9 | 29.8 | 29.9 | +0.3 % |
| PortCard PENTAGON MoonSound OUT #00C4 | 31.1 | 31.3 | 30.3 | 30.2 | -0.4 % |
| PortCard ATM3 MoonSound OUT #00C4 | 33.3 | 33.8 | 32.2 | 32.3 | +0.3 % |

## 9. Slot-built cards: the ZX-MultiSound (MultiSound MS-4, 2026-10-04)

The first card written for slots ([MultiSound tdd-integration.md](../2026-10-03-zx-multisound/tdd-integration.md) §3.2)
needed the framework pieces SL-4 left for later. Committed on `zx-bus-slots` (`cdac570ec`..`dcaf21a18`).

| Step | What changed in the framework |
|---|---|
| 1 | `ICard` / `CardType` / `CardContext` (`slots/card.{h,cpp}`, architecture.md §3.2 "As built"); `SlotManager::BuildCards` / `ReleaseCards` / `FindCard` / `Cards` (Core::Init after the network manager, Core::Release first); `CardClaims(def, options)` public in `slotplanner.h`; `PortDevice::portDeviceReadCycle(port, drives)` (a card may assert IORQGE and leave the data bus alone; the table's read counts only drivers); `[MIDI] Bank=` (`CONFIG::midiBank`, moved to `Config::GetMidiBank` in §12). **Plan at creation:** an INI that leaves `ay-socket` unconfigured lets a card take a socketed chip out (Q7, the ZX-Evo YM2149); `ay-socket = ay` keeps it and the card is not fitted. The removed chip is a `BuiltIn::removed` flag and `Apply` leaves the socket empty (`TurboSoundKind::None`) |
| 2 | The claim table's `Read` / `Write` in production for slot-built cards (architecture.md §4.3 "As built (MS-4)"): `PortDecoder::ConfigureSlotBus` / `AttachSlotCard` / `DetachSlotCard` / `IsBuiltInShadowed` / `SetBuiltInRemoved` / `BindSlotSignals`, `ReadSlotCardCycle` / `WriteSlotCardCycle`; legacy low-byte lookups ignore card slots; `PortClaimTable::ClearBuiltIns`, `SetBuiltInRemoved`, `IsRemovedBuiltInRead`; `BM_PortSlotCard` |
| 3 | `SoundManager::attachSlotCard` / `detachSlotCard` (rows into the registry, wide mix), card frames, `BusReset` on reset, `SetOutputRate` on a core-rate change, `setDeviceState` / `deviceState` (a shadowed socket chip's rows say `shadowed by zxbus.N`; the mixer report carries `state`); the step-2 signals object and removed-chip list moved out of `PortDecoder`'s layout (into `SlotManager` and the table's built-in list) |
| 4 | Integration tests; the slot report's built-in carries `removed: true` |

**Tests** (`core/tests/emulator/slots/cards/multisound/multisoundslotcard_test.cpp`, `MultiSoundSlotCard_Test`, 10,
each under 50 ms): options from the slot, the card built with them and the bank, the ZX-Evo YM2149 taken out (and
kept with `ay-socket = ay`), Pentagon CardWins shadowing (board AY gets no `#FFFD` / `#BFFD` cycle, card drives the
read, passive SAA / SounDrive writes locked from the ROM), ZX-Evo RdWr on board ports (`#FFFD`, `#1F`), the five
rows only with the card, a tone reaching its row, a Z80 program (control byte, YM2203 FM + SSG, SAA, SounDrive, a GS
command, a MIDI Note On bit-banged on U4's IOA2 at 31250 baud) making all five rows non-silent on a Pentagon and a
ZX-Evo, and the matrix refusals at creation (`multisound` + `gs` either order, `tsfm` in the socket, the 128K edge).
Changed: `SlotManager_Test.NotEmulatedCardsAreDisabled` (the MultiSound is emulated).

**Benchmarks.** `BM_PortIn` / `BM_PortOut` / `BM_PortCard`, every row; A = `140979aee` (the branch before MS-4,
`bin/core-benchmarks-ms4base`), B = the final MS-4 code; `UNREAL_NICE=0`, `--benchmark_min_time=0.3s`, six rounds
interleaved A B, B A, ...; 1-minute load per run start -> end: 16.1 -> 12.7, 12.7 -> 10.8, 10.8 -> 8.8, 8.8 -> 8.7,
8.7 -> 8.0, 8.0 -> 9.4, 9.4 -> 10.0, 10.0 -> 10.3, 10.3 -> 11.6, 11.6 -> 17.6, 17.6 -> 16.9, 16.9 -> 14.8. Spread =
(max - min) / median.

| Benchmark (us per 1000 accesses, median of 6) | A | B | B/A | A spread | B spread |
|---|--:|--:|--:|--:|--:|
| PortIn 48K IN #00FF | 38.6 | 38.4 | -0.5 % | 5 % | 7 % |
| PortIn 128k IN #00FF | 43.0 | 42.8 | -0.4 % | 5 % | 7 % |
| PortIn PENTAGON IN #00FF | 40.6 | 39.2 | -3.4 % | 6 % | 6 % |
| PortIn 48K IN #40FF | 51.7 | 51.3 | -0.8 % | 5 % | 6 % |
| PortIn 128k IN #40FF | 53.4 | 53.5 | +0.3 % | 5 % | 4 % |
| PortIn PENTAGON IN #40FF | 40.6 | 39.8 | -2.1 % | 5 % | 4 % |
| PortIn 48K IN #00FE | 50.2 | 50.2 | +0.0 % | 5 % | 5 % |
| PortIn 128k IN #00FE | 52.5 | 52.7 | +0.4 % | 5 % | 3 % |
| PortIn PENTAGON IN #00FE | 45.7 | 42.9 | -6.1 % | 4 % | 4 % |
| PortIn 48K IN #FFFD | 35.8 | 35.5 | -0.7 % | 3 % | 4 % |
| PortIn 128k IN #FFFD | 39.6 | 39.5 | -0.2 % | 2 % | 3 % |
| PortIn PENTAGON IN #FFFD | 37.1 | 34.7 | -6.4 % | 4 % | 4 % |
| PortOut 48K OUT #00FF | 30.0 | 31.0 | +3.3 % | 3 % | 3 % |
| PortOut 128k OUT #00FF | 28.6 | 30.5 | +6.5 % | 3 % | 3 % |
| PortOut PENTAGON OUT #00FF | 28.9 | 27.9 | -3.6 % | 2 % | 3 % |
| PortOut 48K OUT #40FF | 40.1 | 40.6 | +1.3 % | 4 % | 3 % |
| PortOut 128k OUT #40FF | 38.4 | 39.4 | +2.5 % | 5 % | 3 % |
| PortOut PENTAGON OUT #40FF | 28.8 | 27.8 | -3.2 % | 6 % | 3 % |
| PortOut 48K OUT #00FE | 70.0 | 72.5 | +3.5 % | 5 % | 2 % |
| PortOut 128k OUT #00FE | 70.8 | 72.9 | +3.1 % | 4 % | 1 % |
| PortOut PENTAGON OUT #00FE | 41.6 | 40.7 | -2.2 % | 4 % | 2 % |
| PortCard PENTAGON no card IN #00FD | 30.5 | 27.9 | -8.7 % | 4 % | 3 % |
| PortCard ATM3 no card IN #00FD | 30.6 | 29.1 | -5.0 % | 5 % | 3 % |
| PortCard PENTAGON MoonSound IN #00FD | 30.6 | 27.9 | -8.9 % | 7 % | 2 % |
| PortCard ATM3 MoonSound IN #00FD | 30.7 | 29.0 | -5.7 % | 5 % | 2 % |
| PortCard PENTAGON no card OUT #00FD | 40.0 | 38.8 | -3.0 % | 5 % | 3 % |
| PortCard ATM3 no card OUT #00FD | 44.6 | 44.2 | -0.9 % | 4 % | 2 % |
| PortCard PENTAGON MoonSound OUT #00FD | 40.0 | 38.9 | -2.8 % | 5 % | 2 % |
| PortCard ATM3 MoonSound OUT #00FD | 44.7 | 44.2 | -1.2 % | 5 % | 4 % |
| PortCard PENTAGON no card IN #00C4 | 30.6 | 28.3 | -7.6 % | 4 % | 2 % |
| PortCard ATM3 no card IN #00C4 | 31.6 | 30.2 | -4.4 % | 5 % | 2 % |
| PortCard PENTAGON MoonSound IN #00C4 | 37.9 | 36.0 | -5.2 % | 6 % | 2 % |
| PortCard ATM3 MoonSound IN #00C4 | 37.0 | 36.5 | -1.5 % | 3 % | 2 % |
| PortCard PENTAGON no card OUT #00C4 | 40.0 | 38.9 | -2.8 % | 4 % | 5 % |
| PortCard ATM3 no card OUT #00C4 | 31.8 | 30.2 | -5.3 % | 5 % | 3 % |
| PortCard PENTAGON MoonSound OUT #00C4 | 31.5 | 30.8 | -2.3 % | 5 % | 13 % |
| PortCard ATM3 MoonSound OUT #00C4 | 35.1 | 33.2 | -5.4 % | 5 % | 14 % |

- **Machines without a slot-built card** (every row above: no MultiSound fitted): no row on the Pentagon or the
  ZX-Evo is slower (-0.9 to -8.9 %), the 48K / 128K reads are at parity (-0.8 to +0.4 %), the 48K / 128K writes are
  +1.3 to +6.5 % (128K `OUT #00FF` +1.9 ns). The code these rows run did not change: the unclaimed path is the same
  inline bit test, the claimed legacy path (MoonSound `#C4`) gained one compare and is 1.5-5.4 % faster. The
  48K / 128K write rows moved with code placement, not with a code path: the step-2 binary (all the port-path
  changes) measured them at parity on a quiet machine (load 7-9: 48K `OUT #00FF` 30.1 / 30.1 us, 128K 29.2 / 29.1),
  the step-3 changes (mixer rows; the benchmark never runs the mixer) moved them to +9-10 %, and relocating the new
  code (slot-card functions to the end of `portdecoder.cpp`, `SoundManager`'s new members to the end of the class,
  the slot-bus signals and the removed-chip list out of `PortDecoder`) took them to +3-6 %. A quiet-machine rerun is
  listed in TODO.md.
- **With the card** (`BM_PortSlotCard`, B only, a `[SLOTS]` section on both sides): an unclaimed port costs the same
  with and without the card (Pentagon / ATM3 `#00FD` IN 27.9 / 28.7-28.8, OUT 38.7-38.8 / 43.9 us); the card's own
  `#FFFD` on the Pentagon (CardWins, the board hidden) IN 33.9 vs 31.3 us without the card, OUT 33.1 vs 30.8; on the
  ZX-Evo (BoardWins, RdWr: the board decodes too, the board-port and removed-chip checks run) IN 54.2 vs 32.2, OUT
  50.8 vs 33.1 us, about 20 ns per access - a few hundred accesses per frame, against the card's ~1 ms frame render.

## 10. SL-5 as built (2026-10-04)

Built on branch `slots-ttd` (from `zx-bus-slots` at `140979aee`, commit `29250c546`, merged after MS-4), TTD and
slot-report code only: card building and the claim table are untouched (MS-4, §9). New file `slots/slotttd.cpp` (the `SlotManager` TTD members, declared in
`slotmanager.h`); `slotmanager.cpp` changed by one line (the fingerprint fields computed in `PlanAtCreate`).

| Item | As built |
|---|---|
| Fingerprint (R-NF-2) | `CaptureConfigFingerprint` adds `SlotManager::AddTtdFingerprint`: `slots.<slot>` = FNV-1a 64 of `card\|options\|adapter` (options as `FormatCardOptions`: every option's effective value) per fitted slot, `slots.builtin.<id>` = 1 / 0 per switchable built-in; all `affectsRestore`. Disabled cards are not fitted, not listed. Computed once at creation (the fingerprint is taken every frame by the engine). The GS RAM keeps its older field `sound.gs_ram_kb` too |
| Registration | `RegisterMachinePeripherals` registers the AY socket's board, the GS card and MoonSound as before (same ids, same blobs) but names each engine device by its slot (`SlotCardInstance`: `ay-socket.tsfm`, `zxbus.1.neogs`, `zxbus.2.moonsound`), then checks the registry against the plan (`TtdDevicesMatchPlan`): a card the plan fits has its device and the other way round, the socket's board decides the id; the GS personality may differ from the plan (the runtime switch, SL-6). A mismatch refuses recording with the slots named. `UpdatePeripheral` (the GS switch) keeps the slot name. Covox (one module for the board Covox and the cards) and the network cards keep their registration |
| Session guard | The TurboSound-slot guard, the General Sound personality guard and `PortDecoder::TtdSessionMatches` (Sprinter ISA) are one call: `SlotManager::TtdSessionMatches` compares the cards the baseline checkpoint holds (blob ids + not-recorded mask) with the live registry per position (AY socket, GS card, MoonSound card), lists every difference under the plan's slot id (`slot set differs from the recording: ay-socket: recorded tsfm, this machine ay / ts; zxbus.1: recorded gs, this machine neogs; ...`), then asks the port decoder for the machine's own slots (the Sprinter ISA population, until SL-8). `TimeTravelManager::TurboSoundSessionKindMatches` and its six unit tests are removed (cases moved to `SessionMismatchListsEveryDifference`) |
| Two instances of one module | Verified on this base: `TTDPeripheralRegistry::Register` still let a second device under a held id silently replace the first (the v1 checkpoint is keyed by the blob id). `Register` now refuses it (returns false), keeps the first and `CheckDeviceTable` names both (`two devices under id 53: zxbus.1.saa and zxbus.2.saa`), so recording is refused instead of losing a card's state. The engine side is ready (`TTDDeviceKey{type, instance}` with slot instances). No card pair of today's catalog fits two of one module (the MultiSound is not emulated), hence the registry-level test |
| R-OP-7 | `SlotManager::ChangeRefusal()` -> `RecordingGuard(TTDGuardedAction::ChangeSlots)`: `Cannot change the slot set while TTD is recording session #<n>, started at frame <f>: ...`. v1 sessions have no id: `#<n>` counts the instance's recordings (`RecordingSessionLabel`). No production caller yet (SL-6's restart path) |

**Behavior changes (intended):** the guard is symmetric - a session recorded without a socket device / GS / MoonSound
no longer loads into a machine that has one (before: loaded, the live card's state kept and counted as a missing
blob); a session recorded with a MoonSound is refused on a machine without one. The refusal texts changed:
`TtdTsfm_Test.SessionKindMismatchRefused`, `SessionWithSlotDeviceRefusedOnEmptySlot` and two
`TTDGeneralSoundSwitch_Test` cases now match the new wording. The `ay` / `ts` socket boards share blob id 0, so a v1
file cannot tell them apart; the fingerprint (v2) can.

**Unchanged:** every blob id and layout; the TTD corpus (`TTD_Corpus_Test`), the bench gate byte counts
(`TTDBench_Test.CiGate`, v1 files carry no fingerprint) and `CoreGolden` green without touching `testdata/`. v2 session
files written by the engine gain the `slots.*` fingerprint fields and the slot instance names in their device table
(no file compatibility before release).

**Checks:** full build without compiler warnings; full `core-tests` green (20 shards); MinGW `-fsyntax-only -Werror`
on every changed core translation unit; mutations (the guard always matching, the registry overwriting again) fail
`SessionMismatchListsEveryDifference`, `SessionMismatchRefused` and `TwoInstancesOfOneModuleAreRefusedByName`.

## 11. Conflicting configs refuse the machine (owner decision Q8, 2026-10-05)

[open-questions.md](open-questions.md) Q8 replaces R-CFG-3's "first wins". `SlotManager::Plan` still takes the entries
in slot order without the replace flag; for an entry whose plan is refused (not hard) it plans again with the flag to
see what it would displace, and an allowed plan may still disable a card for an accidental port clash. Each configured
entry it would remove (D1, D3 / D12), a socketed chip an explicit `ay-socket = ay` keeps in while the card needs it
out (Q7) and a D7 port clash becomes a `SlotManager::Conflict` (both slots, cards, sources, the rule and the planner's
sentence). The conflicting entry is left out (`disabled`, "conflicts with ...") so later conflicts are found too.

- `Result::Refusal()` joins them: `the [SLOTS] cards conflict, the machine is not created (Q8): zxbus.2 = gs and
  zxbus.1 = multisound: shares `gs` (D1: one function, one card); ...`.
- `PlanAtCreate` returns false, logs it as an error and applies nothing; `Core::Init` fails and keeps the reason
  (`Core::GetInitError`, the failed init releases the slot manager), `Emulator::Init` returns false with it
  (`Emulator::GetInitError`) instead of throwing on the missing core, and `EmulatorManager::CreateEmulatorWithModel` /
  `...AndRAM` hand it out as the create error (WebAPI HTTP 400 `message`, CLI, MCP).
- Unchanged: a hard refusal (a fixed built-in, D4), a missing bus signal without the override, an unknown or not
  emulated card leave that entry out and the machine starts; the Pentagon's own AY under a card is shadowed, no
  conflict. Every shipped config creates; none fits the MultiSound (owner addition to Q8).
- Planner: `RemovedCard::rule` (the rule that removes it), so the reason names D1 / D3 / D12.

Tests: `SlotManager_Test.IniConflictRefusesCreation` (replaces `IniLoadUsesSamePlan`: one pair, three pairs, a
conflict-free set), `PointlessSocketPairAndKeptChipAreConflicts` (D3 on the Pentagon, the shadowed own AY allowed, Q7
on the ZX-Evo both ways), `ConflictRefusalReachesTheCreateError` (the reason through `EmulatorManager`),
`FitOverrideNeverDisplaces` (the second GS card is a conflict too), `SlotManagerShipped_Test.FitsTheDevicesOfMaster`
(the zx-diagnostics config, parsed only, plans without a refusal); `MultiSoundSlotCard_Test.MatrixConflictsRefuseCreation`
(was `MatrixRefusalsHoldAtCreation`: `multisound` + `gs` either order, `tsfm` under the card, `ay-socket = ay` on the
ZX-Evo refused with their rules; the 128K edge without IORQGE still starts without the card) and
`TakesTheZxEvoYm2149OutOfItsSocket` (the explicit `ay-socket = ay` is refused). No catalog pair produces a D7 clash
today (the matrix has no **P** cell), so that path is covered by the plan engine's own tests only.

**`#BFFD` claimed In + Out (2026-10-05).** The ZX-MultiSound's CPLD decodes IORQGE from the address and M1 only, so an
`IN #BFFD` asserts it while the card leaves the data bus alone (its YM data port is write-only). The reference data
claimed `#BFFD` for writes only, so a Pentagon read of `#BFFD` went to the board's decode and its shadowed AY answered.
The claim is `InOut` now; the card's read returns "not driving", the claim table hides the cycle from the board
(CardWins) and nobody drives: the read floats. On the ZX-Evo (BoardWins, RdWr) nothing changes: the board AY's
`#BFFD` claim is write-only, so the planner sees no new read overlap. Regenerated tables: only the card row's
`#C00F/#800D` cell lost its "write" note. Tests: `MultiSoundSlotCard_Test.ClaimsAssertIorqgeWhereTheRtlDoes` (every
port, both directions, claims against the RTL's IORQGE term), `PentagonCardShadowsTheBoardAy` (`IN #BFFD` floats, the
board decodes nothing).

## 12. Quiet-machine rerun of the MS-4 A/B (2026-10-05)

A = `bin/core-benchmarks-ms4base` (`140979aee`, the branch before MS-4), B = the tree after MS-4, SL-5 and MS-5 steps
1-3 (Q8 refusal, `#BFFD` claim, MultiSound TTD). `BM_PortIn` / `BM_PortOut` / `BM_PortCard` / `BM_PortSlotCard`,
`UNREAL_NICE=0`, `--benchmark_min_time=0.3s`, two runs of eight rounds interleaved (A B, B A, ...), each round started
only with the 1-minute load below 12; loads per run 3.2-16.8 (one round each in two runs ended at 15-35 when another
build started; those rounds are kept, the medians are robust to them). Then the same against B with the configuration
fix below (another 2 x 8 rounds, loads 3.2-14.3). CPU time medians of 16 rounds:

| Benchmark (us per 1000 accesses, median of 16) | A | B | B/A | A (2nd pair) | B config fix | fix/A |
|---|--:|--:|--:|--:|--:|--:|
| PortIn 48K IN #00FF | 37.8 | 37.5 | -0.8 % | 37.5 | 37.0 | -1.3 % |
| PortIn 128k IN #00FF | 42.1 | 42.0 | -0.2 % | 41.7 | 41.4 | -0.8 % |
| PortIn PENTAGON IN #00FF | 39.7 | 38.6 | -2.7 % | 39.5 | 38.2 | -3.2 % |
| PortIn 48K IN #40FF | 50.0 | 49.7 | -0.6 % | 49.7 | 49.0 | -1.4 % |
| PortIn 128k IN #40FF | 52.4 | 51.5 | -1.7 % | 51.7 | 50.9 | -1.5 % |
| PortIn PENTAGON IN #40FF | 39.8 | 38.7 | -2.6 % | 39.4 | 38.2 | -3.1 % |
| PortIn 48K IN #00FE | 49.4 | 48.8 | -1.2 % | 49.0 | 47.8 | -2.5 % |
| PortIn 128k IN #00FE | 52.3 | 51.3 | -2.0 % | 51.4 | 50.5 | -1.8 % |
| PortIn PENTAGON IN #00FE | 45.3 | 42.0 | -7.4 % | 44.7 | 41.4 | -7.3 % |
| PortIn 48K IN #FFFD | 34.9 | 34.2 | -1.9 % | 34.5 | 34.0 | -1.6 % |
| PortIn 128k IN #FFFD | 38.7 | 38.1 | -1.4 % | 38.0 | 37.7 | -0.8 % |
| PortIn PENTAGON IN #FFFD | 36.4 | 33.9 | -6.9 % | 36.0 | 34.1 | -5.1 % |
| PortOut 48K OUT #00FF | 29.3 | 29.9 | +2.1 % | 29.0 | 29.5 | +1.5 % |
| PortOut 128k OUT #00FF | 28.5 | 29.0 | +1.7 % | 28.0 | 28.5 | +1.9 % |
| PortOut PENTAGON OUT #00FF | 28.3 | 27.5 | -2.9 % | 27.8 | 27.3 | -1.9 % |
| PortOut 48K OUT #40FF | 39.4 | 39.3 | -0.2 % | 38.5 | 38.9 | +1.1 % |
| PortOut 128k OUT #40FF | 38.3 | 38.1 | -0.4 % | 37.5 | 37.9 | +1.3 % |
| PortOut PENTAGON OUT #40FF | 28.3 | 27.6 | -2.6 % | 27.8 | 27.3 | -1.8 % |
| PortOut 48K OUT #00FE | 70.2 | 70.2 | +0.0 % | 68.7 | 69.4 | +1.0 % |
| PortOut 128k OUT #00FE | 71.2 | 70.6 | -0.8 % | 69.6 | 70.0 | +0.6 % |
| PortOut PENTAGON OUT #00FE | 40.9 | 40.6 | -0.9 % | 40.6 | 40.2 | -1.0 % |
| PortCard PENTAGON no card IN #00FD | 29.9 | 27.4 | -8.4 % | 29.6 | 27.7 | -6.4 % |
| PortCard ATM3 no card IN #00FD | 30.2 | 28.6 | -5.1 % | 29.8 | 28.3 | -5.1 % |
| PortCard PENTAGON MoonSound IN #00FD | 29.8 | 27.4 | -7.8 % | 29.5 | 27.6 | -6.5 % |
| PortCard ATM3 MoonSound IN #00FD | 30.2 | 28.7 | -4.9 % | 29.8 | 28.4 | -4.9 % |
| PortCard PENTAGON no card OUT #00FD | 39.4 | 39.0 | -0.8 % | 39.1 | 39.9 | +2.0 % |
| PortCard ATM3 no card OUT #00FD | 44.2 | 43.9 | -0.5 % | 43.7 | 44.2 | +1.3 % |
| PortCard PENTAGON MoonSound OUT #00FD | 39.5 | 39.0 | -1.5 % | 39.1 | 39.9 | +2.0 % |
| PortCard ATM3 MoonSound OUT #00FD | 44.0 | 43.9 | -0.2 % | 43.8 | 43.9 | +0.2 % |
| PortCard PENTAGON no card IN #00C4 | 30.0 | 27.8 | -7.3 % | 29.9 | 28.0 | -6.4 % |
| PortCard ATM3 no card IN #00C4 | 30.9 | 29.8 | -3.7 % | 30.8 | 29.1 | -5.6 % |
| PortCard PENTAGON MoonSound IN #00C4 | 37.0 | 35.7 | -3.6 % | 36.9 | 35.6 | -3.7 % |
| PortCard ATM3 MoonSound IN #00C4 | 36.7 | 36.1 | -1.7 % | 36.6 | 35.6 | -2.9 % |
| PortCard PENTAGON no card OUT #00C4 | 39.2 | 39.2 | -0.0 % | 39.2 | 39.7 | +1.3 % |
| PortCard ATM3 no card OUT #00C4 | 30.9 | 30.2 | -2.5 % | 30.9 | 29.9 | -3.0 % |
| PortCard PENTAGON MoonSound OUT #00C4 | 30.9 | 30.5 | -1.5 % | 30.9 | 30.3 | -1.9 % |
| PortCard ATM3 MoonSound OUT #00C4 | 34.4 | 32.7 | -5.0 % | 34.2 | 32.3 | -5.6 % |

- **Machines without a slot-built card:** every row is at parity or faster except the 48K / 128K writes to an
  undecoded port (`OUT #00FF`), +1.5 to +2.1 % (about 0.5 ns per access), slower in 6-8 of 8 paired rounds in every
  run; `#40FF` / `#00FE` writes are within +-1.3 %, reads -0.2 to -7.4 %.
- **Cause looked for:** the machine code of everything that path runs (`Z80::Z80Step`, `Z80::out`,
  `PortDecoder_Spectrum48::DecodePortOut` / `Spectrum128`, `PortDecoder::GetPCAddressLocator` / `OnPortOutComplete`,
  the logger, memory) was compared function by function between A and B (`objdump`, addresses normalized). B differed
  from A only in field offsets: MS-4's `CONFIG::midiBank` (a `std::string`) grew `CONFIG` by 24 bytes and moved every
  `EmulatorContext` member after it, so `Z80::out` and `Z80::Z80Step` addressed `emulatorState` 24 bytes further.
  **Fix:** the `[MIDI] Bank=` value lives in the configuration loader (`Config::GetMidiBank`, read through
  `Emulator::GetConfigLoader`, like the media set), not in `CONFIG`; with it, the instructions of those functions are
  identical to A's.
- **After the fix** the `OUT #00FF` rows still measure +1.5 / +1.9 % (and the other 48K / 128K writes moved by +0.6 to
  +1.3 %, the Pentagon's by -1.0 to -1.9 %) with byte-identical code: the difference is where the linker places the
  functions (every function on the path starts at another offset within its cache line: `DecodePortOut` 32 -> 4,
  `Z80::out` 40 -> 52, `Z80Step` 56 -> 4 bytes into a 64-byte line), which moves with any code added to the binary.
  It is not the cost of a code path a machine without the card runs; making it disappear needs a layout decision for
  the whole build (function alignment, a hot / cold order file), an owner question in [TODO.md](TODO.md).
  Raw results: the session's `scratch/ms5/ab-*` (not in the repo).

## 13. Slot-built cards under time travel (MultiSound MS-5, 2026-10-05)

The framework side of [MultiSound tdd-integration.md](../2026-10-03-zx-multisound/tdd-integration.md) §4.1:

| Item | As built |
|---|---|
| `ICard` | `CollectTtdDevices(std::vector<CardTtdDevice>&)` (id, device, instance, region source), `TtdFingerprint`, `TtdSessionMatches` (defaults: none / none / match) |
| `CardType` | `ttdIds`: the ids the card's devices register, its own first (MultiSound: 58, 53, 59, 60) |
| Registration | `RegisterMachinePeripherals` registers every built card's devices before the plan check |
| `TtdDevicesMatchPlan` | every fitted slot-built card has every id its type declares; a type without ids (no time-travel state) refuses recording naming the slot; the slot-built card types are positions of their own (first id), as the AY socket, GS card and MoonSound are |
| `TtdSlotSetMatches` / `TtdSessionMatches` | the same positions on load (`zxbus.1: recorded none, this machine multisound`), then each card's `TtdSessionMatches` |
| Fingerprint | `_ttdCardFingerprint` from the built cards (`BuildCards`), added after the slot set's fields |
| Ids | peripheral 58 `MultiSound`, 59 `Sam2695`, 60 `MultiSoundGs`; region 17 `MultiSoundGsRam` (next free on this branch; `ttd.ksy`, `ttdfileinfo.cpp` names, the contract test's id table) |

Before this, a machine with a slot-built card recorded sessions that silently lacked the card's state; SL-5's guard
did not see the card either. Tests: `TtdMultiSound_Test.*` (`core/tests/debugger/ttd/ttdmultisound_test.cpp`) and
the card in `TTDModelStateContract_Test.EveryDeviceMatchesItsDescriptorOnEveryModel`. No device format changed:
corpus, bench gate and `CoreGolden` unchanged.


## 14. SL-6 as built (2026-10-05)

Every slot change is planned against the instance's slot set and applied by restarting the machine (owner decision Q6,
R-OP-3 / R-OP-8); a model switch carries the slot set (R-OP-9); the General Sound personality is on the plan.

| File | Content |
|---|---|
| `core/src/emulator/slots/slotmanager.{h,cpp}` | `ConfigOf(Result)` (the fitted set as `[SLOTS]`: given options, adapter, fit override, every switchable built-in), `UseSlots(SlotConfig, CONFIG&)` (the `[SLOTS]` section and the card fields it stands for, as the parser leaves them), `PlanChange` (static: the plan engine over the fitted set, the new `[SLOTS]`, checked as the machine's creation checks it; instance: plus the media with unsaved writes and the TTD guard), `Carry` + `CarryReport` (model switch), `GeneralSoundRequest` / `GeneralSoundSwitchRefusal` / `FollowGeneralSoundSwitch` (personality), `Snapshot()` (a copy under a mutex for another thread), `BuildCards()` returns false with `BuildError()` when a card cannot be built, `SetBuildFaultForTests` |
| `core/src/emulator/slots/slotchange.{h,cpp}` | `SlotChange::Run(SlotChangeRequest)` -> `SlotChangeResult` (status `applied` / `dry-run` / `refused` / `recording` / `no-machine` / `failed`, message, the change plan, the restarted emulator, the previous id, whether the old one ran, the media report, stranded media) |
| `core/src/emulator/media/modelswitch.{h,cpp}` | `ModelSwitchRequest::slotSet` (the new machine's exact set: a restart), `keepConfigOverride` (the old instance's own create override applied again: same model), carrying by default; `ModelSwitchResult::slotCarry` (kept / dropped cards), the slot lines first in `result.report` |
| `core/src/emulator/emulator.h` | `GetConfigOverride()` |
| `core/src/emulator/cpu/core.cpp` | a card that cannot be built refuses the machine with the reason (`Core::GetInitError`) |
| `core/src/emulator/sound/soundmanager.cpp` | the personality request asks the plan; a done switch (and the `gs_lightweight` card fitted at creation) moves the plan |
| `core/src/emulator/slots/slotttd.cpp` | `TtdDevicesMatchPlan` compares the General Sound personality too |

**A change, step by step.** `SlotChange::Run` finds the instance and its `SlotManager`, which plans the request
(`slots::SlotRequest`: plug / remove / set options, `replaceIfIncompatible`, `dryRun`, `mediaDisposition`) with the
media that hold unsaved writes as `PlanContext::dirtyMedia`. Refused: a TTD user recording runs (R-OP-7, the
`ChangeRefusal()` text naming the session, status `recording`, also for a dry run); the plan engine refuses it or it
needs the replace flag (`refused: ...` / `needs replaceIfIncompatible: ...`, the plan listing every card it would
remove with its full options for an undo); a card the plan would plug in but disable for an accidental port clash
(R-COMP-6), since a configuration with such a pair is not created (Q8); the resulting `[SLOTS]` planned as at creation
(`Plan` on a copy of the configuration) has a conflict or a card the emulator cannot build. Allowed and not a dry run:
`ModelSwitch::Run` with the same model and RAM, `slotSet` = the new set, `keepConfigOverride`, the removed cards' dirty
media handled by the disposition (none: refuse; save; discard). The new machine is created next to the old one; if
that fails (a card cannot be built: `SlotChange_Test.FailedStartKeepsTheMachine`), the old one has never stopped and
keeps its slot set, and the result says why. Applied, the old machine is released, the media move by id
(`MediaManager::TakeMediaSet` / `AdoptMediaSet`: the live medium with its unsaved writes), the removed card's media are
reported closed (or detached), and the new emulator (a new id, as after a model switch) is created, not started:
`wasRunning` tells the caller to start it. The new set is the new instance's `[SLOTS]` (its create override), so the next
change or model switch plans from it.

**The model switch carries the slot set** (`SlotManager::Carry`, run inside the new machine's config override, after
its INI is loaded and the process hook has run): the old machine's cards (its plan's fitted entries, `ConfigOf`) come
first, each in the slot it had when the new machine has that bus, else where the planner puts the card
(`SuggestSlot`, numbered after the kept slots of that bus) behind the adapter that connects its bus. Planned alone first,
the carried cards the new machine cannot take at all (a missing bus signal without the override, a fixed built-in, a
card not emulated there) are dropped with the reason before they could push out one of its own cards. Then the new
machine's own configured cards (its `[SLOTS]`, or its legacy keys, with the fields code changed after the INI) fill the
slots left free, and in a conflict (Q8) the new machine's card gives way to the carried one; between two carried cards
the later in slot order goes. Built-in switches: the new machine's, overridden by the carried ones with the same id. The
report (`ModelSwitchResult::slotCarry`, lines also in `result.report`): kept cards (`zxbus.2 -> edge.2 = soundrive`),
dropped ones with the reason (`zxbus.1 = multisound not carried to ZX-Spectrum 128k: ...`), the new machine's cards that
gave way. Example (`CarryKeepsTheCardsTheNewMachineTakes`): Pentagon `zxbus.1 = multisound` -> the shipped ZX-Evo set:
the MultiSound holds `zxbus.1` (the ZX-Evo's NeoGS was there), its `ay-socket = ts` and SounDrive give way, the MoonSound
stays, the YM2149 leaves its socket for the card (Q7).

**General Sound personality.** As a slot change (R-OP-8): `SlotManager::GeneralSoundRequest(current, config, kind)`
builds the plug of `gs` / `gs-lw` / `neogs` into the slot the fitted GS card is in, replacing it (the replace flag
set: replacing that card is the request; the RAM option as the legacy keys give it), and `SlotChange::Run` applies it by
the restart. The surfaces' existing frame-boundary switch (`SoundManager::requestGeneralSoundCardSwitch`, used by the
WebAPI, CLI, MCP, Lua, Python, Qt and the `gs_lightweight` feature) stays in place until SL-7 moves the surfaces (owner
question in [TODO.md](TODO.md)), but now runs through the plan: a real change is refused when the plan refuses it or
would remove anything besides the card in the GS slot, and a done switch (`switchGeneralSoundCard`, also the lightweight
card the feature fits at creation) moves the plan's GS slot and the TTD fingerprint with it - the fingerprint equals the
one of a machine created with that card. `TtdDevicesMatchPlan` therefore checks the personality now (before: any GS
device matched a GS slot).

**Deviations from the design, with the reason:**

1. **"Restore of the previous configuration if the start fails"** needs no restore: the model-switch path creates the
   new machine first, so the old one keeps running until the new one exists.
2. **No in-place restart:** an applied change gives a new emulator id, as a model switch does (`previousEmulatorId` in
   the result); the symbolic id and the selection carry over.
3. **A card the plan plugs in but disables (D7)** is refused as a change: since Q8 a configuration with such a pair is
   not created, so applying it would fail at the restart.
4. **A model switch merges** the carried cards with the new machine's own configured cards (owner question in
   [TODO.md](TODO.md)); removals are not carried: a card the user took out of the old machine comes back if the new
   machine's config fits it.
5. **The instance's create-time override** (machine variant board, create options, a test's `[SLOTS]`) is applied again
   on a restart (same model), not on a model switch (as before: it belongs to the old model).
6. **A fitted card that cannot be built** (its factory fails or throws) refuses the machine now (before: silently left
   out), with the reason in the create error.

**Tests** (each under 50 ms; a restart builds a second machine, 3-34 ms): `SlotChange_Test.*` (8:
`PlugRestartsTheMachineWithTheNewSet`, `DisplacementNeedsTheReplaceFlag`, `DryRunChangesNothingAndOptionsRestartToo`,
`RefusedWhileTtdRecords`, `FailedStartKeepsTheMachine`, `MediaCarriedAcrossTheRestart`,
`RequestsThatCannotApplyAreRefused`, `GsPersonalitySwitchIsSlotReplace`), `SlotManager_Test.CarryKeepsTheCardsTheNewMachineTakes`,
`CarryReportsTheCardsTheNewMachineCannotTake`, `ModelSwitch_Test.CarriesTheSlotSet`,
`TtdSlots_Test.RuntimePersonalitySwitchMovesThePlanAndTheFingerprint`; changed: `TtdSlots_Test.RegistryFollowsThePlan`
(a lightweight card under a `gs` slot is a mismatch now; under a `gs-lw` slot it matches). Full `core-tests` green after
every step (one run of step 3 lost a shard to `EmulatorStepOverObserver_Test.DestroyedEmulatorLeavesNoHandlerBehind`, a
debugger race outside the slot code - the test alone segfaults within 40 repeats in `BreakpointManager::GetBreakpointById`
on the emulation thread while the test thread stops the machine; the rerun was green). MinGW `-fsyntax-only -Werror` clean
on every changed core translation unit. No benchmark: no per-instruction or per-port path changed, and no `CONFIG` /
`EmulatorContext` field was added.

**Not in SL-6:** the surfaces (SL-7: `SlotControl` over `SlotChange::Run` and `PlanChange`, 1:1 with the request and
result types above); `NetworkManager::RequestChange` (the runtime network card change) still bypasses the slot set;
snapshots that carry a slot set (architecture.md §8).

## 15. SL-7 as built (2026-10-05; MultiSound MS-6 alongside)

Every surface reads and changes the slots through one core layer, `SlotControl` (`core/src/emulator/slots/slotcontrol.{h,cpp}`):
`Execute(SlotControlRequest) -> SlotControlReply` with the verbs `list` (DeviceState::Slots), `catalog` (every card of
the reference data with its options, functions, ports, media and how it fits this machine: the planner's suggested
slot, fit, `fits` / `needs-replace` / `refused`, what it would remove), `matrix` (the generated tables), `plug` /
`remove` / `set` (SlotChange::Run; the reply 1:1 from SlotChangeResult: status, message, plan with every removed card's
options and `undo`, shadowed devices, lost functions, media, the new `[SLOTS]` lines, the restart with the new id, the
media report) and `gs` (the General Sound personality, Q10). HTTP status: 200 applied / dry-run, 409 refused /
recording, 400 bad request, 404 no machine, 500 failed. `SlotControl::CarryValue` is the model switch's slot carry on
every surface, `SlotControl::CreateOverride` the create-time `"slots": {...}` form (architecture §9).

| Surface | As built |
|---|---|
| WebAPI | `GET /slots`, `/slots/catalog`, `/slots/matrix[?table=]`, `POST /slots/{slot}/plug|remove|options`, `PUT /slots/{slot}/options` (`api/slots_api.cpp`); create / start take `"slots"`; the model switch reply has `slots` (SlotCarry) and `report`; `POST /control/audio/gs` `switch_personality` is the `gs` verb; OpenAPI `openapi_slots.inc`, `openapi_slots_schemas.inc` (tag `Slots`) |
| CLI | `slots [list|catalog|matrix|plug|remove|set|gs] ... [--replace] [--dry-run] [--media] [--adapter] [--json]` (`cli-slots.h`, header-only, parsed and rendered in core-tests); `gs switch_personality` -> `slots gs`; `model` prints the carry lines |
| MCP | `inspect_state` aspect `slots`; `emulator_manage` `slots_catalog`, `slots_matrix`, `slots_plug`, `slots_remove`, `slots_set` (`replace_if_incompatible`, `dry_run`, `media_disposition`), create `slots`, `switch_model` prints the carry, `gs_switch_personality` shows the plan and the restart |
| Lua | `slots_state()`, `slots_catalog()`, `slots_matrix([table])`, `slots_plug(slot, card, opts)`, `slots_remove(slot, opts)`, `slots_set(slot, opts)`, `slots_gs(card, opts)`; an interpreter bound to the restarted instance follows it |
| Python | module functions `unreal.slots_*` with `emulator_id` (a change replaces the machine, so not on `Emulator`); `Emulator.gs_switch_personality` -> the restart |
| Qt | Machine > Slots (`unreal-qt/src/cardslots/slotswindow.*`): the tree, the catalog with fit / outcome, option editors (sets as check boxes: the MultiSound DIP), live plan preview; `SlotChangeController`: plan with the replace flag (Q1), confirm the restart (Q6), dirty media save / discard, the removed cards named with Undo (`SlotChangeRequest::slotSet`, `SlotManager::PlanSet`); the audio settings' GS combo goes through it (Q10); the model switch names the cards not carried |

**Core additions:** `SlotManager::PlanSet` / `CreationRefusal` (an exact slot set checked as a creation, the TTD guard),
`SlotChangeRequest::slotSet` (Undo); `slotchange.h` / `slotcontrol.h` guard the Qt `slots` macro.

**Deviations, with the reason:**

1. `GET /slots/matrix` returns the markdown tables (the reviewed document's form); the per-machine fit of every card is
   the catalog's `thisMachine`.
2. Python's slot functions are module functions with `emulator_id`, not `Emulator` methods: a change destroys the
   object the method was called on.
3. The MultiSound recipe is `.recipe/peripherals/multisound.md` (the library keeps sound cards in `peripherals/`), not
   `.recipe/sound/`.
4. File names `slots_api.cpp`, `openapi_slots.inc` follow their directories (`*_api.cpp`, `openapi_*.inc`).
5. The runtime network card change (`network set card=`) still works in place: open question Q11.

**Tests:** `SlotControl_Test.*` (8: the §2.5 parity script - list, catalog, matrix, refused plug with the plan equal
to `SlotManager::PlanChange` field by field, the plug with the flag, options, remove, dry run -, GS personality, the
recording refusal, bad requests, the create-time form, Undo by slot set, the carry value), `CliSlots_Test.*` (2),
`McpTools_Test` slot tests (7, the GS switch test updated to the restart reply), `SlotsWindow_Test.*` in
`unreal-qt-tests` (3: preview, plug + warning + Undo, cancel; options + Undo button; the MIDI view). The WebAPI routes
and the Lua / Python bindings are thin adapters over the tested layer; they were verified against a running unreal-qt
(own ports, `.recipe/machines/slots.md`): MCP, WebAPI, CLI and Lua live; Python by `-fsyntax-only` (the build does not
enable it).
