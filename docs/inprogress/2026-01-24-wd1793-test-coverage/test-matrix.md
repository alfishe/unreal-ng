# WD1793 Test Matrix

Complete test matrix showing all required tests organized by feature area.

## Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Implemented and passing |
| 🚧 | In progress |
| ❌ | Not implemented |
| ⚠️ | Partially implemented |

---

## Type 1 Commands (Positioning)

### RESTORE ($00-$0F)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T1-001 | `Restore_BasicOperation` | ✅ | P2 | Via motor tests |
| T1-002 | `Restore_TracksTo0` | ⚠️ | P2 | Implicit |
| T1-003 | `Restore_VerifyFlag` | ❌ | P2 | |
| T1-004 | `Restore_StepRates` | ❌ | P3 | r0r1 variations |

### SEEK ($10-$1F)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T1-010 | `Seek_ToTrack0` | ❌ | P2 | |
| T1-011 | `Seek_ToTrack40` | ❌ | P2 | |
| T1-012 | `Seek_ToTrack79` | ❌ | P2 | |
| T1-013 | `Seek_WithVerify` | ❌ | P2 | |
| T1-014 | `Seek_StepRates` | ❌ | P3 | |

### STEP ($20-$3F)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T1-020 | `Step_DirectionIn` | ❌ | P2 | |
| T1-021 | `Step_DirectionOut` | ❌ | P2 | |
| T1-022 | `Step_UpdateTrackReg` | ❌ | P2 | U flag |
| T1-023 | `Step_NoUpdateTrackReg` | ❌ | P2 | |

### STEP IN ($40-$5F)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T1-030 | `StepIn_BasicOperation` | ❌ | P2 | |
| T1-031 | `StepIn_AtTrack79` | ❌ | P2 | Boundary |

### STEP OUT ($60-$7F)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T1-040 | `StepOut_BasicOperation` | ❌ | P2 | |
| T1-041 | `StepOut_AtTrack0` | ❌ | P2 | TR00 signal |

---

## Type 2 Commands (Read/Write Sector)

### READ SECTOR ($80-$9F)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T2-001 | `ReadSector_BasicOperation` | ⚠️ | P1 | Via FORMAT |
| T2-002 | `ReadSector_AllSectorNumbers` | ❌ | P1 | 1-16 |
| T2-003 | `ReadSector_VerifyDataIntegrity` | ❌ | P1 | |
| T2-004 | `ReadSector_DRQTiming` | ⚠️ | P1 | |
| T2-005 | `ReadSector_MultiSector` | ❌ | P3 | M flag |
| T2-006 | `ReadSector_SideCompare` | ❌ | P3 | C flag |
| T2-007 | `ReadSector_DeletedMark` | ❌ | P3 | F8 DAM |
| T2-008 | `ReadSector_128Bytes` | ❌ | P1 | N=0 |
| T2-009 | `ReadSector_512Bytes` | ❌ | P1 | N=2 |
| T2-010 | `ReadSector_1024Bytes` | ❌ | P1 | N=3 |

### WRITE SECTOR ($A0-$BF)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T2-020 | `WriteSector_BasicOperation` | ⚠️ | P1 | Via FORMAT |
| T2-021 | `WriteSector_AllSectorNumbers` | ❌ | P1 | |
| T2-022 | `WriteSector_ReadAfterWrite` | ❌ | P1 | |
| T2-023 | `WriteSector_DRQTiming` | ✅ | P1 | Fixed today! |
| T2-024 | `WriteSector_MultiSector` | ❌ | P3 | M flag |
| T2-025 | `WriteSector_WriteProtect` | ❌ | P2 | |
| T2-026 | `WriteSector_DeletedMark` | ❌ | P3 | a0 flag |

---

## Type 3 Commands (Track Operations)

### READ ADDRESS ($C0-$CF)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T3-001 | `ReadAddress_BasicOperation` | ❌ | P2 | |
| T3-002 | `ReadAddress_CRCData` | ❌ | P2 | |
| T3-003 | `ReadAddress_AllSectors` | ❌ | P2 | |

### READ TRACK ($E0-$EF)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T3-010 | `ReadTrack_FullTrack` | ❌ | P2 | |
| T3-011 | `ReadTrack_VerifyMFM` | ❌ | P2 | |

### WRITE TRACK ($F0-$FF)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T3-020 | `WriteTrack_BasicOperation` | ✅ | P1 | |
| T3-021 | `WriteTrack_MFMPatterns` | ✅ | P1 | mfm_parser_test |
| T3-022 | `WriteTrack_TRDOSFormat` | ✅ | P1 | Integration |

---

## Type 4 Commands (Control)

### FORCE INTERRUPT ($D0-$DF)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| T4-001 | `ForceInt_I0` | ❌ | P2 | Not-Ready→Ready |
| T4-002 | `ForceInt_I1` | ❌ | P2 | Ready→Not-Ready |
| T4-003 | `ForceInt_I2` | ❌ | P2 | Index pulse |
| T4-004 | `ForceInt_I3` | ❌ | P2 | Immediate |
| T4-005 | `ForceInt_D0` | ⚠️ | P2 | Used in FORMAT |

---

