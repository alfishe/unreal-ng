# SNA Loader Hardening - Walkthrough

## Objective
Harden the SNA snapshot loader with defensive programming, state-independent loading, and comprehensive test coverage following the same pattern as the Z80 loader.

## Changes Made

### 1. Defensive Programming in [loader_sna.cpp](core/src/loaders/snapshot/loader_sna.cpp)

#### File Size Validation
Added comprehensive file size validation in the [validate()](core/src/loaders/snapshot/loader_z80.cpp#44-129) method:

**48K SNA Validation:**
- Exact size check: 49,179 bytes (27 header + 49,152 RAM)
- Rejects any deviation from the expected size

**128K SNA Validation (Flexible Format):**
- Minimum size: 49,183 bytes (27 header + 3×16KB + 4 ext header)
- Accepts 1-8 additional 16KB RAM banks (flexible per spec)
- Common sizes: 131,103 bytes (5 banks), 147,487 bytes (8 banks)
- Validates whole banks (no partial pages)
- Requires at least 1 additional bank (base structure alone is invalid)

#### Detection Logic Fix
Fixed critical bug in [is128kSnapshot()](core/src/loaders/snapshot/loader_sna.cpp#186-212):
- **Bug**: Used `_sna128HeaderSize` which equals `_snaHeaderSize + sizeof(sna128Header)` (31 bytes)
- **Fix**: Changed to `sizeof(sna128Header)` directly (4 bytes)
- **Impact**: Fixed double-counting of main header in minimum size calculation

#### State-Independent Loading
Applied the **Unlock-Write-Assign-Set-Sync** pattern in [applySnapshotFromStaging()](core/src/loaders/snapshot/loader_sna.cpp#397-541) for 128K SNAs:
1. **Unlock**: `_context->pPortDecoder->UnlockPaging()`
2. **Set**: Configure 128K memory banks (5, 2, currentTopPage)
3. **Write**: [DecodePortOut(0x7FFD, _ext128Header.port_7FFD, z80.pc)](core/src/emulator/ports/portdecoder.cpp#141-184)
4. **Assign**: `_context->emulatorState.p7FFD = _ext128Header.port_7FFD`
5. **Sync**: Handle TR-DOS ROM activation if needed

### 2. Test Coverage

#### Invalid File Tests (10 tests - all passing)
Generated 14 invalid test files and implemented tests for 10 of them:
- `rejectEmptyFile` - 0 bytes
- `rejectTruncatedHeader` - 10 bytes
- `rejectHeaderOnly` - 27 bytes
- `rejectTruncated48K` - 30,000 bytes
- `rejectTruncated128K` - 49,183 bytes (minimum size, no additional banks)
- `rejectWrongFormat_PNG` - PNG file disguised as .sna
- `rejectWrongFormat_JPEG` - JPEG file disguised as .sna
- `rejectTextFile` - Plain text file
- `rejectAllZeros` - File filled with 0x00
- `rejectAllFF` - File filled with 0xFF

#### Lock/State Verification Tests (3 tests - all passing)
- `load128KWithPreLockedPort` - Load 128K SNA after locking paging
- `load48KAfterLocked128K` - Load 48K SNA after locked 128K state  
- `repeatedLoadIsIdempotent` - Load same 128K SNA twice with lock between

### 3. Format Research
Researched SNA format specification from official sources:
- [ZX-Evo documentation](https://raw.githubusercontent.com/tslabs/zx-evo/refs/heads/master/pentevo/docs/Formats/sna.txt)
- Confirmed 128K SNA format is flexible with variable number of RAM banks
- Specifications states "remaining RAM banks in ascending order"

## Test Results
✅ **20/20 tests passing** (100%)
- 7 existing tests
- 10 invalid file tests  
- 3 lock/state verification tests

## Key Technical Insights

### 1. 128K SNA Format Flexibility
The128K SNA format is more flexible than initially understood:
```
Offset   Size   Description
------------------------------------------------------------------------
0        27     bytes  SNA header
27       16Kb   bytes  RAM bank 5
16411    16Kb   bytes  RAM bank 2
32795    16Kb   bytes  RAM bank n (currently paged bank)
49179    2      word   PC
49181    1      byte   port 7FFD setting
49182    1      byte   TR-DOS rom paged (1) or not (0)
49183    16Kb*N bytes  remaining RAM banks in ascending order (N = 1-8)
------------------------------------------------------------------------
Total: 131103 or 147487 bytes (most common sizes)
```

### 2. Critical Bug Fix
The `_sna128HeaderSize` constant was misleadingly named:
- **Definition**: `_snaHeaderSize + sizeof(sna128Header)` = 27 + 4 = 31 bytes
- **Name implied**: Just the extended header size (4 bytes)
- **Solution**: Use `sizeof(sna128Header)` directly where needed

### 3. Validation Strategy
Implemented "Permissive Validation" with exact constraints:
- Strict on structure (whole banks, correct sizes)
- Flexible on content (allows 1-8 additional banks)
- Clear error messages for diagnostics

## Files Modified
- [loader_sna.cpp](core/src/loaders/snapshot/loader_sna.cpp) - Added defensive programming and validation
- [loader_sna_test.cpp](core/tests/loaders/loader_sna_test.cpp) - Added 13 new tests

## Documentation Updated
- Created [docs/inprogress/2026-01-19-sna-loader/SCOPE.md](docs/inprogress/2026-01-19-sna-loader/SCOPE.md) - Scope of work document
