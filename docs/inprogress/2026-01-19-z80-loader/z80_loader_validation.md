# Z80 Snapshot Loader Validation Report

## Status: ✅ COMPLETE

All issues identified and fixed. 14 tests pass.

---

## Validation Results

### ✅ Header Structure: VERIFIED

Struct inheritance with `#pragma pack(push, 1)` correctly aligns all fields:
- `sizeof(Z80Header_v1)` = 30 bytes ✓
- `sizeof(Z80Header_v2)` = 55 bytes ✓  
- `sizeof(Z80Header_v3)` = 87 bytes ✓

### ✅ v1/v2/v3 Loaders: WORKING

All format versions load correctly with proper bounds checking.

---

## Changes Made

### 1. Added BANK_INVALID Sentinel
**File:** [memory.h](core/src/emulator/memory/memory.h#L19-L24)
```cpp
enum MemoryBankModeEnum : uint8_t {
    BANK_ROM = 0,
    BANK_RAM = 1,
    BANK_INVALID = 0xFF  // Sentinel for invalid pages
};
```

### 2. Bounds Checking in loadZ80v2/v3
- Validates memory block descriptor within file before access
- Validates compressed data within file before decompression
- Skips unknown pages with warning instead of crashing

### 3. Page Validation in resolveSnapshotPage()
- Initializes `result.mode = BANK_INVALID` as default
- Added default cases for unknown page numbers
- Replaced exceptions with warnings for unsupported modes

### 4. Minimum File Size Validation
- v1: ≥31 bytes (header + data)
- v2: ≥55 bytes (full v2 header)
- v3: ≥86 bytes (full v3 header)

### 5. Fixed Comment Typo
Line 219: "PC register is zero" (was incorrectly "not zero")

---

## Test Results

```
[==========] 14 tests from LoaderZ80_Test
[  PASSED  ] validateSnapshotFile
[  PASSED  ] stageLoad
[  PASSED  ] load
[  PASSED  ] validateV1Snapshot (prelim.z80)
[  PASSED  ] loadV1Snapshot
[  PASSED  ] validateV3Snapshot (dizzyx.z80)
[  PASSED  ] loadV3Snapshot
[  PASSED  ] load128KSnapshot (BBG128.z80)
[  PASSED  ] rejectEmptyFile
[  PASSED  ] rejectTruncatedHeader
[  PASSED  ] rejectInvalidExtendedHeaderLen
[  PASSED  ] handleNonExistentFile
[  PASSED  ] handleTruncatedV1Snapshot
[  PASSED  ] handleTruncatedV2Snapshot
[==========] 14 tests passed.
```

---

## Files Modified

| File | Description |
|------|-------------|
| `core/src/emulator/memory/memory.h` | Added BANK_INVALID sentinel |
| `core/src/loaders/snapshot/loader_z80.cpp` | Bounds checking, validation |
| `core/tests/loaders/loader_z80_test.cpp` | 14 test cases |

## Test Fixtures

| File | Size | Purpose |
|------|------|---------|
| `testdata/loaders/z80/prelim.z80` | 5497 | v1 format |
| `testdata/loaders/z80/invalid/empty.z80` | 0 | Empty file |
| `testdata/loaders/z80/invalid/truncated_header.z80` | 15 | Truncated header |
| `testdata/loaders/z80/invalid/invalid_extlen.z80` | 32 | Invalid ext len |
| `testdata/loaders/z80/invalid/truncated_v1.z80` | 2500 | Truncated v1 |
| `testdata/loaders/z80/invalid/truncated_v2.z80` | 1000 | Truncated v2 |
