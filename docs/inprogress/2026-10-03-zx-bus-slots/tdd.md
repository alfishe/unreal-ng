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
