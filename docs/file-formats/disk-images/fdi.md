# FDI — Floppy Disk Image

## Overview

Sector-based format with per-sector flags for CRC errors, deleted marks, and variable sizes.

- **Extensions**: `.fdi`
- **Magic**: `FDI` at offset 0
- **Loader**: `core/src/loaders/disk/loader_fdi.cpp`

## File Layout (little-endian)

```
0x00  3   "FDI"
0x03  1   Write-protect flag
0x04  2   Cylinder count
0x06  2   Head count
0x08  2   Offset to track descriptions
0x0A  2   Data offset
0x0C  2   Extended header length
0x0E  X   Extended header (ignored)

Track descriptions (for each cyl, head):
  4     Track offset from data area
  2     Reserved
  1     Sector count (N)
  N×7   Sector headers

Sector header (7 bytes):
  1     Cylinder (C)
  1     Head (H)
  1     Sector number (R)
  1     Size code (N): 0=128, 1=256, 2=512, 3=1024
  1     Flags: bit 0=CRC error, bit 1=deleted DAM, bit 6=no data
  2     Data offset within track

Data area:
  Sector data concatenated per track
```

## Sector Flags

| Bit | Meaning |
|-----|---------|
| 0 | CRC error |
| 1 | Deleted data mark |
| 6 | No data field (ID only) |

## Mapping to Model

- Per-track sector list with arbitrary C/H/R/N
- Flags map to `Sector.isDataCRCValid()`, `Sector.deleted`, `Sector.hasData`
- Track formatted with discovered sector geometry
- CRCs and clock marks regenerated

## Save

Exports sector data with flags. Variable sector sizes and deleted marks preserved.
