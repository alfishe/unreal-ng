# 034 — measurements 2026-10-23: the vtrd.in corpus

## Setup

- Host: Apple M1 Ultra (20 cores), macOS 15, `cmake-build-agent-release` (clang, `-O3`, thin LTO). The machine was
  shared: load average 49-59 during every run (other sessions building), so all timings are pessimistic.
- Library: `core/swsignature` as committed on the branch `ctl-signatures` (sector path for TRD / SCL, the catalog
  rule, uniform RAM pages left out); the tool: [`cpp/sigpoc.cpp`](cpp/sigpoc.cpp), target `signature-poc`.
- Corpus: `/Volumes/TB4-4Tb/Data/zx-spectrum/vtrd/files/{gamez,full_ver,translat,demo_ver,demoz,sbor,press,system}`,
  read in place (ZIP members inflated in memory): 20 745 ZIPs, 21 279 media, 1904 MB. `pcutilz` and `emulz` skipped.
- Snapshots: the five pairs in `testdata/loaders/rzx/oracle` (`<game>-300.z80`, `<game>-end.z80`) and every
  `*.z80 / *.sna / *.szx` under `/Volumes/TB4-4Tb/Data/zx-spectrum` outside vtrd.
- Variants per title: every 10th disk / tape (`--repack-every 10`) re-packed with the core's writers
  (`FloppyFormats::Save` to TRD / SCL / FDI / UDI, `TzxArchiveWriter`, `TapArchiveWriter`), renamed (the file; the
  disk label at sector 9 + `#F5`) and saved to (`TrdosFileSystem::Write`: a 200-byte `HISCORE` file added as data and
  as code; the first data file and the first code file rewritten with one byte changed; TR-DOS-style, the old entry
  deleted and the new one written).
- Raw output: `scratch/2026-10-23-poc-034/` (`full/` with `--cross-check`, `full2/` and `final/` the rule runs,
  `speed/`).

Commands:

```bash
V=/Volumes/TB4-4Tb/Data/zx-spectrum/vtrd/files
cmake-build-agent-release/bin/signature-poc --out scratch/2026-10-23-poc-034/final --repack-every 10 \
    --snapshots testdata/loaders/rzx/oracle --snapshots /Volumes/TB4-4Tb/Data/zx-spectrum \
    $V/gamez $V/full_ver $V/translat $V/demo_ver $V/demoz $V/sbor $V/press $V/system
cmake-build-agent-release/bin/signature-poc --out scratch/2026-10-23-poc-034/full --repack-every 10 --cross-check ...
cmake-build-agent-release/bin/signature-poc --out scratch/2026-10-23-poc-034/speed --speed 5000 $V/gamez $V/full_ver $V/press
```

## Formats

| Kind, format | Media | Without a content signature |
|---|---:|---:|
| disk SCL | 19 663 | 0 |
| disk TRD | 1 052 | 60 (no TR-DOS catalogue, empty) |
| disk FDI | 82 | 62 (not TR-DOS) |
| disk hobeta | 68 | 0 |
| disk UDI | 37 | 10 |
| disk TD0 | 6 | 4 |
| tape TAP / TZX | 312 / 1 | 0 |
| snapshot SNA / SZX (inside ZIPs) | 30 / 3 | 0 |
| unknown (`$B`, `$C`, `$W` without a TR-DOS header; 16 SCL with a bad byte sum) | 25 | 25 |

## Re-pack and rename (content signature equal)

| Variant | Equal / tried |
|---|---:|
| SCL → TRD, FDI, UDI | 1975 / 1975 each |
| TRD → FDI, UDI | 93 / 93 each |
| TRD → SCL | 91 / 93 (93 / 93 after the SCL writer fix) |
| FDI → SCL, UDI; UDI → FDI, SCL, TRD | 2/3 (3/3 after the fix), 3/3, 3/3, 3/3, 2/2 |
| TAP → TZX, TAP → TAP, TZX → TAP | 32 / 32, 32 / 32, 1 / 1 |
| the file renamed | 2107 / 2107 |
| the disk label changed | 2070 / 2070 |
| **all** | **10 458 / 10 461 = 99.97 %; 10 461 / 10 461 = 100 % after the fix** |

