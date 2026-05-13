# WD1793 Full Implementation & Test Coverage Plan

> **Work Item:** 2026-01-24-wd1793-test-coverage  
> **Status:** Planning  
> **Priority:** High  
> **Prerequisite:** [DiskImage Modernization](../2026-01-24-diskimage-modernization/README.md)  

> [!IMPORTANT]
> This work depends on DiskImage Modernization (8-12 days) which must be completed first.
> DiskImage provides the infrastructure for variable sector sizes, write protection, and change tracking.  

## Goal Statement

Bring the WD1793 FDC implementation to a **fully featured, fully tested state** with:
- Complete coverage of all WD1793 commands and features
- Comprehensive test suite for positive and negative scenarios
- Support for non-standard track layouts (copy protection)
- Modernized DiskImage and MFM subsystems

---

## Priority Framework

### Priority 1: ZX-Spectrum Emulation Features
Features actively used by ZX-Spectrum software, including copy-protected games.

### Priority 2: Full Testability
Complete test coverage for all currently implemented features.

### Priority 3: Extended Features
Full WD1793 datasheet compliance and edge cases.

---

## Phase Overview

| Phase | Name | Priority | Focus | Est. Effort |
|-------|------|----------|-------|-------------|
| 1 | Core Operations | P1 | READ/WRITE SECTOR, error handling | 2-3 days |
| 2 | Non-Standard Layouts | P1 | Copy protection, variable geometry | 2-3 days |
| 3 | DiskImage Modernization | P1 | Variable sector sizes, MFM refactor | 3-4 days |
| 4 | Test Completeness | P2 | All commands, all error conditions | 2-3 days |
| 5 | Positioning Commands | P2 | SEEK, STEP, verify flag | 1-2 days |
| 6 | READ ADDRESS/TRACK | P2 | Type 3 commands | 1-2 days |
| 7 | Advanced Features | P3 | Multi-sector, deleted marks | 2 days |
| 8 | Timing Accuracy | P3 | Precise timing, stepping rates | 2 days |

**Total Estimated Effort:** 15-21 days

---

## Phase 1: Core Operations (Priority 1)

### Objective
Ensure READ SECTOR and WRITE SECTOR work correctly with proper error handling.

### 1.1 READ SECTOR Tests

| Test | Description | File |
|------|-------------|------|
| `ReadSector_BasicOperation` | Read single sector, verify data | `wd1793_readsector_test.cpp` |
| `ReadSector_AllSectorNumbers` | Read sectors 1-16 sequentially | ↑ |
| `ReadSector_VerifyDataIntegrity` | Compare read data with known pattern | ↑ |
| `ReadSector_DRQTiming` | Verify DRQ raised and timing correct | ↑ |

### 1.2 WRITE SECTOR Tests

| Test | Description | File |
|------|-------------|------|
| `WriteSector_BasicOperation` | Write single sector, read back | `wd1793_writesector_test.cpp` |
| `WriteSector_AllSectorNumbers` | Write to sectors 1-16 | ↑ |
| `WriteSector_ReadAfterWrite` | Write pattern, verify read | ↑ |
| `WriteSector_DRQTiming` | Verify DRQ timing (regression) | ↑ |

### 1.3 Error Condition Tests

| Test | Description | File |
|------|-------------|------|
| `Error_CRCMismatch_IDAM` | Detect bad IDAM CRC, set S3 | `wd1793_error_test.cpp` |
| `Error_CRCMismatch_Data` | Detect bad data CRC, set S3 | ↑ |
| `Error_RecordNotFound` | Missing sector, set S4 after 5 revs | ↑ |
| `Error_WriteProtect` | Reject write on protected disk | ↑ |
| `Error_LostData_Read` | DRQ not serviced during read | ↑ |
| `Error_NotReady` | No disk inserted | ↑ |

### 1.4 Implementation Changes

