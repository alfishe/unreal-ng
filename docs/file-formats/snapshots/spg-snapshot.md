# SPG ("Spectrum Prog") Program Format

`.spg` is the program format of the TS-Conf SDK (the TS-Labs ZX-Evo / TS-Conf computers). Unlike a snapshot of a running
machine it is a **program image**: blocks of RAM at physical addresses, optionally packed, plus the address to start at. The
SD-card shell (Wild Commander) loads one and runs it; Unreal-NG loads one the same way, on the TS-Conf machine only.

Two versions exist, 1.0 and 1.1 (a 0.x draft also exists and is not supported). All values are little-endian.

## Header (1024 bytes)

Reserved fields are zero. Bit fields are numbered from low to high.

| Offset | Bytes | Description |
|-------:|------:|-------------|
| 0x000 | 32 | Author's string |
| 0x020 | 12 | Magic `SpectrumProg` |
| 0x02C | 1 | Low nibble: format sub-version (0 for 1.0, 1 for 1.1). High nibble: format version (1) |
| 0x02D | 1 | Day of the month |
| 0x02E | 1 | Month |
| 0x02F | 1 | Year (add 2000) |
| 0x030 | 2 | Run address (#4000 to #FFFF) |
| 0x032 | 2 | SP value |
| 0x034 | 1 | The page at #C000 |
| 0x035 | 1 | Bits 0-1: CPU clock. Bit 2: INT enabled. Bits 3-7: reserved |
| 0x036 | 2 | Address where the pager is loaded (0 = none; its code is at most 32 bytes) |
| 0x038 | 2 | Address where the resident is loaded (16 bytes) |
| 0x03A | 2 | Number of blocks |
| 0x03C | 1 | Second |
| 0x03D | 1 | Minute |
| 0x03E | 1 | Hour |
| 0x03F | 17 | Reserved in 1.0. In 1.1, see below |
| 0x050 | 32 | Creator utility string |
| 0x070 | 144 | Reserved |
| 0x100 | 768 | Block descriptors, up to 256 of 3 bytes |

Version 1.1 uses the 17 bytes at 0x03F for a picture shown while the program loads and for the sizes of its read-only and
read-write sections:

| Offset | Bytes | Description |
|-------:|------:|-------------|
| 0x03F | 1 | Bit 0: picture enabled. Bits 1-2: picture compression (0 none, 1 MegaLZ, 2 Hrust). Bit 3: wait for a key after loading. Bits 4-5: graphics mode. Bits 6-7: reserved |
| 0x040 | 1 | Video page used to show the picture |
| 0x041 | 1 | Picture data size (0 = 512 bytes, 255 = 128 KB) |
| 0x042 | 3 | Size of the RO section, in sectors |
| 0x045 | 3 | Size of the RW section, in sectors |
| 0x048 | 8 | Reserved |

### Block descriptors

| Byte | Bits | Description |
|-----:|------|-------------|
| 0 | 0-4 | Address inside the page, in 512-byte units (0 = #C000, 0x1F = #FE00) |
| 0 | 5-6 | Reserved |
| 0 | 7 | Last-block marker (1 = no more blocks) |
| 1 | 0-4 | Size, in 512-byte units minus one (0 = 512 bytes, 0x1F = 16 KB) |
| 1 | 5 | Reserved |
| 1 | 6-7 | Compression: 0 none, 1 MegaLZ (MLZ), 2 Hrust |
| 2 | 0-7 | RAM page (#00 to #DF) |

The data of the blocks follows the header, in the order of the descriptors.

## What a program sees at start

The ancestor format and the SDK define the launch state: ZX screen 0, the 48K BASIC ROM at #0000, RAM page 5 at #4000, page 2 at
#8000, the header's page at #C000, interrupt mode 1, I = #3F.

## In Unreal-NG

Snapshot overview and how loading works: [README](README.md).

- **Loading only**; there is no SPG writer.
- Versions 1.0 and 1.1 are read. The 1.1 picture and section fields, the pager and resident addresses (installed by the SD
  shell, not by the file) are not used. A 0.x file is refused.
- The whole file is parsed and depacked before the machine is touched: a corrupt file, a block running past the end, or a
  broken MegaLZ / Hrust stream changes nothing, and the error says which block.
- Blocks are written by **physical RAM address** (page times 16 KB plus the offset), not through the Z80's windows.
- The program is committed **from the neutral snapshot image** by the TS-Conf machine's own policy (`tsconf-program`; the load report names it). The image holds the blocks as physical runs, the CPU, and the two TS-Conf registers the file names (the page at #C000 and the clock bits) as the `spg:header` extension's payload. `commit=legacy` runs the same code from the same image.
- The commit resets the machine, leaves the SD card as the shell would (idle, initialized), puts MEM_CONFIG in mapped mode with
  the BASIC-48 ROM at #0000, pages 5 and 2 in windows 1 and 2 and the header's page in window 3, sets SYS_CONFIG from the
  clock bits, #7FFD = #10, IY = #5C3A, HL' = #2758, I = #3F, IM 1, and the interrupt enable from the header.
- Only the **TS-Conf** machine (`TSL`, 4096 KB) takes an SPG. The automation launchers and the Qt window switch the model to
  it first; with `switch_model=false` the load is refused naming the model needed.
- The MegaLZ and Hrust depackers are checked against the reference packer (mhmt) with real SDK programs.

Test material: `testdata/machines/tsconf/spg/` (`empty.spg`, `sprites.spg`, `slideshow.spg`, with a README).

## References

- [SPG v1.0](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/SPGv1_0.txt) and
  [v1.1](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/SPGv1_1.txt) (zx-evo-docs, TS-Labs); this page follows them.
  The [Formats folder](https://github.com/tslabs/zx-evo-docs/tree/main/Formats) also holds the draft
  [v0.2](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/SPGv0_2.txt)
- [zx-evo-docs: TSconf](https://github.com/tslabs/zx-evo-docs/tree/main/TSconf), the TS-Conf register and memory documentation
- [lvd's mhmt](https://github.com/lvd2/mhmt), the reference MegaLZ / Hrust packer
