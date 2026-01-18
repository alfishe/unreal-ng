# TR-DOS Integration Test Walkthrough

## Overview
This document tracks the full development history of the WD1793 TR-DOS integration tests, including decision making, issues encountered, and resolutions.

---

## Timeline

### 2026-01-16: Initial Implementation Attempt

#### Goal
Create `Integration_TRDOS_Format_ViaROM` test that:
1. Creates an emulator with Pentagon-128K configuration
2. Inserts an empty disk
3. Activates TR-DOS ROM
4. Executes the FORMAT routine via Z80 CPU
5. Verifies the formatted disk structure

#### Implementation Steps

1. **Created basic test structure** using `Emulator` class for full system setup
2. **Fixed API calls**:
   - `Memory::DirectWriteToZ80Memory()` instead of incorrect `WriteByteToBank()`
   - `FDD::isDiskInserted()` instead of `hasDisk()`
   - `FDD::isWriteProtect()` instead of `isWriteProtected()`
   - `context->pBetaDisk` is `WD1793*` directly, not `Beta128*`

3. **Fixed SetROMDOS()** to properly update state:
   - Added `_bank_mode[0] = BANK_ROM;`
   - Added `SetROMPageFlags()` call
   - Added optional `pPortDecoder->SetROMPage()` update

---

### Issue #1: FORMAT Returns After 113 Cycles

**Problem**: After activating TR-DOS ROM and calling FORMAT at $1EC2, the routine returned almost immediately.

**Investigation**:
- ROM bytes at $1EC2 showed valid TR-DOS code (`21 ff ff 22 d7 5c...`)
- FORMAT calls internal subroutines that return early
- TR-DOS environment not properly initialized

**Solution Attempted**: Call USR 15616 ($3D00) first to initialize TR-DOS
- **Result**: $3D00 is a trap address for SOS→TR-DOS transition, not valid when DOS ROM is already paged

---

### Issue #2: $0000 Returns Wrong ROM Bytes

**Problem**: After `SetROMDOS(true)`, ROM dump at $0000 shows SOS ROM (`f3 11 ff ff...`) instead of DOS ROM.

**Investigation**:
- `DirectReadFromZ80Memory()` uses `_bank_read[0]` which should be set by `SetROMDOS()`
- The init returned after 1 cycle because sentinel was at $0001
- ROM at $1EC2 shows DOS code but $0000 shows SOS code

**Root Cause**: ROM switching shortcuts (`SetROMDOS`, `SetROM48k`, etc.) may not be updating all necessary state for proper Z80 execution.

---

## Current Blocker: ROM/RAM Switching Infrastructure

Before continuing with TR-DOS integration tests, we need to fix and validate the ROM/RAM switching infrastructure.

### Required Changes

#### Phase 0: ROM/RAM Switching Tests
Create comprehensive tests for all Memory switching methods:

1. **Test SetROMDOS()**
   - Verify `_bank_read[0]` points to `base_dos_rom`
   - Verify `_bank_mode[0]` = `BANK_ROM`
   - Verify `isCurrentROMDOS()` returns true
   - Verify `DirectReadFromZ80Memory(0x0000)` returns DOS ROM byte
   - Verify port 1FFD state if `updatePorts=true`

2. **Test SetROM48k()**
   - Same validations for SOS ROM

3. **Test SetROM128k()**
   - Same validations for 128K ROM

4. **Test SetROMSystem()**
   - Same validations for System ROM

5. **Test RAM page switching**
   - Verify bank mode and read/write pointers

#### Design Consideration: Test vs Runtime Compatibility

The ROM switching methods need to work both:
- **In tests**: Where we may not have full context (port decoder, etc.)
- **At runtime**: Where we need full state synchronization

**Solution**: The `updatePorts` parameter already handles this:
- `updatePorts=false`: Minimal state update (for tests)
- `updatePorts=true`: Full synchronization (for runtime)

---

## Next Steps

1. [ ] Write Memory ROM/RAM switching tests
2. [ ] Fix any issues discovered by tests
3. [ ] Return to TR-DOS integration test
4. [ ] Implement proper TR-DOS initialization sequence
5. [ ] Complete FORMAT validation

---

## Files Modified

| File | Changes |
|------|---------|
| `memory.cpp` | Fixed `SetROMDOS()` to set bank_mode and call `SetROMPageFlags()` |
| `wd1793_test.cpp` | Added integration tests with TR-DOS ROM execution |
| `spectrumconstants.h` | Already contains TR-DOS constants |

---

## References

- [TR-DOS Reference (zxpress.ru)](https://zxpress.ru/book_articles.php?id=2277)
- TR-DOS v5.04T FORMAT entry point: $1EC2
- TR-DOS USR 15616 = $3D00 (SOS→TR-DOS transition)
- TR-DOS USR 15619 = $3D03 (TR-DOS command from SOS)
