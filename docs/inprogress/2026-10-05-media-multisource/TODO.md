# Multi-source media — TODO

**Status:** design written 2026-10-05; implementation on branch `media-multisource`: C0-C2 done.
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
- [ ] C3 FAT image sources
- [ ] C4 graft
- [ ] C5 ISO reader, ISO target
- [ ] C6 provenance, attribution (`changes`), S1 flat (+ VHD writer, compact), S2 delta
- [ ] C7 partitions
- [ ] C8 S3 commit, S4 write-back, Qt flatten dialog
- [ ] C9 optional bulk `ReadSectors` (A/B gated)
- [ ] Benchmarks and charts C1-C8 with the results table filled in ([test-and-benchmark-plan.md](test-and-benchmark-plan.md) §5.5)
- [ ] User docs (`docs/features/media.md`) and recipe `.recipe/media/compose-media.md`
- [ ] Follow-up after C0-C9: library extraction and unification, phases X0-X13 ([library-extraction/](library-extraction/README.md)), PLAN.md row **#96**

## As built

### C0 / C1 (2026-10-05)

- Parity: `HostFolderFatParity_Test` holds FNV-1a hashes of 24 corpus volumes recorded on master before the
  swap (sectors up to `UsedSectorEnd`, every 1021st sector, the last one). The refactored `HostFolderFat` builds
  byte-identical volumes.
- `HostFolderFat` is a one-layer `FatSynthVolume`: `HostFolderSource::Enumerate` fills a `FileTree`, the layout
  engine moved unchanged into `FatSynthVolume`. Messages and `Describe()` text are as before.
- `UnionTree` from the TDD is `FileTree` (flat nodes, `FileData` + `Extent`); the union is built by
  `UnionBuilder::Merge` into a new `FileTree`.
- NFR-P1 A/B (`hostfolderfat_benchmark.cpp`, Linux, 4 cores), before -> after: SeqRead fat16 618 -> 571 ns,
  RandRead 1554 -> 1361 ns, MetaRead 2357 -> 1998 ns; Build unchanged. The extent last-hit cache pays for the
  extra indirection.
- Full build: zero warnings (C1 and C2). `core-tests` on Linux x86 gcc: all pass except two failures outside this
  work. (1) `TsfmGolden_Test.*`: all four fail even when run alone; the branch does not touch sound code, and the
  digests were most likely recorded on the macOS host. (2) `TTDContainer_Test.RealFile` crashes: `TTDFileSink` /
  `TTDFileSource` pass `&_error` while initializing `_file`, which is declared before `_error`, so the string is
  written before it exists. The fix is to declare `_error` first.

### C2

- `TargetValidator` is not a class: the FR-20 checks run in `CompositeMediumFactory` (entries per directory, the
  4 GiB file limit) and in `FatSynthVolume` (volume size per FAT type, cluster range).
- `MediaSourceType::Composite`. A descriptor is recognized by its name (`*.ucompose.yaml` / `.yml` / `.json`)
  or given inline (`MediaSource::inlineBody`; on the surfaces, a `path` that starts with `{`, or a WebAPI / MCP
  `descriptor` object).
- A composite is never written in place: `writethrough` is refused, session writes as for a folder volume.
- `rescan` rebuilds a composite with the FS it was built with: the guest never sees the FAT type change under it.
- Surfaces: `media compose <descriptor>` (slotless: builds, reports, inserts nothing; options `fs`, `codepage`,
  `free`) and `media layers <slot>`. WebAPI: `GET /media/compose?path=` and `POST /media/{slot}/layers`; MCP
  actions `compose` / `layers`; Lua `media_compose` / `media_layers`; Python `media_compose` / `media_layers`.
- Sprinter IDE disks: `fsCompatibility = {Fat16}` (Estex DSS reads FAT12 / FAT16 only), set by `IdeController` for
  `[HDD] Scheme=SPRINTER`, so shared code names no Sprinter model id (`SprinterIsolation_Test`).
- ACC-C1: `ZXEvoErs_Test.NedoOsBootsFromTwoComposedFolders`. The upper layer's `bin/AUTOEXEC.BAT` shadows
  the lower layer's `bin/autoexec.bat` (FAT names fold case).
- ACC-C2: `TsConfBootSd_Test.ComposeWildCommanderListsFilteredFat32Layers` on Wild Commander Improved v1.11i
  (`testdata/machines/tsconf/wc-improved/`, MIT). WC is a folder layer itself, so ACC-C2 needs no image source.
