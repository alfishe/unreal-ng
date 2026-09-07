# Test plan — universal track model

> Part of [2026-09-02-universal-track-model](README.md). All tests are GoogleTest, picked up automatically by the
> `file(GLOB_RECURSE)` in `core/tests/CMakeLists.txt`; test data goes under `testdata/loaders/<format>/`.

## 1. Unit tests — `core/tests/emulator/io/fdc/diskimage_test.cpp` (rewritten)

| # | Test | What it proves |
|---|------|----------------|
| U1 | `AddressMarkRecord_Size_And_CRC` | 7-byte packed overlay, `recalculateCRC()/isCRCValid()` (kept from old suite) |
| U2 | `TrdosFormat_IsByteIdenticalToLegacyLayout` | `formatTrack(72,0)` yields 6250 bytes; offsets of gap/sync/A1/FE/gap/sync/A1/FB/data/CRC/gap per sector at stride 388; end gap 42×4E; every A1 has a clock mark, nothing else does |
| U3 | `TrdosFormat_SectorIndex` | 16 sectors, numbers 1..16 in the given order, `getRawSector(i)->id->sector == order[i]`, `getSector(n-1)` finds number n, all CRCs valid |
| U4 | `Interleave_Patterns` | for the three TR-DOS patterns `getSector(logical)` == `getRawSector(physical)` per the table (replaces `SectorAccessConsistency`) |
| U5 | `Format_9x512_Plus3` | `TrackFormatSpec::plus3()`: 9 sectors of 512, numbers 1..9, `fits()`, reindex finds 9, data sizes 512 |
| U6 | `Format_10x512_PlusD`, `Format_5x1024`, `Format_26x128` | other legal WD1793 geometries fit in 6250 and reindex correctly |
| U7 | `Format_MixedSectorSizes` | `sectorSizeCodes = {1,2,0,3}`: each `Sector::dataSize` follows its own N |
| U8 | `Format_DuplicateSectorNumbers` | two sectors numbered 5: `sectorCount()==16`, `findSector(5)` returns the first, `findSector(5, afterFirst)` the second |
| U9 | `Format_MissingSectorNumbers` | order `{1,2,3,5,...}` (no 4): `getSector(3) == nullptr`, no crash, others intact |
| U10 | `Format_CustomCylHead` | `sectorCylinders/sectorHeads` overrides land in the ID fields (copy-protection style) |
| U11 | `Reindex_IdOnlySector_NoDataField` | hand-built stream with FE record and no DAM within 43 bytes: `hasData == false`, `data == nullptr`, `getDataForSector` returns nullptr |
| U12 | `Reindex_DeletedDataMark` | DAM F8 → `deleted == true`; F9/FA/FB → false |
| U13 | `Reindex_BadIdCrc_And_BadDataCrc_ArePreserved` | corrupt bytes stay corrupt after reindex; `idCrcValid/dataCrcValid` false; `recalculateDataCRC()` fixes only the data CRC |
| U14 | `Reindex_UsesClockMarks` | `A1 A1 A1 FE` written *inside* a data field without clock marks is not a sector; the same bytes with clock marks are |
| U15 | `Reindex_FallbackWithoutClockBitmap` | a stream with no clock bits at all (legacy buffer) still finds sectors by byte pattern and skips data fields |
| U16 | `Reindex_DataCrossingIndex` | data field that would run past `rawSize()`: `hasData == false` |
| U17 | `TrackLength_Variable` | `resizeRaw(6208)`, `resizeRaw(6464)`, `resizeRaw(3125, FM)`: bitmaps resize, `rawSize()` reported, `MIN/MAX` clamped |
| U18 | `TrackLength_FmNominal` | `TrackFormatSpec{encoding=FM, trackLength=3125, gapFill=0xFF}` formats and reindexes (byte-level) |
| U19 | `ClockBitmap_RoundTrip` | `setClockMark/clockMark`, `setClockBitmap` copy, bit i ↔ byte i |
| U20 | `WeakBits_Optional` | empty by default, `setWeakByte` allocates lazily |
| U21 | `WriteSectorData_ClampsToSectorSize_RecomputesCrc_MarksDirty` | 512-byte sector accepts 512, rejects beyond, CRC valid after write, `isSectorDirty` |
| U22 | `DirtyTracking_*` (5 tests kept from the old suite) | unchanged semantics: initial clean, write marks sector+track+image, no change → no dirty, `markClean`, multiple sectors |
| U23 | `DiskImage_TrackAddressing` | `getTrack`, `getTrackForCylinderAndSide`, bounds (replaces `TrackPositioning`; the "uniform spacing" assertion is dropped since it only held for POD tracks) |
| U24 | `Track_Move_KeepsSectorPointersValid` | move a `Track` into a vector; `Sector::data` still points into the moved buffer |
| U25 | `Sector_Cap_255` | stream with 300 fake ID fields indexes at most 255 |

