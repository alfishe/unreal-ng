# MGT / IMG — +D / DISCiPLE Disk Image

## Overview

Raw sector dump for Miles Gordon Technology +D and DISCiPLE interfaces. 80 cylinders, 2 sides, 10 sectors × 512 bytes per track.

- **Extensions**: `.mgt`, `.img`
- **Size**: 819,200 bytes (fixed)
- **Loader**: `core/src/loaders/disk/loader_mgt.cpp`

## Geometry

| Parameter | Value |
|-----------|-------|
| Cylinders | 80 |
| Sides | 2 |
| Sectors/track | 10 |
| Sector size | 512 bytes |
| Sector numbering | 1–10 |

## Track Order

The extension determines track interleaving:

| Extension | Order |
|-----------|-------|
| `.mgt` | Side-interleaved: cyl 0 side 0, cyl 0 side 1, cyl 1 side 0, ... |
| `.img` | Side-sequential: all side 0 tracks, then all side 1 tracks |

## Mapping to Model

- Track formatted with 10 × 512-byte sectors, numbered 1–10
- MFM encoding with standard gap lengths
- Clock marks generated on format

## Save Constraints

MGT can only store 10 × 512-byte sectors numbered 1–10 per track. `writeImage()` validates geometry:
- Exactly 80 cylinders, 2 sides
- 10 sectors per track, each 512 bytes
- Sector numbers 1–10

If validation fails, save is rejected and the emulator re-targets to UDI.
