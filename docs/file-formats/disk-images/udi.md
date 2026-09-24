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

## Weak-Bit Map (`UDIW`)

> Status 2026-09-23: **implemented** — the loader parses and serializes `UDIW`
> (round-trip covered by `LoaderUDI_Test`). Design and outcome:
> [docs/inprogress/2026-09-22-udi-weak-bit-storage/design.md](../../inprogress/2026-09-22-udi-weak-bit-storage/design.md).

UDI has no on-disk weak-bit field, so before 2026-09-23 a track whose weak bitmap is set
(`DiskImage::RawTrack::_weak`, consumed by `FlakySectorEmulator`) was saved
**without** its weak bits. The implemented extension carries
them in the **trailer** — the bytes between the last track descriptor and the trailing
CRC-32, where TRX2X files already keep their ASCIIZ comment. Any conformant reader ignores
unknown comment bytes, so the chunk is spec-legal and covered by the file's CRC.

Layout (little-endian), appended after any existing comment:

```
"UDIW" | u16 version = 1 | u16 recordCount | recordCount × record
record: u8 cylinder | u8 side | u8 flags | u16 streamOffset | u16 length
```

- `streamOffset`/`length` address a byte range in that track's raw stream (the same
  coordinate space as the clock bitmap); weakness is idempotent, so adjacent or overlapping
  ranges are legal
- `flags` reserved 0 — bit-mapped visibility duty cycle is the earmarked future use;
  unknown bits ⇒ warning, record still applied (forward compatibility)
- Parse: first `"UDIW"` occurrence in the trailer; bounds-check geometry and
  `streamOffset+length` against that track's `rawSize()`, then `setWeakByte(offset, true)`
  per byte; strip the chunk from the preserved comment so a later save cannot duplicate it;
  malformed chunk ⇒ warning + normal load (the chunk is advisory metadata, never structural)
- Serialize: one record per maximal run of set weak bits (RLE — marks are contiguous
  IDAM/data fields, so a handful of records per protected track), appended after the
  preserved comment; replaces the silent drop, making saving lossless

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

> Since 2026-09-23 saving round-trips weak bits through the `UDIW` trailer chunk (above);
> only an image with more than 65535 weak ranges in total still loses the excess
> (warning at `loader_udi.cpp:343`).

## Compatibility Notes

- Files from TRX2X may include a trailing ASCIIZ comment before the CRC (preserved on round-trip)
- Real-drive dumps may have non-standard track lengths (6400 bytes for fast drives)
- Byte 0x0B is 1 in TRX2X-generated files (preserved)
