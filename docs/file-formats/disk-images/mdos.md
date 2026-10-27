# MDOS raw image (.d40, .d80)

## Overview

The 512-byte sectors of a Didaktik D40 / D80 floppy (MDOS 1 / 2) one after another in logical order, no header, as
Fuse and the MDOS3 tools keep them.

- **Extensions**: `.d40`, `.d80` (any extension works: the boot sector decides)
- **Sizes**: cylinders x sides x SPT x 512: D80 80 x 2 x 9 (737 280 bytes, the size of a 720 KB PC dump), D40
  40 x 1 or 2 x 9 (184 320 / 368 640 bytes); an MDOS3 disk cut out of its container 94 x 2 x 9 (865 280 bytes)
- **Loader / writer**: `core/src/3rdparty/unreal-disk/src/loaders/loader_mdos.cpp` (`LoaderMDOS`)
- **Format id**: `mdos` (`FloppyFormats::Probe`, `Medium::Format`)
- **File system**: `mdos` volume, read only ([disk file systems](../../emulator/design/disk-file-systems.md#mdos-layout))

## Layout

Logical sector `LSN = (cylinder * sides + side) * SPT + (sector - 1)`: cylinder-major, side 0 then side 1, sector ids
1..SPT. The image holds LSN 0, 1, 2, ... in turn.

## Detection

By the boot sector (LSN 0), before the raw PC size rule: `SDOS` at 204, cylinders (#B2, 1..94) x sides (2 when bit 4
of #B1 is set) x SPT (#B3, 1..18) x 512 equal to the file size. A D80 therefore stays MDOS although it has the size of
a 720 KB PC dump; without the mark the raw PC rule takes it.

## Mapping to the model

Every track is an MFM track of SPT sectors of 512 bytes (N = 2), ids 1..SPT, in the IBM layout with the widest gap 3
that fits (84 down to 8 in steps of 4: 84 on a 9-sector track). More than 10 sectors make an HD track of 12 500 bytes
(500 kbit/s), else a DD track of 6 250 bytes.

## Saving

The sectors are written back in LSN order while the layout is regular: every track with the same SPT sectors 1..SPT
of 512 bytes. Otherwise the save is refused with the reason (export as UDI to keep it). A loaded image writes back
byte for byte (`MdosFs.D80DiskListsAndReads`).

## MDOS3 containers (.md3)

An `.md3` file is not a floppy: it is an MDOS3 partition alone (slots of 1693 sectors, an info sector and a 94 x 2 x 9
disk each). It goes into a hard disk slot, or to `zxdisk`, as a raw block image; every slot is a volume `mdos-<slot>`.
