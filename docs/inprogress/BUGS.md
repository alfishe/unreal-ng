# Bug Registry

> ### 📌 Registry System Rules & Annotations
> 1. **Grouping & Numbering:** Bugs are grouped chronologically by their discovery date. The bug number (`#X`) increments sequentially within that specific date group.
> 2. **Retention Policy:** A bug is removed from this tracker once it is fixed and verified; its history is in git (the fix commits reference `BUGS.md <date> #N`). A fix still waiting for a verification run is tracked in `PLAN.md` until then.
> 3. **Related Documents:** If a bug relates to functionality that is currently in progress, you must update the corresponding files in `docs/inprogress/*.md` and `PLAN.md` with relevant details or task dependencies.
> 4. **Methodology:** The tracking process (triage → confirmation → root cause → fix verification ladder) and the agent permission rules are defined in [`docs/testing/bug-tracking-guide.md`](../testing/bug-tracking-guide.md). Status markers: 🔴 Open → 🟠 Confirmed → 🔵 In Progress → 🟣 Fix Proposed → 🟢 Fixed (set by developer only).

---

2026-10-11
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

## 🔴 [Open] #10: Test fixtures that never free their machine
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
