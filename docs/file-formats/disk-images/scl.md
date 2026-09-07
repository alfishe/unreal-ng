# SCL — TR-DOS File Archive

## Overview

Archive of TR-DOS files with directory entries. Does not preserve deleted files or unmapped sectors.

- **Extensions**: `.scl`
- **Magic**: `SINCLAIR` at offset 0
- **Loader**: `core/src/loaders/disk/loader_scl.cpp`

## File Layout

```
0x00  8   "SINCLAIR"
0x08  1   File count (N)
0x09  N×14  Directory entries
      ...   File data (concatenated)
```

### Directory Entry (14 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 | Filename (space-padded) |
| 8 | 1 | Extension (type): B=BASIC, C=CODE, D=DATA, #=screen |
| 9 | 2 | Start address (little-endian) |
| 11 | 2 | Length (little-endian) |
| 13 | 1 | Sector count |

## Loading

Creates a blank TR-DOS disk, then injects each file into the catalog. Files are placed sequentially starting at track 1, sector 1.

## Save (Export)

Reads the TR-DOS catalog from track 0, exports non-deleted files only. Unmapped sectors and disk-level metadata (label, geometry) are lost.

## Mapping to Model

- Creates DS80 blank disk (80 cylinders, 2 sides)
- Files injected via TR-DOS catalog manipulation
- CRCs and clock marks generated on format