```cpp
// wd1793.cpp - Add CRC validation in processSearchID()
void WD1793::processSearchID() {
    // After finding IDAM, validate CRC
    uint16_t calculatedCRC = calculateIDCRC(idam);
    uint16_t storedCRC = readIDCRC(idam);
    if (calculatedCRC != storedCRC) {
        raiseCrcError();
        return;
    }
    // Continue...
}
```

### Deliverables
- [ ] `core/tests/emulator/io/fdc/wd1793_readsector_test.cpp`
- [ ] `core/tests/emulator/io/fdc/wd1793_writesector_test.cpp`
- [ ] `core/tests/emulator/io/fdc/wd1793_error_test.cpp`
- [ ] CRC validation in READ_SECTOR path
- [ ] Write protect check in WRITE_SECTOR

---

## Phase 2: Non-Standard Track Layouts (Priority 1)

### Objective
Support copy-protected disks that use non-standard sector arrangements.

### 2.1 Features Required

| Feature | Description | Used By |
|---------|-------------|---------|
| Variable sectors/track | 1-26 sectors per track | Copy protection |
| Missing sectors | Intentionally absent sector numbers | Alkatraz, etc. |
| Duplicate sectors | Same sector number, different data | Some protections |
| Weak bits | Sectors that read differently | Speedlock |
| Extra tracks | Beyond standard 80 tracks | Some games |
| Bad CRC sectors | Intentional CRC errors | Copy protection |

### 2.2 DiskImage Changes

```cpp
// diskimage.h - Per-track sector count
struct TrackDescriptor {
    uint8_t sectorCount;           // 1-26
    uint8_t sectorNumbers[26];     // Actual sector IDs
    uint8_t sectorSizes[26];       // Size codes (0-3)
    uint16_t sectorOffsets[26];    // Byte offset in raw data
};

class Track {
    TrackDescriptor descriptor;    // NEW: Per-track geometry
    uint8_t rawData[6250];         // MFM encoded data
    // ...
};
```

### 2.3 MFM Parser Enhancements

```cpp
// mfm_parser.h - Support arbitrary sector layouts
struct ParseOptions {
    bool allowDuplicateSectors = false;
    bool allowMissingSectors = false;
    bool reportWeakBits = false;
};

TrackParseResult parseTrack(const uint8_t* data, size_t len, ParseOptions opts);
```

### 2.4 Tests

| Test | Description | File |
|------|-------------|------|
| `NonStandard_9SectorsPerTrack` | IBM PC format | `wd1793_nonstandard_test.cpp` |
| `NonStandard_18SectorsPerTrack` | High density | ↑ |
| `NonStandard_MissingSector` | Sector 5 missing | ↑ |
| `NonStandard_DuplicateSector` | Two sector 1s | ↑ |
| `NonStandard_BadCRCSector` | Intentional CRC error | ↑ |
| `NonStandard_40vs80Track` | 40-track image on 80-track drive | ↑ |

### Deliverables
- [ ] `TrackDescriptor` structure in `diskimage.h`
- [ ] Per-track geometry detection in `reindexFromMFM()`
- [ ] `core/tests/emulator/io/fdc/wd1793_nonstandard_test.cpp`
- [ ] Test images with non-standard layouts

---

## Phase 3: DiskImage & MFM Modernization (Priority 1)

### Objective
Refactor DiskImage and MFM systems for flexibility and maintainability.

### 3.1 Variable Sector Size Support

| Size Code | Bytes | Support Status |
|-----------|-------|----------------|
| N=0 | 128 | To implement |
| N=1 | 256 | ✅ Current |
| N=2 | 512 | To implement |
| N=3 | 1024 | To implement |

### 3.2 DiskImage Refactoring

