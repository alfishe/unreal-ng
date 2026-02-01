# TR-DOS FORMAT Integration Test - Fix Walkthrough

## Overview

This document describes the complete debugging and fix process for the TR-DOS FORMAT integration test (`TRDOS_FORMAT_FullOperation`). The test was failing with "Disc Error Trk 0 sec 9" after formatting all 160 tracks, indicating that TR-DOS couldn't read back sector 9 after the FORMAT operation completed.

---

## Root Causes Identified

Three distinct issues were discovered and fixed:

### 1. CRC Calculation Mismatch (F7 Command)
**Symptom:** MFM validation showed "0/16 valid sectors - Sector 1 IDAM CRC invalid"

**Root Cause:** The F7 command in `processWriteTrack()` used inline CRC calculation that didn't match the algorithm in `CRCHelper::crcWD1793()`. Specifically, `crcWD1793()` includes a byte swap at the end (`_byteswap_ushort(crc)`), but the inline calculation didn't.

### 2. TR-DOS 1:2 Sector Interleave Not Handled
**Symptom:** After CRC fix, still getting "Disc Error Trk 0 sec 9"

**Root Cause:** TR-DOS uses 1:2 interleave pattern where physical sector positions don't match logical sector numbers:
- Physical position 0 → Sector 1
- Physical position 1 → Sector 9  
- Physical position 2 → Sector 2
- Physical position 3 → Sector 10
- ... etc.

The existing `reindexSectors()` assumed sector N was at physical position N-1, which broke sector lookups.

### 3. WRITE_SECTOR DRQ Timing Issue
**Symptom:** After above fixes, 8 LOSTDATA errors occurred after cylinder 79

**Root Cause:** `processWriteSector()` used `transitionFSM(S_WRITE_BYTE)` (immediate transition) after raising DRQ, not giving the CPU time to write data to the data register before the LOSTDATA check in `processWriteByte()`.

---

## Debugging Process

### Phase 1: Identify the Failure Point

**Test Output Analysis:**
```
[STEP 6b] HEAD 1  CYLINDER 79
[STEP 6b] ⚠ WD1793 idle but no progress/completion detected
[STEP 6b] Debug: lastCmd=10, status=0x4
Disc Error
Trk 0 sec 9
Retry,Abort,Ignore?
```

**Key Observations:**
- All 160 tracks formatted successfully (cylinder 0-79, both heads)
- Error occurred after FORMAT, during TR-DOS system info write
- `lastCmd=10` = READ_SECTOR, `status=0x4` = bit 2 set
- For Type II commands, bit 2 = LOSTDATA (not addressed initially)

### Phase 2: CRC Validation

**Debug Added:** Modified `processWriteTrack()` to output MFM validation results:
```cpp
// In processWriteTrack() after write completes:
std::cout << "[DEBUG] MFM validation: " << (result.passed ? "PASSED" : "FAILED")
          << " (" << result.parseResult.validSectors << "/16 valid sectors)" << std::endl;
```

**Result:** `[DEBUG] MFM validation: FAILED (0/16 valid sectors)`
**First issue:** `Sector 1 IDAM CRC invalid`

**Analysis:** Compared CRC algorithms:
1. `CRCHelper::crcWD1793()` uses table lookup and returns `_byteswap_ushort(crc)` 
2. Inline calculation in F7 command didn't swap bytes

### Phase 3: Sector Mapping Analysis

**Debug Added:** Dumped IDAM sector numbers after WRITE_TRACK:
```cpp
std::cout << "[DEBUG] Track 0 IDAM sector numbers: ";
for (int i = 0; i < 16; i++) {
    std::cout << (int)_writeTrackTarget->sectors[i].address_record.sector;
}
```

**Result:** `1,9,2,10,3,11,4,12,5,13,6,14,7,15,8,16`

This confirmed TR-DOS's 1:2 interleave pattern. Sector 9 is at physical position 1, not position 8.

### Phase 4: LOSTDATA Tracing