The three misses (`full_ver/POLECACL.zip`, `press/emga/EMGA.zip` TRD → SCL, `full_ver/X-REVERS.zip` FDI → SCL) were
one core defect: their catalogues hold entries past the file count of the volume sector (#E4: POLECACL 10 counted,
12 entries, the last two 0-sector catalogue text lines; EMGA 63 counted, 119 entries), and the SCL writer stopped at
the count. TR-DOS ends the catalogue at the first entry starting with 0x00: CAT (`A2FC6`) and `FIND_NAME` (LOAD, RUN,
...) in the TR-DOS 5.04T source walk all 128 slots up to that marker, #E4 is only the number CAT prints and the slot
SAVE writes next. `LoaderSCL::writeImage` now exports every live entry up to the marker (regression test
`LoaderRoundTrip_Test.SCL_WriteImage_ExportsEntriesPastTheFileCount`); the re-run 2026-10-24
(`scratch/2026-10-24-poc-034-fix/`, `--repack-every 10 --cross-check`) has no miss left.

## Saved to

A = the first-draft fuzzy rule (Jaccard ≥ 0.8 over the content tokens, no code file of the reference missing).
G = every reference entry `(name, type, start, length)` kept, ≤ 1 file with other data, ≤ max(2, n) entries added.
H = G and ≥ 1 reference file unchanged: **the catalog rule the library adopted** (`CompareFileSets` with `n:` tokens).
The tool checks the library's `CompareFileSets` against its own prototype of H on every saved-to variant and every
cross-release pair: 0 disagreements (run `final/`).

| Save | A | G | H |
|---|---:|---:|---:|
| add a code file | 410 / 2058 = 19.9 % | 2057 / 2058 = 100.0 % | 2057 / 2058 = 100.0 % |
| add a data file | 410 / 2058 = 19.9 % | 2057 / 2058 = 100.0 % | 2057 / 2058 = 100.0 % |
| rewrite a code file | 43 / 1993 = 2.2 % | 1987 / 1993 = 99.7 % | 972 / 1993 = 48.8 % |
| rewrite a data file | 87 / 316 = 27.5 % | 307 / 316 = 97.2 % | 306 / 316 = 96.8 % |
| **all** | **950 / 6425 = 14.8 %** | **6408 / 6425 = 99.7 %** | **5392 / 6425 = 83.9 %** |

By the reference's file count (the corpus is mostly small disks: 1 file 53 %, 2 files 17 % of the 20 701 distinct disks):

| Save, files | A | G | H |
|---|---:|---:|---:|
| add a file, 1 | 0 % | 99.9 % | 99.9 % |
| add a file, 2 | 0 % | 100 % | 100 % |
| add a file, 3-4 | 36.4 % | 100 % | 100 % |
| add a file, 5-8 / 9+ | 100 % / 100 % | 100 % / 100 % | 100 % / 100 % |
| rewrite a code file, 1 | 3.0 % | 99.6 % | 3.0 % |
| rewrite a code file, 2 / 3-4 / 5-8 / 9+ | 1.8 / 0.6 / 2.5 / 0 % | 100 / 99.7 / 100 / 99.4 % | 100 / 99.7 / 100 / 99.4 % |
| rewrite a data file, 2 / 3-4 / 5-8 / 9+ | 0 / 0 / 0 / 98.9 % | 94.8 / 97.9 / 97.1 / 98.9 % | 94.8 / 97.9 / 97.1 / 98.9 % |

