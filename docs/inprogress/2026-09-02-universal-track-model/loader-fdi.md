# Loader: FDI (Full Disk Image, UKV Spectrum Debugger)

> Part of [2026-09-02-universal-track-model](README.md). Fixtures: `testdata/loaders/fdi/VORON1.FDI`, `VORON2.FDI`.
> **Status: implemented** (`core/src/loaders/disk/loader_fdi.{h,cpp}`, `core/tests/loaders/disk/loader_fdi_test.cpp`,
> wired into `Emulator::LoadDisk` and the Qt Save path).

## 0. Findings (verified against the fixtures)

* Flags: bit *n* set means "data field present and its CRC is correct for size code *n*" — the fixtures use 0x02 for
  256-byte, 0x04 for 512-byte and 0x08 for 1024-byte sectors, and exactly one sector with flags 0x00 (data present,
  CRC error). Bit 7 (no data field) and bit 6 (deleted) are honoured as designed.
* Both VORON images are 81 cylinders × 2 heads with mixed geometries per track: 16×256 (TR-DOS), 9×512 and 5×1024,
  plus a few empty tracks. Everything fits the nominal 6250-byte revolution with TR-DOS gaps (5×1024 = 5780 bytes).
* The description text sits between the track headers and the data area; it is preserved on save (and editable).
* Save policy implemented: sector lists in physical order, flags from `dataCrcValid/deleted/hasData`; a warning is
  issued when the image carries content FDI cannot express (FM tracks, non-nominal length, bad ID CRCs).

## 1. File layout (all integers little-endian)

```
Header (14 bytes + extension)
0x00  3   "FDI"
0x03  1   write-protect flag (0 = writable)
0x04  2   cylinders
0x06  2   heads
0x08  2   offset of description text (0 = none), from file start
0x0A  2   offset of sector data area, from file start
0x0C  2   extra header length E
0x0E  E   extra header (ignored)
then  track headers, one per (cylinder, head) in cylinder-major order

Track header
0x00  4   offset of this track's data, relative to the data area
0x04  2   reserved (0)
0x06  1   sector count S
0x07  7×S sector headers:
          +0  cylinder (C)      value stored in the ID field, may differ from physical
          +1  head (H)
          +2  sector number (R)
          +3  size code (N) 0..3 (⚠ values >3 appear in some tools; treat N&3 with a warning)
          +4  flags:
                bit 0..5  data field present and CRC correct for size 128<<bit
                          (i.e. bit N set ⇒ data CRC OK; bit N clear with data present ⇒ CRC error)
                bit 6     deleted data mark (F8)
                bit 7     ⚠ "no data field" in some descriptions / unused in others — treated as ID-only
          +5  2 data offset relative to the track data offset
Description text: NUL-terminated, at 0x08 offset (optional)
```

## 2. Mapping to the model

FDI has no gaps, no clock information and no track length. Each track is *rebuilt*:

1. `TrackFormatSpec spec; spec.sectorNumbers = R[]; spec.sectorSizeCodes = N[]; spec.sectorCylinders = C[];
   spec.sectorHeads = H[]; spec.trackLength = 6250;` gaps: `gapPreID 10 / sync 12 / gapPostID 22`, `gapPostData`
   computed so that `fits()` holds — shrink from 60 down to 16 (datasheet minimum gap3 for MFM is 16), then, if it
   still does not fit, enlarge `trackLength` up to `MAX_TRACK_SIZE` with a warning.
2. `track.formatTrack(cyl, head, spec)`; then for each sector copy `128 << N` bytes from the data offset into
   `getRawSector(i)->data` and `recalculateDataCRC()`.
3. Flags → model: bit 6 → `setDataAddressMark(0xF8)`; data CRC bit clear → after the copy, flip one CRC byte so
   `dataCrcValid == false` (the real CRC value is not stored in FDI; the corrupted-CRC convention is documented);
   bit 7 → sector formatted as ID-only (`spec.idOnly[i] = true`, the formatter emits no DAM/data for it).
4. Write-protect flag → `DiskImage::setWriteProtected(true)` (field exists in the modernization plan; until then
   `FDD::setWriteProtect`).

Sector count per track is whatever the file says (0 allowed → empty track with gaps only).

## 3. Save

`writeImage()` walks each track's index and emits one sector header per `Sector` (C/H/R/N from the ID field, flags
from `dataCrcValid/deleted/hasData`), then the data fields. Gaps, clock marks and CRC values are lost — that is
inherent to FDI and reported once via `lastWarnings()` only when the image has clock-mark-dependent content
(non-standard sync or a track length ≠ 6250).

## 4. Tests

All rows are implemented in `LoaderFDI_Test` (11 tests, all passing).

| Test | Assertion |
|------|-----------|
| `Detect_Signature` | "FDI" at 0 → `fdi` |
| `Load_Voron1_Geometry` | cylinders/heads from header; every track reindexes to the header's sector count; every sector's C/H/R/N equal the header |
| `Load_Voron_Data_Matches` | for sampled sectors the bytes equal the file bytes at the recorded offset |
| `Load_Flags` | synthetic FDI with CRC-bad, deleted and ID-only sectors → `dataCrcValid/deleted/hasData` |
| `Load_MixedSizes` | synthetic track with N = 0,1,2,3 → four `dataSize` values, `fits()` true |
| `Load_TooManySectors` | 30 × 512 → track length grows to MAX or loader warns and drops the tail |
| `Save_RoundTrip` | load → save → load: identical sector list and data |
| `Save_FromTrdos` | TRD image saved as FDI, loaded again, `LoaderTRD::validateTRDOSImage` passes |
| `Negative_Truncated`, `Negative_BadOffsets` | returns false, no allocation leaks (ASan) |