## Error Conditions

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| E-001 | `Error_CRCMismatch_IDAM` | ❌ | P1 | |
| E-002 | `Error_CRCMismatch_Data` | ❌ | P1 | |
| E-003 | `Error_RecordNotFound` | ❌ | P1 | |
| E-004 | `Error_WriteProtect` | ❌ | P2 | |
| E-005 | `Error_LostData_Read` | ❌ | P1 | |
| E-006 | `Error_LostData_Write` | ✅ | P1 | Fixed today! |
| E-007 | `Error_NotReady` | ❌ | P2 | |

---

## Status Register

| Test ID | Test Name | Bit | Status | Priority |
|---------|-----------|-----|--------|----------|
| S-001 | `Status_Busy_Set` | S0 | ⚠️ | P2 |
| S-002 | `Status_Busy_Clear` | S0 | ⚠️ | P2 |
| S-003 | `Status_DRQ_ReadSector` | S1 | ⚠️ | P2 |
| S-004 | `Status_DRQ_WriteSector` | S1 | ⚠️ | P2 |
| S-005 | `Status_LostData` | S2 | ⚠️ | P2 |
| S-006 | `Status_CRCError` | S3 | ❌ | P2 |
| S-007 | `Status_RecordNotFound` | S4 | ❌ | P2 |
| S-008 | `Status_SpinUp` | S5 | ❌ | P3 |
| S-009 | `Status_WriteProtect` | S6 | ❌ | P2 |
| S-010 | `Status_NotReady` | S7 | ❌ | P2 |

---

## Non-Standard Layouts (Copy Protection)

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| N-001 | `NonStandard_9SectorsPerTrack` | ❌ | P1 | IBM PC |
| N-002 | `NonStandard_18SectorsPerTrack` | ❌ | P1 | HD |
| N-003 | `NonStandard_MissingSector` | ❌ | P1 | |
| N-004 | `NonStandard_DuplicateSector` | ❌ | P1 | |
| N-005 | `NonStandard_BadCRCSector` | ❌ | P1 | |
| N-006 | `NonStandard_40vs80Track` | ❌ | P2 | |
| N-007 | `NonStandard_WeakBits` | ❌ | P3 | Speedlock |

---

## Sector Sizes

| Test ID | Test Name | Size | Status | Priority |
|---------|-----------|------|--------|----------|
| SZ-001 | `SectorSize_128Bytes` | N=0 | ❌ | P1 |
| SZ-002 | `SectorSize_256Bytes` | N=1 | ✅ | P1 |
| SZ-003 | `SectorSize_512Bytes` | N=2 | ❌ | P1 |
| SZ-004 | `SectorSize_1024Bytes` | N=3 | ❌ | P1 |
| SZ-005 | `SectorSize_Mixed` | Mix | ❌ | P2 |

---

## Timing Tests

| Test ID | Test Name | Status | Priority | Notes |
|---------|-----------|--------|----------|-------|
| TM-001 | `Timing_IndexPulse` | ⚠️ | P3 | 200ms |
| TM-002 | `Timing_StepRate6` | ❌ | P3 | |
| TM-003 | `Timing_StepRate12` | ❌ | P3 | |
| TM-004 | `Timing_StepRate20` | ❌ | P3 | |
| TM-005 | `Timing_StepRate30` | ❌ | P3 | |
| TM-006 | `Timing_HeadLoad` | ❌ | P3 | |
| TM-007 | `Timing_ByteTime` | ❌ | P3 | 32µs |

---

## Sleep Mode (Complete)

| Test ID | Test Name | Status | Priority |
|---------|-----------|--------|----------|
| SL-001 | `SleepMode_StartsInSleepMode` | ✅ | P2 |
| SL-002 | `SleepMode_HandleStepSkipsWhenSleeping` | ✅ | P2 |
| SL-003 | `SleepMode_WakeUp` | ✅ | P2 |
| SL-004 | `SleepMode_WakeUpIdempotent` | ✅ | P2 |
| SL-005 | `SleepMode_EnterSleepMode` | ✅ | P2 |
| SL-006 | `SleepMode_AutoSleepAfterIdleTimeout` | ✅ | P2 |
| SL-007 | `SleepMode_NoAutoSleepWhileMotorRunning` | ✅ | P2 |
| SL-008 | `SleepMode_NoAutoSleepWhileFSMActive` | ✅ | P2 |
| SL-009 | `SleepMode_PortAccessWakesUp` | ✅ | P2 |
| SL-010 | `SleepMode_CompleteLifecycle` | ✅ | P2 |

---

## Summary

| Category | Total | ✅ | ⚠️ | ❌ |
|----------|-------|----|----|-----|
| Type 1 Commands | 16 | 1 | 1 | 14 |
| Type 2 Commands | 17 | 1 | 3 | 13 |
| Type 3 Commands | 8 | 3 | 0 | 5 |
| Type 4 Commands | 5 | 0 | 1 | 4 |
| Error Conditions | 7 | 1 | 0 | 6 |
| Status Register | 10 | 0 | 4 | 6 |
| Non-Standard | 7 | 0 | 0 | 7 |
| Sector Sizes | 5 | 1 | 0 | 4 |
| Timing | 7 | 0 | 1 | 6 |
| Sleep Mode | 10 | 10 | 0 | 0 |
| **TOTAL** | **92** | **17** | **10** | **65** |

**Coverage: 29% complete** (17 + partial 10 = ~24%)
