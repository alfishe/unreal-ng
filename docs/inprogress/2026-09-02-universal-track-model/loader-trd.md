# Loader: TRD (TR-DOS raw sector dump) — migration to the universal track model

> Part of [2026-09-02-universal-track-model](README.md). Existing loader: `core/src/loaders/disk/loader_trd.{h,cpp}`.

## 1. Format

Raw dump of the logical sectors: cylinder-major, side, sector 1..16, 256 bytes each. No gaps, CRCs, clock marks or
interleave information. Sizes: 163 840 (SS40), 327 680 (DS40 / SS80), 655 360 (DS80); a file may be truncated after
the last used cylinder (`getTrackNoFromImageSize()` rounds up). Disk type is stored in the volume sector
(track 0, sector 9, offset 0xE3): 0x16 DS80, 0x17 DS40, 0x18 SS80, 0x19 SS40.

## 2. Mapping to the model

| TRD | Model |
|-----|-------|
| cylinders from file size, 2 sides always (`TRD_SIDES`) | `DiskImage(cylinders, 2)` |
| no low-level information | every track `formatTrack(cyl, side, TrackFormatSpec::trdos(interleave))`; interleave pattern index from `config.trdos_interleave` (unchanged) |
| 256-byte sector *n* | `getSector(n-1)->data` (lookup by ID number); `recalculateDataCRC()` after copy |
| — | clock marks set on every A1 by the formatter, so a later UDI save is honest |

`format()` today calls `track.reset()`, `applyInterleaveTable(pattern)`, then fixes C/H/R/N and ID CRC per sector.
After migration it calls `track.formatTrack(cylinder, side, TrackFormatSpec::trdos(pattern))` which does all of it,
then `populateEmptyVolumeInfo()` as before.

## 3. Write path

TRD can hold **only** 16 sectors of 256 bytes numbered 1..16 per track. `writeImage(path)` therefore first runs
`isTrdosGeometry(image)`: every track must have exactly the sector numbers 1..16, each with `dataSize == 256` and a
data field. If any track fails, `writeImage` returns **false**, records the reason in `lastWarnings()`
("track 12/0: 9 sectors × 512 bytes cannot be stored in TRD") and writes nothing. The caller
(`Emulator::SaveDisk` / UI / WebAPI) re-targets the save to UDI (see loader-registry.md §4). No truncation, padding
or silent data loss ever happens on the TRD path. For a plain TR-DOS image the output is byte-identical to the input
(test L1).

## 4. Validation (`validateTRDOSImage`)

Unchanged logic; it already goes through `getDataForSector()`. It gains a geometry precheck: track 0 side 0 must have
a sector numbered 9 with `dataSize == 256`, otherwise `INVALID_DISK_TYPE`.

## 5. Tests

* Existing `LoaderTRD_Test.*` (load, sector 9, validate, validate empty, format, track addressing) unchanged.
* L1 round trip on the four fixtures; L2 volume sector via WD1793 READ SECTOR.
* `Format_UsesConfiguredInterleave` — for pattern index 0/1/2 the stream order equals the table.
* `WriteImage_RefusesNonTrdosGeometry` — format one track 9×512, `writeImage` returns false, file not created,
  `lastWarnings()` names the track; the image stays dirty.
* `WriteImage_RefusesMissingSector` — remove sector 7 from a track: refused.