## 2. Unit tests — `mfm_parser_test.cpp` (new)

| # | Test |
|---|------|
| M1 | `parseTrack` on a `trdos()` track: 16 found, 16 valid, offsets equal `Sector::idamOffset/dataOffset` |
| M2 | `parseTrack(size=3125)` and `(size=6464)` do not read past the buffer (ASan-clean) |
| M3 | `parseTrack` on a 9×512 track reports 9 sectors with 512-byte CRC coverage |
| M4 | `MFMValidator::validate(expectedSectors=16)` keeps the TR-DOS interleave diagnostics |

## 3. WD1793 unit tests — additions to `wd1793_test.cpp` / `wd1793_write_commands_test.cpp`

| # | Test |
|---|------|
| W1 | `ReadSector_512Bytes` — track formatted `plus3()`, READ SECTOR 3 returns 512 bytes, then BUSY drops |
| W2 | `ReadSector_1024Bytes`, `ReadSector_128Bytes` |
| W3 | `ReadSector_IdOnly_ReturnsRNF` |
| W4 | `ReadSector_DeletedMark_SetsBit5` (kept), `ReadSector_BadDataCrc_SetsCrcErr` (kept) |
| W5 | `ReadSector_Multi_StopsAtFirstMissingNumber` — track 1..9: m-flag read from 7 reads 7,8,9 then RNF |
| W6 | `WriteSector_RecomputesDataCrc` — write 256 bytes, read back, no CRC error, `Sector::dirty` |
| W7 | `WriteSector_DeletedFlag_WritesF8` (command bit a0) |
| W8 | `ReadTrack_VariableLength` — `resizeRaw(6300)`: READ TRACK yields 6300 bytes |
| W9 | `ReadTrack_Fm3125` — FM track yields 3125 bytes |
| W10 | `WriteTrack_SetsClockMarks` — after formatting with F5/F6, every A1/C2 has a clock mark and gap bytes do not |
| W11 | `WriteTrack_9x512_Then_ReadSector` — CPU-side format routine writes 9×512; READ SECTOR 5 returns the written 512 bytes |
| W12 | `WriteTrack_StopsAtTrackLength` — 6208-byte track: exactly 6208 bytes accepted |
| W13 | `WriteTrack_MarksRawTrackDirty` |
| W14 | `reindex` tests (8 existing `reindexFromIDAM` tests) rewritten against the stream: sequential, TR-DOS 1:2, reverse, invalid number 0, number 17, duplicates, partial |

## 4. Loader regression tests (existing suites, unchanged expectations)

`LoaderTRD_Test.*`, `LoaderSCL_Test.*` plus new:

| # | Test |
|---|------|
| L1 | `LoaderTRD_RoundTrip_ByteIdentical` — load each `testdata/loaders/trd/*.trd`, `writeImage(tmp)`, compare bytes |
| L2 | `LoaderTRD_Load_Then_WD1793_ReadSector9_Track0` — volume sector through the controller |
| L3 | `LoaderSCL_RoundTrip_FilesPreserved` — load `.scl`, write `.scl`, reload, compare catalog + data |
| L4 | `LoaderSCL_To_TRD_To_SCL` |

## 5. Integration tests

| # | Test |
|---|------|
| I1 | `TRDOSIntegration_test` (existing) — boot + catalog on real ROM stays green |
| I2 | `FormatViaRom_MatchesTrackFormatSpec` — run TR-DOS `FORMAT` through the emulated CPU on a blank image, compare the resulting 6250 bytes and clock bitmap with `TrackFormatSpec::trdos(pattern)` (extends `2026-01-23-trdos-format-integration-test`) |
| I3 | `TTD_WD1793_*` / `TTD_FDD_*` (existing) — serializer sizes unchanged |
| I4 | `WebApi_DiskTrack_RawSize` — `/disk/track` reports `raw_size == rawSize()` and `sectors == sectorCount()` for a 9×512 track |

## 6. Per-format tests (designed in each loader document)

Each loader document ends with its own table: detection, load of fixture(s), model mapping assertions, save
round-trip, and negative cases (truncated file, bad CRC, unsupported variant). Fixtures already present:
`testdata/loaders/fdi/VORON1.FDI`, `VORON2.FDI`, `testdata/loaders/udi/Zvezdnoe Nasledie.udi`, `beta128-empty.udi`.

## 7. Tooling

`core/tests/emulator/io/fdc/tools/*.py` (track generator / validator) get a `--sectors`, `--size-code`,
`--track-length`, `--clock-bitmap` option set so hand-made fixtures for U11–U16 can be regenerated.
