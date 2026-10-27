# DSK / EDSK — Amstrad CPC Disk Image

## Overview

Amstrad CPC format with sector data per track. Standard DSK has uniform geometry; Extended DSK (EDSK) supports variable sector sizes per track.

- **Extensions**: `.dsk`
- **Magic**: `MV - CPC` (standard) or `EXTENDED CPC DSK` (extended)
- **Loader**: `core/src/3rdparty/unreal-disk/src/loaders/loader_dsk.cpp` (`LoaderDSK`), tests
  `core/tests/loaders/disk/loader_dsk_test.cpp`

## Standard DSK Layout

```
0x00    34   Signature "MV - CPC..." or "MV - CPCEMU..."
0x22    16   Creator name
0x30    1    Cylinder count
0x31    1    Head count
0x32    2    Track size (uniform, includes 256-byte header)
0x34    ...  Track data

Track header (256 bytes):
  0x00  12   "Track-Info\r\n"
  0x10  1    Track number
  0x11  1    Side number
  0x14  1    Sector size code (uniform for all sectors in standard DSK)
  0x15  1    Sector count
  0x16  1    GAP#3 length
  0x17  1    Filler byte
  0x18  N×8  Sector info (C, H, R, N, ST1, ST2, size)
```

## Extended DSK Additions

- Signature: `EXTENDED CPC DSK`
- Per-track size table at offset 0x34 (high byte of track size in 256-byte units)
- Per-sector size in sector info (2 bytes at offset 6)
- Supports 0-length sectors (ID only)
- Sector data follow the track header back to back, in sector-info order; the track block is padded to a multiple of
  256 bytes

## Sector Status (ST1/ST2)

| Bit | ST1 Meaning | ST2 Meaning |
|-----|-------------|-------------|
| 0 | Missing address mark | Missing data mark |
| 1 | — | Bad cylinder |
| 2 | — | — |
| 5 | CRC error in ID | CRC error in data |
| 6 | — | Deleted data mark |

## Stored Length Other Than the Field

The "actual data length" of an EDSK sector is what the dumping program got from the uPD765, not always one data
field:

- **Several copies** (a multiple of 128 << N): a weak sector read more than once. The first copy is the data, the bytes
  that differ between copies become weak bits.
- **N >= 4, longer than the model's field**: a uPD765 read with a large N runs on over the gaps and fields after the
  sector (Speedlock +3 and the like). The data field keeps all the dumped bytes, so a read of 128 << N bytes returns
  them and then the raw track after them.
- **Overlapping long fields** (CPDRead protection tracks: 16 sectors with C = H = R = N = 0..15, stored 128 << N up to
  6144, N >= 7 stored as 0 bytes): every long dump holds the ID and data marks of the sectors after it. Where the next
  ID is found at the same distance in every dump, the loader lays the track out as on the disk (field length, GAP#3
  and sector distance from the dumps; Licence To Kill, Captain Blood, Driller: 128-byte fields, GAP#3 200, 390 bytes
  apart, 6240 of 6250 bytes) and fills each field from the dump that covers it, so every long read returns its dump.
  Where the dumps disagree, the longest fields are cut until the track fits `RawTrack::MAX_TRACK_SIZE`. Both cases
  leave a warning.

### CPDRead slots

CPDRead (v3.24, creator `CPDRead v3.24`) does not store the sector data back to back: a sector stored as 128 bytes
takes a 256-byte slot of the file, its data then 128 zero bytes, and every sector after it starts 128 bytes later. The
other stored lengths (512, 6144, 16, ...) are written without padding. The size of the track block is still the sum of
the stored lengths rounded up to 256, so the slots of the last sectors can fall past it. All such sectors in the 3096
collection images checked are sectors without a data field (ST1 / ST2 missing mark).

The loader reads the slots when the creator starts with `CPDRead`: a 128-byte sector advances 256 bytes, and a sector
without a data field whose slot starts past the block loads ID only. A sector with data that does not fit is still a
truncated (damaged) image. Read back to back, the long dumps of such a track seem to start 128 bytes before their
fields (Captain Blood: 518 bytes apart, longer than a revolution); read in slots, they lie as on the disk.

Evidence: over 1068 CPDRead images, the long fields of the 46 tracks with a 128-byte sector have a valid data CRC at
their start in 66 of 66 cases read in slots and in none read back to back; the padding of all 25 slots inside a block
is zero. SamDisk ([`dsk.cpp`](https://github.com/simonowen/samdisk/blob/main/src/types/dsk.cpp)) and the
[EDSK specification](https://simonowen.com/mirror/extdsk.html) describe data back to back and do not mention the
slots. `tools/verification/disk/dsk-tracks.py` reads images the same way ([README](../../../tools/verification/disk/README.md)).

## Mapping to Model

- Each track gets `sectorCount` sectors with specified C/H/R/N
- ST1/ST2 map to CRC validity and deleted mark
- Standard DSK: uniform sector size per track
- EDSK: variable sector sizes supported

## Save

Writes as EDSK to support variable geometries. Standard TR-DOS disks export cleanly; non-standard geometries preserved.
