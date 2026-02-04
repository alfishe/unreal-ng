# Unit Test Plan: WRITE_SECTOR, WRITE_TRACK & Sector Reindexing

## Scope

Unit tests for:
- `WD1793::cmdWriteSector()` / [processWriteSector()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/wd1793.cpp#2075-2114) / [processWriteByte()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/wd1793.cpp#2115-2184)
- `WD1793::cmdWriteTrack()` / [processWriteTrack()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/wd1793.cpp#2192-2362) (F5, F6, F7 handling)
- `RawTrack::reindexFromIDAM()`

---

## 1. [reindexFromIDAM()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/diskimage.h#469-496) Tests

### Positive Cases

| Test | Description | Expected |
|------|-------------|----------|
| **Sequential_Sectors** | Sectors 1-16 at positions 0-15 | `sectorsOrderedRef[0-15]` point to `sectors[0-15]` |
| **TR_DOS_Interleave** | 1:2 pattern: 1,9,2,10,3,11,4,12,5,13,6,14,7,15,8,16 | `sectorsOrderedRef[8]` → `&sectors[1]` (sector 9) |
| **Reverse_Order** | Sectors 16,15,...,1 at positions 0-15 | `sectorsOrderedRef[15]` → `&sectors[0]` |
| **Custom_Interleave** | 1:3 pattern testing | Correct mapping for any interleave |

### Negative Cases

| Test | Description | Expected |
|------|-------------|----------|
| **Invalid_SectorNo_Zero** | Sector number = 0 in IDAM | Reference stays `nullptr` |
| **Invalid_SectorNo_17** | Sector number = 17 in IDAM | Reference stays `nullptr` |
| **Duplicate_SectorNo** | Two sectors claim same number | Last one wins (or first - document behavior) |
| **All_Invalid** | All 16 IDAMs have invalid sector numbers | All `sectorsOrderedRef[]` = `nullptr` |
| **Partial_Valid** | Only 8 valid sectors (1-8), rest invalid | First 8 mapped, rest `nullptr` |

---

## 2. [processWriteTrack()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/wd1793.cpp#2192-2362) Tests

### Positive Cases - MFM Control Bytes

| Test | Description | Expected |
|------|-------------|----------|
| **F5_WritesA1** | Write 0xF5 to data register | 0xA1 written to buffer, CRC preset to 0xCDB4 |
| **F6_WritesC2** | Write 0xF6 to data register | 0xC2 written to buffer |
| **F7_GeneratesCRC** | Write F5,FE,00,00,01,01,F7 | 2 valid CRC bytes appended |
| **F7_CRC_Matches_Helper** | CRC from F7 vs `CRCHelper::crcWD1793()` | Identical values |
| **Full_Sector_Format** | Complete sector: gap,sync,A1×3,FE,IDAM,CRC,gap,sync,A1×3,FB,data,CRC,gap | Valid sector structure |
| **Full_Track_16_Sectors** | 16 sectors with proper gaps | 6250 bytes written, 16 valid sectors |

### Positive Cases - Timing & Completion

| Test | Description | Expected |
|------|-------------|----------|
| **Completes_At_6250** | Write exactly 6250 bytes | Transitions to S_END_COMMAND |
| **ReindexFromIDAM_Called** | After completion | `sectorsOrderedRef[]` updated |
| **INTRQ_Raised** | After completion | INTRQ asserted |
| **Status_Cleared** | After completion | BUSY=0, no error bits |

### Negative Cases

| Test | Description | Expected |
|------|-------------|----------|
| **No_Disk** | WRITE_TRACK with no disk | WDS_NOTRDY set, command aborts |
| **Write_Protected** | Disk is write-protected | WDS_WRITEPROTECTED set |
| **DRQ_Timeout** | CPU doesn't provide next byte in time | WDS_LOSTDATA set after ~32µs |
| **Invalid_F7_Position** | F7 before any F5 (no CRC start) | CRC from position 0 |
| **Buffer_Overflow_Protected** | Try to write >6250 bytes | Stops at 6250, no memory corruption |
| **Track_Size_Exact** | Write exactly 6250 bytes | Completes at index 6250, no overrun |
| **Fault_Info_Detailed** | DRQ timeout occurs | Status has LOSTDATA, byte position logged |

---

## 3. [processWriteSector()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/wd1793.cpp#2075-2114) / [processWriteByte()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/wd1793.cpp#2115-2184) Tests

### Positive Cases

| Test | Description | Expected |
|------|-------------|----------|
| **Write_256_Bytes** | Single sector write (TR-DOS size) | 256 bytes to sector data |
| **Write_512_Bytes** | PC-style sector | 512 bytes written |
| **First_Byte_Timing** | DRQ raised, delayed transition | CPU has time to respond |
| **All_Bytes_Written** | Write full sector | All bytes from DR to buffer |
| **Sector_CRC_Updated** | After write completes | Sector's `data_crc` recalculated |
| **Multi_Sector** | CMD_MULTIPLE bit set | Writes sequential sectors |
| **Stops_At_SectorSize** | Provide 300 bytes to 256-byte sector | Only 256 bytes written, command ends |

### Negative/Boundary Cases

| Test | Description | Expected |
|------|-------------|----------|
| **No_Disk** | Write with no disk | WDS_NOTRDY set |
| **Sector_Not_Found** | Invalid sector number | WDS_NOTFOUND set |
| **Write_Protected** | Disk is protected | WDS_WRITEPROTECTED set |
| **DRQ_Not_Serviced** | CPU doesn't write byte in time | WDS_LOSTDATA, command terminates |
| **Track_0_Side_1** | Edge case: side selection | Writes to correct side |
| **Overflow_Prevented** | CPU keeps writing after sector full | No buffer overflow, DRQ cleared |
| **ByteCount_Exact** | Write exactly sector size bytes | Command completes, no error |
| **Buffer_Alignment** | Write to sector 5 | Data starts exactly at `sectors[4].data[0]` |
| **No_Buffer_Shift** | Write all 16 sectors | Each sector's data at correct struct offset |

---

## 4. CRC Validation Tests

| Test | Description | Expected |
|------|-------------|----------|
| **IDAM_CRC_Valid** | After F7 following FE+IDAM | [MFMParser](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/mfm_parser.h#126-253) validates CRC |
| **Data_CRC_Valid** | After F7 following FB+256 bytes | [MFMParser](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/mfm_parser.h#126-253) validates CRC |
| **CRC_ByteOrder** | Check HIGH-LOW byte order | Matches [crcWD1793()](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/fdc.h#195-236) output |
| **CRC_Start_After_A1** | F5 presets CRC, includes all after | CRC covers FE through sector_size |

---

## 5. Integration Scenarios

| Test | Description | Expected |
|------|-------------|----------|
| **Format_Then_Read** | WRITE_TRACK all sectors, READ_SECTOR each | All 16 sectors readable |
| **Format_Interleaved_Read** | Format with 1:2, read sector 9 | Data matches what was written |
| **Write_Sector_Verify** | WRITE_SECTOR then READ_SECTOR same | Data identical |
| **Multiple_Tracks** | Format tracks 0,1,2 | Each track independent |

---

## Test Implementation Notes

### Mock/Stub Requirements
- [FDD](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/fdd.cpp#11-44) with configurable disk insertion, write-protect
- [DiskImage](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/emulator/io/fdc/diskimage.h#644-645) with raw track access
- Clock/timing simulation for DRQ timeout tests

### Assertions to Use
```cpp
// Sector mapping
EXPECT_EQ(track->sectorsOrderedRef[8], &track->sectors[1]);  // Sector 9 at phys 1

// CRC validation
auto result = MFMValidator::validate(rawData, 6250);
EXPECT_TRUE(result.passed);
EXPECT_EQ(result.parseResult.validSectors, 16);

// Status register
EXPECT_EQ(wd1793->getStatusRegister() & WDS_LOSTDATA, 0);
EXPECT_EQ(wd1793->getStatusRegister() & WDS_BUSY, 0);
```

### File Location
`core/tests/emulator/io/fdc/wd1793_write_commands_test.cpp`