**Debug Added:** In `raiseLostData()`:
```cpp
std::cout << "[DEBUG] raiseLostData() called - _lost_data set to true\n" << std::flush;
```

**Result:** 8 calls after cylinder 79 completed - during WRITE_SECTOR operations when TR-DOS writes system info.

**Analysis:** Traced to `processWriteByte()` checking `if (_drq_out && !_drq_served)` on the first call, before CPU had time to respond.

---

## Code Changes

### Fix 1: CRC Calculation (`wd1793.cpp`)

**Location:** `processWriteTrack()` F7 command handler (~line 2329)

**Before:**
```cpp
case 0xF7:
{
    // Generate and write 2 CRC bytes - calculate over range from _crcStartPosition
    uint16_t crc = 0xCDB4;
    for (size_t i = _crcStartPosition; i < _rawDataBufferIndex; i++)
    {
        crc ^= static_cast<uint16_t>(_rawDataBuffer[i]) << 8;
        for (int j = 0; j < 8; j++)
        {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        }
    }
    // Write CRC bytes...
}
```

**After:**
```cpp
case 0xF7:
{
    // Generate and write 2 CRC bytes - calculate over range from _crcStartPosition
    // Use CRCHelper::crcWD1793 which matches the algorithm used by MFMParser for validation
    // Note: crcWD1793 includes a byte swap at the end, so the returned value is ready to write as-is
    size_t crcLen = _rawDataBufferIndex - _crcStartPosition;
    uint16_t crc = CRCHelper::crcWD1793(&_rawDataBuffer[_crcStartPosition], static_cast<uint16_t>(crcLen));
    
    // Write CRC bytes - crcWD1793 returns bytes in swapped order ready for disk
    if (_rawDataBufferIndex < DiskImage::RawTrack::RAW_TRACK_SIZE)
    {
        _rawDataBuffer[_rawDataBufferIndex++] = static_cast<uint8_t>(crc >> 8);
        _bytesToWrite--;
    }
    if (_rawDataBufferIndex < DiskImage::RawTrack::RAW_TRACK_SIZE)
    {
        _rawDataBuffer[_rawDataBufferIndex++] = static_cast<uint8_t>(crc & 0xFF);
        _bytesToWrite--;
    }
    // ...
}
```

---

### Fix 2: Sector Mapping (`diskimage.h`)

**Location:** New method `reindexFromIDAM()` in `RawTrack` struct (~line 469)

**New Code:**
```cpp
/// Reindex sector access by reading IDAM sector numbers from each physical sector
/// Called after Write Track to rebuild sector mapping based on what was actually written
/// This handles TR-DOS's 1:2 interleave pattern correctly
void reindexFromIDAM()
{
    // Clear existing references
    for (uint8_t i = 0; i < SECTORS_PER_TRACK; i++)
    {
        sectorsOrderedRef[i] = nullptr;
        sectorIDsOrderedRef[i] = nullptr;
    }
    
    // Scan all 16 physical sectors and map by their IDAM sector number
    for (uint8_t physIdx = 0; physIdx < SECTORS_PER_TRACK; physIdx++)
    {
        RawSectorBytes* sectorRef = &sectors[physIdx];
        uint8_t sectorNo = sectorRef->address_record.sector;
        
        // TR-DOS uses sector numbers 1-16
        if (sectorNo >= 1 && sectorNo <= 16)
        {
            uint8_t logicalIdx = sectorNo - 1;  // Convert to 0-based index
            sectorsOrderedRef[logicalIdx] = sectorRef;
            sectorIDsOrderedRef[logicalIdx] = &sectorRef->address_record;
        }
    }
}
```

**Caller Change** (`wd1793.cpp` line ~2268):
```cpp
// After Write Track completes:
if (_writeTrackTarget)
{
    // Map sectors by their IDAM sector numbers, not by array position
    _writeTrackTarget->reindexFromIDAM();
    _writeTrackTarget = nullptr;
}
```

---

### Fix 3: WRITE_SECTOR Timing (`wd1793.cpp`)

