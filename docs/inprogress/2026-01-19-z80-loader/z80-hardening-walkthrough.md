# Z80 Loader Hardening - Complete Implementation

**Date:** 2026-01-19  
**Status:** ✅ Complete - All tests passing, manually verified in unreal-qt

## Summary

Complete hardening of Z80 snapshot loader with defensive programming, state-independent loading, and comprehensive test coverage. The critical fix enables 128K snapshots (like BBG128.z80) to load correctly on a fresh emulator by properly unlocking and initializing memory configuration.

## Key Changes

### 1. PortDecoder API Extension

Added privileged operations for snapshot loading and debug sessions:

```cpp
// portdecoder.h
void UnlockPaging();  // Clear lock bit for snapshot/debug
void LockPaging();    // Set lock bit for testing/accuracy
```

**Implementation** ([portdecoder.cpp](core/src/emulator/ports/portdecoder.cpp#L398-L420)):
- Direct manipulation of `emulatorState.p7FFD` to bypass port device lock checks
- Used by snapshot loaders and debug sessions to override emulation state
- Inherited by all port decoder subclasses

### 2. State-Independent 128K Loading

**Problem:** BBG128.z80 would only load after other snapshots had configured 128K mode. On fresh 48K-initialized emulator, it would fail.

**Root Cause:** Port 7FFD lock bit (bit 5) prevents bank changes. Snapshot's port values include lock=1, so writes were ignored.

**Solution** ([loader_z80.cpp](core/src/loaders/snapshot/loader_z80.cpp#L189-L221)):
```cpp
case Z80_128K:
{
    uint8_t bank3Page = _port7FFD & 0x07;
    
    // Step 1: Unlock paging
    ports.UnlockPaging();
    
    // Step 2: Set port values  
    ports.PeripheralPortOut(0x7FFD, _port7FFD);
    ports.PeripheralPortOut(0xFFFD, _portFFFD);
    
    // Step 3: Explicit state assignment (including lock bit)
    _context->emulatorState.p7FFD = _port7FFD;
    
    // Step 4: Configure 128K memory banks (5-2-variable)
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(bank3Page);
    
    // Step 5: Trigger ROM selection (respects snapshot's ROM choice)
    memory.UpdateZ80Banks();
}
```

### 3. Defensive Programming Additions

| Feature | File | Description |
|---------|------|-------------|
| **Bounds checking** | loader_z80.cpp | Page numbers validated against MAX_ROM_PAGES/MAX_RAM_PAGES |
| **Overflow detection** | loader_z80.cpp | Decompression RLE overflow warnings (non-fatal) |
| **IFF flag sanitization** | loader_z80.cpp | Clamp to 0-1 instead of rejection (real-world compatibility) |
| **Header validation** | loader_z80.cpp | Extended header length and model number checks |
| **Min file size** | loader_z80.cpp | < 31 bytes rejected early |

### 4. Test Coverage

**Total: 33 tests** (10 fuzzing + 18 functional + 5 lock/state tests)

#### Fuzzing Tests (10)
- Random data at small/medium/large sizes
- Corrupted valid files (light/heavy corruption)
- Malformed headers (all zeros/ones/patterns)
- Extreme values (max header, huge blocks)

#### Functional Tests (18)
- v1/v2/v3 snapshot versions
- 48K and 128K snapshots
- Invalid file rejection (empty, truncated, wrong format)
- Synthetic invalid files (PNG, JPEG, GIF, markdown, text)

#### Lock/State Tests (5)
- `load128KAfterLockedPort` - Pre-lock via API, verify unlock
- `load48KAfter128K` - State isolation between snapshots
- `load128KWithPreLockedPort` - Unlock verification
- `load48KAfterLocked128K` - Lock preserved in emulatorState  
- `repeatedLockedLoads` - Multiple locked/unlocked cycles

## Files Modified

### Core Implementation
- `core/src/loaders/snapshot/loader_z80.cpp` - 128K init sequence, validation
- `core/src/loaders/snapshot/loader_z80.h` - LoaderZ80CUT test helper
- `core/src/emulator/ports/portdecoder.h` - UnlockPaging/LockPaging API
- `core/src/emulator/ports/portdecoder.cpp` - API implementation

### Test Files
- `core/tests/loaders/loader_z80_test.cpp` - State-independent tests (5 new)
- `core/tests/loaders/loader_z80_fuzzing_test.h` - Fuzzing test suite (10 tests)
- `testdata/loaders/z80/invalid/*` - Synthetic invalid test files

### Test Data Fixes
- `testdata/loaders/z80/newbench.z80` - Fixed IFF flags (2 → 0)
- `testdata/loaders/z80/truncated_v1.z80` - Fixed IFF flags (2 → 0)

## Verification

### Automated Tests
```bash
./bin/core-tests --gtest_filter="LoaderZ80*"
# Result: 33/33 tests passing
```

### Manual Testing
✅ BBG128.z80 loads correctly in unreal-qt on fresh emulator start

## Technical Notes

### Port 7FFD Bit Layout
```
Bit 0-2: RAM page at 0xC000 (bank 3)
Bit 3:   Screen selection (0=normal, 1=shadow)
Bit 4:   ROM selection (0=128K ROM, 1=48K/SOS ROM)
Bit 5:   Paging lock (1=locked, prevents further writes)
Bit 6-7: Unused
```

### Why Explicit State Assignment?
`PeripheralPortOut()` delegates to port device handlers which check lock BEFORE updating state. If port is locked, state doesn't update. Snapshot loading must bypass this to restore exact snapshot state, including the lock bit itself.

### SetRAMPage/SetROMPage Virtual Methods
These are **not** obsolete stubs. They're overridden by specific port decoder models:
- `PortDecoder_Spectrum3::SetRAMPage()` - +3 specific paging
- `PortDecoder_Pentagon128::SetRAMPage()` - Pentagon memory model
- `PortDecoder_Profi::SetRAMPage()` - Profi specific behavior
- etc.

Base class provides empty implementation for models that don't need it (48K).

## Related Work

- Original validation document: `z80_loader_validation.md`
- Task tracking: `task.md`
- Implementation plan: (archived after completion)
