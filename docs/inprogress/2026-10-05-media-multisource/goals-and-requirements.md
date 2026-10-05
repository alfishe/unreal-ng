# Multi-source media: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Extends** | the unified media manager, [technical-design.md](../2026-09-28-storage-manager/technical-design.md) §5 (block stack) and §6 (folder pipeline, `HostFolderFat`) |
| **Coordinates with** | media history H1-H5, [media-history-design.md](../2026-09-28-storage-manager/media-history-design.md) (the versioned change layer that will replace `SessionWriteMap`) |
| **Next** | [architecture.md](architecture.md), [tdd.md](tdd.md) |

## 0. Problem

Today every block or optical medium has **one source**. That source is one of these:

- an image file (raw, HDF, HDI, fixed VHD, CHD, ISO, CUE/BIN)
- a host folder presented as a FAT16 / FAT32 volume (`HostFolderFat`)
- a folder of audio files presented as an audio CD (`AudioFolderDisc`)
- a blank disk in memory (`MemoryDisk`)

On top of the source there is one access layer: `ReadOnlyGuard`, or the change layer
`SessionWriteMap`. The access layer is already a form of layering, but only for **guest writes**.

Users now ask for media whose contents come from **several places at once**:

1. Several host folders gathered into one disk. For example, `~/zx/games` goes into `/GAMES`,
   `~/zx/demos/2024` into `/DEMOS`, and `~/work/build/out` into `/` with only `*.com` files. The
   result is one SD card or hard disk with a FAT file system, or one CD with an ISO 9660 file system.
2. A real disk image (HDD, SD or CD) as the base, with one or more layers of host folders on top.
   For example, a NedoOS SD image plus a folder of fresh builds, without rebuilding the image after
   every compile.
3. Several disk images joined into one virtual disk. This can be one file tree merged from all of
   them, or one disk with several partitions.
4. Any combination of the above.

When the guest writes to such a medium, the changes stay in a layer. When the user asks to save or
flatten, we must know **exactly which layer, which file and which blocks** each change goes to.

## 1. Goals

| ID | Goal |
|---|---|
| G-1 | **One medium, many sources.** Build a block medium (SD, IDE disk) or an optical medium (CD) from an ordered stack of sources: host folders, FAT disk images (any partition), ISO 9660 images. |
| G-2 | **Declarative.** A descriptor file (YAML or JSON) states the target file system, size, label and layer order, with per-layer mount points, subpaths and filters. The same descriptor always gives a byte-identical medium. |
| G-3 | **Overlay semantics.** Upper layers shadow lower layers by path, as in overlayfs. Directories merge. Explicit whiteouts hide lower entries. Conflicts are reported, never silent. |
| G-4 | **Base image plus layers ("graft").** When the bottom layer is a FAT image, upper layers are grafted into its free space. The base layout is preserved byte for byte outside the touched sectors, so boot sectors and system files at fixed positions keep working. |
| G-5 | **Join disks.** Tree merge: several images into one synthesized volume. Partitions: several volumes as partitions of one disk behind a synthesized MBR. Partitions keep each source's file system, so FAT16 and FAT32 can be mixed. |
| G-6 | **Known destination for every write.** Every sector of the medium has a computable owner (layer, file, metadata region). Flatten and save report what goes where before anything is written. |
| G-7 | **A flat image is always available.** The current state, sources plus guest changes, can be exported to a plain `.img`, a fixed `.vhd` or a `.chd` with no layers. |
| G-8 | **Zero-copy translation.** File data is never copied at build time. Reads go straight from the owning source into the caller's buffer. Memory is proportional to the number of entries and extents, never to data size. |
| G-9 | **Measured.** Unit tests, real-guest acceptance tests, benchmarks for every mode, and scalability charts. |

## 2. Non-goals (this design)

