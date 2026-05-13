# WD1793 Reference Data Integration Tests

## Goal

Create comprehensive integration tests for WD1793 FDC using synthesized TR-DOS reference disk data. These tests verify:
1. Correct MFM reindexing of tracks loaded from raw data
2. DiskImage domain model API correctness
3. WD1793 READ SECTOR operations return correct data

## Approach

### Option A: Synthesized Reference Disk (Recommended)
Generate a complete TR-DOS formatted disk image **programmatically** in the test:
- 40 tracks, 1 side (standard TR-DOS disk)
- 16 sectors per track, 256 bytes each
- Proper MFM structure (IDAMs, DAMs, CRCs)
- Sector 9 on Track 0 contains TR-DOS volume info

**Pros**: No external dependencies, deterministic, easy to modify
**Cons**: Requires synthesis code

### Option B: Captured Reference TRD File
Use an existing `.trd` file from testdata:
- Load via TRD loader
- Verify read operations match expected content

**Pros**: Real-world data
**Cons**: Dependency on external file

---

## Proposed Changes

### [NEW] `core/tests/emulator/io/fdc/wd1793_reference_data_test.cpp`

New test file containing:

#### Test Group 1: DiskImage Domain Model Tests
- `ReferenceTrack_LoadAndReindex` - Load reference track, verify reindexFromMFM() finds 16 sectors
- `ReferenceTrack_SectorOrderedRef` - Verify `sectorsOrderedRef[]` points to correct sectors
- `ReferenceTrack_GetSector_ReturnsCorrectData` - Verify [getSector(1-16)](core/src/emulator/io/fdc/diskimage.h#283-303) returns expected data
- `ReferenceTrack_GetIDForSector_ReturnsCorrectIDAM` - Verify IDAM fields (cylinder, head, sector, size)
- `ReferenceTrack_GetDataForSector_ReturnsCorrectOffset` - Verify data pointer offset in raw buffer

#### Test Group 2: TR-DOS System Sector Tests
- `SystemSector_VolumeInfo_ValidStructure` - Verify sector 9 on track 0 contains valid TR-DOS info
- `SystemSector_FreeSectors_Count` - Verify free sector count matches expected (2544 for empty disk)
- `SystemSector_FirstFreeSector_Position` - Verify free sector pointer (should be track 1, sector 0)
- `SystemSector_DiskType_Correct` - Verify disk type byte (0x16 for DS/80T, 0x17 for SS/80T)

#### Test Group 3: WD1793 READ SECTOR Tests
- `ReadSector_Track0Sector9_ReturnsVolumeInfo` - WD1793 READ SECTOR returns system sector
- `ReadSector_AllSectors_SequentialRead` - Read all 16 sectors on track 0, verify each
- `ReadSector_MultiSector_FirstToLast` - Multi-sector read from sector 1 to 16
- `ReadSector_NonExistent_ReturnsRNF` - Reading beyond track count returns Record Not Found
- `ReadSector_DataCRC_Valid` - Verify data CRC matches calculated CRC

#### Test Group 4: Interleave Pattern Tests
- `Interleave_PhysicalToLogical_Mapping` - Verify 1:2 interleave: physical 0→sector 1, physical 1→sector 9
- `Interleave_SequentialLogicalRead_CorrectOrder` - Reading sectors 1-16 logically traverses track correctly

---

### [NEW] `core/tests/emulator/io/fdc/trdos_disk_reference.h`

Header with synthesized TR-DOS disk data:

```cpp
namespace TRDOSDiskReference {
    // TR-DOS System Sector (Sector 9, Track 0) - 256 bytes
    constexpr uint8_t kSystemSector[256] = {
        // 0x00-0xE0: Unused (zeros)
        // 0xE1: First free sector (track number)
        // 0xE2: First free sector (logical sector within track) 
        // 0xE3: Disk type (0x16=DS/80T, 0x17=SS/80T, 0x18=DS/40T, 0x19=SS/40T)
        // 0xE4: Number of files (0 for empty disk)
        // 0xE5-0xE6: Free sectors count (little-endian, 2544 for DS/80T)
        // 0xE7: TR-DOS ID (0x10)
        // 0xE8-0xE9: Reserved (zeros)
        // 0xEA-0xF4: Disk name (11 bytes, space-padded)
        // 0xF5-0xFF: Reserved
    };
    
    // Complete formatted track for track 0
    constexpr uint8_t kTrack0[6250] = { ... };
    
    // Factory function to create complete disk
    DiskImage* createFormattedDisk(uint8_t cylinders, uint8_t sides);
}
```

---

## TR-DOS System Sector Structure (Sector 9, Track 0)

| Offset | Size | Description |
|--------|------|-------------|
| 0x00-0xDF | 224 | Unused (zeros) |
| 0xE0 | 1 | Reserved |
| 0xE1 | 1 | First free track number |
| 0xE2 | 1 | First free sector (logical sector within track, 0-based) |
| 0xE3 | 1 | Disk type: 0x16=DS/80T, 0x17=SS/80T, 0x18=DS/40T, 0x19=SS/40T |
| 0xE4 | 1 | Number of files |
| 0xE5-0xE6 | 2 | Free sectors count (little-endian) |
| 0xE7 | 1 | TR-DOS ID: always 0x10 |
| 0xE8-0xE9 | 2 | Reserved (zeros) |
| 0xEA-0xF4 | 11 | Disk name (space-padded) |
| 0xF5-0xFF | 11 | Reserved |

---

## Verification Plan

### Automated Tests
Run the new test file:
```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng
cmake --build build --target core-tests -j8
./build/bin/core-tests --gtest_filter="*ReferenceData*"
```

### Test Coverage Goals
- All 16 sectors readable via DiskImage API
- All 16 sectors readable via WD1793 READ SECTOR command
- System sector (sector 9) contains valid TR-DOS volume info
- Interleave mapping correctly translates physical→logical

---

## Open Questions

1. **Disk geometry**: Should we test 40T/SS, 80T/DS, or both?
2. **CRC calculation**: Should we verify CRCs match expected values, or just that they're valid?
3. **Multi-track tests**: Should we synthesize multiple tracks (e.g., tracks 0-3) for more comprehensive testing?
