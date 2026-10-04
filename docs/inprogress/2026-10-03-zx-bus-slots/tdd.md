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
| **SL-3** | Rule migration: R6, `OverrideDecodeForFullDecodeClaim`, self-decoding dispatch, the exact port map's peripheral entries become claims; the three old mechanisms removed one at a time | each removal its own merge with the TTD corpus and machine boot tests green | L |
| **SL-4** | Card migration, one per merge: `ay`/`ts`/`tsfm` (socket), `gs`/`gs-lw`/`neogs`, `moonsound`, `covox-fb`/`soundrive`, `zxnetusb`/`zx-wifi`; `[SLOTS]` config + legacy key translation; `data/configs` converted | per card: its existing tests unchanged and green, A/B on its port path, TTD fixtures unchanged | L |
| **SL-5** | TTD: slot set in the configuration fingerprint, `SlotManager` as the source of fitted devices in `RegisterMachinePeripherals`, the session population guard generalized (from the Sprinter's `TtdSessionMatches`) | TTD tests §2.4 green; corpus unchanged | M |
| **SL-6** | Apply by restart: write the slot set into the configuration, restart through the model-switch path, media carried over with the stranded-media rules, restore of the previous configuration if the start fails; model switch carrying the slot set; the GS personality switch moved onto it | tests §2.3 green | M |
| **SL-7** | Surfaces: `SlotControl`, `DeviceState::Slots`, WebAPI + OpenAPI, CLI, MCP, Lua, Python, Qt slot window, recipe `.recipe/machines/slots.md`, user doc `docs/features/slots.md` | automation parity tests §2.5 green; recipe verified against a running emulator | M |
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
| `SlotManager_Test.IniLoadUsesSamePlan` | an INI with a clash: first wins, later disabled, machine starts |
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

| Test | Checks |
|---|---|
| `SlotManagerApply_Test.ChangeRestartsMachine` | an allowed plug restarts the instance with the new slot set; no card object is created in the running machine |
| `SlotManagerApply_Test.FailedStartRestoresPreviousConfig` | a card factory throws at start: the previous configuration is started again, the reply carries the error |
| `SlotManagerApply_Test.MediaCarriedAcrossRestart` | media in kept cards survive the restart; removed NeoGS: `sd.ngs` reported as stranded, dirty medium refused without a disposition |
| `SlotManagerApply_Test.GsPersonalitySwitchIsSlotReplace` | the GS personality switch goes through the plan and a restart |
| `SlotManagerApply_Test.ModelSwitchCarriesSlots` | Pentagon -> ZX-Evo keeps fitting cards; ZX-Evo -> 128K reports the MultiSound as not fitting |

### 2.4 TTD (`ttdslots_test.cpp`)

| Test | Checks |
|---|---|
| `TtdSlots_Test.FingerprintHasSlotSet` | slot set and options in the session fingerprint |
| `TtdSlots_Test.SessionMismatchRefused` | load a session into a machine with another slot set: refused, difference listed |
| `TtdSlots_Test.MigratedCardsKeepBlobIds` | blob ids and layouts unchanged after SL-4 (corpus round-trip) |

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
**Skipped (later phases):** `RefusedWhileTtdRecords` (SL-5: the TTD session guard), `IniLoadUsesSamePlan` and
`LegacyKeysTranslated` (SL-4: `[SLOTS]` and legacy key translation).

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