| ID | Non-goal | Why / where instead |
|---|---|---|
| NG-1 | Floppy (TR-DOS, +3 DOS) and tape composition | Different media model (`DiskImage`, `TapeImage`). `FolderDiskBuilder` already covers one folder. A later extension can reuse the union tree. |
| NG-2 | File systems other than FAT12/16/32 and ISO 9660 (+ Joliet) | exFAT, NTFS, ext*, HFS are not used by ZX guests. TR-DOS images *inside* a FAT volume are plain files and work. |
| NG-3 | Raw block-level overlay of two different file systems | Two FAT volumes cannot be merged sector by sector. Their FATs and directories collide. Composition is file-level (§3, decision D-1). |
| NG-4 | Live host-folder sync (inotify / FSEvents) | Snapshot semantics stay as in `FolderSnapshot`: an explicit rescan rebuilds. |
| NG-5 | Writable CD targets | A CD is read-only to the guest. An optical composite has no write path. |
| NG-6 | JBOD (plain LBA concatenation without a partition table) | No guest use case. Partitions (G-5) cover it with a table guests understand. |
| NG-7 | GPT | ZX guests read MBR only. The partition table writer is an interface, so GPT can be added later. |
| NG-8 | Writing *into* FAT images that are lower layers, file by file | Needs a FAT writer. Strategy S3 commits whole blocks into a graft base instead ([flatten-strategies.md](flatten-strategies.md)). |

## 3. Decisions taken (owner, 2026-10-05)

