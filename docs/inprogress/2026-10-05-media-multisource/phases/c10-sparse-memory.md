# C10 — sparse and in-memory images, efficient packing on save / flatten

**Status:** done (C10a, C10b; C10c measured, no change), 2026-10-06; designed (owner request 2026-10-05: "think about sparse images, working with images in memory
and packing them back efficiently on save / flatten"). Phase C10 of [tdd.md](../tdd.md) §14. Exit: the design here,
and memory and save-time A/B numbers against C6 / C8.

## 1. Where the bytes and the time go today

| Case | Today | Cost |
|---|---|---|
| A blank card (`media create sd --size 2GiB`) | `MemoryDisk`: the whole size allocated and zeroed | 2 GiB of RAM for an empty card |
| Export / flat save of a big composite (a 4 GiB FAT32 volume holding 20 MiB) | every sector read through the stack (the free space memset sector by sector), the zero ones skipped on write (C6a) | about 8 M sector reads for 40 K sectors of data |
| A CHD export of the same | every hunk read and tested for zero | the same reads |
| A sparse host image (a 2 GB card image of which 50 MB are written) | read sector by sector; the holes read as zeros by the OS | the same reads |
| `.vhd` | fixed VHDs only, read and written | a fixed VHD is as big as the disk |
| Reads of an image file | one seek and one read per sector (`RawImage` over a stream) | measured in §5 |

## 2. Known-zero runs (C10a)

`IBlockDevice::ZeroRun(lba)`: how many sectors from `lba` on are known to read as zeros, without reading them
(0: not known, read it). The default says 0, so nothing changes for a device that does not know.

| Device | Answer |
|---|---|
| `SparseMemoryDisk` (new) | up to the next stored sector |
| `FatSynthVolume` | in the data area: up to the next run (a free cluster reads zeros); past `UsedSectorEnd`: to the end |
| `GraftVolume` | 0 for base sectors (the base decides: it asks the base device), up to the next patch or run in the free data area the graft owns |
| `RawImage` (POSIX) | the hole at `lba` (`lseek(SEEK_DATA)`); Windows: 0 (no allocated-ranges query yet) |
| `SessionWriteMap` | the base's run, cut at the next changed sector; 0 on a changed sector |
| `SubRangeDevice`, `PartitionedDisk`, the access layers (`ReadOnlyGuard`, `HostWriteHold`, `MediaReadTap`) | the inner device's, cut at their own boundaries; a partitioned disk's gaps are zero |

Users: `ExportBlockDevice` (raw, VHD) and the CHD writer skip such runs without reading them; the time-travel read
tap does not see them (nothing was read).

## 3. Sparse memory (C10a)

- **`SparseMemoryDisk`** replaces `MemoryDisk` for blank media (`media create` of a card or hard disk): sectors are
  stored in 64 KiB chunks allocated on the first non-zero write; a chunk written back to zeros is freed. A 2 GiB blank
  card costs nothing until the guest formats it (a few hundred KiB then).
- `MemoryDisk` stays for tests that want a flat buffer (`Data()`).

## 4. Dynamic VHD (C10b)

- **Read**: `HddImageFormats::OpenBlock("vhd")` opens type 3 (dynamic) too: the dynamic header (`cxsparse`), the
  block allocation table (BAT), 2 MiB blocks each with its sector bitmap. An unallocated block reads zeros and is a
  known-zero run. Differencing VHDs (type 4) stay refused.
- **Write**: export / flat save to a `.vhd` with `vhd: dynamic` writes a dynamic VHD: only blocks that hold a
  non-zero sector are allocated (known-zero runs are not even read). `vhd: fixed` (the default, the most compatible)
  stays as in C6a.
- **Writes into a dynamic VHD in place** (a `writethrough` medium, or `save` back into it): a write to an unallocated
  block appends the block (bitmap + data) at the end, moves the footer after it and updates the BAT entry; the block
  is zero-filled first, so its unwritten sectors read zeros.

## 5. Images in memory (C10c)

Measure first: `BM_RawImageRead` (sequential and random 512-byte reads through `RawImage`) against the same over a
memory buffer. If the stream costs more than the budget of NFR-P1 for the guest's read path, read-only and session
images up to a size limit (`[MEDIA] ImageMemoryLimit`, default 64 MiB) are read into memory at insert; `writethrough`
images stay streamed, since every write must reach the file. The decision and the numbers are recorded in §7.

## 6. Tests

| Part | Test | Checks |
|---|---|---|
| C10a | `ZeroRun_Test.*` | each device's answer against reading the sectors (property: a known-zero run reads zeros); a session cuts runs at its changes |
| C10a | `SparseMemoryDisk_Test.*` | reads, writes, zero writes freeing a chunk, the memory it holds |
| C10a | `ZeroRun_Test.ExportSkipsFreeSpaceWithoutReading` | a 1 GiB FAT32 composite exported to `.img` and `.chd`: equal to the stack, with a bound on the sectors read (a count, not a time: stable on any machine) |
| C10b | `VhdDynamic_Test.*` | read a dynamic VHD (made by our writer and by a hand-built fixture); write one and read it back; in-place writes allocate blocks; footer and BAT checksums; `Probe` |
| C10c | `BM_RawImageRead` | the numbers that decide §5 |
| A/B | `composeimage_benchmark` additions | export time of the big composite before and after; the blank card's memory |

## 7. As built

**C10a** (known-zero runs, sparse memory):

- `IBlockDevice::ZeroRun(lba)` answers as in §2. `RawImage` asks the host lazily through a second descriptor
  (`lseek(SEEK_DATA)`); a file system without hole queries (or Windows) answers 0. A held write (`HostWriteHold` with
  pending sectors) answers 0 rather than working out which runs the held sectors cut.
- `ExportBlockDevice` (raw, fixed VHD) and the CHD writer (`chd::WriteChd`) skip runs. An export of a 1 GiB FAT32
  composite holding one file reads its reserved area, FATs and the file's clusters, not the free space
  (`ZeroRun_Test.ExportSkipsFreeSpaceWithoutReading` bounds the reads).
- `SparseMemoryDisk` (`io/storage/sparsememorydisk.{h,cpp}`): 64 KiB chunks, a zero write never allocates, a chunk
  written back to all zeros is freed. `media create` of a card or hard disk uses it.
- The property test (`ZeroRun_Test.EveryDeviceClaimsOnlyZeros`) samples runs on a rebuilt FAT32 volume, a session
  over it, a graft, a partitioned disk, a sparse host file, a window of it and a sparse memory disk; each claimed run
  must read zeros (its first and last 32 sectors).

**C10b** (dynamic VHD), `io/storage/vhdimage.{h,cpp}`:

- `vhd::Footer` / `vhd::Geometry` are shared by the fixed writer (C6a, byte-identical output) and the dynamic one.
- `HddImageFormats::OpenBlock("vhd")` reads the footer type: 2 stays a `RawImage`, 3 opens a `VhdDynamicImage`
  (read-only or read-write as asked), 4 (differencing) is refused with "merge it into its parent first".
- `vhd::WriteDynamic`: footer copy, dynamic header, BAT (padded to sectors), then only the blocks with a non-zero
  sector, each with a full bitmap, then the footer. Known-zero runs are not read.
- In place: a write to an unallocated block writes the block (full bitmap, zeros) where the footer was, the footer
  after it, then the BAT entry; a zero write into an unallocated block allocates nothing. A sector whose bitmap bit is
  clear (written by another tool) gets the bit set when written.
- Option `vhd: fixed|dynamic` on `save` to a path, `export` and `flatten flat`; it applies to a `.vhd` target only and
  needs a path (a save into the medium's own `.vhd` writes the changed sectors in place, whatever its kind). Surfaces:
  MediaControl, MCP `media` schema, OpenAPI, CLI help (`--vhd`). The Qt flatten dialog does not offer it yet.

**C10c** (measured, `core/benchmarks/emulator/io/sparsemedia_benchmark.cpp`; Release, Linux container, Xeon 2.1 GHz,
2026-10-06):

| Benchmark | Time | Note |
|---|---|---|
| `RawImageSeqRead` (64 MiB file, 512-byte reads) | 0.69 µs / sector | the OS cache serves it |
| `RawImageRandRead` | 1.0 µs / sector | |
| `MemoryImageRandRead` (the same file in a buffer) | 0.04 µs / sector | |
| `BigCompositeExport/1/1` (4 GiB FAT32 holding 1 MiB → `.img`, known-zero runs) | 12.5 ms | |
| `BigCompositeExport/1/0` (the same, runs hidden: the C6a path) | 525 ms | 42x |
| `BigCompositeExport/2/1` (→ dynamic `.vhd`) | 141 ms | the FAT blocks are written whole (2 MiB each) |
| `BlankCard/1` (512 MiB `SparseMemoryDisk`, first 1 MiB written) | 0.1 ms, 1 MiB held | |
| `BlankCard/0` (`MemoryDisk`) | 310 ms, 512 MiB held | |

**Decision on §5: images stay streamed; no `[MEDIA] ImageMemoryLimit`.** A sector from the file costs about 1 µs. The
fastest guest read path (Sprinter IDE / SD, a few hundred sectors a second at most) spends under 1 ms a second in it,
under 0.1 % of the time; a memory copy would save that and cost the image's size in RAM per slot. Revisit
only if a profile of a real guest shows `RawImage::ReadSector` (C9, bulk reads, is the next place to look).

## 8. Findings after landing: full disks and memory bounds

Owner questions (2026-10-06): does a sparse disk lose to a flat one once the guest has filled it, and what happens
when a disk is larger than the host's RAM?

### 8.1 A blank medium is a session over an empty disk

`media create` of a card or hard disk wraps the `SparseMemoryDisk` in `AccessMode::Session`
(`MediaControl::Create`). Every guest write lands in the `SessionWriteMap` above it
(`std::map<lba, std::array<uint8_t, 512>>`); the sparse disk itself stays empty until the medium is saved. So for a
blank medium the cost of a full disk is the session's, not the sparse disk's.

### 8.2 Full disks, measured

`sparsemedia_benchmark.cpp`, a 256 MiB card with every sector written non-zero (Release, the same machine as §7;
peak RSS of the benchmark process holding one card):

| Device | Random read / sector | Rewrite / sector | Fill (256 MiB) | Peak RSS |
|---|---|---|---|---|
| `MemoryDisk` (flat buffer) | 53 ns | 90 ns | 192 ms | 262 MiB |
| `SparseMemoryDisk` | 263 ns | 279 ns (a zero write: 431 ns) | 71 ms | 263 MiB |
| `SessionWriteMap` over an empty `SparseMemoryDisk` (a blank medium) | 952 ns | 897 ns | 189 ms | 287 MiB |

- **Speed.** The sparse disk is about 5x slower per sector than a flat buffer (a `std::map` lookup per sector), the
  session about 18x. In absolute terms both stay at or under 1 µs a sector, the same as a sector read from an image
  file (§7). A Z80 guest moves at most a few hundred to a few thousand sectors a second, so this is under 0.1 % of
  the emulation time: not visible.
- **Memory on a full disk.** The sparse disk holds what a flat buffer holds (+0.4 %: the chunk map). The session adds
  about 10 % (a `std::map` node per sector).
- **Against C6 (before C10).** A blank medium was a session over a `MemoryDisk` allocated and zeroed at create: a
  256 MiB card cost 256 MiB at once and about 550 MiB when full. Now it costs nothing at once and about 287 MiB when
  full. C10 is not worse for full disks; it is better for every fill level.
- **Cheap improvement.** A flat table of chunk pointers instead of the `std::map` (2 GiB / 64 KiB = 32768 pointers,
  256 KiB) makes the sparse disk's lookup O(1).

### 8.3 Disks larger than RAM

| Medium | What holds memory | Bounded |
|---|---|---|
| An image file (`.img`, `.hdf`, `.hdi`, `.vhd`, `.chd`), `writethrough` | nothing per sector: reads are streamed (§7), writes go to the file | yes: a 64 GB image runs in constant memory |
| A composite (folders, images, `*.ucompose.yaml`) | the metadata: directory bytes, the run table, patched reserved sectors (`FatSynthVolume`); file contents are read from the host on demand | yes, by the tree's size |
| A blank medium (`media create`) | the session (§8.1) | by the 2 GiB cap of `create` (about 2.3 GiB when full) |
| **Any medium in `session` mode** (folders, CHDs, composites, blank media, an image inserted with `access: session`) | **every sector the guest wrote, until save or discard** | **no** |

The last row is the gap: a session over a 64 GB CHD whose guest writes 20 GB holds about 22 GB of RAM, then swaps
or is killed. In practice a ZX guest writes slowly (a Z80 over IDE: at most a few hundred KB/s, about an hour of
continuous writing per GB), but a long-running machine (a BBS, a logger, a benchmark loop, a time-travel session
replayed at turbo speed) gets there, and nothing stops it.

### 8.4 Decision (owner, 2026-10-06)

"No point holding more than 128 MB without flushing to disk." Follow-up phase **C10d** (session spill): a session
keeps at most `[MEDIA] SessionMemoryLimit` (default 128 MiB) of changed sectors in memory and moves the rest to a
spill file on disk; the reads, `Changes()`, saves and deltas see one session as before. With that, `create` can
lift its 2 GiB cap. Design: [c10d-session-spill.md](c10d-session-spill.md).
