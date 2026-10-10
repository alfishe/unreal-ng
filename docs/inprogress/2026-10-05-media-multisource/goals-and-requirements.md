# Multi-source media: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-05; review round 1 2026-10-09 |
| **Status** | Built and merged (C0-C11, [phases/README.md](phases/README.md)); requirements checked against the as-built record, round 1 |
| **Extends** | the unified media manager, [technical-design.md](../2026-09-28-storage-manager/technical-design.md) §5 (block stack) and §6 (folder pipeline, `HostFolderFat`) |
| **Coordinates with** | media history H1-H5, [media-history-design.md](../2026-09-28-storage-manager/media-history-design.md) (the versioned change layer that will replace `SessionWriteMap`; not built yet, so `SessionWriteMap` is still the change layer) |
| **Next** | [architecture.md](architecture.md), [tdd.md](tdd.md); as built: [phases/](phases/README.md), [benchmarks/README.md](benchmarks/README.md), [TODO.md](TODO.md), user doc [media.md](../../features/media.md#composite-media-several-sources-in-one-disk) |

## Review round 1 (2026-10-09)

**Status:** every goal is met. 7 requirements are met only in part:

- FR-6, FR-14 and FR-21 each lack one option;
- FR-45 cannot commit a partitioned disk;
- FR-54 takes no disposition;
- NFR-M5 has no extent budget;
- ACC-C5 lacks its NedoOS half.

5 are changed by later decisions: FR-23, FR-32, NFR-S2, D-4 and D-8. The NFR numbers are from C11
([benchmarks/README.md](benchmarks/README.md), measured 2026-10-09). Still open: the owner's check on macOS / Windows
(the Qt flatten dialog, `HostTrash`, the journal's positional I/O) and the P2 items in [TODO.md](TODO.md).

What changed in this document:

- A **Status** column in §1, §3 and §5-§7: met / partly / not done / changed, with the phase that records it.
- §3.1 new: the owner decisions and findings made after 2026-10-05 (C4 mount-point times, FAT16-only Sprinter and
  Profi slots, C9 dropped, C10c streamed images, C10d / C10e session limits, the 2026-10-07 lean defaults).
- §0 and FR-40: the change layer as built. `SessionWriteMap` now keeps up to 16 MiB in arenas. The rest goes to a
  temp file in `SpillFolder`, or to a journal `<source>.usession` when `SessionJournal = on`, which is off by default.
- New in the text: the file names next to a descriptor or image (`.delta`, `.delta.bad`, `.whiteout`, `.attributes`,
  `.writeback`, `.unreal-staging-<n>`, `<base>.ujournal`, `.usession`, `.usession.<n>.stale`) and the option names
  (`writable`, `onDelete`, `deletedFolder`, `writes.upper`, `writes.delta`, `onBadName`, `fixedTime`, `iso`, `vhd`,
  `compact`, `plan`, `onConflict`, `journal`).
- Corrected:
  - FR-32: the rule `auto` follows.
  - FR-23: the refusals are `BadRequest`, not `NotSupported`.
  - FR-22: an ISO time is clamped to 1900-2099.
  - NFR-P3: the test's real name.
  - NFR-S2: S3 writes in place under an undo journal.
  - D-4: a trash failure surfaces at apply, not in the plan.
  - D-8: a plain `save`, an eject with `save` and the emulator closing follow `writes.save` (2026-10-09).
- Gaps found, not in [TODO.md](TODO.md) before:
  - the descriptor's per-directory `order` (FR-14) and `onBadName: replace` (FR-21) are parsed but not applied;
  - a folder layer takes only `exclude` from its manifest (FR-6);
  - there is no extent budget (NFR-M5);
  - a partitioned composite cannot be committed (FR-45).

## 0. Problem

As of 2026-10-05, before this work, every block or optical medium had **one source**. That source was one of these:

- an image file (raw, HDF, HDI, fixed VHD, CHD, ISO, CUE/BIN)
- a host folder presented as a FAT16 / FAT32 volume (`HostFolderFat`)
- a folder of audio files presented as an audio CD (`AudioFolderDisc`)
- a blank disk in memory (`MemoryDisk`; a blank card or disk is now a `SparseMemoryDisk`, C10a)

On top of the source there is one access layer: `ReadOnlyGuard`, or the change layer `SessionWriteMap`. The access
layer is already a form of layering, but only for **guest writes**. Since C10d / C10e, `SessionWriteMap` keeps at most
`[MEDIA] SessionMemoryLimit` (16 MiB) in 1 MiB arenas. The rest goes to a temp file in `SpillFolder`, or to a journal
`<source>.usession` with `SessionJournal = on`, which is off by default
([c10e-session-journal.md](phases/c10e-session-journal.md) §9).

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

| ID | Goal | Status |
|---|---|---|
| G-1 | **One medium, many sources.** Build a block medium (SD, IDE disk) or an optical medium (CD) from an ordered stack of sources: host folders, FAT disk images (any partition), ISO 9660 images. | met: folders ([C2](phases/c2-composite-descriptor.md)), FAT images ([C3](phases/c3-image-sources.md)), ISO / CUE / CD CHD and ISO targets ([C5](phases/c5-iso.md)) |
| G-2 | **Declarative.** A descriptor file (YAML or JSON) states the target file system, size, label and layer order, with per-layer mount points, subpaths and filters. The same descriptor always gives a byte-identical medium. | met (C2). Byte-identical across hosts needs `target.fixedTime`; without it, times come from the sources |
| G-3 | **Overlay semantics.** Upper layers shadow lower layers by path, as in overlayfs. Directories merge. Explicit whiteouts hide lower entries. Conflicts are reported, never silent. | met (`UnionBuilder`, [C1](phases/c1-core-and-parity.md)). S4 adds whiteouts from `<descriptor>.whiteout` |
| G-4 | **Base image plus layers ("graft").** When the bottom layer is a FAT image, upper layers are grafted into its free space. The base layout is preserved byte for byte outside the touched sectors, so boot sectors and system files at fixed positions keep working. | met ([C4](phases/c4-graft.md)); [C4b](phases/c4b-lazy-graft-base.md): only the base directories the upper layers reach are read |
| G-5 | **Join disks.** Tree merge: several images into one synthesized volume. Partitions: several volumes as partitions of one disk behind a synthesized MBR. Partitions keep each source's file system, so FAT16 and FAT32 can be mixed. | met (C3, [C7](phases/c7-partitions.md)). The slot's FS rule still applies: Sprinter and Profi IDE disks take FAT12 / FAT16 only |
| G-6 | **Known destination for every write.** Every sector of the medium has a computable owner (layer, file, metadata region). Flatten and save report what goes where before anything is written. | met: `IComposedLayout::OwnerOf` ([C6](phases/c6-provenance-flatten.md) §7). `plan` covers S3 / S4. S1 and S2 write only new files and have no plan |
| G-7 | **A flat image is always available.** The current state, sources plus guest changes, can be exported to a plain `.img`, a fixed `.vhd` or a `.chd` with no layers. | met (C6a); also a dynamic `.vhd` (`vhd: dynamic`, [C10b](phases/c10-sparse-memory.md) §7) |
| G-8 | **Zero-copy translation.** File data is never copied at build time. Reads go straight from the owning source into the caller's buffer. Memory is proportional to the number of entries and extents, never to data size. | met: no allocation per read; 16.6 MiB at 100 000 entries (C11) |
| G-9 | **Measured.** Unit tests, real-guest acceptance tests, benchmarks for every mode, and scalability charts. | met ([benchmarks/README.md](benchmarks/README.md), charts C1-C8); ACC-C5 in part (§7) |

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
| NG-8 | Writing *into* FAT images that are lower layers, file by file | Needs a FAT writer. Strategy S3 commits whole blocks into a graft base instead ([flatten-strategies.md](flatten-strategies.md)). As built, S4 copies a lower image's changed file up into a writable folder layer. |

## 3. Decisions taken (owner, 2026-10-05)

| ID | Decision | Status |
|---|---|---|
| D-1 | **Composition is file-level (overlayfs-like).** All layers resolve into one union tree at build time. The medium is a synthesized FAT or ISO volume whose file data points into the sources by extents. A block-level overlay is used only for guest writes. | as decided |
| D-2 | **Both ways of joining disks:** tree merge and partitions. Partitions are expected to be used much less often. | built (C3, C7) |
| D-3 | **Write-back strategies are researched** ([flatten-strategies.md](flatten-strategies.md)). One strategy is mandatory: flatten to a single flat `.img` / `.vhd` file with no layers. | built: S1 C6a, S2 C6c, S3 C8a, S4 C8b + [C8d](phases/c8d-writeback-tails.md) |
| D-4 | **Guest deletes follow a per-layer delete policy** (`onDelete`), applied by S4: `keep` (default: the host file stays, a whiteout in the descriptor keeps it hidden), `trash` (the host's trash), `move` (a deleted-files folder), `delete` (removed for good), `ignore` (the host file stays and no whiteout is written, so the delete is not carried over and the file reappears on the next build). A guest delete never removes a host file unless the layer says `trash`, `move` or `delete`. | changed in detail: the whiteout goes to `<descriptor>.whiteout`, never into the descriptor ([C8](phases/c8-commit-writeback.md) §3). `move` puts the file in `<deletedFolder>/<UTC time>/<layer>/<path>`; `deletedFolder` defaults to `.deleted` next to the descriptor. `trash` is `HostTrash` (C8d §2): a failing trash stops the apply, and the next insert retries it. It is not a plan error (C8d §6) |
| D-5 | **The descriptor is its own file**: `*.ucompose.yaml` / `*.ucompose.json`. The folder manifest `.unreal-media.yaml` stays a description of one folder and gets no `compose:` key. | built; `*.ucompose.yml` too. The machine-owned sidecars next to it: `.delta`, `.whiteout`, `.attributes`, `.writeback`, `.usession` |
| D-6 | **Boot structures come from the bottom source or from a boot layer.** The bottom layer's boot structures are carried over: El Torito on an ISO (boot catalog rebuilt with the new LBAs, boot images served from the source by extent), MBR boot code, the volume boot code and reserved boot sectors on a FAT image. When the source has none, or another is wanted, a **boot layer** (`boot:` in the descriptor) lays one on top: its boot images and boot code come from files in any layer or on the host. The boot layer wins over the bottom source. | built (C5b, C5 §10). Also carried: the sectors between the MBR and the partition (the DSS loader, C6 §6), and on a partitioned disk the first source's MBR code (C7 §7) |
| D-7 | **Which strategy `save` uses depends on who asks.** Automation (WebAPI, CLI, MCP, Lua, Python, config-driven eject) follows the save policy: the request's `strategy`, else the descriptor's `writes.save`, else S2 delta (non-destructive), and the result names the strategy used. Interactive saves (the Qt media panel, an eject or insert over a dirty composite in the GUI) **ask the user**: a dialog with S1-S4, the descriptor's `writes.save` preselected, and the flatten plan shown before anything is written. `export` is always S1. | built. A `save` with a path and no `strategy` is S1 (C6 §8). The Qt dialog (C8 §7) is coded; the owner's build and check on macOS / Windows / Linux is pending ([TODO.md](TODO.md)) |
| D-8 | **Sources are changed only on an explicit request.** S3 (commit) and S4 (write-back) run only from an explicit `flatten`, or a `save` / eject disposition whose request names `strategy: commit` / `write-back`, or the user's choice in the GUI dialog. A `writes.save: commit` / `write-back` in the descriptor alone never fires on eject: that eject saves as S2 delta and the report says why. | changed 2026-10-09 (owner): **a medium that leaves follows the policy**. One rule for `save`, an eject / swap / insert / rescan disposition `save` and the emulator closing: the request's `strategy`, else `writes.save` (`commit` and `write-back` included), else S2. `writes.save` also takes `discard` (a medium that leaves drops its writes; a plain `save` refuses) and `ask` (the GUI asks; automation saves S2). A commit or write-back that fails during a disposition keeps the writes as S2 and says why, unless the request says `strict: true`. Eject, swap, insert, create and rescan take `strategy`, `onConflict` and `strict` with `save`. On close, every dirty composite is saved by its policy (`MediaManager::SaveByPolicyOnRelease`). The descriptor's `writes.save` is the explicit request |
| D-9 | **Phase order**: the read side first (C1-C5); then attribution (`media changes`, read-only) with S1 and S2 (C6); partitions (C7); S3 and S4 last (C8). | followed. Then C4b, C8d, C9 (dropped), C10a-C10e, C11 ([phases/README.md](phases/README.md)) |

Every policy (merge, names, build strategy, file system, boot, attribution, save, delete,
conflicts, delta, commit, eject, rescan) is drawn as a decision tree; the map of all trees is
[architecture.md](architecture.md) §12.

### 3.1 Later decisions and findings (2026-10-05 to 2026-10-07)

| Date | What | Where |
|---|---|---|
| 2026-10-05 | A folder layer's mount point takes its folder's time, not 1980. `HostFolderFat`'s label entry follows, the one exception to FR-34 | [c4-graft.md](phases/c4-graft.md) §8 |
| 2026-10-05 | Sprinter IDE disks take FAT12 / FAT16 only (DSS) | [c2-composite-descriptor.md](phases/c2-composite-descriptor.md) §1 |
| 2026-10-06 | Profi IDE disks take FAT12 / FAT16 only: PQ-DOS ignored a FAT32 partition (ACC-C4) | [c7-partitions.md](phases/c7-partitions.md) §7 |
| 2026-10-06 | S3 leaves the descriptor alone and rebases the slot on the base image (not flatten-strategies.md S3 step 5) | [c8-commit-writeback.md](phases/c8-commit-writeback.md) §2 |
| 2026-10-06 | C9 bulk `ReadSectors` dropped: 14x cheaper at the device, about 2 % of a guest's cost per sector | [c9-bulk-read.md](phases/c9-bulk-read.md) |
| 2026-10-06 | Images stay streamed; there is no `[MEDIA] ImageMemoryLimit` | [c10-sparse-memory.md](phases/c10-sparse-memory.md) §7 |
| 2026-10-06 | Session writes bounded: 128 MiB, then a spill file (C10d), then 16 MiB in 1 MiB arenas, a 30 s flush and a recoverable journal (C10e). The journal is written by an I/O pool, off the emulation thread | [c10d-session-spill.md](phases/c10d-session-spill.md), [c10e-session-journal.md](phases/c10e-session-journal.md) §8 |
| 2026-10-06 | A graft reads only the base directories the upper layers reach, and counts the rest on demand | [c4b-lazy-graft-base.md](phases/c4b-lazy-graft-base.md) |
| 2026-10-07 | Lean defaults: `SessionJournal = off`, one journal I/O thread (`SessionIoThreads = 1`) | [c10e-session-journal.md](phases/c10e-session-journal.md) §9 |

## 4. Actors and use cases

| ID | Actor | Use case |
|---|---|---|
| UC-1 | Developer | Cross-builds Z80 programs on the host. Mounts `build/out` over a NedoOS SD image. Each rescan picks up new binaries without rebuilding the 2 GB image. |
| UC-2 | Collector | Gathers games from several host folders into one FAT32 SD card for Wild Commander, `*.trd`, `*.scl` and `*.tap` only, with readable 8.3 names. |
| UC-3 | Sprinter / Profi user | Boots DSS or PQ-DOS from a real HDD image. Adds a host folder of utilities under `/UTIL` without touching the image. Saves the session; later flattens into a new image. |
| UC-4 | CD author | Builds an ISO 9660 + Joliet CD from three folders to test a CD player or a CD-booting loader (ZX-Evo "D. CD boot", `AUTORUN.ZX`). |
| UC-5 | Archivist | Joins `dos.img` (FAT16) and `data.img` (FAT32) as two partitions of one IDE disk, as a real machine had them. On Sprinter and Profi slots the FAT32 partition is refused (§3.1). |
| UC-6 | Automation / CI | Composes media from a descriptor in a test, runs the guest, then asks *which files changed, and in which layer* through the API. |

## 5. Functional requirements

### 5.1 Descriptor and sources

| ID | Requirement | Status |
|---|---|---|
| FR-1 | A **composition descriptor** (`*.ucompose.yaml` or `*.ucompose.json`) is a media source in its own right (`MediaSourceType::Composite`). It is accepted by `media insert` on every surface and by the `[MEDIA]` config. | met (C2); `*.ucompose.yml` too. The registry recognizes the name, so config entries work as files |
| FR-2 | The descriptor sets the **target**: kind (`block` / `optical`), file system (`auto` / `fat16` / `fat32` / `iso9660`), size or free room, label, code page, partition scheme (`mbr` / `none`) and build strategy (`rebuild` / `graft` / `auto`). The file system obeys the slot's `fsCompatibility` and `defaultFs` exactly as folder volumes do; `auto` is resolved by the rule in [fs-compatibility.md](fs-compatibility.md) §6. | met (C2, C4, C5). More keys: `fixedTime`, `onBadName` (`skip` / `replace`), `iso: {level, joliet, relaxDepth}` |
| FR-3 | **Layers** are listed bottom first. Each has a source (`folder`, `image` with an optional `partition`, `iso`), an optional `from` subpath inside the source, a `mount` path in the target, `include` / `exclude` wildcards, a `conflict` policy, `whiteout` paths and an `opaque` directory list. | met (C2, C3, C5). `partition` is an MBR primary entry 1-4. `iso` takes ISO, CUE / BIN or a CD CHD (the first data track). S4 adds `writable`, `onDelete` and `deletedFolder` (C8) |
| FR-4 | Paths in a descriptor are relative to the descriptor file. `~` expands to the home folder. Absolute paths are allowed. Nothing in the descriptor is machine specific unless the user writes it. | met (C2) |
| FR-5 | **Partitions** mode lists partitions instead of layers. Each partition is a passthrough of an image's partition or a nested composition. | met (C7) |
| FR-6 | A folder layer honors the folder's own `.unreal-media.yaml` manifest (`exclude`, `order`, per-file names). The descriptor's settings win over the manifest's. | **partly**: only the manifest's `exclude` is applied, added to the layer's (`CompositeMediumFactory::Build`). Its `order` and `files` names are not used, and neither are its `codepage` and `label`. The manifest's report lines are passed on |
| FR-7 | Inline descriptors: the WebAPI, MCP and scripting surfaces accept the descriptor body as JSON instead of a file path. | met (C2: `MediaControl` takes `{...}` text in place of a path; WebAPI insert takes a `descriptor` object). An inline descriptor has no default `.delta` and cannot take write-back |

### 5.2 Union semantics

| ID | Requirement | Status |
|---|---|---|
| FR-10 | Upper layers shadow lower ones. A file shadows a file or a directory at the same target path. A directory merges with a directory unless it is `opaque` in the upper layer. | met (C1) |
| FR-11 | Paths are compared under the **target file system's equivalence**. FAT compares case-insensitively, after long-name and 8.3 mapping. ISO compares the Joliet name case-sensitively and the ISO name in upper case. `Readme.txt` in one layer and `README.TXT` in another are the same target entry. | met: the FAT key (C1, ACC-C1) and `UnionBuilder::ExactKey` for ISO (C5) |
| FR-12 | Every shadowing, whiteout, filter exclusion, name change and skip is listed in the medium's **build report**, grouped per layer. | met: each line names its layer (`layer 'x': ...`). Lines are in build order, not grouped |
| FR-13 | The `conflict` policy per layer: `shadow` (default) means the upper entry wins. `keep-lower` means the lower entry wins. `error` means any shadowing fails the build. | met (C1) |
| FR-14 | Entry order inside a directory is deterministic: the descriptor's `order` for that directory first, then directories, then files, byte-wise by UTF-8 name. Two hosts give the same bytes. | **partly**: directories first, then files, byte-wise. The layer key `order` is parsed and ignored: `composedescriptor.cpp` marks it "phase C2b", and there is no such phase. Not in [TODO.md](TODO.md) |

### 5.3 Validation and compatibility

| ID | Requirement | Status |
|---|---|---|
| FR-20 | The build validates the whole union against the target: volume size, cluster-count range per FAT type, FAT16 root-directory entries, entries per directory, maximum file size, path depth, ISO name and depth rules. A violation fails with `DoesNotFit` and names the entry and the limit. | met: entries per directory and 4 GiB files (`Validate`); size and cluster range (`FatSynthVolume`, `target.size`); a graft's fixed FAT12 / FAT16 root (S-10); ISO depth 8 unless `iso.relaxDepth` (C5). A rebuilt FAT16 root grows to hold its entries. FAT has no path-depth check |
| FR-21 | Names are converted to the target. FAT: long name plus unique 8.3 name in the chosen code page (CP866 / CP1251), with `FatNameMapper`'s rules. ISO: Level 1 (or 2) ISO name plus a Joliet name. Unconvertible names are reported, and the entry is skipped or renamed according to the descriptor. | **partly**: conversion met (C2, C5: `~N` tails on collisions, reported). `target.onBadName: replace` is not implemented. It is reported ("replace is not implemented yet"), and such names are skipped |
| FR-22 | Timestamps are clamped to the target's range: FAT 1980-2107 with 2-second resolution, ISO 1900-2155. Attributes are mapped (FAT read-only / hidden / system ↔ ISO hidden). | met, with one difference: ISO times are clamped to 1900-2099 (`isosynthvolume.cpp`) |
| FR-24 | **Boot carry-over and boot layers** (D-6). ISO target: a bootable bottom ISO layer keeps its El Torito entries (platform ids, emulation types, load segments, sector counts; images read from the source by extent); a `boot.eltorito` list adds or replaces entries, each naming an image by target path (a file of the union) or host path, with emulation (`none` / `floppy` / `hdd`), load segment, sector count and platform. FAT target: the bottom FAT image's MBR boot code (bytes 0-445), volume boot code (outside the BPB fields the builder writes) and reserved boot sectors are carried; `boot.mbrCode`, `boot.volumeCode` and `boot.reserved` (`{lba, file}` ranges, for example the DSS loader at LBA 1-3) replace them from files. Boot data that cannot be carried (a bootable ISO in an upper layer, an El Torito catalog into a FAT target, boot code larger than its area) is reported. | met (C5b, [c5-iso.md](phases/c5-iso.md) §10). An implicit carry that does not fit is reported and left out; an explicit `boot:` that does not fit fails `DoesNotFit` |
| FR-23 | Only supported combinations build. The matrix of source and target file systems is in [fs-compatibility.md](fs-compatibility.md). Any other combination fails with `NotSupported` and the reason. | met in effect. The error kinds changed: a kind or FS mismatch is `BadRequest` with the reason ("the slot reads FAT16, not FAT32", "a CD slot takes an ISO 9660 volume"). A floppy or tape slot answers `NotSupported`, and `writethrough` answers `KindMismatch` |

### 5.4 Build strategies

| ID | Requirement | Status |
|---|---|---|
| FR-30 | **Rebuild**: a fresh FAT16, FAT32 or ISO volume is synthesized from the union tree, laid out like `HostFolderFat` today (contiguous runs, FAT computed per sector). | met (C1, C5) |
| FR-31 | **Graft**: the bottom layer is a FAT image. Its layout is kept. Upper-layer files are placed in its free clusters, and its directories and FAT are patched in a sparse sector map. Unpatched sectors read straight from the base. | met (C4). Since C4b, only the base directories the upper layers reach are read. A base layer with `include` / `exclude` is read in full |
| FR-32 | **Auto** (default): graft when the bottom layer is a FAT image whose FAT type equals the target's and its free space holds the upper layers; otherwise rebuild. The choice is in the report. | changed ([c4-graft.md](phases/c4-graft.md) §2). `auto` grafts when the bottom layer is a FAT image at `/` taken from `/`, and the slot and `target.fs` allow its FAT type (FAT12 counts as the FAT16 family). `target.size` must be absent or equal to the base's. The upper layers must fit in the free clusters and, on FAT12 / FAT16, in the root slots. Otherwise it rebuilds and the report says why. An ISO target is always rebuilt |
| FR-33 | **Partitions**: a synthesized MBR (with an EBR chain beyond four partitions) maps partitions to child devices. Each child is an image's partition passed through, or a composed volume. | met (C7) |
| FR-34 | `HostFolderFat` today becomes a rebuild composite with one folder layer. Its output stays **byte-identical**, and existing configs and tests are unchanged. | met (C1, `HostFolderFatParity_Test`). One exception by owner decision: the volume label entry carries the folder's time (C4 §8) |

### 5.5 Writes, provenance, flatten

| ID | Requirement | Status |
|---|---|---|
| FR-40 | Guest writes go to **one change layer** on top of the whole composite: `SessionWriteMap` today, `MediaChangeLayer` after H1. Sources never change during a session. | met: `SessionWriteMap`, since H1 is not built. As built it is an `IChangeView` (C10d) holding at most 16 MiB in arenas. The rest goes to a temp file in `SpillFolder`, or to `<source>.usession` when the journal is on (C10e; off by default, §9 there) |
| FR-41 | **Provenance**: for any sector, the medium answers who owns it: MBR / boot / FSInfo / FAT / directory of a given folder / data of a given file (layer, source, offset) / grafted base sector / free space. | met as an internal API: `IComposedLayout::OwnerOf` on `FatSynthVolume` and `GraftVolume` (C6 §7). A graft's unread base directories answer `unlisted`, indexed at the first query (C4b §7). Used by `changes` and write-back; no verb of its own |
| FR-42 | **Change attribution**: the medium answers which files were created, modified, deleted or renamed, and in which layer each lives. It works from the changed sectors and a re-read of the merged volume. Available read-only at any time (`media changes <slot>`). | met (C6b); also `mkdir`, `rmdir`, `attributes`; per partition on a partitioned disk (C7) |
| FR-43 | **Flatten to a flat image** (mandatory, S1): export sources plus changes as one raw `.img`, fixed `.vhd` or `.chd`. Optional `--compact` re-synthesizes a defragmented volume from the merged tree. | met (C6a: sparse raw, fixed VHD, `compact` with `fs` / `size`). C10a skips known-zero runs; C10b adds `vhd: dynamic`. 125 % of a raw copy (C11) |
| FR-44 | **Persisted session** (S2): the change layer is saved next to the descriptor and restored on the next insert when the composite's content id matches. Otherwise the user is told why it cannot be applied. | met (C6c): `<descriptor>.delta`, or `writes.delta`. A damaged one becomes `*.delta.bad`; one over other sources is kept and its layers are named, and saving over it needs `force`. With a journal, the journal is replayed instead of the delta (C10e §4) |
| FR-45 | **Commit into the graft base** (S3): when the base is opened writable, the patched metadata, the changed sectors and the grafted file data are written into the base image. Every overwritten sector is journaled first. | met (C8a, C8d §4). The base is a raw, HDF, HDI or VHD image, not a CHD. The old sectors go to `<base>.ujournal`, synced, and the next open rolls back an interrupted commit. The sectors are streamed, with no list in memory. Afterwards the slot holds the base; the descriptor is not rewritten. **Not done:** a partitioned composite cannot be committed ("commit needs a graft composite") |
| FR-46 | **File-level write-back** (S4, opt-in, folder layers only): file operations are routed to layers by the policy in [flatten-strategies.md](flatten-strategies.md). There is always a dry-run plan first. A host file that changed since the snapshot is a conflict, never overwritten. | met (C8b, C8d). It writes into layers marked `writable: true`; the upper layer is `writes.upper`, else the topmost writable one. `plan` comes first, and `onConflict` is `refuse` or `keep-both`. Files are staged as `.unreal-staging-<n>`, then journaled in `<descriptor>.writeback`. Whiteouts go to `<descriptor>.whiteout`, attributes to `<descriptor>.attributes`, `trash` goes through `HostTrash`. Composed partitions write back each into its own layers |
| FR-47 | Eject and insert dispositions (`save` / `export` / `discard`) work on composites as on any block medium. `save` picks its strategy per D-7: the policy (request `strategy` → `writes.save` → S2) for automation, a strategy dialog for interactive use. | met (C6c, C8c); a disposition follows `writes.save`, S3 / S4 included, with the S2 fallback (D-8) |

### 5.6 Integration

| ID | Requirement | Status |
|---|---|---|
| FR-50 | Composites live in slots like any medium. Selectors, dispositions, model switch (M5), the TTD recording guard and `MediaReadTap` journaling all work unchanged. | met by construction: a composite is a `Medium` in a slot, and the commit plan reads through the session, never through the read tap (C8 §5). No test covers a composite across M5 or TTD |
| FR-51 | `ContentId` of a composite is a hash of the normalized descriptor, every layer's identity (folder snapshot identity, image content id) and the build options. Snapshots and TTD refer to it like any medium. | met (C2 `Normalized()`, C3 identity; `.whiteout` and `.attributes` lines are part of the normalized form) |
| FR-52 | The "same source in two slots" rule (G5 of the storage design) applies per layer source. An image used read-only as a layer may also be in another slot read-only. An image that is a graft base being committed (S3) is exclusive. | met: a commit refuses a base used in another slot, as a file or as a layer (`InUse`, C8 §5) |
| FR-53 | Surfaces: `media compose` (validate and build a descriptor without inserting; prints the report and the layout), `media layers <slot>`, `media changes <slot>`, `media flatten <slot> --strategy … [--plan]`. Available on the WebAPI with OpenAPI, CLI, MCP, Lua and Python. The Qt media panel shows layers and changes. | met: CLI; WebAPI `GET /media/compose` and `POST /media/{slot}/{layers,changes,flatten}`; MCP; Lua and Python `media_compose` / `media_layers` / `media_changes` / `media_flatten`. Qt: a Layers... button and the flatten dialog (C8 §7; the owner's build is pending) |
| FR-54 | `rescan` rebuilds from fresh folder snapshots. With pending guest changes it follows the dirty rules (refuse, or a disposition). | met in part: with unsaved writes `rescan` refuses (`Dirty`). It takes no disposition |

## 6. Non-functional requirements

### 6.1 Performance

The emulator calls `IBlockDevice::ReadSector` from the emulation thread, inside a frame. Every
nanosecond counts during DMA-like bulk reads (ATA READ MULTIPLE, SD multi-block, CD READ (10)).
All "Measured" values below are from C11 ([benchmarks/README.md](benchmarks/README.md), 2026-10-09, Linux container,
gcc 13, medians of 3) unless named otherwise.

| ID | Requirement | Measured by | Status |
|---|---|---|---|
| NFR-P1 | **Parity**: a composite with one folder layer reads data sectors no slower than `HostFolderFat` today (median ≤ 1.10×) and metadata sectors no slower (≤ 1.05×). | `BM_ComposeRead/parity` vs `BM_HostFolderFatRead` (A/B per performance guidelines §4) | met: 1.03× sequential, 0.98× random, 0.94× metadata (`ComposeMode` c1f vs hff, chart C5); the A/B of `HostFolderFat` itself is in C1 §3 |
| NFR-P2 | **Layer count does not matter at read time**: layers are resolved at build time. Read latency at 64 layers ≤ 1.15× latency at 1 layer. | chart C2 | met: 1.00× |
| NFR-P3 | **Zero copies on the read path**: data sectors go from the source straight into the caller's buffer. No intermediate buffer, no per-read heap allocation (verified with `HeapCounter`). | `ComposeRead_Test.NoHeapAllocationPerRead`, benchmark counters | met: `ComposeReadAllocations_Test.NoAllocationPerSectorRead` (rebuild, graft, partitions, ISO, with and without a session). C11 removed one allocation per boot-sector read |
| NFR-P4 | Lookup cost O(log runs) per sector, with an O(1) fast path for sequential reads (last-hit cache). | chart C3 | met: sequential 36-44 ns whatever the extents; random 44 → 128 ns from 1 to 4096 extents |
| NFR-P5 | **Build time** O(n log n) in entries; ≤ 1.5 s for 100 000 entries (scan excluded) on the dev host. | chart C1 | met: 100 000 entries in 366 ms (1 layer) to 1331 ms (64 layers), scan included. 64 layers took 1.8 s before the C11 fixes |
| NFR-P6 | **Graft build time** independent of base size beyond one FAT scan (free-cluster bitmap). It grows with the upper layers' entries and the touched base directories only. | chart C6 | met (C4b): 1.17 / 1.40 / 3.03 ms at 1 K / 10 K / 100 K base entries, the growth being the free-cluster scan. The first `layers` reply pays the deferred counts once |
| NFR-P7 | **Zero cost for media that are not composites**: no new virtual hop, branch or check on `RawImage`, `HostFolderFat`-as-before or CHD paths. Machines without block media pay nothing. | A/B on `chdimage_benchmark`, `zcontrollerspi_benchmark` | met: inside the noise band on 11 benchmarks; the container's spread is wide, so the A/B is worth repeating on macOS |
| NFR-P8 | Flatten / export streams with bounded memory (≤ 16 MiB working set whatever the disk size) at ≥ 80% of a raw-to-raw copy's throughput for `.img`. | chart C7 | met for throughput: 125 % (chart C7b). Memory is bounded by design (sector streaming, `ExportBlockDevice`) but was not measured |
| NFR-P9 | Change attribution for 10 000 changed sectors on a 100 000-entry volume ≤ 500 ms. | chart C7 | met: 12 ms at 10 K entries, 84 ms at 100 K (chart C7a) |

### 6.2 Memory

| ID | Requirement | Status |
|---|---|---|
| NFR-M1 | No file data is ever held in memory by the composite (directories and patched metadata sectors only). | met. Guest writes are the change layer's (FR-40), bounded at 16 MiB since C10e |
| NFR-M2 | Per entry ≤ 128 bytes of tree and layout state plus its name bytes. Per extent 16 bytes. Directory clusters as the target needs them (32 bytes per FAT entry and long-name slot). 100 000 entries in ≤ 32 MiB in total. | met: 16.6 MiB for 100 000 entries (rebuild; ISO 19.6, graft 0.2). C11 cut `SourcePool`'s host-file records from 47.6 to 17.4 MiB. 64 layers of the same 100 K entries hold 42 MiB, because each layer brings its own 1 000 folders |
| NFR-M3 | One opened instance per source, shared by every layer and partition that names it (`SourcePool`), so CHD hunk caches and file handles are not duplicated. | met (C3: `ComposeFat_Test.ChdSourceReadsThroughSharedCache`; two partitions of one image are two windows over one device) |
| NFR-M4 | At most `kMaxOpenFiles` (8 today) host files open per composite, shared across all its folder layers. | met: `SourcePool::kMaxOpenFiles = 8`, an LRU |
| NFR-M5 | Fragmented FAT sources: extents are coalesced. A per-composite extent budget (default 4 M extents = 64 MiB) fails the build with `DoesNotFit` and names the most fragmented files rather than exhausting memory. | **partly**: extents are coalesced (`ChainExtents`, C3 §3). **Not done:** there is no extent budget in the code, and it is not in [TODO.md](TODO.md). The workaround is `compact` (S1) on the source |

### 6.3 Determinism, safety, portability

| ID | Requirement | Status |
|---|---|---|
| NFR-S1 | Same descriptor, same source identities, same options → byte-identical medium on every host (fixed serials, sorted orders, UTC times). | met: sorted orders, DOS times as UTC, the VHD footer's timestamp 0 and its UUID from the content id; `fixedTime` for the times; lazy and full grafts byte-identical (`GraftLazy_Test`) |
| NFR-S2 | Nothing is written to any source except by an explicit S3 or S4 flatten. S3 and S4 always have a dry-run plan, a journal and temp-file-then-rename writes. | changed. Both have `plan`. S3 writes the base **in place** after a synced undo journal (`<base>.ujournal`), with no temp file (C8 §5). S4 stages `.unreal-staging-<n>` and renames, under `<descriptor>.writeback`. "Explicit" includes a `save`, an eject with `save` or the emulator closing, when `writes.save` names S3 or S4 (D-8) |
| NFR-S3 | A source that shrank or vanished after the build reads zeros and is reported (as `HostFolderFat` does today). It never crashes and never reads out of bounds. | met (`SourcePool`: zeros and one warning). An image shorter than its partition reads zeros past its end (C7 §7) |
| NFR-S4 | Cross-platform: path handling with `std::filesystem` and UTF-8 everywhere. Windows, macOS and Linux. gcc, clang, MSVC, mingw. Zero warnings. | built and tested on Linux gcc 13. The owner's check on macOS / Windows is pending (`HostTrash`, the journal's positional I/O, the Qt dialog; [TODO.md](TODO.md)) |
| NFR-S5 | Descriptor parsing never aborts. Unknown keys and bad values are reported. `rapidyaml` is used with the throwing handler, like `FolderManifest`. | met (C2) |

## 7. Acceptance

| ID | Scenario | Pass when | Status |
|---|---|---|---|
| ACC-C1 | ZX-Evo `sd.zc` from a descriptor: NedoOS SD folder + a second folder with `bin/AUTOEXEC.BAT` replaced (upper shadows lower) | NedoOS boots to `M:/bin>`; the shell runs the upper batch file (its marker), never the lower one | met: `ZXEvoErs_Test.NedoOsBootsFromTwoComposedFolders` |
| ACC-C2 | Wild Commander (ZX-Evo with TS-Conf) on a FAT32 composite of 3 folders with filters | WC lists exactly the filtered files under each mount, with the expected 8.3 names | met: `TsConfBootSd_Test.ComposeWildCommanderListsFilteredFat32Layers` (WC Improved v1.11i) |
| ACC-C3 | Sprinter: DSS 1.71 HDD image (graft base) + host folder at `/UTIL` | DSS boots; `DIR C:\UTIL` lists the folder; a file written by the guest appears in `media changes` attributed to the right layer; a FAT32 target on a Sprinter slot is refused | met on **DSS 1.62.92**: `SprinterBoot_Test.ComposeDssGraftedUtilFolder` and `.ComposeDssGuestWriteAttributed`, where the guest writes only `mkdir` because DSS 1.62 has no COPY. FAT32 refusal: `MediaControl_Test.SprinterHardDiskTakesFat16CompositesOnly`. DSS 1.71's MKDIR corrupts the test disk (P2 in [TODO.md](TODO.md)) |
| ACC-C4 | Profi: PQ-DOS image as partition 1 + a composed FAT16 as partition 2; then the same with a FAT32 partition 2 | PQ-DOS sees both drives; the FAT32 run records whether PQ-DOS reads FAT32, and the Profi slot descriptor is set from that result | met: `ProfiPlusComposed_Test.Fat16SecondPartition` (partition 1 is a graft over `pqdos-hdd-small.img`'s partition 1). PQ-DOS ignored FAT32, so the Profi slots take FAT16 only, and `.Fat32SecondPartitionRefused` checks the refusal |
| ACC-C5 | ATAPI CD on ZX-Evo: ISO composite of two folders | NedoOS lists the CD contents; `AUTORUN.ZX` boot works from the ERS menu | **partly**: the ERS boot works (`ZXEvoErs_Test.ErsBootsAutorunFromComposedIso`). **Not done:** the NedoOS listing, which needs NedoOS's ISO driver in the fixtures (P2 in [TODO.md](TODO.md)) |
| ACC-C6 | Flatten S1: the ACC-C3 session exported to `.img` and `.vhd` | the exported image boots alone (no layers) and `FatVolumeReader` shows the same tree as the live medium | met: `SprinterBoot_Test.ComposeFlatImageBootsAlone` (`.img`, `.vhd`, compact `.img`) and `FlattenFlat_Test.ImgVhdChdEqualMergedView`. S3 has its own boot test: `.ComposeCommitBootsFromTheBase` |
| ACC-C7 | Persisted session S2 | after a restart the guest sees its earlier writes; changing a source makes the delta refuse with a clear reason | met: `SprinterBoot_Test.ComposeDeltaSurvivesRestart` |
| ACC-C8 | Benchmarks and charts | every NFR-P row has a measured value in [test-and-benchmark-plan.md](test-and-benchmark-plan.md) §5 "results" and the charts C1-C8 are generated by the plot script | met: §5.5 there and [benchmarks/README.md](benchmarks/README.md) (`tools/bench/plot-media-compose.py`). The 1 M-entry build, the `cchd` mode and the 10 repetitions were not run (benchmarks §6) |
