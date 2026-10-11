# Bug Registry

> ### 📌 Registry System Rules & Annotations
> 1. **Grouping & Numbering:** Bugs are grouped chronologically by their discovery date. The bug number (`#X`) increments sequentially within that specific date group.
> 2. **Retention Policy:** A bug is removed from this tracker once it is fixed and verified; its history is in git (the fix commits reference `BUGS.md <date> #N`). A fix still waiting for a verification run is tracked in `PLAN.md` until then.
> 3. **Related Documents:** If a bug relates to functionality that is currently in progress, you must update the corresponding files in `docs/inprogress/*.md` and `PLAN.md` with relevant details or task dependencies.
> 4. **Methodology:** The tracking process (triage → confirmation → root cause → fix verification ladder) and the agent permission rules are defined in [`docs/testing/bug-tracking-guide.md`](../testing/bug-tracking-guide.md). Status markers: 🔴 Open → 🟠 Confirmed → 🔵 In Progress → 🟣 Fix Proposed → 🟢 Fixed (set by developer only).

---

2026-10-11
## 🔴 [Open] #7: MCP async chains hold themselves: every request through them leaks its state
* **Date Opened:** 2026-10-11
* **Found by:** valgrind memcheck profile (`tools/sanitize/valgrind.sh`, `.238`, 2026-10-11): 29 `definitely lost`
  blocks from `make_shared<std::function<...>>` in `mcp::RunSeries`, plus `inspect_state` lambdas.

### Description
Five chains of asynchronous MCP calls store their next step in a `shared_ptr<std::function>` whose lambda captures
that same `shared_ptr`: the count never reaches 0, the closure and everything it captured (steps, the accumulated JSON,
the `done` callback) stay allocated after the chain ends. `unreal-qt` serving MCP loses memory on each such request.
Sites: `mcp::RunSeries` (`mcp-tool-utils.h:317`, every series tool), `inspect_state` chip details (`mcp-tools.cpp:2557`),
the SSE keepalive `ping` (`automation-mcp.cpp:300`, one per connection), the capture `cycle` and the digest `sample`
(`mcp-media.cpp:543`, `:615`).

### Requirements / Acceptance Criteria
- [ ] The chain holds itself only while a step is pending (the continuation keeps it alive; the closure refers to
  itself weakly); it is freed when the chain ends.
- [ ] No `definitely lost` from these sites in the valgrind profile; MCP unit tests pass.

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