```cpp
// Before: Fixed 256-byte sectors
struct RawSectorBytes {
    uint8_t data[256];
    uint16_t data_crc;
};

// After: Variable sector size
struct Sector {
    std::vector<uint8_t> data;    // Variable size
    uint16_t dataCRC;
    uint8_t sizeCode;             // N field (0-3)
    bool crcValid;                // CRC status
    bool deleted;                 // F8 vs FB
};
```

### 3.3 MFM Parser Modernization

```cpp
// Cleaner API
class MFMParser {
public:
    struct Config {
        bool validateCRC = true;
        bool detectWeakBits = false;
        uint8_t expectedSectors = 16;
    };
    
    TrackLayout parseTrack(std::span<const uint8_t> rawData, Config config);
    bool validateTrack(const TrackLayout& layout);
    std::string generateReport(const TrackLayout& layout);
};
```

### 3.4 Tests

| Test | Description | File |
|------|-------------|------|
| `SectorSize_128Bytes` | N=0 sectors | `wd1793_sectorsize_test.cpp` |
| `SectorSize_256Bytes` | N=1 sectors (current) | ↑ |
| `SectorSize_512Bytes` | N=2 sectors | ↑ |
| `SectorSize_1024Bytes` | N=3 sectors | ↑ |
| `SectorSize_Mixed` | Different sizes on same track | ↑ |
| `MFM_ParseStandardTrack` | 16×256b | `mfm_parser_extended_test.cpp` |
| `MFM_ParseIBMTrack` | 9×512b | ↑ |
| `MFM_ParseHDTrack` | 18×512b | ↑ |

### Deliverables
- [ ] Variable-size `Sector` class
- [ ] Updated `DiskImage` API
- [ ] `MFMParser` refactoring
- [ ] `core/tests/emulator/io/fdc/wd1793_sectorsize_test.cpp`
- [ ] `core/tests/emulator/io/fdc/mfm_parser_extended_test.cpp`

---

## Phase 4: Test Completeness (Priority 2)

### Objective
Achieve 100% test coverage for all implemented features.

### 4.1 Status Register Tests

| Test | Bit | Description |
|------|-----|-------------|
| `Status_Busy_Set` | S0 | Set during command |
| `Status_Busy_Clear` | S0 | Clear after command |
| `Status_DRQ_ReadSector` | S1 | DRQ during read |
| `Status_DRQ_WriteSector` | S1 | DRQ during write |
| `Status_LostData` | S2 | DRQ timeout |
| `Status_CRCError` | S3 | Bad CRC |
| `Status_RecordNotFound` | S4 | Sector not found |
| `Status_SpinUp` | S5 | Motor spin-up |
| `Status_WriteProtect` | S6 | Write protected |
| `Status_NotReady` | S7 | No disk |

### 4.2 FORCE INTERRUPT Tests

| Test | Condition | Description |
|------|-----------|-------------|
| `ForceInt_I0` | i0=1 | Not-Ready→Ready |
| `ForceInt_I1` | i1=1 | Ready→Not-Ready |
| `ForceInt_I2` | i2=1 | Index pulse |
| `ForceInt_I3` | i3=1 | Immediate |
| `ForceInt_D0` | $D0 | No interrupt bits |

### Deliverables
- [ ] `core/tests/emulator/io/fdc/wd1793_status_test.cpp`
- [ ] `core/tests/emulator/io/fdc/wd1793_forceint_test.cpp`

---

## Phase 5: Positioning Commands (Priority 2)

### Objective
Test all Type 1 commands thoroughly.

### 5.1 Tests

| Test | Command | Description |
|------|---------|-------------|
| `Seek_ToTrack0` | SEEK | Seek to track 0 |
| `Seek_ToTrack40` | SEEK | Seek to middle track |
| `Seek_ToTrack79` | SEEK | Seek to last track |
| `Seek_WithVerify` | SEEK+V | Verify flag behavior |
| `Step_DirectionIn` | STEP | Step toward center |
| `Step_DirectionOut` | STEP | Step toward edge |
| `Step_BeyondTrack0` | STEP_OUT | Boundary check |
| `Step_BeyondTrack79` | STEP_IN | Boundary check |
| `StepRates_6ms` | r0r1=00 | Fastest |
| `StepRates_30ms` | r0r1=11 | Slowest |

