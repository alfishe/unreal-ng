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
| **SL-1** | Core types: `BusDeclaration`, `CardType`, `CardCatalog`, `ICard`, `PortClaim`, `SlotManager::Plan` (pure), `DescribeBuses()` on every decoder (data only, no behavior change) | plan tests §2.1 green; no production path uses it yet | M |
| **SL-2** | `PortClaimTable` with IORQGE and passive claims, shadowing, read-conflict rule, ROM-fetch lock; wired into the Z80 funnel behind the existing mechanisms (they register into it) | claim tests §2.2 green; all of `core-tests` green; A/B benchmark: no regression on the port hot path, the no-card case at least as fast | L |
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
| `PortClaimTable_Test.RebuildOnlyOnSlotChange` | no allocation on the access path |
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
