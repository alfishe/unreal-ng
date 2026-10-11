# Bug Registry

> ### 📌 Registry System Rules & Annotations
> 1. **Grouping & Numbering:** Bugs are grouped chronologically by their discovery date. The bug number (`#X`) increments sequentially within that specific date group.
> 2. **Retention Policy:** Closed bugs must be completely wiped from this tracker **no later than one week** after their `Date Fixed` to keep the file lean.
> 3. **Related Documents:** If a bug relates to functionality that is currently in progress, you must update the corresponding files in `docs/inprogress/*.md` and `PLAN.md` with relevant details or task dependencies.
> 4. **Methodology:** The tracking process (triage → confirmation → root cause → fix verification ladder) and the agent permission rules are defined in [`docs/testing/bug-tracking-guide.md`](../testing/bug-tracking-guide.md). Status markers: 🔴 Open → 🟠 Confirmed → 🔵 In Progress → 🟣 Fix Proposed → 🟢 Fixed (set by developer only).

---

2026-10-11
## 🟢 [Fixed] #1: The .z80 loader frees its staging pages with `delete` instead of `delete[]`
* **Date Opened:** 2026-10-11
* **Date Fixed:** 2026-10-11
* **Commit ID:** 9643b9623
* **Found by:** a full `core-tests` run under valgrind memcheck on Linux (gcc 14, RelWithDebInfo, 12 shards; host `.238`, 2026-10-10/11): `Mismatched free() / delete / delete []` and `Mismatched new/delete size value: 1` in
  `LoaderZ80::freeStagingMemory()` (`loader_z80.cpp:1552`, `:1563`), 33 times.

### Description
Every `.z80` load ends in undefined behavior: the staging ROM / RAM pages come from `new uint8_t[PAGE_SIZE]` and go
back through scalar `delete`. The default allocators hide it today; a sized or instrumented allocator does not.

### Fix
`delete[]` for both page tables.

### Requirements / Acceptance Criteria
- [ ] The `LoaderZ80` tests are clean under valgrind memcheck (no mismatched delete).

## 🟢 [Fixed] #2: `RESET=` missing from the ini compares an uninitialized buffer; the reset ROM can be anything
* **Date Opened:** 2026-10-11
* **Date Fixed:** 2026-10-11
* **Commit ID:** 202db83c2
* **Found by:** a full `core-tests` run under valgrind memcheck on Linux (gcc 14, RelWithDebInfo, 12 shards; host `.238`, 2026-10-10/11): `Conditional jump or move depends on uninitialised value(s)` in
  `StringHelper::CompareCaseInsensitive` from `Config::ParseConfig` (`config.cpp:257`), 74 times
  (`Config_Test.OldFmTrimDbCountsFromTheOldBaseline` and every test loading an ini without `RESET=`).

### Description
`Config::CopyStringValue` writes nothing when the key is absent (`GetValue` returns `nullptr`), so `line` keeps whatever
it held: uninitialized stack memory at the `RESET=` lookup. That garbage is matched against the `RESET=` names (`SOS`,
`128`, `DOS`, `SYS`, ...): a config without `RESET=` can start with a ROM other than the documented default `RM_SOS`.
The other `line` lookups in the parser clear the buffer first; this one did not.

`StringHelper::CompareCaseInsensitive(a, b, len)` compares `len + 1` characters. Not a defect: with
`len == strlen(b)` that makes it an exact match, and its callers rely on it (now said in its header).

### Fix
`line` is cleared before the `RESET=` lookup, as at the other lookups.

### Requirements / Acceptance Criteria
- [x] `Config_Test.ResetRomMissingIsBasic48`: `RESET=128` gives `RM_128`, then a file without `RESET=` gives `RM_SOS`
  (passes before the fix too unless the stack holds a ROM name: valgrind is the evidence).
- [ ] No uninitialised-value report from `Config::ParseConfig` under valgrind.

## 🟢 [Fixed] #3: A test thread left joinable after a failed check aborts the whole test process
* **Date Opened:** 2026-10-11
* **Date Fixed:** 2026-10-11
* **Commit ID:** 131b357de
* **Found by:** a full `core-tests` run under valgrind memcheck on Linux (gcc 14, RelWithDebInfo, 12 shards; host `.238`, 2026-10-10/11): shard 2 died with `terminate called without an active exception` in
  `TimeTravelManager_PublishedInfo_Test.AutomationReadsWhileTheLoopRecords`; the rest of the shard never ran.

### Description
Under valgrind the test's wait for the machine timed out; the `ASSERT` returned from the test body while its
`std::thread` was still joinable, and the thread's destructor called `std::terminate`. A slow host (valgrind, a loaded
CI runner) turned one failed expectation into a lost run.