### Deliverables
- [ ] `core/tests/emulator/io/fdc/wd1793_positioning_test.cpp`

---

## Phase 6: READ ADDRESS/TRACK (Priority 2)

### Objective
Implement and test Type 3 commands.

### 6.1 READ ADDRESS

Returns 6 bytes: Track, Head, Sector, Size, CRC1, CRC2

| Test | Description |
|------|-------------|
| `ReadAddress_BasicOperation` | Read next IDAM |
| `ReadAddress_CRCData` | Verify CRC bytes |
| `ReadAddress_AllSectors` | 16 sequential reads |

### 6.2 READ TRACK

Reads entire raw track data.

| Test | Description |
|------|-------------|
| `ReadTrack_FullTrack` | Read 6250 bytes |
| `ReadTrack_VerifyMFM` | Compare with known pattern |

### Deliverables
- [ ] READ_ADDRESS implementation verification
- [ ] READ_TRACK implementation verification
- [ ] `core/tests/emulator/io/fdc/wd1793_type3_test.cpp`

---

## Phase 7: Advanced Features (Priority 3)

### 7.1 Multi-Sector Operations

| Test | Description |
|------|-------------|
| `MultiSector_Read4` | Read 4 sectors sequentially |
| `MultiSector_Write4` | Write 4 sectors sequentially |
| `MultiSector_EndOfTrack` | Handle sector 16→1 wrap |

### 7.2 Deleted Data Marks

| Test | Description |
|------|-------------|
| `DeletedMark_Write` | Write with F8 DAM |
| `DeletedMark_Detect` | Read detects deleted mark |

### Deliverables
- [ ] Multi-sector M flag handling
- [ ] Deleted data mark (a0) flag
- [ ] `core/tests/emulator/io/fdc/wd1793_advanced_test.cpp`

---

## Phase 8: Timing Accuracy (Priority 3)

### 8.1 Precise Timing

| Feature | Timing | Test |
|---------|--------|------|
| Index pulse | 5 RPM = 200ms/rev | `Timing_IndexPulse` |
| Step rate 6ms | 21,000 T-states | `Timing_StepRate6` |
| Step rate 12ms | 42,000 T-states | `Timing_StepRate12` |
| Head load | 30-60ms | `Timing_HeadLoad` |
| Byte time | 32µs = 112 T-states | `Timing_ByteTime` |

### Deliverables
- [ ] Timing validation tests
- [ ] `core/tests/emulator/io/fdc/wd1793_timing_test.cpp`

---

## Success Criteria

### Phase Completion Checklist

- [ ] **Phase 1:** All READ/WRITE SECTOR tests pass
- [ ] **Phase 2:** Non-standard layout samples work
- [ ] **Phase 3:** All sector sizes supported and tested
- [ ] **Phase 4:** Status register 100% covered
- [ ] **Phase 5:** All positioning commands tested
- [ ] **Phase 6:** Type 3 commands verified
- [ ] **Phase 7:** Multi-sector and deleted marks work
- [ ] **Phase 8:** Timing within 5% accuracy

### Metrics

| Metric | Current | Target |
|--------|---------|--------|
| Test count | ~93 | 150+ |
| Command coverage | 60% | 100% |
| Error case coverage | 20% | 90% |
| Sector size support | 1/4 | 4/4 |
| Non-standard layouts | 0 | 6+ |

---

## Related Documents

- [Gap Analysis](../reviews/wd1793-test-gap-analysis.md)
- [TR-DOS Track Structure](../design/io/fdc/trdos-format-track-structure.md)
- [WD1793 Datasheet](../../datasheets/wd1793.pdf)
