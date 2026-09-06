# Loader: UDI (Ultra Disk Image v1.0)

> Part of [2026-09-02-universal-track-model](README.md). Fixtures: `testdata/loaders/udi/Zvezdnoe Nasledie.udi`,
> `beta128-empty.udi`. **Status: implemented** (`core/src/loaders/disk/loader_udi.{h,cpp}`,
> `core/tests/loaders/loader_udi_test.cpp`, wired into `Emulator::LoadDisk` and the Qt Save / Save as .udi menu).
> UDI is the lossless native format for the model.

## 0. Findings while implementing (verified against the fixtures)

* The CRC is CRC-32 with a signed `int` accumulator (`crc >>= 1` is an arithmetic shift), initial value -1, fed with
  the whole file except the last 4 bytes. `CRCHelper::crcUDI` in `fdc.h` already had this variant; the standard
  zlib CRC-32 does **not** match. Reference: `beta128-empty.udi` ends with `EA 4E 96 EF`.
* Clock bitmap bit order: bit `i & 7` of byte `i >> 3` (LSB first) marks byte `i`.
* Header byte 0x0B ("unused") is 1 in files written by TRX2X; it is preserved on save.
* TRX2X appends an ASCIIZ comment (`\r\nCreated by TRX2X converter\r\n(C)2002 Alex Makeev ...`) between the last
  track and the CRC; the loader keeps it as an opaque trailer and writes it back, so the round trip is byte-exact.
* `Zvezdnoe Nasledie.udi` is a real-drive dump with 6400-byte tracks (fast drive) and an 80-byte pre-index gap;
  `beta128-empty.udi` is an 86-cylinder blank TR-DOS disk in the legacy 6250-byte layout.

## 1. File layout (little-endian)

```
0x00  4   "UDI!"  ("udi!" = compressed variant, not supported → clear error)
0x04  4   file size without the trailing CRC (i.e. offset of the CRC)
0x08  1   version (0)
0x09  1   max cylinder (cylinders - 1)
0x0A  1   max head (0 or 1)
0x0B  1   unused
0x0C  4   extended header size X
0x10  X   extended header (ignored, preserved on save)
then  for cyl in 0..maxCyl: for head in 0..maxHead:
        1   track type: 0 = MFM, 1 = FM, 2 = mixed (MFM+FM bitmap follows), 0x80 | n = ⚠ multi-revolution variants (unsupported → warn, first copy used)
        2   TLEN  (typically 6208..6464 for MFM, ~3100..3200 for FM)
        TLEN      raw track bytes, offset 0 = index pulse
        ⌈TLEN/8⌉  clock bitmap, bit i (LSB first) = byte i was written with missing clock (A1 / C2 sync marks)
        [⌈TLEN/8⌉ FM/MFM selection bitmap, only for type 2]
last  4   CRC-32 of everything before it (polynomial 0xEDB88320, the "crcUDI" variant already present in fdc.h)
```

## 2. Mapping to the model — direct

| UDI | Model |
|-----|-------|
| TLEN | `track.resizeRaw(TLEN, encoding)` |
| raw bytes | `track.setRaw(ptr, TLEN, encoding)` |
| clock bitmap | `track.setClockBitmap(ptr, ⌈TLEN/8⌉)` |
| type 0/1 | `Encoding::MFM` / `Encoding::FM` |
| type 2 | `Encoding::MFM` with the selection bitmap stored in a track "aux" bitmap (kept for save), warning logged |
| — | `track.reindex()` builds the sector list from the stream using the clock bitmap |

Nothing is synthesised; a track with no sectors, a 9×512 track, a 6464-byte track, deliberate CRC errors and
duplicate IDs all survive untouched. `beta128-empty.udi` must load into an image on which
`LoaderTRD::validateEmptyTRDOSImage` passes (proof that reindex + TR-DOS geometry agree).

## 3. Save

For each track write type (`encoding`), TLEN = `rawSize()`, `rawData()`, `clockBitmap()`. Extended header from
the loaded file is preserved (empty for images that started as TRD/SCL). CRC appended. Round trip is byte-exact when
the source was a supported UDI (type 0/1, no multi-revolution).

Because WD1793 WRITE TRACK now records clock marks (see [wd1793-integration.md](wd1793-integration.md)), a disk
formatted inside the emulator and saved as UDI is indistinguishable from one imaged from real hardware.

## 4. Tests

All rows below are implemented in `LoaderUDI_Test` (13 tests, all passing).

| Test | Assertion |
|------|-----------|
| `Detect_Signature`, `Reject_Compressed_And_BadVersion` | "UDI!" ok, "udi!" → false + message, version ≠ 0 refused, truncated header refused |
| `Crc_Verified` | flip one byte → load fails with CRC error; option `ignoreCrc` loads with warning |
| `Load_Beta128Empty_IsValidTrdos` | 160 tracks; `validateEmptyTRDOSImage` passes; `rawSize()` per track equals TLEN |
| `Load_ZvezdnoeNasledie` | all tracks reindex; sector counts/size codes stable; at least one track with TLEN ≠ 6250 (proves variable length path) |
| `ClockBitmap_DrivesIndex` | clear the bitmap of one loaded track → `reindex()` finds 0 sectors in strict mode (fallback disabled) |
| `Save_RoundTrip_ByteExact` | load → save → compare files (after CRC) |
| `Save_FromTrd` | TRD → UDI → TRD equals original TRD |
| `Save_AfterWriteTrack_ReadBackThroughController` | format 9×512 on a TRD-born image (TRD refuses it), save UDI, reload, READ SECTOR 5 through the WD1793 returns the 512 bytes |
| `Save_PreservesNonTrdosContent` | +3, FM, 6464-byte, duplicate/ID-only and bad-CRC/deleted tracks survive serialize → parse byte for byte |
| `Fm_Track_Type1` | FM track is written as type 1 / 3125 bytes and loads with `Encoding::FM` |
| `Header_ExtendedHeader_And_Trailer_Preserved` | extended header, reserved byte and trailer round trip; size field == file size − 4 |
| `Load_Truncated_And_MultiRevolution_Rejected` | truncated track data and multi-revolution flag refused; mixed FM/MFM (type 2) loads as MFM with a warning |
| `Load_ZvezdnoeNasledie_RealDriveDump` | 160 tracks of 6400 bytes, TR-DOS system track found, TRX2X trailer kept |
