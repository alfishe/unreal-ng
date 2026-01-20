# Z80 Loader Hardening Tasks

## Status: ✅ COMPLETE

## Code Changes Made
- [x] Fix comment typo in version detection (loader_z80.cpp:219)
- [x] Add bounds checking in loadZ80v2()
- [x] Add bounds checking in loadZ80v3()
- [x] Add page number validation in resolveSnapshotPage()
- [x] Add minimum file size validation in validate()
- [x] Add memory cleanup on error paths
- [x] Add BANK_INVALID sentinel to MemoryBankModeEnum

## Files Modified
| File | Changes |
|------|---------|
| `core/src/emulator/memory/memory.h` | Added `BANK_INVALID = 0xFF` sentinel |
| `core/src/loaders/snapshot/loader_z80.cpp` | Bounds checking, page validation, min size validation |
| `core/tests/loaders/loader_z80_test.cpp` | Added 14 test cases |

## Test Coverage
- [x] v1 snapshot validation and loading (prelim.z80)
- [x] v2 snapshot validation and loading (newbench.z80)
- [x] v3 snapshot validation and loading (dizzyx.z80)
- [x] 128K snapshot loading (BBG128.z80)
- [x] Empty file rejection
- [x] Truncated header rejection
- [x] Invalid extended header length rejection
- [x] Non-existent file handling
- [x] Truncated v1 snapshot handling
- [x] Truncated v2 snapshot handling

## Test Fixtures Created
| File | Size | Purpose |
|------|------|---------|
| `testdata/loaders/z80/prelim.z80` | 5497 | Real v1 format test file |
| `testdata/loaders/z80/z80-xcf-flavor.z80` | 49179 | v1 format (from SNA) |
| `testdata/loaders/z80/invalid/empty.z80` | 0 | Empty file rejection |
| `testdata/loaders/z80/invalid/truncated_header.z80` | 15 | Truncated header rejection |
| `testdata/loaders/z80/invalid/invalid_extlen.z80` | 32 | Invalid extLen rejection |
| `testdata/loaders/z80/invalid/truncated_v1.z80` | 2500 | Truncated v1 handling |
| `testdata/loaders/z80/invalid/truncated_v2.z80` | 1000 | Truncated v2 handling |

## Verification
All tests pass - see z80_loader_validation.md for test results.