**Location:** `processWriteSector()` (~line 2111)

**Before:**
```cpp
transitionFSM(WD1793::S_WRITE_BYTE);
```

**After:**
```cpp
// Use delayed transition to give CPU time to respond to DRQ before first byte check
transitionFSMWithDelay(WD1793::S_WRITE_BYTE, WD93_TSTATES_PER_FDC_BYTE);
```

---

## Debug Print Locations (For Future Reference)

### 1. CRC/MFM Validation (after WRITE_TRACK completes)
**File:** `wd1793.cpp` in `processWriteTrack()`, after `_bytesToWrite <= 0` check
```cpp
auto result = MFMValidator::validate(
    reinterpret_cast<const uint8_t*>(_writeTrackTarget),
    DiskImage::RawTrack::RAW_TRACK_SIZE);
std::cout << "[DEBUG] MFM validation: " << (result.passed ? "PASSED" : "FAILED")
          << " (" << result.parseResult.validSectors << "/16 valid sectors)" << std::endl;
```

### 2. IDAM Sector Numbers (after WRITE_TRACK completes)
**File:** `wd1793.cpp` in `processWriteTrack()`, after `reindexFromIDAM()`
```cpp
std::cout << "[DEBUG] Track IDAM sector numbers: ";
for (int i = 0; i < 16; i++) {
    std::cout << (int)_writeTrackTarget->sectors[i].address_record.sector;
    if (i < 15) std::cout << ",";
}
std::cout << std::endl;
```

### 3. Sector Not Found (in READ_SECTOR)
**File:** `wd1793.cpp` in `cmdReadSector()` lambda, when `_sectorData` is null
```cpp
std::cout << "[DEBUG READ] Sector " << (int)sectorReg << " not found on track " 
          << (int)trackReg << " side " << (int)sideUp << std::endl;
std::cout << "[DEBUG READ] sectorsOrderedRef state: ";
for (int i = 0; i < 16; i++) {
    std::cout << (track->sectorsOrderedRef[i] ? "+" : "-");
}
std::cout << std::endl;
```

### 4. LOSTDATA Detection
**File:** `wd1793.h` in `raiseLostData()`
```cpp
std::cout << "[DEBUG] raiseLostData() called - _lost_data set to true\n" << std::flush;
```

### 5. Status Register on Error
**File:** `wd1793_integration_test.cpp` in progress loop
```cpp
std::cout << "[STEP 6b] Debug: lastCmd=" << (int)lastCmd 
          << ", status=0x" << std::hex << (int)wdStatus << std::dec << "\n";
```

---

## Test Results After Fix

```
========================================
[FORMAT] Test Summary:
  ✓ ROM initialized and 128K menu visible
  ✓ Navigated to TR-DOS
  ✓ Empty disk inserted
  ✓ FORMAT command injected and executed
  ✓ Disk structure validated
  Tracks formatted: 160 / 160
========================================

[STEP 6] Disk info from sector 8:
  First free sector: 0
  First free track: 1
  Disk type: 0x16
  Number of files: 0
  Free sectors: 2544

[  PASSED  ] 1 test.
```

---

## Key Learnings

1. **CRC algorithms must match exactly** - Both calculation AND byte order matter. Use the same helper function everywhere.

2. **Sector interleave patterns vary by DOS** - TR-DOS uses 1:2 interleave which is different from simple sequential ordering. After WRITE_TRACK, sector references must be rebuilt from the actual IDAM data written.

3. **DRQ timing is critical** - The CPU needs time to respond to DRQ before checking if data was provided. Use `transitionFSMWithDelay()` not `transitionFSM()` for the first byte.

4. **Status register bits have different meanings** - Bit 2 means TRK00 for Type I commands but LOSTDATA for Type II/III. Always consider the command context when interpreting status.

5. **Debug prints at key decision points** - Adding temporary `std::cout` statements to validation results, flag setters, and error paths is invaluable for diagnosing complex state machine issues.