### Fix
`core/tests/_helpers/testthread.h`: `TestThread(body, stop)` stops (the loop's flag, the gate, the lease, ...) and joins
on every exit path. Used where an `ASSERT` sits between a thread's start and its join: the TTD published-info test,
`ExecutionArbiter_Test` (8 threads), `ExecutionArbiterMachine_Test` (3), `EmulatorManager_Test` (2),
`MediaControl_Test.SyncWaitsForTheSlotAsyncDoesNot`, `GdbSessionTakeBack_Test`. The other threaded tests were checked:
they join before any fatal check, or their thread lives in a class that joins in its destructor.

Seen on the way, not changed here: `AudioRingBuffer_Test.SPSC_ConcurrentIntegrity` hangs at the producer's join when
its sequence check fails (the consumer stops reading); `EmulatorManager_Test.RemovalDoesNotHoldTheLockAHandlerWaitsFor`
leaves its MessageCenter observer (it captures locals by reference) registered when an early `ASSERT` fails.
Both fixed afterwards (`test-fixes`): the SPSC producer stops when the stream breaks, the consumer has a deadline and
the ring is no longer static (a broken stream injected on purpose now fails 3 / 3 repeats in 0 ms instead of hanging);
the observer is unregistered by a guard on every exit path.

### Requirements / Acceptance Criteria
- [ ] The changed tests pass; the next valgrind run loses no shard to `std::terminate`.

## 🟢 [Fixed] #4: Leaks reported in the test fixtures (`definitely lost`)
* **Date Opened:** 2026-10-11
* **Date Fixed:** 2026-10-11
* **Commit ID:** befe0e730
* **Found by:** a full `core-tests` run under valgrind memcheck on Linux (gcc 14, RelWithDebInfo, 12 shards; host `.238`, 2026-10-10/11).

### Description
Biggest records, all reached from test code (production frees the same objects):
- `Keyboard_Test::SetUp` (`keyboard_test.cpp:10`): the `EmulatorContext` was never deleted (190 KB a test).
- `DiskAutostart_Test::LoadImage`: the image `LoaderTRD::parse` returned (1.2 MB a test). The loader hands its image
  over and does not free it; the test's comment said the opposite.
- `ScorpionMachineFixture` and the other fixtures that build a `Core` without an `Emulator`: the four `FDD`s
  `WD1793::WD1793` creates (`wd1793.cpp:91`). `Emulator::Release` frees them (`emulator.cpp:739`); a bare core does not.

### Fix
`Keyboard_Test` deletes its context; `DiskAutostart_Test` owns the loaded image; `EmulatorTestHelper::ReleaseBareCore`
frees the drives, the core and the context in the order of `Emulator::Release`, used by the Scorpion, Profi, TS-Conf,
Sprinter, models-regression and Pentagon border fixtures. Fixtures that never delete their `Core` at all (the Z80,
video and loader tests that build one per test) are left for the valgrind profile of PLAN.md #109.

### Requirements / Acceptance Criteria
- [ ] No `definitely lost` record from these fixtures in the next valgrind run.

## 🟢 [Fixed] #5: ZX-Poly status reads the members' Z80 registers while they run
* **Date Opened:** 2026-10-11
* **Date Fixed:** 2026-10-11
* **Commit ID:** a5aa1b910
* **Found by:** ThreadSanitizer on Linux (gcc 14, `docker/linux/build.sh --tsan`, host `.238`, 2026-10-11):
  `ExecutionArbiterMachineFuzz_Test.ZXPolyGroupUnderConcurrentControl`, data races in `Z80::IncrementCPUCyclesCounter`
  and `Z80::HandleINT` (a member stepped by `Emulator::ExecuteStep`) against `ZXPolyGroup::ReadControlState`
  (`zxpolygroup.cpp:1879-1881`) from `GetStatus` → `CheckLockstep` on another thread.

### Description
Before the first frame boundary after a load, a replication or a TTD restore (`_boundaryChecked` false),
`CheckLockstep` compares the members' live control state (PC, SP, I, IM, IFF1, HALT, T). It only waited for the
pipelined slave work; a member stepped or run on its own (automation `run_frames` / `step` on that instance, the
master's loop) could be inside an instruction. `GetStatus` serves the WebAPI, CLI, Lua and Python status, so a status
request could report a divergence that is not there (or miss one) and is a data race by the standard.

### Fix
In that branch `CheckLockstep` takes an `Emulator::CpuHold` of every member, the master first (as `GroupHold` does),
then reads. At a boundary it still reads the copies taken there, without holding anything.

### Requirements / Acceptance Criteria
- [x] `ExecutionArbiterMachineFuzz_Test.ZXPolyGroupUnderConcurrentControl` clean under TSan on Linux (it reported
  both races before: 8 reports in one run of master; none with the fix).
- [x] The ZX-Poly tests pass.

## 🟢 [Fixed] #6: Two overscan switches at once both see the old video mode
* **Date Opened:** 2026-10-11
* **Date Fixed:** 2026-10-11
* **Commit ID:** c13e28bb0
* **Found by:** ThreadSanitizer on Linux (nightly `.238`, 2026-10-11): `ExecutionArbiterMachineFuzz_Test`
  (`ConcurrentControlKeepsOneDriverAndExactSteps`), a data race on `Screen::_mode` between `Screen::SetVideoMode`
  (`screen.cpp:470`) under one thread's `Emulator::SetOverscanMode` and `Screen::GetVideoMode` (`screen.cpp:738`) from
  another thread's `SetOverscanMode`.

### Description
`Emulator::SetOverscanMode` read the current mode, decided, and only then took the `CpuHold` for the switch. A second
caller read the mode while the first one was writing it, and both could decide to switch from the same old mode (the
second reallocates the framebuffer again for nothing, or its decision is based on a torn state).

### Fix
The hold is taken first; the read, the decision and the switch all happen under it.

### Requirements / Acceptance Criteria
- [ ] No `SetVideoMode` / `GetVideoMode` race in `ExecutionArbiterMachineFuzz_Test` under TSan on Linux (next nightly).
- [x] core-tests pass.

2026-10-10
## 🟢 [Fixed] #1: Removing a running TS-Conf or ATM Turbo 2+ instance writes into freed memory (heap damage, a later crash elsewhere)
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** fa8b68fbe
* **Found by:** an automation agent's crash while creating and deleting instances through the WebAPI (macOS report
  2026-10-10: `free_tiny` in `VideoWriteLog::BeginFrame` on another emulator thread); confirmed with AddressSanitizer.

### Description
Deleting instances corrupted the heap; the crash came later, in an unrelated `free` of another running instance.

### Root cause
`Core::Release` deletes the keyboard and the memory before the port decoder. `~PortDecoder_TSConf` detached from the
`TsConfMemory` it cached and `~PortDecoder_ATM710` cleared the PS/2 sink on the keyboard it cached: both freed already.

### Fix
Both destructors take the context's pointer, which `Core` clears before it deletes the part (as the other decoders do).

### Requirements / Acceptance Criteria
- [x] `EmulatorManager_Test.EveryModelCanBeRemovedWhileRunning` (every creatable model created, run, removed while
  running next to a running 48K) is clean under ASan; it reported both before.

## 🟢 [Fixed] #2: Laser Genius detection reads past data that ends inside a token
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** 7d40cd730
* **Found by:** a full `core-tests` run under AddressSanitizer (heap-buffer-overflow in `LaserGeniusCodec::Detect`).

### Description
`Detect` runs on every file of a disk (asm files, symbol import from a disk's sources): a file ending inside a token
was read beyond its buffer.

### Root cause
`SkipRaw` steps over a token's operand bytes and returned a stop past the data's end; `RawText` read up to it.

### Fix
`SkipRaw` ends at the data's end.

### Requirements / Acceptance Criteria
- [x] `LaserGeniusCodec_Test.DataEndingInsideATokenIsNotReadPast` (every token byte, alone and before every second
  byte, at the end of an exact-size buffer): failed under ASan before; all `unreal-asm-tests` clean under ASan.

## 🟢 [Fixed] #3: GIF writer indexes past its r/g/b array on pictures with few colors; the same frame can encode differently
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** dc0b47c9f
* **Found by:** a full `core-tests` run under AddressSanitizer (stack-buffer-overflow in `GifGetClosestPaletteColor`).

### Description
Every frame with fewer colors than the palette (every ZX screen in a 256-color GIF) read the k-d tree's garbage nodes.

### Root cause
`GifSplitPalette` returns from a subtree without pixels before it fills the node; `GifWriteFrame` builds its palettes
on the stack, so those nodes kept whatever the stack held: a split component above 2 indexed `comps[3]`, garbage leaf
colors could be picked.

### Fix
`GifMakePalette` zeroes the palette first (a local change to the vendored gif-h).

### Requirements / Acceptance Criteria
- [x] `Gif_Test.APaletteOfAFewColorsHasEveryNodeFilled` (palette prefilled with 0xFF) failed before, passes now; Gif
  tests clean under ASan.

## 🟢 [Fixed] #4: TTD with a shadow engine erases its whole timeline every frame and reads the empty front
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** 53325eb5b
* **Found by:** a `core-tests` run under AddressSanitizer (container-overflow in `GlobalT` from `SyncTimelineFront`).

### Description
Tests and benchmarks that attach a shadow engine (`SetShadowEngine`) ran on undefined behavior; production is not
affected (there the shadow engine is the controller's own).

### Root cause
`SyncTimelineFront` trims the timeline to what the controller's engine holds; that engine never opens a session while
a shadow engine takes the captures, so "nothing held" erased every checkpoint, then `_timeline.front()` was read.

### Fix
A closed engine holds no history to follow: `SyncTimelineFront` returns.

### Requirements / Acceptance Criteria
- [x] `TimeTravelManager_Screenshot_Test` checks the timeline keeps its 51 checkpoints (0 before); TTD tests clean
  under ASan.

## 🟢 [Fixed] #5: Qt debugger crashes in the disassembler while the debug manager is not available
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** d2e4f3f84
* **Found by:** two macOS crash reports of unreal-qt on 2026-10-09 (`disassembleSingleCommandWithRuntime`, fault at 0x18).

### Description
`DebuggerWindow::updateState` -> `DisassemblerWidget::refresh` crashed with `this == nullptr`.

### Root cause
`getNextCommandAddress` (and `getPreviousCommandAddress` through it) dereferenced `getDisassembler()`, an empty
`unique_ptr` while the debug manager is not available; `setDisassemblerAddress` already checked it.

### Fix
The same check in `getNextCommandAddress`.

### Requirements / Acceptance Criteria
- [x] Build clean. No automated test: the Qt debugger widgets have no test harness.

## 🟢 [Fixed] #6: The WebAPI instance list and status do not show `symbolic_id`
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** 206ff1b8f
* **Found by:** checking the agent cleanup recipe against a live instance (`symbolic_id` was null in `GET /emulator`).

### Description
A script naming its instances at create could not find its leftovers by name: only the create / start answer had it.

### Fix
`GET /api/v1/emulator` and `GET /api/v1/emulator/{id}` carry `symbolic_id` (empty when none); OpenAPI `EmulatorInfo`
documents it; MCP `emulator_manage list` passes it through.

### Requirements / Acceptance Criteria
- [x] Checked live: named and unnamed instances in the list, the status, OpenAPI and MCP.

## 🟢 [Fixed] #7: Removing an instance deadlocks the WebAPI and the GUI while the GUI follows the selection
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** fc9d535e5
* **Found by:** WebAPI create / delete stress on an ASan unreal-qt: the app stopped answering; `sample` showed the deadlock.

### Description
`DELETE /emulator/{id}` (and the shutdown of all instances) hung the WebAPI and the GUI for good.

### Root cause
`RemoveEmulatorInstance` drained the message queue before taking `_emulatorsMutex`, but a message posted after the
drain was dispatched while `Stop()` / `Release()` ran under the lock: `~RecordingManager` removing an observer waits
for the dispatch in flight, whose handler (`MainWindow::handleEmulatorSelectionChanged` -> `GetEmulator`) waited for
the lock.

### Fix
The instance leaves the map under the lock and is stopped and released after it; `ShutdownAllEmulators` the same way.

### Requirements / Acceptance Criteria
- [x] `EmulatorManager_Test.RemovalDoesNotHoldTheLockAHandlerWaitsFor`: deadlocked 3 / 3 before, 14 ms now.

## 🟢 [Fixed] #8: A DDCB / FDCB instruction writes into the last created CPU (freed memory once it is removed)
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** c68ea8a24
* **Found by:** the same stress (heap-use-after-free in `op_FD` on a removed instance's Z80, traced with address logs).

### Description
With several instances, `SET/RES/RL... n,(IX+d)` on any machine wrote a byte into the last created CPU; the undocumented
forms changed that machine's registers. After that instance was removed the byte went into freed memory: heap
corruption that surfaces later in an unrelated `free` (a likely cause of the original automation-agent crash).

### Root cause
`direct_registers[]` was one global table each `Z80` constructor refilled with its own registers.

### Fix
The destination register is taken from the running CPU; the table and the trash register are gone.

### Requirements / Acceptance Criteria
- [x] `Z80_Test.DdcbResultRegisterBelongsToTheCpuRunningIt`: wrong CPU's B changed before; right one now; clean under ASan.

## 🟢 [Fixed] #9: Instances created from several automation threads at once can get the same id
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** b9291e6a7

### Root cause
`UUID::Generate` drew from one static `mt19937` shared by every thread without a lock.

### Fix
A generator per thread, seeded from `random_device` through a `seed_seq`.

### Requirements / Acceptance Criteria
- [x] `UUID_Test.ThreadsGeneratingAtOnceGetDistinctIds`: ~30 duplicates of 12000 before, none now.

## 🟢 [Fixed] #10: The GUI reads a removed instance's context (menu update, FDC status widget)
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** 39770502f (menu), ea5696507 (FDC widget)
* **Found by:** the same stress (heap-use-after-free on the GUI thread: `MenuManager::updateMenuStates` -> `FDD::getDiskImage`;
  `FdcStatusWidget::seedFromContext` -> WD1793).

### Fix
Both take a `ContextLease` for their read (a removal in progress gives none), as the status bar and tool bar do.

### Requirements / Acceptance Criteria
- [x] The stress no longer reports these two (checked under ASan); no automated GUI test.

## 🟢 [Fixed] #11: GUI code reads instance contexts without a lease (about 144 call sites)
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** f38e72b8e
* **Found by:** the WebAPI create / delete stress: #10's two sites were the first it hit; `grep GetContext() unreal-qt/src`
  finds about 144 unleased reads (14 files lease).

### Description
A GUI handler queued before an instance is removed runs after its context is freed. Fixing sites one by one as the
stress finds them does not converge.

### Fix
Structural (design: [2026-10-10-gui-instance-lifetime](2026-10-10-gui-instance-lifetime/README.md)). A removal retires
the instance (`Emulator::Retire`: stopped, detached, refuses execution and machine changes, state `StateRetired`)
and frees nothing; the last `shared_ptr<Emulator>` holder frees it. Every removal wait is bounded. In the GUI,
`EmulatorBinding` owns the instance and no class stores `Emulator*` / `EmulatorContext*`
(`tools/verification/gui-instance-reads.sh`, in CI). The same change covers the WebAPI / CLI / MCP handlers.

### Repro
ASan unreal-qt: `tools/sanitize/stress_round.sh --busy-gui 900 <tag>` (windows open, half of the instances selected
in the GUI before they are deleted). Master before the fix hung after ~3 minutes and crashed in
`ZXPolyGroup::AdvanceSlaves`.

### Requirements / Acceptance Criteria
- [x] `EmulatorLifetime_Test` (15) and `RemovalRace_Test` (7, unreal-qt-tests) pass; full `test.sh` green
- [x] Busy-GUI ASan stress, 15 minutes, clean (1364 cycles, 645 GUI selections of the instance being deleted)
- [ ] TSan, Linux ASan, more ASan rounds, the manual check: skipped by the owner
- [ ] Debugger window, main window, menu: no automated removal test (not in unreal-qt-tests); stress only

## 🟢 [Fixed] #12: A failed instance creation leaks the half-built instance
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** d5e197532
* **Found by:** macOS `leaks --atExit` and LeakSanitizer (Linux, Docker) on `EmulatorManager_Test.CreateEmulatorWith*`
  (docs/testing/sanitizers.md §9).

### Description
`CreateEmulatorWithModel` for an unknown model, an unsupported RAM size or a non-creatable model returns null and
leaves the `Emulator` it started building: its `Config`, `EmulatorContext` and module loggers (`leaks`: `ROOT LEAK
<Config>`, `ROOT CYCLE <EmulatorContext>`; LSan: ~700 KB indirect from `Emulator::Emulator` / `Emulator::Init`).
Every failed WebAPI / CLI create leaks again.

### Root cause
`~Emulator` released only an initialized machine. An unknown model or RAM size never reaches `Init`; a core that
refuses the configuration made `Init` return before `ReleaseNoGuard`; a device that throws out of `Init` skips it.

### Fix
The destructor releases while the context lives; `Init` sends every failure through `ReleaseNoGuard` and runs the
completeness checks only for a machine that claims success.

### Requirements / Acceptance Criteria
- [x] `EmulatorManager_Test.FailedCreateFreesTheHalfBuiltInstance`: `EmulatorContext::LiveCount()` is unchanged after
  an unknown model, an unsupported RAM size and GMX; it failed on all three before the fix.

## 🟢 [Fixed] #13: Undefined behavior in product code (UBSan baseline)
* **Date Opened:** 2026-10-10
* **Date Fixed:** 2026-10-10
* **Commit ID:** d5e197532
* **Found by:** a full `core-tests` run on the `address,undefined` build (docs/testing/sanitizers.md §2).

### Description
- `modulelogger.cpp:434`: `_settings.submodules[module]` read with `module == 12` on a 12-element array (a module id
  without its row).
- `bithelper.h:18`: `IsBitSet(value, 255)` shifts by 255 - `GetFirstSetBitPosition(0)` returns 255.
- `soundchip_turbosound.cpp`, `soundchip_turbosoundfm.cpp`: float samples converted to `short` out of range (no clamp).
- `loader_scl.cpp`: misaligned `uint32_t` loads from the image buffer.
- `z80.cpp:192`, `z80.cpp:429`: a reference bound to a null `Emulator` (reached from tests that build a bare `Core`).

### Fix
- `ModuleLogger`: `MODULE_RECORDING` has its settings row and its names (both tables `static_assert`ed against the
  enum); the module check reads the module's own bit (as `TurnOffLoggingForModule` clears it); the submodule-name
  bound is `>=`.
- `BitHelper::IsBitSet`: a bit past the field is not set.
- TurboSound / TurboSound FM: samples clamp to int16. On arm64 the out-of-range conversion wrapped around (a full-
  scale click); in-range samples are unchanged. The TSFM low-quality golden digest moved: it held wrapped samples.
  The missing headroom itself is TSFM ISSUES.md #6.
- `LoaderSCL`: the CRC is read byte by byte. `Z80`: no reference to a null `Emulator`.

### Requirements / Acceptance Criteria
- [x] `BitHelper_Test.IsBitSetPastTheFieldIsNotSet`, `ModuleLogger_Test.RecordingModuleHasItsOwnSettingsRow`,
  `LoaderSCL_Test.crcReadsAtAnyOffset`; full `core-tests` green on macOS.

---

2026-10-07
## 🟢 [Fixed] #1: TTD engine - the VDAC2 (FT812) picture at a frame boundary differs from the live one for some frames
* **Date Opened:** 2026-10-07
* **Date Fixed:** 2026-10-07
* **Commit ID:** see the commit "fix(ttd): the VDAC2 picture at a frame end on the engine"
* **Found by:** moving `TTDVdac2_Test` onto the engine (Phase 5 Step 4b of the TTD v2 migration), 2026-10-07.

### Description
A seek to the end of a frame on a TS-Conf + VDAC2 machine showed, for some frames, the FT812 picture in flight (its
top rows from the frame being drawn) instead of the finished one the live run showed; card state and chip memory
were exact.

### Root cause
Two parts. (1) The controller composed a frame target as v1 did - the current frame run to its end - while D13 puts
"frame N" at the boundary `{N+1, 0}`, and it never asked for a frame target at all. (2) After the first fix it asked
by the landing position (`tInFrame == 0`), but a seek lands on the first instruction boundary at or after its target,
a few T-states in: for those frames the in-flight rule composed the picture.

### Fix
A seek whose target is a frame boundary composes the frame that ended there, from its lead-in
(`PresentPosition(target.tInFrame == 0)`, `ComposeDisplay(frameTarget, kEndedFrame)`); the clip export composes its
own frame (`kCurrentFrame`). `TTDVdac2_Test` runs on the engine (6 / 6, the frame-number seeks go to
`FrameEndPosition`; the history window allows whole segments); two mutants caught.

### Requirements / Acceptance Criteria
- [x] `TTDVdac2_Test` passes on the engine (the file left `ttdv1tests.h`).

---

2026-10-05
## 🟢 [Fixed] #1: Live border stays black when nothing wrote port #FE, while a TTD-composed picture fills it from #FE
* **Date Opened:** 2026-10-05
* **Date Fixed:** 2026-10-06
* **Commit ID:** 73e4b793a
* **Found by:** the D13 test (`TTDControl_Test.AFrameWithoutATStateIsItsEnd`, Phase 5 of the TTD v2 migration,
  branch `ttd-engine`), 2026-10-05.

### Description
A machine created with `new Emulator` + `Init()` whose PC is moved to a RAM program before the ROM runs (so nothing
has written port `#FE` yet) shows a black border in the live framebuffer: border pixels are `0xFF000000` while
`emulatorState.pFE` is `0xFF` (border color 7, white). A TTD seek to any position in the same session composes the
picture (`TimeTravelController::ComposeDisplay`, its static base `RenderOnlyMainScreen` + `FillBorderWithColor(pFE & 7)`)
and shows a white border (`0xFFCACACA`). The two pictures of the same machine state differ in all 52,224 border pixels
(352x288 - 256x192).

### Repro
1. Create an emulator (`new Emulator(LoggerLevel::LogError); Init()`), Pentagon default.
2. Write a program at `#8000` that never executes `OUT (#FE)` (e.g. `DI; LD HL,#4000; loop: INC (HL); INC HL; JR loop`),
   set PC to `#8000`.
3. Start TTD, run a few frames, read the framebuffer: border black.
4. Seek to a recorded frame: border white.

Once the program writes `#FE` (the D13 test does `LD A,2; OUT (#FE),A`) both pictures agree.

### Open questions (triage)
- Which one is right: the live renderer draws the border only after a `#FE` write, or the initial `pFE` value
  (`0xFF`) is not what a power-on machine has. Real hardware: the ULA border latch is undefined / zero at power-on on
  most models; the ROM sets it.
- Whether a live machine started normally (ROM boot) can ever hit this - the ROM writes `#FE` early, so it may be
  limited to tests and automation that skip the ROM.

### Root cause and fix (2026-10-06)
After a reset the border color lived in three places that disagreed: the port decoder's reset set the ULA latch
`pFE` (0xFF on 128K / Profi: white; 0xF8 on Scorpion: black), `Core::Reset` set `border_attr` to 7, and the screen's
`_borderColor` stayed 0 (black) until the first `OUT (#FE)`. The renderer draws with `_borderColor`, a TTD-composed
picture fills from `pFE`. `Core::Reset` now takes `border_attr` and the renderer's color from the latch the decoder
reset left (`Screen::ResetBorderColor`, no drawing), as an `OUT (#FE)` does. Test
`TTDControl_Test.TheBorderBeforeAnyOutToFEIsTheLatchs` on both backends (mutant caught). No golden picture changed.

### Requirements / Acceptance Criteria
- The live picture and a TTD-composed picture of the same machine state are identical, border included, whether or
  not `#FE` was ever written.
- A test that reproduces the case above and fails before the fix.

## 🟢 [Fixed] #2: Running forward from a session's mid-frame baseline diverges the TurboSound FM state
* **Date Opened:** 2026-10-05
* **Date Fixed:** 2026-10-06
* **Commit ID:** fae373a49
* **Found by:** the black-box fast-tape test (Phase 5, Step 3 of the TTD v2 migration, branch `ttd-engine`), 2026-10-05.

### Description
A recording that starts mid-frame takes its baseline checkpoint there. Seeking to that first checkpoint and running
the machine forward (detached, the recorded history playing) reaches a later position with CPU and RAM equal to the
live run but the TurboSound FM device (TTD id 4, two YM2203) different at byte 51 of its 2,008-byte state - chip 0's
clock phase, right after the latch. Restores from frame-start checkpoints and replays from them are exact; only the
mid-frame baseline is affected. The FM chips are synced lazily behind the CPU (`TTDSyncedTime`) and save their
render cursor relative to the CPU's time, so a restore at a position that is not a frame start may leave the clock
phase where the live chip did not have it.

### Repro
Pentagon (TSFM in the TurboSound slot), the TTD controller: run 12,345 T-states, `StartRecording`, run 3 frames and
20,000 T-states, note the position and the device states, `StopRecording`, `SeekTo(checkpoint 0)`, step until the
noted position: device 4 differs.

### Narrowed (2026-10-06)
- A seek replaying from the mid-frame first checkpoint inside its frame: exact, every device
  (`TimeTravelController_Test.ASeekFromAMidFrameFirstCheckpointIsExact`, a guard).
- The machine running forward from it (`RunNFrames`, `RunTStates`) across frame boundaries: CPU, RAM and both YM2203
  chips exact (`RunningFromAMidFrameFirstCheckpointKeepsTheFmChips`, a guard); only the TSFM render cursor
  (blob byte 1222: `_renderT - _chipT`, the start of the v4 timeline tail) ends 16 master clocks - one render tick -
  behind the live run after the first frame end.
- Stepping instruction by instruction (`RunSingleCPUCycle`) from it: also chip 0's FM clock phase (byte 51) differs.
  Syncing the chips to the CPU before the baseline capture (`syncTo(nowT())`) removed the byte-51 difference on that
  path but not the cursor's, and made no difference on the batch path: not adopted.
- From a frame-start checkpoint both paths are exact.
- Suspects: the TSFM render progress and the mixer's sample phase at the first frame end after a mid-frame restore
  (`TTDLoadState`: `_lastTStates`, `adoptSamplePhase`; `followSamplePhase` from 74d9b8ad5), and the single-step
  path's per-step audio work. Audio rendering state only - the guest's program and memory replay exactly.

### Root cause and fix (2026-10-06)
Only with SoundHQ off (the LQ render path). Setting the sound configuration (quality, core rate) arms TSFM's
`_renderReanchor`: the render cursor re-anchors at the next frame start. The flag is consumed there, so every
frame-start checkpoint holds it clear - but a recording started inside the frame right after such a setting captured
it set, and the blob did not carry it: `TTDLoadState` cleared it, the restored machine skipped the re-anchor at the
first frame end and its cursor stayed one render tick (16 master clocks) behind the live one; instruction by
instruction the chips' FM clock phase drifted as well. The pending flag is now bit 3 of the blob's board byte (no size
or version change; older blobs read it as clear, as before). Test
`TimeTravelController_Test.RunningFromAMidFrameFirstCheckpointIsExact` - every device equal by frames and instruction
by instruction (mutant caught). The 2026-10-06 narrowing above was right about the path, wrong about the suspect.

### Requirements / Acceptance Criteria
- Running forward from any checkpoint, the baseline included, reproduces every device state of the live run.
- A test for the mid-frame baseline that fails before the fix.

## 🟢 [Fixed] #3: A bookmark at the current position fails after `ttd stop` (and while recording)
* **Date Opened:** 2026-10-05
* **Date Fixed:** 2026-10-06
* **Commit ID:** ea9cd124e
* **Found by:** the Phase 5 recipe re-run (TTD v2 migration, branch `ttd-engine`), 2026-10-05; the same on v1 and on the engine.

### Description
`POST /ttd/bookmarks {"label": "symptom"}` without a position takes the current position. After `ttd/stop` the
machine stands inside the frame after the last checkpoint (or ahead of the history while recording), so the add answers
409 "bookmark position (frame=1027, tInFrame=2125) is beyond the session end (frame=1027)". The recipes
(`bug-hunt-ttd.md` Phase 1, `ttd-reverse-debugging.md` step 2, `ttd-visual-inspection.md`, `ttd-recording.md` "defaults to
current position") expect it to work. It works after a seek, find-last or reverse-continue.

### Fix (engine, 2026-10-05)
The history of a stopped recording reaches to where it stopped (`SessionEndPosition` returns the stop point inside the
frame after the last checkpoint; StopRecording already hands the engine that frame's journals), so a bookmark, a seek
and a resume at the stop point work. While recording, a bookmark may sit at the present (a seek there pauses the
recording at it). Test `TimeTravelController_Test.TheHistoryReachesWhereTheRecordingStopped` (two mutants caught).
v1 (`UNREAL_TTD_BACKEND=v1`) keeps the old behavior.

### Requirements / Acceptance Criteria
- A bookmark without a position works right after `stop` (the history reaches to where it stopped) and while
  recording, or the recipes say what to do instead; a test on both backends.

## 🟢 [Fixed] #4: `GET /ttd/status` lags the recording's head while the machine runs
* **Date Opened:** 2026-10-05
* **Date Fixed:** 2026-10-06
* **Commit ID:** 26d3a63df
* **Found by:** the Phase 5 recipe re-run, 2026-10-05 (engine; v1 not checked).

### Description
While a recording runs, `current_end_frame` / `checkpoint_count` in the status trail `position.session_end` (seen: 167 /
309 / 1 against 308 / 408 / 30); they catch up when the machine pauses or the recording stops. The status of a
running machine is the published snapshot (`GetPublishedSessionInfo`, refreshed on a throttle), which
`ttd-recording.md` calls the "live head".

### Root cause and fix (engine, 2026-10-06)
The machine publishes a fresh snapshot only after an observer asked, at its next frame boundary and at most every
100 ms; a read returned the snapshot it found, so the first status after a quiet spell was as old as that spell. Now
`TimeTravelController::ReadSessionInfo` - the status verb on every surface - refreshes a snapshot older than the
interval first: it asks and waits for the machine's next frame boundary (at most 250 ms; only while a session is
recording or browsed). The Qt indicators keep the non-blocking `GetPublishedSessionInfo`. Live: status and position
agree while recording (53 / 53, 101 / 104, 155 / 155). Test `TTDControl_Test.TheStatusOfARunningRecordingFollowsIt`
(mutant caught). v1 unchanged.

### Requirements / Acceptance Criteria
- The status of a running recording is at most about one refresh period behind, or the docs say it is a snapshot.

---

2026-10-02
## 🟢 [Fixed] #1: The halted Z80 fetches the HALT itself instead of the byte after it
* **Date Opened:** 2026-10-02
* **Date Fixed:** 2026-10-02 (A/B checked and closed 2026-10-03)
* **Commit ID:** `cf5b28ac1` (merged `1e5782f2f`)

### Description
While halted, the Z80 makes an idle opcode fetch every 4 T at the address **after** the `HALT` (its program
counter already points there) and discards the byte. unreal-ng (and unreal-z80) re-execute the `HALT` itself, so the
idle fetches go to the `HALT`'s own address. When the two addresses differ in contention, the interrupt is taken at
another T: HALT2INT v3 on the 48K, `HALT` at #7FFF (contended; the byte after it, #8000, is not), interrupt at 14335 /
14336 / 14562: the real early 48K shows R = #43 / #43 / #0B, unreal-ng #44 / #44 / #1C, and the program's header
reads "HALT: Unknown" instead of "Early".

### Root cause
`op_76` (`core/src/emulator/cpu/op_noprefix.cpp`) steps PC back, and every following step runs the full opcode
fetch (`Z80::m1_cycle`) at PC, the `HALT` address. The same in unreal-z80 (`op_76`, `Z80HaltT` reports the M1 at PC).
References: the hardware (HALT2INT photos and published screens) and MAME fetch HALT + 1; FUSE, Xpeccy, ZXMAK2 and
SkoolKit fetch the HALT, like unreal-ng.

### Requirements / Acceptance Criteria
- [x] The halted idle fetch goes to PC + 1 on the bus (contention, the +2A / +3 latch, ULA snow, the machine M1 hooks)
      while PC, the instruction-start work, R and the acknowledge stay as today
      ([design](2026-10-02-halt-fetch-address/design.md) §3-4). `Z80::HaltedM1`, branch `halt-fetch-fix`.
- [x] `Halt2Int_Test` passes with no known deviation (48K; the 128K program finds "Early"); host tests pin the idle
      fetch address on the 48K and 128K (`Contention48K_Test` / `Contention128K_Test.HaltedFetchGoesToTheByteAfterTheHalt`).
- [x] No cost on the non-halted path: A/B on 2026-10-03 (A = master with the Z80 part of the fix reverted, B = master,
      `BM_HostFrame_{48K,Pentagon,Scorpion}_Fast`, 5 runs a side interleaved A B A B A B B A B A, load 9.5-11.9): best
      of 5, B vs A +0.12 % / -0.03 % / -0.37 %; the means are +0.7 .. +2 % only because of one pair at load 11.3-11.9, and
      the sign of the pairs is mixed: no cost. unreal-z80 carries the same fix with every suite green (library branch
      `halt-fetch`, done), as do the vendored copies (General Sound, the Sprinter's Z84C15).

Design: [2026-10-02-halt-fetch-address](2026-10-02-halt-fetch-address/design.md). Plan: [PLAN.md](PLAN.md) #81.

---

2026-09-30
## 🟢 [Fixed] #1: TSConf Z-Controller FAT32 Compatibility Matrix
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `79985da7`

### Description
TSConf is expecting **FAT32 ONLY** formatted cards in the Z-controller slot, but the media manager mounts FAT16 by default.

### Requirements / Acceptance Criteria
- [x] Implement a **Media manager device registry compatibility matrix** to define expectations. (`SlotDescriptor::fsCompatibility`; `sd.zc` = `{FAT32}`, `defaultFs = Fat32`)
- [x] On insert/drag'n'drop events, verify requirements:
    - [x] **If Image:** Verify it is a compatible format (FAT32 for TSConf Z-Controller). (`ProbeFatType` on the opened medium; a FAT16 image is refused with `BadRequest`)
    - [x] **If Mounted Host Folder:** Ensure the virtual image is created as FAT32, not FAT16 (independent of folder content size). (the default clamps into the matrix; an explicit `fs=fat16` is a caller error)

---

## 🟢 [Fixed] #2: Media Manager FAT16 to FAT32 Auto-Switching >2GB
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `79985da7`

### Description
The Media manager is blindly creating FAT16 images for mounted host folders based on size. If the size is more than 2GB, it throws an error instead of switching to FAT32 format.

### Requirements / Acceptance Criteria
- [x] If device config meta allows both FAT16 and FAT32 formats, the Media manager must dynamically switch to FAT32 when folder size exceeds 2GB instead of throwing an error. (`OpenFolderVolume` retries as FAT32 over the FAT16 ceiling, with a report line)
- [x] If device config meta allows only single format - check by size if can be fulfilled and either fulfill or return an error. (the single-flavour slot keeps the honest `DoesNotFit`)

Verification: `MediaManager_Test.Fat32OnlySlotBuildsFoldersAsFat32AndChecksImages`, `MediaManager_Test.OversizedFolderSwitchesToFAT32WhenAllowed`, `TsConfMedia_Test.SdSlotIsFat32Only`.

---

## 🟢 [Fixed] #4: TSConf Wild Commander - Parsed Modules Play No Sound
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `d391d73f`

### Description
TS-Conf, Wild Commander (`wc-improved-v1.11i.img`) with a host folder of modules mounted. Opening any non-archived module (.asc / .pt2 and others) shows the player screen with **correctly parsed tags**, but **no music plays**. Suspect: the music ports (AY / TSFM / GS / MoonSound) are not wired in the TS-Conf port decoder.

### Triage (2026-09-30, live instance over MCP / WebAPI)
- Ports are alive: a 30-frame port trace shows the player (PC `0xC4CD-0xC516`) writing a full AY register dump every frame - `#FFFD/#BFFD`, `had_handler: true`, decode arm `Ay`. The TSFM's second SSG carries the song's registers (active channels, volumes).
- `sound_played_since_reset: false` is a stub ("not tracked", devicestate.cpp:124) - a red herring.
- An armed 2 s audio capture collected **0 samples** while running: SoundManager::handleFrameEnd returns before the analyzer tap when `turbo_mode && !turbo_mode_audio` (soundmanager.cpp:754) - and `Core::EnableTurboMode` **always mutes audible output**.
- Instance settings at the time: `turbo_mode = true`, `turbo_active = true`, `speed = 2`, `turbo_audio = false`.
- **Decisive test**: turbo off + speed 1x -> a 2 s capture returns `dominant_hz: 1764.75`, RMS 0.083 - the module plays.

### Root cause
Turbo mode was on - by design it mutes all audio and skips the host-audio path entirely. The likely trigger: turbo is bound to **bare Tab** (menumanager.cpp:624), a key a file manager invites constantly. The port decoder, Wild Commander and the TSFM are correct.

### Requirements / Acceptance Criteria
- [x] Triage on the live instance: audio hardware config, port trace for AY port activity (#BFFD/#FFFD) while the player runs. (activity confirmed)
- [x] Root cause: player never writes the ports, or the decoder swallows/misroutes them, or the chip is not fitted. (none of these: turbo mute)
- [x] A module played from Wild Commander produces audio (AY path at minimum). (works with turbo off; the trap is gone with the rebind below)
- [x] Proposal: rebind Turbo Mode off bare Tab (a guest-app key) - e.g. `Ctrl+Tab` / `F11` - and/or surface the turbo state on the player screen (status bar already shows it). Decision: developer. (rebound to Ctrl+Tab in `d391d73f`)

---

## 🟢 [Fixed] #5: "28 MHz", Changing Frequency, 2x-Fast Time, Flicker on TSConf
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `d391d73f`

### Description
The machine appears to jump to a turbo frequency of ~28 MHz, the number keeps changing, emulated time flows about 2x faster than real time, and the picture flickers. TS-Conf is expected to support at most 14 MHz.

### Triage (2026-09-30)
- 28 MHz is arithmetic, not a phantom clock: host speed multiplier was **2**, Wild Commander toggles SYS_CONFIG 3.5 <-> 14 MHz around SD I/O every frame (port trace, PC `0x6BD5`), so the displayed effective clock jumps **7 <-> 28 MHz**. At host 8x the same toggle reads 28 <-> 112.
- "Time flows 2x faster" = the host speed multiplier 2 (turbo active runs unthrottled on top).
- The flicker = turbo mode's render decimation.
- The hardware turbo composition itself is correct: `ApplyHardwareTurboNow` rescales the in-frame position, recomputes frame timing and keeps the INT video-locked (50 Hz), so SYS_CONFIG 7/14 MHz does not change song tempo.
- SYS_CONFIG[1:0] combo 3 is deliberately clamped to 14 MHz (`kRatio[4] = {1, 2, 4, 4}`, portdecoder_tsconf.cpp:972) - the TS-BIOS setup only offers 3.5 / 7 / 14, and nothing in the shipped firmware sets 28.

### Requirements / Acceptance Criteria
- [x] Verify what the 4th SYS_CONFIG combo does. (clamped to 14 - matches the BIOS; the tslabs spec's 28 MHz combo is never issued by shipped firmware)
- [x] Verify frame/INT timing under hw turbo. (frame budget scales with the ratio; INT stays video-locked)
- [x] Proposal: same as #4 - the turbo shortcut + turbo state visibility. The frequency display could also show the composition (e.g. "14 MHz x2") instead of a bare product. (turbo rebound to Ctrl+Tab; the label shows the half-second range, e.g. "3.5-14 MHz", at 2 Hz - `d391d73f`)

---

## 🟢 [Fixed] #6: Native macOS MP4 Recording - Frame 2x Too Wide on TSConf
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-10-01
* **Commit ID:** `6c14bb57`

### Description
TS-Conf with the module player running, video recording started - native (macOS VideoToolbox), MP4, fullscreen. The video records fine **with sound**, but the file is **1440x576** where the natural frame is **720x288**: the horizontal resolution is doubled and the picture is stretched 2x horizontally. Recorded sample: `/Users/dev/Movies/unreal_20260930_205229.mp4` (emulator left paused for triage, file no longer present).

### Root cause
TS-Conf stores its fat pixels at **half height** internally: the 360x288-dot raster is kept at 720 (2 px/dot wide) x 288 (1 stored line per dot row). `RecordingManager` had no awareness of this and passed the raw `fb.width x fb.height` (720x288) straight through as the recording's target resolution - already wrong aspect before any user-chosen scale.

Separately, the GUI's recording dialog and "Quick Record" presets default to `SetScaleFactor(2)` (a "2x nearest-neighbor upscale, keeps ZX pixels crisp" convenience, `unreal-qt/src/debugger/widgets/videorecordingwidget.cpp:388-394`, `unreal-qt/src/mainwindow.cpp:3374`), which every encoder backend applies **uniformly to both axes** (`VideoToolboxEncoder::Start`, `core/recording/src/platform/macos/videotoolbox_encoder.mm:56-61`; the ffmpeg `scale=iw*N:ih*N` filter, `core/recording/src/encoders/ffmpeg_pipe_encoder.cpp:767-772`).

With no TS-Conf correction, `720x288` uniformly doubled by the GUI's default `scaleFactor=2` gives exactly the reported `1440x576`: the width got its (otherwise legitimate) 2x upscale, the height needed a 2x *aspect* correction first and never got one, so it reads as "width doubled, height not" even though the real defect is a missing height fix, not an added width one. A direct/test call to `RecordingManager::StartRecording` never touches `SetScaleFactor` (class default `1`), so that path was never visibly wrong - it just produced an uncorrected `720x288` with no upscale to make the mismatch visible.

### Fix
`core/src/emulator/video/videofamily.h`: `StoresHalfHeightLines(VideoModeEnum)` - true for the four TS-Conf modes (`M_TS16/M_TS256/M_TSTX/M_TSZX`), false elsewhere.

`core/recording/src/recordingmanager.cpp`: both `StartRecording` and `StartRecordingEx` double `_videoHeight` when `StoresHalfHeightLines(fb.videoMode)` before it ever reaches an encoder config, and `CaptureFrame` duplicates every captured row (720x288 -> 720x576) before handing the frame to `EncodeVideoFrame`. This is encoder-agnostic: it runs once, upstream of all three backends.

Checked that no backend double-applies or skips the fix:
- VideoToolbox (native) and the ffmpeg software path both read the already-corrected `config.videoWidth/videoHeight` as their un-scaled source size and apply the user's `scaleFactor` uniformly on top - a 2x "crisp pixels" recording of a TS-Conf session now comes out `1440x1152` (uniform 2x of the correct `720x576`), not `1440x576`.
- The GIF encoder ignores `scaleFactor` entirely (by design - chroma subsampling doesn't apply to GIF) and uses `config.videoWidth/videoHeight` directly, so it inherits the corrected `720x576` with no extra step.

### Verification
- New test `core/tests/emulator/recording/tsconf_aspect_test.cpp`: `StoresHalfHeightLines` classification (TS-Conf-only) and the row-doubling algorithm (byte-exact content, width untouched) - red before the fix, green after.
- Full rebuild (`ninja core-tests`, zero warnings) + `test-parallel`, twice, both green (no regression; a first pass accidentally broke `ScorpionTurbo_Test.TurboStrobePostsCpuFreqChanged` via an unrelated notification-throttle change bundled in the same working tree - reverted, see commit notes).
- Full default build (`ninja`, no target) also green, zero non-third-party warnings.
- **Found and fixed a second bug while verifying live**: `RecordingManager::CaptureFrame`'s first draft declared the stretched `FramebufferDescriptor` *inside* the `if (StoresHalfHeightLines(...))` block and kept using the `toEncode` pointer to it after that block closed - a dangling pointer to a destroyed stack local, read by `EncodeVideoFrame` right after. This is what produced "mostly red garbage, worse than before the fix" when actually recording Wild Commander in the running app (not caught by the geometry-only unit test or `test-parallel`, since neither exercises a real `RecordingManager` + encoder round trip). Fixed by hoisting `FramebufferDescriptor stretched;` to function scope next to the existing `cropped` local, same lifetime pattern already used for the crop paths.
- Re-verified live via WebAPI/MCP against the freshly built `unreal-qt` (TSL model, TR-DOS/TSTX boot screen - any TS-Conf framebuffer exercises the same path as Wild Commander): native H.264 recording at `scaleFactor=1` -> clean `720x576` frame (ffprobe + a decoded PNG, no corruption); at `scaleFactor=2` over 60 captured frames -> clean `1440x1152`, image content crisp and correctly proportioned, no garbage, no crash.
- Could not re-probe the *original* user sample - that file no longer exists on disk; the geometry fix is verified by the math above, the new unit test, and this fresh live recording.

### Requirements / Acceptance Criteria
- [x] Triage: probe the file (dimensions, SAR/DAR, pixel format) and read the VideoToolbox recorder's frame-size/scaling path.
- [x] Root cause: which stage doubles the width (capture with devicePixelRatio, a scale-to-even/16 step, or a SAR/PAR mix-up).
- [x] A native MP4 recording of a TS-Conf session has the right geometry (720-wide content stays 720 wide, or scales uniformly with the correct pixel aspect).
- [x] The same check for the other recording paths (GIF, software MP4) - they must not share the bug.

---

## 🟢 [Fixed] #3: Host Folder Insert Freezes the UI Thread
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-10-01
* **Commit ID:** `47078607`

### Description
Inserting a host folder into an HDD / SD slot runs the folder scan and the volume build **on the Qt UI thread**. `MediaPanelWindow::run` (unreal-qt/src/media/mediapanelwindow.cpp:251) executes `MediaControl::Execute` synchronously, and `MediaManager::Insert` does its file I/O and `FolderSnapshot::Scan` "on the caller's thread" by design (core/src/emulator/media/mediamanager.cpp:149). A large folder (or a slow / network disk behind it) freezes the whole UI for the duration of the scan - no repaints, no input, no cancel.

### Root cause
Confirmed by trace: `MediaPanelWindow::insertInto` → `run()` → `MediaControl::Execute` → `MediaManager::Insert` → `MediaFormatRegistry::Open` → `FolderSnapshot::Scan` (recursive `std::filesystem::directory_iterator`, one syscall round-trip per entry, no batching) is a single synchronous call chain with no thread boundary and no cancellation hook anywhere in it. The existing `"async": "true"` option on insert is a red herring for this bug - it only skips `MediaControl::Finish`'s post-insert swap-delay wait (`WaitApplied`), never the scan/build itself, which runs unconditionally before `Finish` is even reached.

### Fix
Kept `core/` fully synchronous (every automation surface - WebAPI, CLI, MCP, Lua, Python - is unaffected and keeps blocking its own request thread, which has no UI to freeze). Added the async wrapper only where there is a UI to protect:

- **Core**: `cancelRequested`/`onProgress(entriesScanned, bytesScanned)` threaded end to end as optional `std::function` fields (`MediaRequest` → `InsertOptions` → `OpenRequest` → `FolderScanOptions` / `FolderDiskBuilder::BuildTrd`'s new parameters) - never serialized, so the wire protocols have no way to set them. `FolderSnapshot::Scan`'s walk checks `cancelRequested` once per entry and calls `onProgress` after every entry visited (bytes = total size of files accepted so far); cancelling unwinds every recursion level, not just the innermost one. New `MediaError::Cancelled` (HTTP 499).
- **GUI**: `MediaPanelWindow` gained the `std::thread` + `QMetaObject::invokeMethod(Qt::QueuedConnection)` worker idiom already used by `TapeExportAudioDialog`/`TapeImportAudioDialog` - no new threading pattern invented. An indeterminate `QProgressBar` + a status label show which slot and path are being scanned (slot name in bold), the entry count and a human-readable size (`QLocale::formattedDataSize`, KB/MB/GB). The scanning row in the slot table is tinted with `QPalette::Accent` (the system accent color - not `QPalette::Highlight`, which fades to gray whenever the window loses focus) so it is unambiguous which mount point a running scan belongs to. A toolbar button (reusing the existing Tools-menu "Media" `QAction`) opens the panel directly. A 1s watchdog timer compares the progress count across ticks and only cancels after `_insertStallTimeoutSeconds` (30, configurable) ticks with **zero** advancement - a slow-but-moving large folder is never aborted, only a genuinely stuck one. The worker takes its own `shared_ptr<Emulator>` (a fresh `EmulatorManager::GetEmulator(id)` lookup, not the GUI binding's raw pointer) so the emulator/context stays alive for the scan's duration regardless of concurrent unbind/rebind; the destructor cancels and joins unconditionally so the worker's completion lambda can never fire on a half-destroyed window.

Design: `docs/inprogress/2026-09-29-media-drop-targets/design.md` §11.

### Verification
- `core/tests/emulator/io/storage/hostfolder/foldersnapshot_test.cpp`: progress counting (entries and bytes), cancellation (exact call-count math verified), nested-recursion unwind (cancelling inside one subfolder never visits a sibling), a directory disappearing mid-walk failing the whole scan instead of truncating it.
- `core/tests/emulator/io/storage/hostfolder/folderdiskbuilder_test.cpp`: cancellation during the scan vs. during the TR-DOS file-read loop, distinguished by call count.
- Full rebuild (`ninja`, zero warnings) + `test-parallel`, both green.
- **Interactively verified** (built and run in an isolated git worktree, since the shared working tree had unrelated concurrent breakage at the time): inserting a real folder through the Media panel keeps the window responsive, shows the scanning row highlighted and the slot/path/entry-count/size in the status label, and completes correctly. Developer-confirmed live, including follow-up tweaks (byte/size display, row highlight color, bold slot name, the toolbar button, the 30s watchdog).

### Requirements / Acceptance Criteria
- [x] The folder scan and volume build for a GUI insert (Media Panel "Insert Folder..." and drag'n'drop) run on a **worker thread**: the Qt main thread stays responsive (event loop keeps running, window repaints, the user can keep working).
- [x] The UI is **notified on completion**: success -> the slot shows the medium; failure -> a readable error, surfaced like every other insert error (the same `report()`/`QMessageBox::warning` path every other verb already uses).
- [x] **Mid-scan source loss** (the host disk unmounted, the folder deleted, permission gone): no crash, no leaked background work, later inserts into the same slot work (verified by the lifetime design and thread join discipline). A structural I/O failure anywhere in the walk (the initial `directory_iterator` open, or `increment` failing partway through a directory) now fails the **whole** scan with a clear error instead of silently continuing past it - `Scanner` tracks this the same way as cancellation (`_ioError`/`IsIoError()`), unwinds every recursion level, and `FolderSnapshot::Scan` returns false with the `ec.message()`-derived error (mapped to `MediaError::UnreadableSource` at the `MediaFormatRegistry`/`FolderDiskBuilder` call sites, same as any other unreadable source). Policy exclusions (a symlink, the manifest's exclude list, a service file, a size/count limit) are unaffected and still just `Skip()` - only genuine filesystem errors fail the operation. Test: `FolderSnapshot_Test.DirectoryGoneMidWalkFailsInsteadOfTruncating` (deletes a subfolder from under the walk via the `onProgress` hook, deterministically, before the scanner recurses into it).
- [x] **Stall watchdog**: when the scan makes no progress for 30 s (configurable), it is aborted with an error to the UI instead of hanging forever.
- [x] The synchronous `Insert` semantics of the automation surfaces (WebAPI / MCP, their own HTTP threads) are preserved - `core/` is untouched for every caller that does not explicitly set the new callback fields.
- [x] Design note added to `docs/inprogress/2026-09-29-media-drop-targets/design.md` and `docs/inprogress/PLAN.md` (registry rule 3).

---
