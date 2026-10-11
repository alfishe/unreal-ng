# Bug Registry

> ### 📌 Registry System Rules & Annotations
> 1. **Grouping & Numbering:** Bugs are grouped chronologically by their discovery date. The bug number (`#X`) increments sequentially within that specific date group.
> 2. **Retention Policy:** A bug is removed from this tracker once it is fixed and verified; its history is in git (the fix commits reference `BUGS.md <date> #N`). A fix still waiting for a verification run is tracked in `PLAN.md` until then.
> 3. **Related Documents:** If a bug relates to functionality that is currently in progress, you must update the corresponding files in `docs/inprogress/*.md` and `PLAN.md` with relevant details or task dependencies.
> 4. **Methodology:** The tracking process (triage → confirmation → root cause → fix verification ladder) and the agent permission rules are defined in [`docs/testing/bug-tracking-guide.md`](../testing/bug-tracking-guide.md). Status markers: 🔴 Open → 🟠 Confirmed → 🔵 In Progress → 🟣 Fix Proposed → 🟢 Fixed (set by developer only).

---

2026-10-11
## 🔴 [Open] #8: DSD recording sets output bits from an uninitialized byte
* **Date Opened:** 2026-10-11
* **Found by:** valgrind memcheck profile: `PCMToDSDConverter::ProcessFloat` (`pcm_to_dsd_converter.cpp:252`),
  564 000 reports in `DSDEncoderE2ETest.PCMModeStillWritesDSF`; origin: a stack variable at line 184.

### Description
`ProcessFloat` passes `uint8_t bit;` (uninitialized) to `DeltaSigmaModulator::Process(sample, &bit, 1)`, which only sets
or clears bit 0 of it, then tests the whole byte (`if (bit)`). Garbage in bits 1-7 turns a -1 output into a 1: the DSD
stream depends on what was on the stack.

### Requirements / Acceptance Criteria
- [ ] The byte starts at 0 and only bit 0 is tested; no memcheck error in the DSD tests.

## 🔴 [Open] #9: Adding a breakpoint that already exists leaks its descriptor
* **Date Opened:** 2026-10-11
* **Found by:** valgrind memcheck profile: `BreakpointManager::AddExecutionBreakpointInPage` in
  `BreakpointManager_test.PageSpecific_DuplicateRegistration_ReturnsExisting`.

### Description
`AddBreakpoint(BreakpointDescriptor*)` takes ownership of the descriptor. For a memory or port breakpoint on a key
that already has one, `AddMemoryBreakpoint` / `AddPortBreakpoint` return the existing id and neither store nor free the
new descriptor. Every repeated "add the same breakpoint" from the debugger or automation loses one.

### Requirements / Acceptance Criteria
- [ ] Every path that does not adopt the descriptor frees it (duplicates, an invalid type); the duplicate test is clean
  under valgrind.

## 🔵 [In Progress] #10: Test fixtures that never free their machine
* **Date Opened:** 2026-10-11
* **Found by:** valgrind memcheck profile: ~200 `definitely lost` records, all from tests.

### Description
Fixtures build a bare `Core` / `EmulatorContext` and never delete it (Z80, ScreenZX, ATM video, INT timing, IO and
memory contention, loader SNA / Z80 / TAP, video map service, Scorpion raster, AY, port decoder, memory, disassembler,
full-decode claim, memory access tracker, TR-DOS integration), and the WD1793 tests delete the controller but not the
drives its constructor creates. Test-only, but the noise hides a product leak in the valgrind summary.

### Requirements / Acceptance Criteria
- [ ] The fixtures release what they build (`EmulatorTestHelper::ReleaseBareCore` or the context's own delete); the
  WD1793 tests free the drives; no `definitely lost` from tests in the valgrind profile.

### Fix
Test code only. Bare-core fixtures (Z80, Z80 logic/jump, ScreenZX, screen, Scorpion raster, ATM video, INT timing, IO
and memory contention, loader SNA / Z80 / Z80 fuzzing / TAP) release through `EmulatorTestHelper::ReleaseBareCore`; its
drive part is now `EmulatorTestHelper::ReleaseDiskDrives(context)`, used directly by the `unique_ptr` / stack cores
(video map service, device state video; `ScopedDiskDrivesRelease` for a `Core` on the stack). Hand-built WD1793
controllers use `WD1793TestCUT` (`core/tests/_helpers/wd1793testcut.h`): it frees the 4 FDDs its own constructor put in
`coreState.diskDrives` (a test may build several controllers on one context; no eject, a test-local image may be gone).
The AY, port decoder, memory, disassembler and full-decode claim fixtures delete their context; the memory access
tracker deletes its `DebugManager` and `CoreCUT`; loaded `DiskImage`s are held by a `std::unique_ptr` (TRD, SCL loader
tests, disk autostart, ATM710 TR-DOS) or by the fixture until its emulator is released (TR-DOS integration, ATM450 and
ATM710 CP/M boot, UDI Zvezdnoe boot). The breakpoint records (`breakpoints_test`, `analyzermanager_test`) are #9.

## 🔴 [Open] #11: miniz inflate branches on uninitialized state for a damaged RZX stream
* **Date Opened:** 2026-10-11
* **Found by:** valgrind memcheck profile: `tinfl_decompress` (`miniz.c:2924`) from `rzx::Inflate`
  (`rzxformat.cpp:77`) in `RzxReader_Test.FuzzedCorpusNeverCrashes`; origin: the state allocated in `mz_inflateInit2`.

### Description
Only on fuzzed (damaged) input. Whether such a stream is refused or decoded can depend on heap garbage. Third-party
code: check whether the vendored miniz has a fix upstream, or zero the state after `mz_inflateInit2`.

### Requirements / Acceptance Criteria
- [ ] No memcheck error in `RzxReader_Test.FuzzedCorpusNeverCrashes`.

Not bugs (known false positives of this profile, gcc -O2 + valgrind): `CompositeMediumFactory::Build`
(`compositemediumfactory.cpp:935`, a disengaged `std::optional<FatType>` whose payload gcc reads before the flag) and,
to be confirmed, `TTDDeviceTable::SameSet` (origin: the padding of a `TTDDeviceEntry` copy).
