# C7 — partitioned composites

**Status:** built 2026-10-06 (as-built notes in §7). Phase C7 of [tdd.md](../tdd.md) §14 (§8 there; the descriptor's partitions variant in
§1.1). Exit: ACC-C4 (Profi: PQ-DOS on partition 1, a composed partition 2), fs-compatibility S-5 (a FAT16 and a FAT32
partition on one disk).

## 1. Descriptor

```yaml
version: 1
target: {size: 3GiB}                       # optional: the disk; default: the end of the last partition
partitions:
  - {name: dos, source: {image: pqdos.img, partition: 1}}          # passthrough: the source's sectors and type byte
  - {name: data, fs: fat16, size: 64MiB, compose: {layers: [{source: {folder: ./data}}]}}
  - {name: dos2, compose: {build: graft, layers: [                   # a composed partition over an image partition
        {source: {image: pqdos.img, partition: 1}}, {source: {folder: ./patch}}]}}
  - {name: big, fs: fat32, type: 0x0C, compose: {layers: [{source: {folder: ./big}}]}}
```

| Key (per partition) | Meaning |
|---|---|
| `name` | for reports, `media layers` and `media changes` (default `p1`, `p2`, ...) |
| `source` | passthrough: `{image: path, partition: n}` (no `partition`: the whole image, a superfloppy). Exclusive with `compose` |
| `compose` | a composition without `version`: `layers`, `boot`, a `target` map, or target keys given directly (`build`, `free`, `label`, `codepage`, `fixedTime`) |
| `fs`, `size`, `label` | shorthands for the composed partition's `target.fs`, `target.size`, `target.label` |
| `type` | the MBR type byte; default: the passthrough source's own, or from the file system (§3) |

A composed partition is built through the same factory as a whole composite (DT-4 for rebuild / graft) with
`partition: none`. Unset `fixedTime` and `codepage` come from the outer `target`. The normalized form, and so the
content id, includes every partition.

## 2. `PartitionedDisk`

```cpp
class PartitionedDisk : public IBlockDevice
{
public:
    struct Part
    {
        std::string name;
        uint8_t type = 0;
        uint64_t sectors = 0;                    ///< the partition's size
        std::shared_ptr<IBlockDevice> device;    ///< its volume at its own LBA 0 (may be shorter: zeros after it)
        uint64_t start = 0;                      ///< set by Build
    };
    static std::unique_ptr<PartitionedDisk> Build(std::vector<Part> parts, std::optional<uint64_t> totalSectors,
                                                  std::string* error);
    const std::vector<Part>& Parts() const;
    std::vector<uint64_t> TableSectors() const;  ///< the MBR and every EBR
};
```

- **Layout.** Every partition starts on a 1 MiB boundary (LBA 2048, ...), in the order listed. Up to four:
  four primary entries. More: the first three primary, the rest logical in an extended partition (type `#0F`, the
  fourth MBR entry): each logical partition has its EBR 1 MiB before it (entry 1: the partition relative to the EBR;
  entry 2: the next EBR relative to the extended partition's start, type `#05`). A `target.size` below the layout
  fails with `DoesNotFit`.
- **Table sectors** are synthesized: no boot code, a disk signature from the content id, CHS from LBA with
  255 heads / 63 sectors (capped at 1023/254/63 when out of reach), `#55AA`.
- **Reads.** A table sector, a partition's sector from its device (zeros past the device's end: a cut-down image whose
  partition is larger than the file, as `pqdos-hdd-small.img`), zeros in the gaps.
