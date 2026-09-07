# Loader: SCL (SINCLAIR file container) — migration to the universal track model

> Part of [2026-09-02-universal-track-model](README.md). Existing loader: `core/src/loaders/disk/loader_scl.{h,cpp}`.

## 1. Format

```
0   8   "SINCLAIR"
8   1   file count N
9   14×N  catalog entries: name[8] type[1] start[2] length[2] sectors[1]   (no track/sector position)
…   data of all files back to back, sectors × 256 bytes each, in catalog order
end 4   checksum = 32-bit sum of all preceding bytes (little-endian)
```

## 2. Mapping to the model

`loadImage()` creates a DS80 image, formats it exactly like `LoaderTRD::format()` (same interleave configuration),
then `addFile()` places each file at the first free TR-DOS sector, updating catalog sectors 1..8 and the volume
sector 9. All accesses are by sector *number* (`getSector(n-1)`, `getRawSector(n)` → both become `getSector(n-1)`
since after formatting the physical order is the interleave order, not 1..16; the current code uses `getRawSector`
for catalog sectors 1..8 which only works because the current `reset()` leaves a 1:1 mapping — the migration makes
this explicit and correct for any interleave).

No 16×256 assumption remains in the loader itself: it only needs `dataSize == 256` for the sectors it touches
and fails with a clear message otherwise.

## 3. Write path (decided by the owner: catalog analysis, non-deleted files only)

`writeImage(path)` is a **TR-DOS catalog export**, not a track dump:

1. Geometry precheck: track 0 side 0 must carry sectors 1..9 of 256 bytes (catalog + volume sector); otherwise
   return false with a warning. Other tracks are only required where a file points to them.
2. Read the volume sector (track 0, sector 9): TR-DOS signature 0x10, `fileCount` (includes deleted entries),
   `deletedFileCount`.
3. Walk catalog entries 0..`fileCount-1` (sectors 1..8, 16 entries × 16 bytes each):
   * `name[0] == 0x00` — end of catalog, stop;
   * `name[0] == 0x01` — deleted file, **skip**;
   * otherwise export: 14-byte header (name, type, start, length, sectors) and the file body by walking
     `StartTrack/StartSector` forward `SizeInSectors` sectors (TR-DOS logical order: sector 0..15 of track
     *n* = ID numbers 1..16, then track *n+1*), reading `getSector(sectorNo)->data`.
   * A file whose chain points outside the disk or onto a track without the needed 256-byte sector is skipped with a
     warning (the image is damaged; do not abort the whole export).
4. Header `"SINCLAIR"` + exported count (not `fileCount`), entries, bodies, 32-bit sum checksum.
5. Sectors not owned by any exported file (free space, deleted file bodies, boot-sector leftovers, non-TR-DOS
   tracks) are **not** stored — this is inherent to SCL and is not reported as lossy.

Compared with the current code, the only behavioural change is that deleted entries are no longer exported and the
count written to the header is the number of exported files.

## 4. Tests

* Existing `LoaderSCL_Test.load` / `addFile` unchanged.
* L3 SCL → disk → SCL preserves catalog + data (`eyeache2.scl`, `insult.scl`); L4 SCL → TRD → SCL.
* `WriteImage_SkipsDeletedFiles` — mark one entry deleted (0x01) on a loaded disk, export, reload: file absent,
  header count decremented, other files intact.
* `WriteImage_StopsAtCatalogEnd` — `fileCount` larger than real entries (0x00 terminator early): export stops there.
* `WriteImage_RefusesNonTrdosTrack0` — track 0 formatted 9×512: returns false.
* `AddFile_UsesInterleavedTrack` — with the 1:2 pattern configured, the file lands in the right *numbered* sectors
  (read back through `getSector(n-1)` and through WD1793 READ SECTOR).
