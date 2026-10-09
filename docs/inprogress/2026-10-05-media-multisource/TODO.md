# Multi-source media — TODO

**Status:** design written 2026-10-05; implementation on branch `media-multisource`: C0-C5 done ([phases/](phases/README.md)).
PLAN.md row **#95**.

## Owner decisions (2026-10-05)

- Composition is file-level (overlayfs-like union into one synthesized volume). A block overlay is
  used only for guest writes.
- Disks are joined both as a merged tree and as partitions (partitions expected to be rarer).
- Write-back strategies are researched. Flatten to one flat `.img` / `.vhd` is mandatory.
- Guest deletes follow a per-layer policy: keep (default, whiteout) / trash / move / delete / ignore.
- The descriptor is its own `*.ucompose.yaml` file.
- Boot structures (El Torito, MBR / volume boot code, reserved sectors) are carried from the bottom source or laid on top by a boot layer.
- `save`: the save policy in automation, a strategy dialog in the GUI; sources (S3 / S4) change only on an explicit request.
- Phase order: read side, then attribution with S1 / S2, partitions, S3 / S4 last.

## Remaining

- [ ] Review round 1: requirements, architecture, FS compatibility, flatten strategies, TDD
- [x] Owner questions answered 2026-10-05 (D-4…D-9 in [goals-and-requirements.md](goals-and-requirements.md) §3; guest FS support researched in [fs-compatibility.md](fs-compatibility.md) §6)
- [x] C0 baseline: `HostFolderFat` corpus hashes and read benchmark numbers on master
- [x] C1 core + `HostFolderFat` parity refactor
- [x] C2 descriptor, validation, manager integration, surfaces (`compose`, `layers`); Sprinter IDE slots `fsCompatibility = {Fat16}`
- [x] C3 FAT image sources
- [x] C4 graft
- [x] C5a ISO reader, ISO sources, ISO target, optical composites, ACC-C5
- [x] C5b D-6 boot carry-over (El Torito, and FAT rebuilds: `BootPlan`, the `boot:` section; its
  tests are in C5's list, [test-and-benchmark-plan.md](test-and-benchmark-plan.md) §3.5)
- [x] C6 provenance, attribution (`changes`), S1 flat (+ VHD writer, compact), S2 delta
  - [x] C6a S1: sparse raw export, fixed VHD writer, `compact` on export / save, ACC-C6
  - [x] C6b provenance, `ChangeAttributor`, `media changes`, ACC-C3 attribution
  - [x] C6c S2 delta file, DT-13 restore, DT-9, ACC-C7
- [x] C7 partitions (`PartitionedDisk`, passthrough and composed partitions, ACC-C4; Profi IDE slots FAT16 only)
- [x] C8 S3 commit, S4 write-back, Qt flatten dialog
  - [x] C8a S3: journaled commit into the graft base, recovery on open, `media flatten`
  - [x] C8b S4 write-back into folder layers; C8d: attributes (sidecar), the host trash, partitioned disks, a commit without a sector list in memory
  - [x] C4b a graft reads only the base directories its upper layers reach
  - [x] C8c Qt strategy dialog (code; awaits the owner's Qt build on macOS / Windows / Linux)
- [x] C9 optional bulk `ReadSectors`: dropped after measuring (14x cheaper at the device, about 2 % of a guest's
  per-sector cost; [phases/c9-bulk-read.md](phases/c9-bulk-read.md))
- [x] Lazy base enumeration for grafts: done as C4b ([phases/c4b-lazy-graft-base.md](phases/c4b-lazy-graft-base.md))
- [x] A folder layer's mount point dated 1980: the snapshot root now has its folder's time (owner decision 2026-10-05,
  [phases/c4-graft.md](phases/c4-graft.md) §8)
- [x] C10 sparse and in-memory images (owner request 2026-10-05; done as C10a-C10e, see [phases/README.md](phases/README.md)): sparse image files and sparse in-memory disks
  (store only written / non-zero sectors: a FAT32 volume is >= 32 MiB, 256 MiB with 4 KiB clusters, nearly all
  zeros), images held in memory instead of on disk where it pays, and packing back efficiently on save / flatten
  (S1-S4): skip zero and unchanged runs, sparse output files, compact VHD / CHD. Design first, in phases/
- [x] Benchmarks and charts C1-C8 with the results table filled in ([test-and-benchmark-plan.md](test-and-benchmark-plan.md) §5.5,
  [benchmarks/README.md](benchmarks/README.md)): every NFR met after the C11 fixes (host-file memory, the merge's keys,
  an allocation per boot-sector read)
- [x] User docs (`docs/features/media.md`) and recipe [`.recipe/media/compose-media.md`](../../../.recipe/media/compose-media.md)
- [ ] Owner's check on macOS / Windows before master: the Qt flatten dialog (partitioned write-back too), `HostTrash`
  (Recycle Bin, `~/.Trash`), the C10e journal's positional I/O
- [ ] Follow-up after C0-C9: library extraction and unification, phases X0-X13 ([library-extraction/](library-extraction/README.md)), PLAN.md row **#96**
- [ ] **P2, blocked upstream** ACC-C5, the NedoOS half (owner request 2026-10-05): NedoOS lists the contents of a composite CD on the
  ZX-Evo's ATAPI drive. Needs NedoOS's CD / ISO 9660 driver in the test fixtures (`testdata/machines/zxevo/nedoos/`);
  the ERS boot of `AUTORUN.ZX` already covers the drive path ([phases/c5-iso.md](phases/c5-iso.md) §9)
  Checked 2026-10-09 against the NedoOS fork `alfishe/NedoOS` at `a750349`: NedoOS has no ISO 9660 and no ATAPI data
  reads. The kernel's IDE driver (`src/kernel/fatfsdrv.asm`, `readidentIDE`) sees the ATAPI signature `0xEB14` and marks
  the drive not ready; sector I/O is ATA READ `0x20` only; the drive letters are FAT and TR-DOS (`src/nedoos_en.md`). The
  only CD program, `cdplay.com`, sends audio packets (TOC, PLAY MSF, PAUSE, STOP) to the slave drive. Nothing to test
  until NedoOS gets an ATAPI READ(10) driver, an ISO 9660 layer and a drive letter for it.
- [ ] **P2** DSS 1.71.66 MKDIR corrupts a `BuildDssHdd` disk (found 2026-10-06 while moving ACC-C3 to the newest DSS,
  whose shell has REN / DEL / ECHO but no COPY and no redirection). On BIOS 3.06 HF2, a plain session image (no
  composite): `mkdir c:\acc` writes LBA 160-220, the first clusters of SYSTEM.DOS, and `cd \acc` + `dir` answers
  "Path not found". DSS 1.62.92 makes directories on the same layout correctly (FAT16, 63 hidden sectors, 4 sectors per
  cluster, 8152 clusters). It is either the test disk's layout or an IDE write path that DSS 1.71's driver takes and
  1.62's does not. Once it is fixed, move `SprinterBoot_Test.ComposeDssGuestWriteAttributed` to DSS 1.71 so that the
  guest's REN and DEL are attributed too ([phases/c6-provenance-flatten.md](phases/c6-provenance-flatten.md) §7)

## Phase documents

The design and as-built record of each phase: [phases/](phases/README.md).
