# TR-DOS Analyzer - Task Tracker

> **Updated**: 2026-01-26

---

## v1 Tasks (Completed)

### Refactor BasicEncoder API ✅
- [x] Implement physical ENTER key injection for TR-DOS
- [x] Move `injectEnterPhysical` to a dedicated method
- [x] Update `runCommand` to use new method

### Fix TR-DOS Analyzer Event Collection ✅
- [x] Handle active TR-DOS state in `onActivate`
- [x] Implement `identifyService` for C register at $3D13
- [x] Implement `identifyUserCommand` for token at CH_ADD
- [x] Read filename from correct address ($5CDD, not $5CD1)

### Event Capture Improvements ✅
- [x] Add `TRDOSUserCommand` enum (CAT, SAVE, LOAD, FORMAT, etc.)
- [x] Add breakpoint at $030A (internal command dispatcher)
- [x] Distinguish service codes from user commands
- [x] Add interrupt state capture (iff1, im)
- [x] Add caller address to event context

### Raw Event Logging ✅ (2026-01-26)
- [x] Add `RawFDCEventType` enum (CMD_START, CMD_END, PORT_ACCESS)
- [x] Add `eventType` field to `RawFDCEvent`
- [x] Capture command byte at START
- [x] Capture status byte at END
- [x] Log track/sector registers at both points

---

## v2 Tasks (Planned)

> See [v2-upgrade-plan.md](./v2-upgrade-plan.md) for detailed design

### Phase 1: $3D2F Trampoline Detection ✅ COMPLETE
- [x] Add breakpoint at $3D2F (ROM switch trampoline)
- [x] Capture IX register (target internal routine address)
- [x] Add `TRDOSEventType::ROM_INTERNAL_CALL` event type
- [x] Map IX values to routine names via `getInternalRoutineName()`
- [x] Add `targetRoutine` field to `TRDOSEvent`
- [x] Add `LoaderType` enum
- [x] Update WebAPI to expose `target_routine` and `loader_type` fields
- [x] Update webapi-interface.md documentation

**Implementation Notes**: Custom loaders using $3D2F trampoline now generate ROM_INTERNAL_CALL events with IX captured.

### Phase 2: System Register Capture 🔴 TODO
- [ ] Populate `systemReg` field in `RawFDCEvent`
- [ ] Add `getSystemRegister()` to WD1793 or BetaDisk
- [ ] Parse density (FM/MFM) and side selection from system register
- [ ] Include in raw event formatting

**Why needed**: FORMAT debugging requires knowing density and side selection.

### Phase 3: Loader Type Classification 🟡 DESIGN
- [ ] Add `LoaderType` enum:
  - `INTERACTIVE` - User at A> prompt
  - `BASIC_COMMAND` - RUN/LOAD from BASIC
  - `API_LOADER` - Uses $3D13 correctly
  - `ROM_INTERNAL` - Uses $3D2F trampoline
  - `DIRECT_FDC` - No ROM calls (exotic)
- [ ] Set loader type in events based on detection method
- [ ] Update event formatting to show loader type

### Phase 4: Stack-Based Classification 🟡 DESIGN
- [ ] Implement `stackContainsROMAddress()` helper
- [ ] In `captureRawFDCEvent()`, analyze stack
- [ ] PC in RAM + no ROM in stack → DIRECT_FDC
- [ ] PC in RAM + ROM in stack → ROM_INTERNAL
- [ ] PC in ROM → API_LOADER or INTERACTIVE

### Phase 5: Enhanced Formatting 🔵 NICE-TO-HAVE
- [ ] Add `format()` method to `RawFDCEvent`
- [ ] Human-readable output with command names
- [ ] Parseable format for offline analysis

---

## System Constants Updates ✅ (2026-01-26)

Added to `spectrumconstants.h`:
- [x] Fixed disk type values (0x16=80T/DS, not 0x19)
- [x] `TRDOS::SideFlags` namespace
- [x] `TRDOS::ROMVariables` namespace with actual ROM addresses
- [x] `TRDOS::ROMVariables::SysSector` - sector 9 field offsets
- [x] `TRDOS::SystemRegister` - port 0xFF bit definitions

---

## Documentation Updates ✅ (2026-01-26)

- [x] Created `/docs/beta-disk-interface/TRDOS_Format_Procedure.md` - comprehensive FORMAT documentation
- [x] Fixed RESTORE opcode ($08, not $00)
- [x] Fixed auto-detect logic (checks H register for Track 1)
- [x] Fixed system sector offsets (E2h=First Free Track, F4h=Deleted Files)
- [x] Added Catch-22 clarification (FORMAT has own working detection)
- [x] Added verify mask note (#7F check at x3F39)

---

## Testing Status

| Test | Status | Notes |
|------|--------|-------|
| Unit tests | ✅ Pass | 20/20 tests |
| CAT command | ✅ Works | Shows COMMAND_START with CAT |
| RUN "boot" | ✅ Works | Shows user command + service calls |
| Custom loader | ⚠️ Partial | Shows FDC commands, but no $3D2F detection yet |
| FORMAT | ⚠️ Partial | Shows WRITE_TRACK commands, needs system register |
