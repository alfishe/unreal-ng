# UDI — Ultra Disk Image v1.0

## Overview

Lossless native format preserving the universal track model: variable track length, clock bitmap, FM/MFM encoding.

- **Extensions**: `.udi`
- **Magic**: `UDI!` at offset 0 (`udi!` = compressed, not supported)
- **Loader**: `core/src/loaders/disk/loader_udi.cpp`

## File Layout (little-endian)

```
0x00  4   "UDI!"
0x04  4   File size minus CRC (offset of trailing CRC)
0x08  1   Version (0)
0x09  1   Max cylinder (cylinders - 1)
0x0A  1   Max head (0 or 1)
0x0B  1   Reserved (preserved on save)
0x0C  4   Extended header size (X)
0x10  X   Extended header (ignored, preserved)

For each track (cyl 0..max, head 0..max):
  1     Track type: 0=MFM, 1=FM, 2=mixed, 0x80|n=multi-rev (unsupported)
  2     TLEN (track length)
  TLEN  Raw track bytes (offset 0 = index pulse)
  ⌈TLEN/8⌉  Clock bitmap (bit i marks sync byte)

4     CRC-32 (signed accumulator variant, polynomial 0xEDB88320)
```

## Clock Bitmap

Bit `i & 7` of byte `i >> 3` (LSB first) marks whether byte `i` was written with missing clock (A1/C2 sync marks).

## CRC Variant

Uses CRC-32 with a **signed** `int` accumulator (`crc >>= 1` is arithmetic shift), initial value -1. This differs from standard zlib CRC-32.

## Mapping to Model

Direct mapping — UDI stores exactly what the model holds:
- `TLEN` → `track.rawSize()`
- Raw bytes → `track.rawData()`
- Clock bitmap → `track.clockBitmap()`
- Type 0/1 → `Encoding::MFM` / `Encoding::FM`

## Save Target

UDI is the designated "save anything" target. When other formats reject non-conforming geometry, the emulator saves as UDI instead.

## Compatibility Notes

- Files from TRX2X may include a trailing ASCIIZ comment before the CRC (preserved on round-trip)
- Real-drive dumps may have non-standard track lengths (6400 bytes for fast drives)
- Byte 0x0B is 1 in TRX2X-generated files (preserved)