| ID | Decision |
|---|---|
| D-1 | **Composition is file-level (overlayfs-like).** All layers resolve into one union tree at build time. The medium is a synthesized FAT or ISO volume whose file data points into the sources by extents. A block-level overlay is used only for guest writes. |
| D-2 | **Both ways of joining disks:** tree merge and partitions. Partitions are expected to be used much less often. |
| D-3 | **Write-back strategies are researched** ([flatten-strategies.md](flatten-strategies.md)). One strategy is mandatory: flatten to a single flat `.img` / `.vhd` file with no layers. |
| D-4 | **Guest deletes follow a per-layer delete policy** (`onDelete`), applied by S4: `keep` (default: the host file stays, a whiteout in the descriptor keeps it hidden), `trash` (the host's trash), `move` (a deleted-files folder), `delete` (removed for good), `ignore` (the host file stays and no whiteout is written, so the delete is not carried over and the file reappears on the next build). A guest delete never removes a host file unless the layer says `trash`, `move` or `delete`. |
| D-5 | **The descriptor is its own file**: `*.ucompose.yaml` / `*.ucompose.json`. The folder manifest `.unreal-media.yaml` stays a description of one folder and gets no `compose:` key. |
| D-6 | **Boot structures come from the bottom source or from a boot layer.** The bottom layer's boot structures are carried over: El Torito on an ISO (boot catalog rebuilt with the new LBAs, boot images served from the source by extent), MBR boot code, the volume boot code and reserved boot sectors on a FAT image. When the source has none, or another is wanted, a **boot layer** (`boot:` in the descriptor) lays one on top: its boot images and boot code come from files in any layer or on the host. The boot layer wins over the bottom source. |
| D-7 | **Which strategy `save` uses depends on who asks.** Automation (WebAPI, CLI, MCP, Lua, Python, config-driven eject) follows the save policy: the request's `strategy`, else the descriptor's `writes.save`, else S2 delta (non-destructive), and the result names the strategy used. Interactive saves (the Qt media panel, an eject or insert over a dirty composite in the GUI) **ask the user**: a dialog with S1-S4, the descriptor's `writes.save` preselected, and the flatten plan shown before anything is written. `export` is always S1. |
| D-8 | **Sources are changed only on an explicit request.** S3 (commit) and S4 (write-back) run only from an explicit `flatten`, or a `save` / eject disposition whose request names `strategy: commit` / `write-back`, or the user's choice in the GUI dialog. A `writes.save: commit` / `write-back` in the descriptor alone never fires on eject: that eject saves as S2 delta and the report says why. |
| D-9 | **Phase order**: the read side first (C1-C5); then attribution (`media changes`, read-only) with S1 and S2 (C6); partitions (C7); S3 and S4 last (C8). |

Every policy (merge, names, build strategy, file system, boot, attribution, save, delete,
conflicts, delta, commit, eject, rescan) is drawn as a decision tree; the map of all trees is
[architecture.md](architecture.md) §12.

## 4. Actors and use cases

| ID | Actor | Use case |
|---|---|---|
| UC-1 | Developer | Cross-builds Z80 programs on the host. Mounts `build/out` over a NedoOS SD image. Each rescan picks up new binaries without rebuilding the 2 GB image. |
| UC-2 | Collector | Gathers games from several host folders into one FAT32 SD card for Wild Commander, `*.trd`, `*.scl` and `*.tap` only, with readable 8.3 names. |
| UC-3 | Sprinter / Profi user | Boots DSS or PQ-DOS from a real HDD image. Adds a host folder of utilities under `/UTIL` without touching the image. Saves the session; later flattens into a new image. |
| UC-4 | CD author | Builds an ISO 9660 + Joliet CD from three folders to test a CD player or a CD-booting loader (ZX-Evo "D. CD boot", `AUTORUN.ZX`). |
| UC-5 | Archivist | Joins `dos.img` (FAT16) and `data.img` (FAT32) as two partitions of one IDE disk, as a real machine had them. |
| UC-6 | Automation / CI | Composes media from a descriptor in a test, runs the guest, then asks *which files changed, and in which layer* through the API. |

## 5. Functional requirements

### 5.1 Descriptor and sources

| ID | Requirement |
|---|---|
| FR-1 | A **composition descriptor** (`*.ucompose.yaml` or `*.ucompose.json`) is a media source in its own right (`MediaSourceType::Composite`). It is accepted by `media insert` on every surface and by the `[MEDIA]` config. |
| FR-2 | The descriptor sets the **target**: kind (`block` / `optical`), file system (`auto` / `fat16` / `fat32` / `iso9660`), size or free room, label, code page, partition scheme (`mbr` / `none`) and build strategy (`rebuild` / `graft` / `auto`). The file system obeys the slot's `fsCompatibility` and `defaultFs` exactly as folder volumes do; `auto` is resolved by the rule in [fs-compatibility.md](fs-compatibility.md) §6. |
| FR-3 | **Layers** are listed bottom first. Each has a source (`folder`, `image` with an optional `partition`, `iso`), an optional `from` subpath inside the source, a `mount` path in the target, `include` / `exclude` wildcards, a `conflict` policy, `whiteout` paths and an `opaque` directory list. |
| FR-4 | Paths in a descriptor are relative to the descriptor file. `~` expands to the home folder. Absolute paths are allowed. Nothing in the descriptor is machine specific unless the user writes it. |
| FR-5 | **Partitions** mode lists partitions instead of layers. Each partition is a passthrough of an image's partition or a nested composition. |
| FR-6 | A folder layer honors the folder's own `.unreal-media.yaml` manifest (`exclude`, `order`, per-file names). The descriptor's settings win over the manifest's. |
| FR-7 | Inline descriptors: the WebAPI, MCP and scripting surfaces accept the descriptor body as JSON instead of a file path. |

### 5.2 Union semantics

| ID | Requirement |
|---|---|
| FR-10 | Upper layers shadow lower ones. A file shadows a file or a directory at the same target path. A directory merges with a directory unless it is `opaque` in the upper layer. |
| FR-11 | Paths are compared under the **target file system's equivalence**. FAT compares case-insensitively, after long-name and 8.3 mapping. ISO compares the Joliet name case-sensitively and the ISO name in upper case. `Readme.txt` in one layer and `README.TXT` in another are the same target entry. |
| FR-12 | Every shadowing, whiteout, filter exclusion, name change and skip is listed in the medium's **build report**, grouped per layer. |
| FR-13 | The `conflict` policy per layer: `shadow` (default) means the upper entry wins. `keep-lower` means the lower entry wins. `error` means any shadowing fails the build. |
| FR-14 | Entry order inside a directory is deterministic: the descriptor's `order` for that directory first, then directories, then files, byte-wise by UTF-8 name. Two hosts give the same bytes. |

### 5.3 Validation and compatibility

| ID | Requirement |
|---|---|
| FR-20 | The build validates the whole union against the target: volume size, cluster-count range per FAT type, FAT16 root-directory entries, entries per directory, maximum file size, path depth, ISO name and depth rules. A violation fails with `DoesNotFit` and names the entry and the limit. |
| FR-21 | Names are converted to the target. FAT: long name plus unique 8.3 name in the chosen code page (CP866 / CP1251), with `FatNameMapper`'s rules. ISO: Level 1 (or 2) ISO name plus a Joliet name. Unconvertible names are reported, and the entry is skipped or renamed according to the descriptor. |
| FR-22 | Timestamps are clamped to the target's range: FAT 1980-2107 with 2-second resolution, ISO 1900-2155. Attributes are mapped (FAT read-only / hidden / system ↔ ISO hidden). |
| FR-24 | **Boot carry-over and boot layers** (D-6). ISO target: a bootable bottom ISO layer keeps its El Torito entries (platform ids, emulation types, load segments, sector counts; images read from the source by extent); a `boot.eltorito` list adds or replaces entries, each naming an image by target path (a file of the union) or host path, with emulation (`none` / `floppy` / `hdd`), load segment, sector count and platform. FAT target: the bottom FAT image's MBR boot code (bytes 0-445), volume boot code (outside the BPB fields the builder writes) and reserved boot sectors are carried; `boot.mbrCode`, `boot.volumeCode` and `boot.reserved` (`{lba, file}` ranges, for example the DSS loader at LBA 1-3) replace them from files. Boot data that cannot be carried (a bootable ISO in an upper layer, an El Torito catalog into a FAT target, boot code larger than its area) is reported. |
| FR-23 | Only supported combinations build. The matrix of source and target file systems is in [fs-compatibility.md](fs-compatibility.md). Any other combination fails with `NotSupported` and the reason. |

### 5.4 Build strategies

| ID | Requirement |
|---|---|
| FR-30 | **Rebuild**: a fresh FAT16, FAT32 or ISO volume is synthesized from the union tree, laid out like `HostFolderFat` today (contiguous runs, FAT computed per sector). |
| FR-31 | **Graft**: the bottom layer is a FAT image. Its layout is kept. Upper-layer files are placed in its free clusters, and its directories and FAT are patched in a sparse sector map. Unpatched sectors read straight from the base. |
| FR-32 | **Auto** (default): graft when the bottom layer is a FAT image whose FAT type equals the target's and its free space holds the upper layers; otherwise rebuild. The choice is in the report. |
| FR-33 | **Partitions**: a synthesized MBR (with an EBR chain beyond four partitions) maps partitions to child devices. Each child is an image's partition passed through, or a composed volume. |
| FR-34 | `HostFolderFat` today becomes a rebuild composite with one folder layer. Its output stays **byte-identical**, and existing configs and tests are unchanged. |

### 5.5 Writes, provenance, flatten

| ID | Requirement |
|---|---|
| FR-40 | Guest writes go to **one change layer** on top of the whole composite: `SessionWriteMap` today, `MediaChangeLayer` after H1. Sources never change during a session. |
| FR-41 | **Provenance**: for any sector, the medium answers who owns it: MBR / boot / FSInfo / FAT / directory of a given folder / data of a given file (layer, source, offset) / grafted base sector / free space. |
| FR-42 | **Change attribution**: the medium answers which files were created, modified, deleted or renamed, and in which layer each lives. It works from the changed sectors and a re-read of the merged volume. Available read-only at any time (`media changes <slot>`). |
| FR-43 | **Flatten to a flat image** (mandatory, S1): export sources plus changes as one raw `.img`, fixed `.vhd` or `.chd`. Optional `--compact` re-synthesizes a defragmented volume from the merged tree. |
| FR-44 | **Persisted session** (S2): the change layer is saved next to the descriptor and restored on the next insert when the composite's content id matches. Otherwise the user is told why it cannot be applied. |
| FR-45 | **Commit into the graft base** (S3): when the base is opened writable, the patched metadata, the changed sectors and the grafted file data are written into the base image. Every overwritten sector is journaled first. |
| FR-46 | **File-level write-back** (S4, opt-in, folder layers only): file operations are routed to layers by the policy in [flatten-strategies.md](flatten-strategies.md). There is always a dry-run plan first. A host file that changed since the snapshot is a conflict, never overwritten. |
| FR-47 | Eject and insert dispositions (`save` / `export` / `discard`) work on composites as on any block medium. `save` picks its strategy per D-7: the policy (request `strategy` → `writes.save` → S2) for automation, a strategy dialog for interactive use. |

### 5.6 Integration

| ID | Requirement |
|---|---|
| FR-50 | Composites live in slots like any medium. Selectors, dispositions, model switch (M5), the TTD recording guard and `MediaReadTap` journaling all work unchanged. |
| FR-51 | `ContentId` of a composite is a hash of the normalized descriptor, every layer's identity (folder snapshot identity, image content id) and the build options. Snapshots and TTD refer to it like any medium. |
| FR-52 | The "same source in two slots" rule (G5 of the storage design) applies per layer source. An image used read-only as a layer may also be in another slot read-only. An image that is a graft base being committed (S3) is exclusive. |
| FR-53 | Surfaces: `media compose` (validate and build a descriptor without inserting; prints the report and the layout), `media layers <slot>`, `media changes <slot>`, `media flatten <slot> --strategy … [--plan]`. Available on the WebAPI with OpenAPI, CLI, MCP, Lua and Python. The Qt media panel shows layers and changes. |
| FR-54 | `rescan` rebuilds from fresh folder snapshots. With pending guest changes it follows the dirty rules (refuse, or a disposition). |

## 6. Non-functional requirements

### 6.1 Performance

The emulator calls `IBlockDevice::ReadSector` from the emulation thread, inside a frame. Every
nanosecond counts during DMA-like bulk reads (ATA READ MULTIPLE, SD multi-block, CD READ (10)).

| ID | Requirement | Measured by |
|---|---|---|
| NFR-P1 | **Parity**: a composite with one folder layer reads data sectors no slower than `HostFolderFat` today (median ≤ 1.10×) and metadata sectors no slower (≤ 1.05×). | `BM_ComposeRead/parity` vs `BM_HostFolderFatRead` (A/B per performance guidelines §4) |
| NFR-P2 | **Layer count does not matter at read time**: layers are resolved at build time. Read latency at 64 layers ≤ 1.15× latency at 1 layer. | chart C2 |
| NFR-P3 | **Zero copies on the read path**: data sectors go from the source straight into the caller's buffer. No intermediate buffer, no per-read heap allocation (verified with `HeapCounter`). | `ComposeRead_Test.NoHeapAllocationPerRead`, benchmark counters |
| NFR-P4 | Lookup cost O(log runs) per sector, with an O(1) fast path for sequential reads (last-hit cache). | chart C3 |
| NFR-P5 | **Build time** O(n log n) in entries; ≤ 1.5 s for 100 000 entries (scan excluded) on the dev host. | chart C1 |
| NFR-P6 | **Graft build time** independent of base size beyond one FAT scan (free-cluster bitmap). It grows with the upper layers' entries and the touched base directories only. | chart C6 |
| NFR-P7 | **Zero cost for media that are not composites**: no new virtual hop, branch or check on `RawImage`, `HostFolderFat`-as-before or CHD paths. Machines without block media pay nothing. | A/B on `chdimage_benchmark`, `zcontrollerspi_benchmark` |
| NFR-P8 | Flatten / export streams with bounded memory (≤ 16 MiB working set whatever the disk size) at ≥ 80% of a raw-to-raw copy's throughput for `.img`. | chart C7 |
| NFR-P9 | Change attribution for 10 000 changed sectors on a 100 000-entry volume ≤ 500 ms. | chart C7 |

### 6.2 Memory

| ID | Requirement |
|---|---|
| NFR-M1 | No file data is ever held in memory by the composite (directories and patched metadata sectors only). |
| NFR-M2 | Per entry ≤ 128 bytes of tree and layout state plus its name bytes. Per extent 16 bytes. Directory clusters as the target needs them (32 bytes per FAT entry and long-name slot). 100 000 entries in ≤ 32 MiB in total. |
| NFR-M3 | One opened instance per source, shared by every layer and partition that names it (`SourcePool`), so CHD hunk caches and file handles are not duplicated. |
| NFR-M4 | At most `kMaxOpenFiles` (8 today) host files open per composite, shared across all its folder layers. |
| NFR-M5 | Fragmented FAT sources: extents are coalesced. A per-composite extent budget (default 4 M extents = 64 MiB) fails the build with `DoesNotFit` and names the most fragmented files rather than exhausting memory. |

### 6.3 Determinism, safety, portability

| ID | Requirement |
|---|---|
| NFR-S1 | Same descriptor, same source identities, same options → byte-identical medium on every host (fixed serials, sorted orders, UTC times). |
| NFR-S2 | Nothing is written to any source except by an explicit S3 or S4 flatten. S3 and S4 always have a dry-run plan, a journal and temp-file-then-rename writes. |
| NFR-S3 | A source that shrank or vanished after the build reads zeros and is reported (as `HostFolderFat` does today). It never crashes and never reads out of bounds. |
| NFR-S4 | Cross-platform: path handling with `std::filesystem` and UTF-8 everywhere. Windows, macOS and Linux. gcc, clang, MSVC, mingw. Zero warnings. |
| NFR-S5 | Descriptor parsing never aborts. Unknown keys and bad values are reported. `rapidyaml` is used with the throwing handler, like `FolderManifest`. |

## 7. Acceptance

| ID | Scenario | Pass when |
|---|---|---|
| ACC-C1 | ZX-Evo `sd.zc` from a descriptor: NedoOS SD folder + a second folder with `term.com` replaced (upper shadows lower) | NedoOS boots to `M:/bin>`; the replaced `term.com` runs (its version string) |
| ACC-C2 | Wild Commander (ZX-Evo) on a FAT32 composite of 3 folders with filters | WC lists exactly the filtered files under each mount, with the expected 8.3 names |
| ACC-C3 | Sprinter: DSS 1.71 HDD image (graft base) + host folder at `/UTIL` | DSS boots; `DIR C:\UTIL` lists the folder; a file written by the guest appears in `media changes` attributed to the right layer; a FAT32 target on a Sprinter slot is refused |
| ACC-C4 | Profi: PQ-DOS image as partition 1 + a composed FAT16 as partition 2; then the same with a FAT32 partition 2 | PQ-DOS sees both drives; the FAT32 run records whether PQ-DOS reads FAT32, and the Profi slot descriptor is set from that result |
| ACC-C5 | ATAPI CD on ZX-Evo: ISO composite of two folders | NedoOS lists the CD contents; `AUTORUN.ZX` boot works from the ERS menu |
| ACC-C6 | Flatten S1: the ACC-C3 session exported to `.img` and `.vhd` | the exported image boots alone (no layers) and `FatVolumeReader` shows the same tree as the live medium |
| ACC-C7 | Persisted session S2 | after a restart the guest sees its earlier writes; changing a source makes the delta refuse with a clear reason |
| ACC-C8 | Benchmarks and charts | every NFR-P row has a measured value in [test-and-benchmark-plan.md](test-and-benchmark-plan.md) §5 "results" and the charts C1-C8 are generated by the plot script |