(A's few "rewrite a code file" hits are bytes changed past the hashed length: the content stayed equal.)

## Matches between releases

20 701 disks of distinct content, 428 510 700 ordered pairs, 56 286 of them sharing a content token. A match between
two ZIPs is a *candidate* false match (the ground truth is the ZIP).

| Rule | Pairs | Disks matching another release |
|---|---:|---:|
| exact content (`trdset:sha256:` equal) | 30 signatures shared by 62 releases | — |
| A (first draft) | 14 | 13 = 0.063 % |
| B (Jaccard ≥ 0.8 only) | 118 | 102 = 0.493 % |
| C (A, reference ≥ 3 files) | 14 | 13 = 0.063 % |
| G | 199 | 146 = 0.705 % |
| **H (the catalog rule)** | **110** | **81 = 0.391 %** |

G by the reference's file count: 1 file 82 / 11 060 (0.74 %), 2 files 22 / 3419, 3-4 files 12 / 3123, 5-8 files
7 / 1274, 9+ files 23 / 1825.

Hand classification of the H pairs (the list is in `final/summary.md`): the same title in another release (a crack,
translation, fix or 128K version: `ROCKSTAR` / `ROCKSTIS` / `RSAMHKEN`, `SOVIET` / `SOVIETVA`, `KUNFU128` / `YKF128VA`,
`X_MELODY` / `XMELODH`, ...), the title on a magazine or compilation disk with a few more files (`BEAST` on
`SP20`, `A_MOZAIK` on `SP12`, `EVACUATR` on `YG4`, `WFANTASY` on `BBG_2017`, ...), and one real different program:
`press/acn/ACN71 listh/BOOT.$B`, a lone hobeta boot loader, inside five test disks of `press/inferno/IG11, IG12`. G's
extra pairs over H are of the real kind: one-file disks whose only entry has the same name, type, start and length
with other data (`PEKIN` / `RNROLL` / `SIMPSNCD` / `DXINOX_E` / `LOOPZ_F`).

## The sector path against the DiskImage loaders (`--cross-check`, run `full/`)

Every disk computed twice: TRD / SCL from their sectors (the default) and through `FloppyFormats::LoadData` +
`TrdosFileSystem` (`Options::sectorImages = false`).

| Disks | Results differ | Default path | DiskImage path |
|---:|---:|---:|---:|
| 20 908 | 1 | 1.97 ms a disk | 21.3 ms a disk |
| 20 908 (re-run 2026-10-24, the loaders fixed, load average ~100) | 0 | 2.92 ms a disk | 29.5 ms a disk |

The one difference: `system/BBR.zip BBR.trd`, an 86-cylinder TRD (704 512 bytes). `FloppyFormats::ProbeData` takes it
as TRD (up to 86 cylinders), but `LoaderTRD::parse` refused 86 (`>= MAX_CYLINDERS`, the other loaders take
1..`MAX_CYLINDERS`) and `LoaderTRD::format` anything but 40 / 80 cylinders, so the DiskImage path fell through to the
tape probe (which accepts the file as a TAP); the sector path reads its TR-DOS catalogue (4 files). Fixed: `format`
lays out 1..86 cylinders on one or two sides (80-track disk type above 42 cylinders, free sectors from the geometry),
`parse` takes 86 (regression test `LoaderTRD_Test.EightySixCylinders`).

## Snapshots (memory signature)

Runs `full2/` (all RAM pages) and `final/` (uniform pages left out, ≥ 16 common pages):

| | All pages, ≥ 90 % | Uniform pages left out, ≥ 90 % |
|---|---|---|
| same game (5 oracle pairs) | 3 / 5 (dargonscrypt 97.9, ericfloaters 90.9, greenberet 96.3; garfield 65.2, thundercats 81.8) | 1 / 5 (dargonscrypt 92.6; greenberet 89.7, thundercats 76.0, ericfloaters 75.5, garfield 67.5) |
| pairs of different files ≥ 90 % | 8918 / 19 423 = 46 % | 1 / 6992 = 0.014 % |
| best non-match | 89.6 % | 62.5 % |
| time | 0.8 ms a snapshot | 0.6 ms |

Most of the extra snapshots are ZX Next boot / test images (`zx-next/collection`, `cards/sn-test-card`, assembler
examples): RAM near the boot state, equal page by page whatever the program.

## Speed (`--speed 5000`, media in memory, no I/O)

5000 media, 236.6 MB (47.3 KB each), three rounds under load 49-59:

| Round | SHA-256 of the files alone | Compute, 1 thread | Compute, 20 threads |
|---|---:|---:|---:|
| 0 (cold) | 32 MB/s (685 files/s) | 480 files/s | 6926 files/s |
| 1 | 71 MB/s (1495 files/s) | 877 files/s | 10 077 files/s |
| 2 | 81 MB/s (1721 files/s) | 802 files/s | 11 387 files/s |

The corpus run (one thread, inflate and the variants interleaved): 46 s of `Compute` for 21 279 media = 461 files/s.
The DiskImage path alone would be ~50 files/s (21.3 ms a disk).
