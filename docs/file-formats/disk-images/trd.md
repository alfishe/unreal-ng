# TRD — TR-DOS Raw Sector Dump

## Overview

Raw dump of logical sectors from a TR-DOS disk. No gap/sync information, no CRCs, no interleave data.

- **Extensions**: `.trd`
- **Loader**: `core/src/loaders/disk/loader_trd.cpp`

## File Layout

Sequential dump: cylinder-major, then side, then sectors 1–16, each 256 bytes.

| Geometry | Cylinders | Sides | Size |
|----------|-----------|-------|------|
| SS40 | 40 | 1 | 163,840 bytes |
| DS40 | 40 | 2 | 327,680 bytes |
| SS80 | 80 | 1 | 327,680 bytes |
| DS80 | 80 | 2 | 655,360 bytes |

Files may be truncated after the last used cylinder.

## Volume Sector

Track 0, sector 9 contains TR-DOS metadata:

| Offset | Field |
|--------|-------|
| 0xE1 | First free sector |
| 0xE2 | First free track |
| 0xE3 | Disk type: 0x16=DS80, 0x17=DS40, 0x18=SS80, 0x19=SS40 |
| 0xE4 | File count |
| 0xE5–0xE6 | Free sectors (little-endian) |
| 0xE7 | TR-DOS signature (0x10) |
| 0xF5–0xFC | Disk label (8 chars, space-padded) |

## Mapping to Model

- Track formatted with `TrackFormatSpec::trdos(interleave)` — 16 sectors × 256 bytes
- Interleave pattern configurable (default: 1:1 for emulation speed)
- Clock marks generated on format for accurate UDI save

## Save Constraints

TRD can only store 16 × 256-byte sectors numbered 1–16 per track. `writeImage()` validates geometry and rejects non-TR-DOS content. If validation fails, the emulator saves as UDI instead.
