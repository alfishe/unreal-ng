# WD1793 TR-DOS Integration Test Plan

## Goal
Create a complex integration test that validates the full WD1793 FDC implementation by:
1. Inserting an empty disk image
2. Formatting it using TR-DOS ROM routine
3. Verifying the TR-DOS catalog structure
4. Validating MFM encoding in raw track data

## Prerequisites

### Pentagon-128K Configuration
- Model: `MM_PENTAGON` with 128KB RAM
- ROM: Pentagon 128K ROM set including TR-DOS v5.04T
- FDC: WD1793 via Beta-128 interface

### TR-DOS Format Process
Per TR-DOS documentation, the FORMAT routine (entry point varies by version):
1. Sets up format parameters in system variables
2. Uses WD1793 Write Track command for each track
3. Creates standard sector interleave pattern (1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16)
4. Writes system sectors on Track 0 (catalog + disk info)

## Test Design

### Option A: Direct FDC Test (No ROM execution)
**Approach**: Simulate TR-DOS format at FDC level by issuing Write Track commands with correct format data
- **Pros**: Fast, deterministic, no ROM dependencies
- **Cons**: Doesn't validate ROM/FDC integration

### Option B: Full ROM Execution Test
**Approach**: Run Z80 emulator executing actual TR-DOS FORMAT routine
- **Pros**: Tests full integration, realistic
- **Cons**: Requires ROM, slower, complex setup

### Recommended: Hybrid Approach
1. **Test A**: Direct FDC format test (validates WD1793 Write Track)
2. **Test B**: TR-DOS catalog structure validation (disk image level)
3. **Test C**: MFM encoding validation (raw track level)

## Implementation

### Phase -1: BasicEncoder Class (PREREQUISITE)

> [!IMPORTANT]
> TR-DOS integration tests require the ability to programmatically load BASIC programs
> into emulator memory for testing FORMAT commands and verifying execution.

#### Purpose
Create `BasicEncoder` class to:
1. **Tokenize** plain text BASIC programs to ZX Spectrum binary format
2. **Inject** tokenized programs into emulator memory with system variable correction
3. **Enable** programmatic test scenarios (e.g., `RANDOMIZE USR 15616: REM: FORMAT "TEST"`)

#### Implementation
- **Files**: `basicencoder.h`, `basicencoder.cpp`, `basicencoder_test.cpp`
- **Dependencies**: Complements existing `BasicExtractor` class
- **Details**: See [basicencoder_plan.md](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/docs/inprogress/2026-01-16-write-track-integration-test/basicencoder_plan.md)

#### Status
- [x] Implementation plan created
- [x] BasicEncoder class implemented
- [x] Unit tests passing (11/16 tests pass)
  - ✅ Tokenization tests (6/6)
  - ✅ Edge case tests (4/4)
  - ✅ Full emulator integration test (1/1)
  - ⚠️ Raw Memory injection tests (0/5) - require full emulator context
- [x] Integration test with Pentagon 128K passing

### Phase 0: ROM/RAM Switching Infrastructure (PREREQUISITE)

> [!IMPORTANT]
> Before TR-DOS integration tests can work, the Memory ROM/RAM switching 
> methods must properly update all internal state.

#### Problem Identified
The `SetROMDOS()`, `SetROM48k()`, etc. methods were not fully updating:
- `_bank_mode[0]` flag
- `_isPage0ROMDOS` and related flags (via `SetROMPageFlags()`)
- Port decoder state (optional, via `updatePorts` parameter)

#### Tests Required (in memory_test.cpp)

```cpp
// Test ROM switching methods update all state correctly
TEST_F(Memory_Test, SetROMDOS_UpdatesAllState)
{
    // 1. Call SetROMDOS(true)
    memory->SetROMDOS(true);
    
    // 2. Verify _bank_read[0] == base_dos_rom
    // 3. Verify _bank_mode[0] == BANK_ROM
    // 4. Verify isCurrentROMDOS() == true
    // 5. Verify DirectReadFromZ80Memory(0x0000) returns DOS ROM byte
    // 6. Verify port decoder ROM page updated
}

TEST_F(Memory_Test, SetROM48k_UpdatesAllState)
TEST_F(Memory_Test, SetROM128k_UpdatesAllState)
TEST_F(Memory_Test, SetROMSystem_UpdatesAllState)
```

