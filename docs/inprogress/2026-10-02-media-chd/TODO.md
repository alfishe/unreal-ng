# CHD as a shared media format — TODO

**Status:** implemented on branch `media-chd` (2026-10-02): read v3 / v4 / v5, write v5,
uncompressed and every hard-disk codec (zlib, lzma, huff, flac, zstd) both ways, parents and
children, metadata, CRC / SHA-1 checks, save back into the CHD, export to raw or CHD, all surfaces.
PLAN.md row **#58** (media manager). Design: [design.md](design.md).

## Done

- [x] `ChdFile` reader, `WriteChd` writer, `ChdImage` block device, own Huffman and FLAC
- [x] Format registry, `targets`, `formats`; IDE units and SD cards of every machine take `.chd`
- [x] `save` / `export` of block media in the target's format (`BlockFormats`), `compression`, `parent`
- [x] Tests (unit, chdman cross-check, ZX-Evo SD boot, Sprinter DSS 1.62 / 1.71 boots), benchmark
- [x] Docs: [chd.md](../../file-formats/disk-images/chd.md), [docs/features/media.md](../../features/media.md),
  recipes [use-media-slots.md](../../../.recipe/media/use-media-slots.md),
  [sprinter-hdd.md](../../../.recipe/media/sprinter-hdd.md)

## Follow-ups

- [ ] CD-ROM CHDs in the CD drive (`cdlz` / `cdzl` / `cdfl` / `cdzs`, `CHT2` tracks) — design.md §8
- [ ] Multi-threaded CHD writing (hunks compressed on a pool, 50 % of the cores)
- [ ] LPC subframes in the FLAC encoder
- [ ] Media history H1-H5 (#58): the versioned change layer over a CHD source
