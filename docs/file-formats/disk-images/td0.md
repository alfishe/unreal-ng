# TD0 — Teledisk Image

## Overview

Sydex Teledisk format with optional LZSS compression, FM support, and embedded comments.

- **Extensions**: `.td0`
- **Magic**: `TD` (uncompressed) or `td` (compressed, advanced)
- **Loader**: `core/src/loaders/disk/loader_td0.cpp`

## File Layout

```
Header (12 bytes):
  0x00  2   Signature "TD" or "td"
  0x02  1   Sequence (0 for single disk)
  0x03  1   Check sequence
  0x04  1   Teledisk version
  0x05  1   Data rate (0=250K, 1=300K, 2=500K)
  0x06  1   Drive type
  0x07  1   Stepping (0=single, 1=double)
  0x08  1   DOS allocation flag
  0x09  1   Sides (1 or 2)
  0x0A  2   Header CRC-16

Comment block (if flag 0x80 in offset 0x07):
  2     CRC-16
  2     Length
  6     Date (year-1900, month, day, hour, minute, second)
  N     Comment text (null-terminated)

Track data:
  For each track:
    1     Sector count (0xFF = end of disk)
    1     Cylinder
    1     Head
    1     CRC (low byte)
    Per sector:
      1     Cylinder
      1     Head
      1     Sector number
      1     Size code
      1     Flags
      1     CRC (low byte)
      2     Data length (encoded/compressed)
      ...   Data (pattern or raw)
```

## Sector Flags

| Value | Meaning |
|-------|---------|
| 0x00 | Normal |
| 0x02 | CRC error in ID |
| 0x04 | Deleted data |
| 0x10 | No data field |
| 0x20 | CRC error in data |
| 0x40 | FM (single density) — per sector |

## Data Encoding

| Pattern | Meaning |
|---------|---------|
| 0 | Raw data follows |
| 1 | 2× repeat block |
| 2 | RLE pattern (len, data[len], count) |

## Mapping to Model

- FM flag sets track encoding per sector (converted to track-level FM if all sectors match)
- Deleted marks and CRC errors preserved
- Comment block stored in `DiskImage` metadata

## Save

Writes uncompressed TD0 (signature `TD`). Advanced compression (`td`) not implemented for write.