#### Design: Test vs Runtime Compatibility

| Parameter | Purpose |
|-----------|---------|
| `updatePorts=false` | Minimal update for tests (no port decoder changes) |
| `updatePorts=true` | Full synchronization for runtime use |

This allows tests to work without full emulator context while runtime maintains proper state sync.

### Phase 1: Add TR-DOS Constants
Update [spectrumconstants.h](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/spectrumconstants.h) with:
- TR-DOS format entry points for common versions
- Sector interleave pattern constants
- Track 0 sector layout (catalog sectors 0-8, disk info sector 9)

### Phase 2: Direct Format Test

```cpp
TEST_F(WD1793_Test, Integration_Format_DirectFDC)
{
    // 1. Create empty 80-track DS disk image
    DiskImage diskImage(80, 2);
    
    // 2. For each track, issue Write Track command with standard format data
    for (int track = 0; track < 160; track++) {
        formatTrackWithFDC(fdc, track / 2, track % 2);
    }
    
    // 3. Write TR-DOS system sectors (Track 0, Sectors 0-8: catalog, Sector 9: disk info)
    writeTRDOSSystemSectors(fdc, &diskImage);
    
    // 4. Validate using LoaderTRD::validateEmptyTRDOSImage()
}
```

### Phase 3: Catalog Validation Test

```cpp
TEST_F(WD1793_Test, Integration_TRDOS_CatalogValidation)
{
    // 1. Format disk directly or load pre-formatted empty TRD
    // 2. Verify Track 0 sectors 0-8 contain valid catalog (128 entries, all $00)
    // 3. Verify Track 0 sector 9 contains disk info:
    //    - Byte 0xE1: First free sector (always 0x01 on empty disk)
    //    - Byte 0xE2: First free track (always 0x01 on empty disk)
    //    - Byte 0xE3: Disk type (0x16=40T DS, 0x17=40T SS, 0x18=80T SS, 0x19=80T DS)
    //    - Byte 0xE4: Number of files (0x00 on empty disk)
    //    - Bytes 0xE5-0xE6: Free sectors count (2544 for 80T DS)
    //    - Bytes 0xE7-0xEE: Disk label (usually spaces or "BOOT")
}
```

### Phase 4: MFM Encoding Validation

```cpp
TEST_F(WD1793_Test, Integration_MFM_Encoding)
{
    // 1. Format a track using Write Track command
    // 2. Read raw track data
    // 3. Verify MFM structure:
    //    - Gap 1: 80x 0x4E
    //    - For each sector:
    //      - Sync: 12x 0x00
    //      - IDAM: 3x 0xA1 (with missing clock) + 0xFE
    //      - ID: cylinder, side, sector, size byte
    //      - CRC: 2 bytes
    //      - Gap 2: 22x 0x4E
    //      - Sync: 12x 0x00  
    //      - DAM: 3x 0xA1 + 0xFB
    //      - Data: 256 bytes (0xE5 per TR-DOS standard)
    //      - CRC: 2 bytes
    //      - Gap 3: 54x 0x4E
    //    - Gap 4: fill to end of track
    // 4. Verify sector interleave: 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16
}
```

## Files to Modify

### [MODIFY] spectrumconstants.h
Add TR-DOS format constants:
- Standard sector interleave pattern
- Empty sector fill byte (0xE5)
- Track 0 layout constants

### [NEW] wd1793_integration_test.cpp (or add to wd1793_test.cpp)
- `Integration_Format_DirectFDC`
- `Integration_TRDOS_CatalogValidation`
- `Integration_MFM_Encoding`

## Verification Criteria

1. **Format Completion**: All 160 tracks (80 cylinders × 2 sides) written without errors
2. **Catalog Valid**: LoaderTRD::validateEmptyTRDOSImage() returns true
3. **MFM Valid**: Raw track data matches expected MFM structure
4. **Sector Interleave**: Physical sector order matches TR-DOS standard (1,9,2,10...)
5. **No Lost Data**: WDS_LOSTDATA never set during format

## Notes

> [!IMPORTANT]
> Full ROM execution test requires TR-DOS ROM to be available at test runtime.
> For CI/CD, the direct FDC tests should be sufficient.

> [!NOTE]  
> TR-DOS uses 0xE5 as the fill byte for empty sectors (not 0x00).
> This is a legacy from CP/M and helps detect uninitialized data.
