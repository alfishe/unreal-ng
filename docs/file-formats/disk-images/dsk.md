# DSK / EDSK — Amstrad CPC Disk Image

## Overview

Amstrad CPC format with sector data per track. Standard DSK has uniform geometry; Extended DSK (EDSK) supports variable sector sizes per track.

- **Extensions**: `.dsk`
- **Magic**: `MV - CPC` (standard) or `EXTENDED CPC DSK` (extended)
- **Loader**: `core/src/loaders/disk/loader_dsk.cpp`

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

## Sector Status (ST1/ST2)

| Bit | ST1 Meaning | ST2 Meaning |
|-----|-------------|-------------|
| 0 | Missing address mark | Missing data mark |
| 1 | — | Bad cylinder |
| 2 | — | — |
| 5 | CRC error in ID | CRC error in data |
| 6 | — | Deleted data mark |

## Mapping to Model

- Each track gets `sectorCount` sectors with specified C/H/R/N
- ST1/ST2 map to CRC validity and deleted mark
- Standard DSK: uniform sector size per track
- EDSK: variable sector sizes supported

## Save

Writes as EDSK to support variable geometries. Standard TR-DOS disks export cleanly; non-standard geometries preserved.
