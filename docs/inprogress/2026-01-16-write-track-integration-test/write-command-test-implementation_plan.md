# WRITE TRACK Integration Test - Implementation Plan

## Goal
Create comprehensive test validating WD1793 Write Track command via TR-DOS FORMAT.

## Approach
Use [TRDOSTestHelper](core/tests/_helpers/trdostesthelper.h#17-116) to execute `FORMAT "a"` BASIC command and verify disk structure.

---

## Test Path

### [NEW] `core/tests/wd1793_integration_test.cpp`

```cpp
TEST_F(WD1793_Integration_Test, Integration_TRDOS_Format_ViaBasicCommand)
{
    // 1. Create emulator with Pentagon model (has TR-DOS)
    TRDOSTestHelper helper(emulator);
    
    // 2. Create blank disk in drive A
    context->coreState.diskDrives[0]->insertDisk(new DiskImage(80, 2));
    
    // 3. Execute FORMAT command via BASIC injection
    helper.executeBasicCommand("FORMAT \"a\"");
    
    // 4. Wait for format (~10 million cycles)
    emulator->RunNCPUCycles(15'000'000);
    
    // 5. Verify disk structure
    auto* diskImage = context->coreState.diskDrives[0]->getDiskImage();
    
    // Check Track 0, Side 0, Sector 9 (system sector)
    auto* track = diskImage->getTrack(0, 0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getSectorCount(), 16);
    
    // Verify signature (0x10 at offset 0xE7)
    auto* sysSector = track->getSector(9);
    EXPECT_EQ(sysSector->data[0xE7], 0x10);  // TR-DOS signature
}
```

---

## Prerequisites

| Item | Status | Notes |
|------|--------|-------|
| BasicEncoder | ✅ | Working in 48K mode |
| TRDOSTestHelper | ✅ | Has `executeBasicCommand()` |
| BASIC WebAPI | ✅ | Verified working |
| Pentagon ROM | ✅ | Loads in tests |
| WD1793 Write Track | ⚠️ | Needs verification |

---

## Verification Steps

1. **Pre-format state**: Verify blank disk has no sector structure
2. **Post-format structure**: 
   - 16 sectors per track (TR-DOS standard)
   - Interleave pattern: 1, 9, 2, 10, 3, 11...
   - System sector at Track 0, Sector 9
   - Catalog in sectors 1-8
3. **MFM encoding**: Verify raw track contains valid MFM markers

---

## Files

| Action | File |
|--------|------|
| NEW | `core/tests/wd1793_integration_test.cpp` |
| MODIFY | [core/tests/CMakeLists.txt](core/tests/CMakeLists.txt) (add test file) |

---

## Build & Run

```bash
cd /Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/tests
cmake --build cmake-build-release
./cmake-build-release/core-tests --gtest_filter="*TRDOS_Format*"
```
