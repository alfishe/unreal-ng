# CHD — MAME Compressed Hunks of Data (hard disks and SD cards)

## Overview

MAME keeps every hard disk and SD card as a CHD file: the disk cut into **hunks** (4 KB = 8 sectors
by default), each hunk compressed or stored as is, with a map that says where each hunk is, and
metadata that carries the drive geometry. MAME's ZX Spectrum family drivers use it for the ATM IDE,
the Nemo IDE (Pentagon, ZX-Evo), SMUC (Scorpion), the NeoGS SD card and the Sprinter IDE, so a CHD
from a MAME setup goes into the same slot here.

- **Extension**: `.chd` (recognized by its content: `MComprHD` at byte 0)
- **Kind**: block medium — any IDE hard-disk unit or SD card slot of any machine
- **Reader / writer**: `core/src/emulator/io/storage/chd/` (`ChdFile`, `ChdImage`, `WriteChd`); own code,
  written from MAME's `src/lib/util/chd.cpp`, `chdcodec.cpp`, `huffman.cpp` and `flac.cpp` as the
  format reference (BSD-3-Clause; no MAME code is vendored)
- **Format id**: `chd` (`HddImageFormats::Probe`, `Medium::Format`)
- **Design**: [docs/inprogress/2026-10-02-media-chd/](../../inprogress/2026-10-02-media-chd/README.md)

Glossary: a **hunk** is the unit of compression (a few sectors); a **unit** is a sector (512 bytes);
a **codec** is a compression method; a **parent** is another CHD a **child** CHD stores only its
differences from.

## What is supported