- **Hidden sectors.** A FAT boot sector (and FAT32's backup at +6) read at a partition's start gets its BPB
  hidden-sectors field (offset 28) set to the partition's start LBA: a passthrough partition moved to another LBA,
  and a composed one built at LBA 0, both boot as DOS expects.
- **Writes** fail (read-only, as every composite: guest writes go to the change layer above).
- **Content id:** the children's ids, starts, sizes and types.

## 3. Types

| File system | Type |
|---|---|
| FAT16, below 32 MiB | `#04` |
| FAT16 | `#06`; `#0E` when the partition ends beyond CHS reach (LBA 16 450 560) |
| FAT32 | `#0B`; `#0C` beyond CHS reach |
| FAT12 (a passthrough superfloppy) | `#01` |
| Passthrough with a partition number | the source entry's type byte |

## 4. Composed partitions over an image partition

A graft over `{image: x.img, partition: n}` is a `GraftVolume` in the image's coordinates (its MBR stays at LBA 0).
The partition's device is a `SubRangeDevice` over it from the volume's start, as long as the volume's BPB says.
`GraftVolume` reports `max(image size, volume end)` sectors and reads zeros past the image's end, so a cut-down
image (the BPB says 2 GB, the file holds 3 MB) works, for a graft as for a passthrough.

## 5. Info, layers, changes, delta

- `CompositeInfo` gains `partitions` (name, kind `image` / `compose`, type, start, sectors, fs, build, first layer).
  `layers` lists every partition's layers with the partition's name in front (`dos2/layer0`); a passthrough
  partition is one layer of kind `image`. `build` is `partitions`, `fsName` `mbr`, the format `compose-mbr`.
- `media changes` on a partitioned composite runs the attribution per FAT partition over a window of the disk
  before and after the writes (the change map shifted to the partition), with the child's layout when it has one
  (offset by its volume start for a graft over an image partition); paths read `dos2:/ACC4`. Writes to a table
  sector are a warning.
- S1 (`export`), S2 (delta) and `compact` work on the whole disk unchanged; `compact` refuses a partitioned disk
  (it re-synthesizes one FAT volume).

## 6. Tests

| Test | Checks |
|---|---|
| `PartitionedDisk_Test.MbrEntriesAndTypes` | starts aligned, sizes, types per §3, CHS, signature, `#55AA`; the gaps read zeros; writes fail |
| `PartitionedDisk_Test.LogicalPartitionsInAnEbrChain` | six partitions: three primary, an extended one, three logical; the EBR chain walked from the MBR |
| `PartitionedDisk_Test.HiddenSectorsFollowTheStart` | a FAT16 and a FAT32 volume built at LBA 0: their boot sectors (and the FAT32 backup) read with the partition's start |
| `PartitionedDisk_Test.ShortDeviceReadsZeros` | a partition larger than its device |
| `ComposePartitions_Test.DescriptorParsesAndNormalizes` | keys, defaults, errors (`source` and `compose` both; neither; `layers` beside `partitions`) |
| `ComposePartitions_Test.Fat16AndFat32OnOneDisk` (S-5) | a passthrough FAT16 image partition and a composed FAT32 one: `FatVolumeReader` reads both through `FindPartition` |
| `ComposePartitions_Test.GraftOverAnImagePartition` | a graft over partition 1 of an MBR image, as partition 1 of the new disk |
| `ComposePartitions_Test.ChangesPerPartition` | guest writes in two partitions: `media changes` names both with their partitions |
| `ComposePartitions_Test.DeltaRoundTrip` | S2 on a partitioned composite |
| ACC-C4 `ProfiPlusComposed_Test.Fat16SecondPartition` / `.Fat32SecondPartitionRefused` | PQ-DOS (`pqdos-hdd-small.img` partition 1, grafted with an `AUTOEXEC.BAT` that runs `md d:\acc4` and DOS Navigator) plus a composed FAT16 partition 2: PQ-DOS boots, and the directory it made is on partition 2. The same with a FAT32 partition 2 records whether PQ-DOS reads FAT32; the Profi slot's `fsCompatibility` follows the result |

## 7. As built

| Piece | Where | Notes |
|---|---|---|
| Descriptor | `ComposePartition`, `Reader::Partitions` / `Composition` (`composedescriptor.cpp`) | `Target()` was split into `TargetKey()` so that a `compose` map takes target keys directly. A composed partition gets `partition: none` and kind `block`; `fs`, `size` and `label` at the partition's level land in its target. A partition with both `source` and `compose`, or with neither, a passthrough that is not an image, and a `compose` without layers are errors. `Normalized()` lists the partitions, a composed one with its own normalized form. |
| `PartitionedDisk` | `io/storage/partitioneddisk.{h,cpp}` | As in §2. Types come from `fatBits` when the part has none (§3). The first primary partition is marked active (#80). `SetMbrCode` puts a source's MBR code (bytes 0-445) in front of the table; without it the MBR has no code and a disk signature from the content id. |
| MBR code (D-6) | `CompositeMediumFactory::BuildPartitioned` | The descriptor's `boot.mbrCode` (a host file, at most 446 bytes), else the code of the first source image with an MBR: a passthrough partition's image, or the bottom image layer of a composed partition over an image partition. The Profi BIOS runs the MBR's Z80 loader: without it PQ-DOS does not start. Report: "boot: MBR code carried from X". |
| Cut-down images | `SubRangeDevice`, `FatVolumeReader::FindPartition`, `GraftVolume` | A partition may run past the end of its image file (`pqdos-hdd-small.img`: the BPB says 1936 MB, the file holds 2.9 MB). `FindPartition` only needs it to start inside the file. A `SubRangeDevice` reads zeros past the base's end and refuses writes there. A `GraftVolume` reports `max(image, volume end)` sectors and reads zeros past the file. `FatVolumeReader::VolumeSectors()` gives the BPB's size. |
| Graft over an image partition | `BuildPartitioned` | The child is a `SubRangeDevice` over the graft from `GraftVolume::VolumeStart()`, sized `VolumeSectors()`; its layout goes to the part with that offset (`OffsetLayout`) for `media changes`. |
| Info | `CompositeInfo::partitions` (`CompositePartitionInfo`), `layers` / `compose` replies (`partitions` array), CLI text | `build` is `partitions`, `fsName` `mbr`, the format `compose-mbr`. The layers of every partition are listed under the partition's name (`data/files`); a passthrough partition is one layer of kind `image`, whose identity is its window's content id. The content id is the disk's own mixed with the normalized descriptor. |
| `media changes` | `media/mediachanges.{h,cpp}` (`ListMediumChanges`, moved out of `MediaControl` so that it can be tested without a machine) | Per FAT partition, as in §5; a written table sector is a warning. |
| `compact` | `BlockFormats::Compact` | Refused when the MBR has more than one used entry. |
| Profi slots | `IdeController` slot descriptors | ACC-C4's FAT32 run: PQ-DOS booted and left the FAT32 partition alone, so the Profi IDE hard disk slots take `fsCompatibility = {Fat16}` as Sprinter's do: FAT32 folders, composites and images are refused with the reason (fs-compatibility.md §6). |

Tests: `PartitionedDisk_Test` (4), `ComposePartitions_Test` (4: the descriptor; S-5 FAT16 passthrough + FAT32 composed;
a graft over an image partition placed second, its boot sector naming its new start; changes and the delta per
partition, compact refused). ACC-C4: `ProfiPlusComposed_Test.Fat16SecondPartition` boots PQ-DOS 2023-09 from partition
1, a graft over `pqdos-hdd-small.img` partition 1 with an `AUTOEXEC.BAT` that runs `md d:\acc4` and DN; the directory
is on the composed FAT16 partition 2 afterwards (~1.3 s). `.Fat32SecondPartitionRefused`: the same disk with a FAT32
partition 2 is refused at insert.
