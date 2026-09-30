# Raw PC floppy — 720 KB / 1.44 MB sector dump

## Overview

A plain dump of a 3.5" PC-formatted floppy: the 512-byte sectors one after another, no header, no
low-level information. Used by the Sprinter (DSS boot floppies, 720 KB and 1.44 MB), Profi CP/M
(720 KB) and PC-formatted +3 disks.

- **Extensions**: `.img`, `.ima` (any extension works: the size decides)
- **Sizes**: 737,280 bytes (720 KB, DD) and 1,474,560 bytes (1.44 MB, HD)
- **Loader / writer**: `core/src/loaders/disk/loader_rawpc.cpp` (`LoaderRawPcFloppy`)
- **Format id**: `rawpc` (`FloppyFormats::Probe`, `Medium::Format`)

## Geometry

| Size | Cylinders | Sides | Sectors / track | Sector IDs | N | Density | Raw track |
|---|---|---|---|---|---|---|---|
| 737 280 | 80 | 2 | 9 | 1-9 | 2 (512 bytes) | DD, 250 kbit/s | 6 250 bytes |
| 1 474 560 | 80 | 2 | 18 | 1-18 | 2 (512 bytes) | HD, 500 kbit/s | 12 500 bytes |

Track order in the file: cylinder-major, sides interleaved (cyl 0 side 0, cyl 0 side 1, cyl 1 side 0, ...).

## Detection

By size only, before the TR-DOS and MGT rules: neither size is a TRD (at most 655 360 bytes) or an MGT
(819 200 bytes), and a PC boot sector or FAT can carry the TR-DOS id byte `#10` at the TR-DOS offset.
In a block (hard disk) slot the same file is a raw block image: the slot kind settles `.img`.

## Mapping to the model

Every track is a standard PC (IBM System 34) MFM track:

| Field | Bytes |
|---|---|
| GAP4a (`4E`) | 80 |
| SYNC (`00`) + IAM (`C2 C2 C2 FC`) | 12 + 4 |
| GAP1 (`4E`) | 50 |
| per sector: SYNC + IDAM (`A1 A1 A1 FE`) + C H R N + CRC | 12 + 4 + 6 |
| GAP2 (`4E`) | 22 |
| SYNC + DAM (`A1 A1 A1 FB`) + 512 data + CRC | 12 + 4 + 514 |
| GAP3 (`4E`) | 84 (DD) / 108 (HD) |
| GAP4b (`4E`) | the rest of the revolution |

Worked totals: DD 146 + 9 × 658 = 6 068 of 6 250 bytes; HD 146 + 18 × 682 = 12 422 of 12 500 bytes.

The raw track length is also what tells the WD1793 the density
([WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md)): a track of 9 375 bytes or
more is HD. A controller at the default 250 kbit/s reads a 720 KB disk and finds no address marks on a
1.44 MB disk (Record Not Found); a machine with a density latch (Sprinter `#BD`, `FdcClockPolicy::Latched`
at 2 MHz / 500 kbit/s) reads the 1.44 MB one. The WD1793 needs no IAM and reads past it.

## Saving

The sectors are written back while the layout is still regular: 80 × 2 tracks, each with the same 9 or 18
sectors numbered 1..N and 512-byte data fields (sector order on the track does not matter). Otherwise the
save is refused with the first track that breaks the rule; the disk can be exported as UDI. Saving to `.img`
picks the MGT writer for a +D disk (10 × 512) and this writer for everything else.