| Feature | Read | Write |
|---|:---:|:---:|
| Header versions | v3, v4, v5 (MAME reads the same; v1 / v2 are refused) | v5 |
| Uncompressed hunks (MAME's writable hard-disk form, `Compression: none`) | ✔ | ✔ |
| Codec `zlib` (deflate) | ✔ | ✔ (miniz) |
| Codec `lzma` | ✔ | ✔ (LZMA SDK 19.00, MAME's properties) |
| Codec `huff` (8-bit Huffman) | ✔ | ✔ (own) |
| Codec `flac` (the hunk as 16-bit stereo audio) | ✔ every frame libFLAC writes (LPC included) | ✔ own encoder: fixed predictors, stereo decorrelation, no LPC |
| Codec `zstd` | ✔ | ✔ (level 19) |
| Self references (a hunk equal to an earlier one) | ✔ | ✔ |
| Parent / child CHDs (MAME's `-diff` files, `chdman createhd -op`) | ✔ the parent is found by its SHA-1 among the `.chd` files next to the child | ✔ `export ... parent=` |
| SHA-1 of the data and of data + metadata; CRC-16 per hunk (v5), CRC-32 (v3 / v4) | checked (CRCs on every read, SHA-1 by `Verify`) | written (compressed files; MAME leaves them zero in uncompressed files) |
| Metadata (`GDDD` geometry, `IDNT` identify data, ...) | ✔ | kept on save / export |
| CD-ROM CHDs (v5: `cdlz`, `cdzl`, `cdzs`, `cdfl`, uncompressed; `CHT2` / `CHTR` track metadata) | ✔ in the CD drive (below) | — |
| GD-ROM, DVD, LaserDisc CHDs (`avhu`), v3 / v4 CD CHDs (`CHCD`) | refused with the reason (`chdman copy` makes a v5 CD CHD) | — |

### CD-ROM CHDs

A CD CHD stores 2448-byte frames (2352 bytes of sector data, 96 of subcode), 8 to a hunk by
chdman's default. Each track takes `FRAMES` frames, padded to a multiple of 4 in the file;
`PGTYPE` starting with `V` means its pregap (`PREGAP` frames) is inside those frames, otherwise
the pregap is silence the CHD does not store. A track's INDEX 01 (the TOC address) is the pregap's
end; `POSTGAP` frames of silence follow the track. Audio is stored **big-endian** (MAME swaps
a CUE's little-endian samples when it writes the CHD); the drive outputs little-endian.
`MODE1` / `MODE2_FORM1` frames store 2048 user bytes, `MODE2` / `MODE2_FORM_MIX` 2336,
`MODE1_RAW` / `MODE2_RAW` / `AUDIO` 2352.

The CD codecs are a base codec for the sector data and a second one for the subcode: `cdlz`
(LZMA + deflate), `cdzl` (deflate + deflate), `cdzs` (Zstandard + Zstandard), each after a
bitmap of the frames whose sync pattern and ECC were stripped (rebuilt on reading, ECMA-130) and
the base stream's length (2 bytes, 3 for hunks of 64 KB and more); `cdfl` codes the sector data
as 16-bit stereo FLAC frames (no byte-order prefix; the samples are written back big-endian)
followed by the subcode in deflate. Implementation: `chdcodec.cpp` (`CreateCdCodec`),
`ChdFile::OpenCd`, the track layout in `core/src/emulator/io/storage/cd/cdimageformats.cpp`.
chdman 0.289 writes a CD CHD it cannot read back itself when `cdzs` or `cdzl` is the only codec
("Decompression error" from `chdman verify`); with a second codec it is fine.

## Layout (v5)

All numbers are big-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | `MComprHD` |
| 8 | 4 | header length (124) |
| 12 | 4 | version (5) |
| 16 | 4 × 4 | codecs: four-character tags, `zlib` `lzma` `huff` `flac` `zstd`; all zero = uncompressed |
| 32 | 8 | logical size in bytes |
| 40 | 8 | map offset |
| 48 | 8 | first metadata entry |
| 56 | 4 | hunk size in bytes (4096 from `chdman createhd`) |
| 60 | 4 | unit size (512: the sector) |
| 64 | 20 | SHA-1 of the data |
| 84 | 20 | SHA-1 of the data and the checksummed metadata |
| 104 | 20 | the parent's SHA-1 (zero: no parent) |

**Uncompressed map** (right after the header): one 32-bit number per hunk, the hunk's offset divided
by the hunk size. 0 means the hunk is not stored: zero, or the parent's hunk of the same number.
Stored hunks sit on hunk-size boundaries, so MAME can write them in place.

**Compressed map** (at the end of the file): a 16-byte header (compressed length, offset of the first
hunk, CRC-16 of the expanded map, bit widths of lengths, self and parent references), then the hunk
types run-length coded and Huffman coded (16 symbols, codes up to 8 bits, the code table itself
run-length coded), then per hunk its compressed length and CRC-16, or the hunk or parent unit it
copies. Runs of consecutive references are folded into pseudo-types that carry no number.
Compressed hunks follow each other with no gaps, in hunk order.

**Metadata**: a chain of entries (tag, flags, 24-bit length, offset of the next entry, data). A
hard disk's `GDDD` is the text `CYLS:4096,HEADS:16,SECS:32,BPS:512` with a terminating NUL.

### Codec details that matter for compatibility

| Codec | Stored as |
|---|---|
| `zlib` | raw deflate (no zlib header) |
| `lzma` | a bare LZMA stream with no end marker and no properties: the reader derives them from level 6 with the dictionary cut down to the hunk (lc 3, lp 0, pb 2) |
| `huff` | a Huffman table for the 256 byte values (code lengths up to 16 bits, run-length coded and Huffman coded with a 24-symbol table), then the bytes |
| `flac` | `L` or `B` (the byte order the samples were read in), then bare FLAC frames of up to 2048 samples, 2 channels, 16 bits, no `fLaC` marker or STREAMINFO |
| `zstd` | one Zstandard frame |

A codec is used for a hunk only when it gives fewer bytes than the hunk; otherwise the hunk is stored
as is. The writer tries every codec of the file and keeps the smallest result, as chdman does.

## Mapping to the model

`ChdImage` is a block device of `logical size / 512` sectors. A sector read decodes its hunk once and
keeps it in a 64-hunk cache, so the next seven sectors of the hunk are a copy (benchmark
`BM_SectorRead_ChdDefaultCodecs`: 16 ns a sector, against 1.4 µs for a raw image read from the
file; a cache miss costs 20-75 µs depending on the codec). `GDDD` becomes the drive's geometry in
the ATA IDENTIFY. Sectors other than 512 bytes are refused.

A CHD is never written in place. Guest writes stay in the medium's change layer (`session`
access; an IDE unit's default `writethrough` turns into `session` for a CHD, with a note in the
insert reply), and the file changes only on `save`.

## Saving and exporting

| Operation | Result |
|---|---|
| `save` (to its own file) | the CHD written again with its codecs, hunk size and metadata; untouched hunks keep their stored bytes (no second compression); a child stays a child of the same parent. Written to `<file>.writing` and renamed over the file |
| `save` with `compression` | the same, with other codecs (`none` makes it uncompressed) |
| `save` to another path | the medium then stands for the new file |
| `export <file>.chd` | a new CHD: the source CHD's codecs, or chdman's default (`lzma,zlib,huff,flac`) for a medium that was not a CHD; `compression` picks others; `parent` writes a child |
| `export <file>.img` (any other extension) | a raw image, sector n at byte n × 512 |

Geometry written to `GDDD`: the medium's own (from the CHD, an HDF / VHD / HDI header), else
chdman's guess (63 down to 2 sectors per track and 16 down to 2 heads that divide the sector count),
else 16 heads × 63 sectors over the whole cylinders that fit. The logical size is always the exact
sector count, so a round trip image → CHD → image gives the same bytes.

Verified with chdman 0.289 (`ChdWriter_Test.ChdmanAcceptsEveryVariant`, `UNREAL_CHDMAN=<chdman>`):
`chdman verify` passes ("Overall SHA1 verification successful") for every codec, all five codecs
together, and children; `chdman extracthd` gives back the source bytes.

## Examples

```text
media insert ide0.master /mame/sprinter/sp_hdd_sys.chd        # DSS 1.71 from MAME's own disk
media save ide0.master                                        # write the guest's changes into the CHD
media export ide0.master /tmp/disk.chd --compression zstd     # a zstd copy
media export ide0.master /tmp/diff.chd --parent /mame/sprinter/sp_hdd_sys.chd   # only the changes
media export sd /tmp/card.img                                 # a raw image of the card
```
