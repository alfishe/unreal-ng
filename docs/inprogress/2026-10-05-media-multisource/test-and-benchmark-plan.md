# Multi-source media: tests, benchmarks and scalability charts

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Plan for review |
| **Design** | [tdd.md](tdd.md) |
| **Rules followed** | [core/tests/README.md](../../../core/tests/README.md) (CUT pattern, no `sleep_for`, < 50 ms per test, scratch paths), [performance-guidelines.md](../../guidelines/performance-guidelines.md) §4 (A/B procedure) |

## 1. Test strategy

### 1.1 Independent oracles

Every builder is checked by a reader written independently from the specification, never by
itself:

| Built by | Checked by | Already exists |
|---|---|---|
| `FatSynthVolume`, `GraftVolume`, `PartitionedDisk` | `FatVolumeReader` (FAT12/16/32, MBR / superfloppy) | ✓ |
| `FatVolumeReader` itself | `FatImageBuilder` (test helper, fatgen103 layouts) | ✓ |
| `IsoSynthVolume` | `Iso9660Reader` (new, production) | — |
| `Iso9660Reader` itself | `IsoImageBuilder` (new test helper, hand-written ECMA-119 + Joliet), and optional external tools (`isoinfo`, `7z`) in CI when present | — |
| `ChangeAttributor` | scripted guest-like edits through `FatVolumeWriterForTests` (a tiny test-only FAT writer: create / append / delete / rename) and the known expected `ChangeSet` | — |

### 1.2 Fixtures

- **Host folders:** generated per test under `TestPathHelper::GetUniqueTestScratchPath()` with the
  existing `scratchfolder.h` helper. Deterministic names, sizes and mtimes (`fixedTime`).
- **FAT images:** `ScratchFatImage` (`fatimagebuilder.h`), sparse, built at run time; nothing
  binary is committed.
- **ISO images:** `ScratchIsoImage` (new `isoimagebuilder.h`), the same pattern.
- **Large-scale:** `ComposeFixture::SyntheticTree(entries, depth, fanout, sizeDistribution)` builds
  a `SourceTree` **in memory** with `FileData::Zero` / synthetic extents over a `MemoryDisk`. This
  lets build-time tests and benchmarks reach 1 M entries without touching the host disk.
- **Real guest media** (acceptance): the existing `testdata/machines/zxevo/nedoos/sdcard/`,
  `testdata/machines/sprinter/dss_1_62_92.img`, `testdata/machines/profi/pqdos/pqdos-hdd-small.img`.

### 1.3 Speed rules

Unit tests stay under 50 ms each. Scale tests above 10 000 entries run in the benchmark binary,
not in `core-tests`. Acceptance tests that boot a real ROM use `EnableTurboMode()` and are allowed
longer runs, with a comment justifying it.

## 2. Test-first order

Each phase (tdd.md §14) begins by adding its tests (red), then the code (green). The lists below
are the tests per source file (one `*_test.cpp` per source file, class `ClassName_Test`).

## 3. Unit and integration tests

### 3.1 C0 / C1 — parity and the core

| Test | Checks |
|---|---|
| `HostFolderFatParity_Test.CorpusHashesMatchMaster` | 12 generated folders × (FAT16, FAT32) × (MBR, superfloppy) × (CP866, CP1251): SHA-256 of every sector equals the hash recorded on master in C0 |
| `HostFolderFatParity_Test.ExistingSuiteUnchanged` | the whole `hostfolderfat_test.cpp` runs unchanged against the refactored class |
| `ExtentReader_Test.SingleExtentDirect` | the source read lands at `dst` and no other buffer is touched (poisoned guard bytes) |
| `ExtentReader_Test.MultiExtentBoundaries` | sectors at each extent's first and last sector, files with 1, 2 and 4 096 extents |
| `ExtentReader_Test.PastEofZero` / `.SlackZeroed` | reads beyond the size return zeros; the tail of the last sector is zeroed even when the source's slack is not |
| `ExtentReader_Test.SequentialUsesLastHit` | instrumentation counter: 0 binary searches on a sequential sweep after the first sector |
| `ExtentReader_Test.NoHeapAllocationPerRead` | `HeapCounter` around 10 000 reads (host file, device extents, zero) = 0 allocations |
| `ExtentReader_Test.HostFileShrankReadsZeroAndWarns` | NFR-S3 |
| `SourcePool_Test.SameSourceOpenedOnce` | two layers and one partition naming the same image → one device instance |
| `SourcePool_Test.OpenHostFilesBounded` | 50 files read round-robin → at most `kMaxOpenFiles` streams open |
| `UnionBuilder_Test.UpperShadowsLowerFile` | the file from the top layer, reported |
| `UnionBuilder_Test.DirectoriesMerge` / `.OpaqueHidesLower` / `.WhiteoutRemoves` | FR-10 |
| `UnionBuilder_Test.CaseInsensitiveCollisionFat` | `Readme.txt` (L0) vs `README.TXT` (L1) → one entry, L1's (FR-11) |
| `UnionBuilder_Test.JolietKeepsCaseVariants` | the same pair on an ISO target → two entries, unique L1 names |
| `UnionBuilder_Test.FileReplacesDirectoryAndBack` | type changes across layers, both directions |
| `UnionBuilder_Test.ConflictErrorFails` / `.KeepLower` | FR-13 |
| `UnionBuilder_Test.MountCreatesIntermediateDirs` | `mount: /A/B/C` on an empty tree |
| `UnionBuilder_Test.FromSubfolder` / `.IncludeExcludeOnSourceNames` | FR-3 |
| `UnionBuilder_Test.DeterministicOrder` | shuffled source enumeration → identical tree |
| `UnionBuilder_Test.ReportListsEverything` | each shadow / whiteout / filter / rename once, per layer |
| `FatSynthVolume_Test.TwoFolderLayersReadBackByOracle` | `FatVolumeReader` lists the union and reads every file's bytes |
| `FatSynthVolume_Test.ShortNamesUniqueAcrossLayers` | `GAME~1`, `GAME~2` from different layers in one directory |

### 3.2 C2 — descriptor, validation, manager

| Test | Checks |
|---|---|
| `ComposeDescriptor_Test.ParsesFullExample` / `.Json` | the tdd.md §1.1 example, YAML and JSON give the same normalized form |
| `ComposeDescriptor_Test.RelativePathsFromDescriptorDir` / `.TildeExpands` | FR-4 |
| `ComposeDescriptor_Test.UnknownKeysReportedNotFatal` / `.MalformedYamlNoAbort` | NFR-S5 |
| `ComposeDescriptor_Test.NormalizedIgnoresFormatting` | key order, whitespace, `2GiB` vs `2147483648` → same `Normalized()` |
| `ComposeDescriptor_Test.LayersAndPartitionsExclusive` | `BadRequest` |
| `TargetValidator_Test.Fat16TooLarge` / `.Fat16RootFull` / `.FileTooLarge` / `.DirectoryTooManyEntries` / `.IsoDepth` | `DoesNotFit` naming the entry and the limit (FR-20) |
| `TargetValidator_Test.Fat32RaisedToMinimum` | fs-compatibility S-11 |
| `TargetValidator_Test.AutoPicksFat16ThenFat32` | the default rule of fs-compatibility §6 |
| `CompositeMediumFactory_Test.SlotFsCompatibilityRestrictsAuto` / `.ExplicitDisallowedFsRefused` | a `{Fat32}` slot (TS-Conf) builds FAT32; `fs: fat16` there → `BadRequest` |
| `ComposeSprinter_Test.Fat32Refused` / `.DssPartitionEntryZero` / `.BootableBaseGraftsNotRebuilds` / `.RebuildFromFoldersWithDssBootLayer` | DSS rules of fs-compatibility §6; a folders-only composite with the DSS loader as a boot layer boots DSS |
| `CompositeMediumFactory_Test.ContentIdStable` / `.ContentIdChangesWithSource` | FR-51 |
| `CompositeMediumFactory_Test.SameImageReadOnlyInTwoSlots` / `.ExclusiveWhenCommitting` | FR-52 |
| `MediaManager_Test.InsertComposite` / `.EjectDisposeComposite` / `.ModelSwitchKeepsComposite` | FR-50 |
| `MediaManager_Test.CompositeSaveStrategyOrder` | request `strategy` beats `writes.save` beats S2; the result names the strategy (D-7) |
| `MediaManager_Test.EjectNeverCommitsFromDescriptorAlone` | `writes.save: commit` + eject with `save` → S2 delta, sources unchanged, report says why; with `strategy: commit` → S3 runs (D-8) |
| `MediaPanel` Qt test: `SaveCompositeAsksStrategy` | the dialog lists S1-S4, preselects `writes.save`, shows the plan; cancel writes nothing |
| `MediaControl_Test.ComposeVerbReportsWithoutInsert` | FR-53 |
| WebAPI / CLI / MCP / Lua / Python parity tests | the same as the existing `media` verbs' tests, one per new verb |

### 3.3 C3 — image sources

| Test | Checks |
|---|---|
| `FatVolumeReader_Test.ChainExtentsCoalesces` | a 3-fragment file → 3 extents; a contiguous one → 1 |
| `FatVolumeReader_Test.SelectsPartitionN` / `.ScanFreeMatchesFat` | |
| `FatImageSource_Test.Fat12Fat16Fat32Sources` | every FAT type as a layer |
| `FatImageSource_Test.CodePagePerLayer` | CP1251 short names in a source, CP866 target: names right in both |
| `FatImageSource_Test.IgnoresDeletedAndOrphanLfn` | fs-compatibility §4 |
| `ComposeFat_Test.MergeFat16AndFat32IntoFat32` / `.IntoFat16WhenFits` / `.IntoFat16TooBigFails` | S-1, S-2 |
| `ComposeFat_Test.ChdSourceReadsThroughSharedCache` | a CHD layer: correct bytes, one `ChdImage` instance |
| `SubRangeDevice_Test.Bounds` | reads and writes outside the range fail |

### 3.4 C4 — graft

| Test | Checks |
|---|---|
| `GraftVolume_Test.UnpatchedSectorsIdenticalToBase` | every sector outside patch and graft runs equals the base's |
| `GraftVolume_Test.AddedFilesReadByOracle` / `.ReplacedFileShowsNewData` / `.WhiteoutHidesBaseFile` | |
| `GraftVolume_Test.FreedClustersReused` | replacing a big base file frees space for the new one |
| `GraftVolume_Test.DirectoryGrowsNewCluster` | appended directory chain in the FAT patch |
| `GraftVolume_Test.Fat16RootFullFallsBackToRebuild` | `auto` → rebuild + report; `graft` → `DoesNotFit` (S-10) |
| `GraftVolume_Test.Fat32FsInfoUpdated` | free count and next-free hint |
| `GraftVolume_Test.BuildCostIndependentOfBaseFiles` | operation counter: directories re-encoded = touched ones only |
| `GraftVolume_Test.BootSectorPreserved` | the base's boot code bytes unchanged |

### 3.5 C5 — ISO

| Test | Checks |
|---|---|
| `Iso9660Reader_Test.ReadsBuilderImages` | `IsoImageBuilder` L1, L2, Joliet, deep tree, multi-extent |
| `Iso9660Reader_Test.RealIsoFromCdTestDisc` | the existing `cdtestdisc.h` disc |
| `IsoSynthVolume_Test.ReadBackByReader` | every file's bytes and names, both trees |
| `IsoSynthVolume_Test.PathTablesBothEndian` / `.VolumeDescriptorSetTerminated` | ECMA-119 structure |
| `IsoSynthVolume_Test.FilesSharedBetweenTrees` | PVD and Joliet records point to the same extents |
| `IsoSynthVolume_Test.ThroughCdImageReadToc` | `CdImage` over it: one data track, the right leadout |
| `IsoSynthVolume_Test.ExternalToolAgrees` | `isoinfo -l` / `7z l` when available, `GTEST_SKIP` otherwise |
| `ComposeIso_Test.FatSourcesIntoIso` / `.IsoPlusFolderIntoIso` | S-7, S-8 |
| `Iso9660Reader_Test.ParsesElToritoCatalog` | no-emulation, floppy-emulation and multi-section catalogs from `IsoImageBuilder`; bad checksum reported |
| `IsoSynthVolume_Test.ElToritoCarriedFromBottomLayer` | the new catalog has the source's entries with relocated LBAs; the boot image bytes equal the source's; the oracle and `isoinfo -d` (when present) agree |
| `IsoSynthVolume_Test.BootableIsoNotBottomReported` | a bootable ISO in an upper layer: no boot record, a report line |
| `IsoSynthVolume_Test.BootLayerAddsElTorito` / `.BootLayerReplacesSourceEntry` | a non-bootable source plus `boot.eltorito` (union file and host file images): catalog entries and image bytes as described |
| `FatSynthVolume_Test.CarriesBaseBootCode` / `.BootLayerMbrAndVolumeCode` / `.BootLayerReservedSectors` / `.BootCodeTooLargeFails` | D-6 on FAT targets; the BPB fields stay the builder's |

### 3.6 C6 — provenance, attribution, S1, S2

| Test | Checks |
|---|---|
| `ProvenanceMap_Test.EveryRegionKind` | one sector of each owner kind, rebuild and graft |
| `ProvenanceMap_Test.ForEachChangedMergeWalk` | owners of 1 000 random changes equal per-LBA `Owner()` calls |
| `ChangeAttributor_Test.Create` / `.Modify` / `.Append` / `.Truncate` / `.Delete` / `.Rename` / `.MoveAcrossDirs` / `.Mkdir` / `.Rmdir` | each edit made with `FatVolumeWriterForTests` gives exactly one expected `FileChange` with the right layer |
| `ChangeAttributor_Test.ModifyFileFromIsoLayerAttributedToIso` | owner = the read-only layer |
| `ChangeAttributor_Test.LostClustersWarned` | half-done guest update → warning, no crash |
| `ChangeAttributor_Test.SkipsUntouchedSubtrees` | read counter: only evidence directories re-read |
| `BlockFormats_Test.VhdFixedFooter` | footer fields, checksum; `HddImageFormats::Probe` → `vhd`; reopen reads the same sectors |
| `FlattenFlat_Test.ImgVhdChdEqualMergedView` | sector-by-sector equal to the live stack |
| `FlattenFlat_Test.CompactDefragmentsAndOracleAgrees` | every file contiguous; the same tree and bytes |
| `FlattenFlat_Test.SparseFreeSpace` | written file's allocated size ≈ used data (where the host FS reports it) |
| `ComposeDelta_Test.RoundTrip` / `.IdMismatchRefusedWithReason` / `.TruncatedFileRefused` | S2 |

### 3.7 C7 — partitions

| Test | Checks |
|---|---|
| `PartitionedDisk_Test.MbrFieldsAndTypes` | LBA start / size, type bytes, CHS fields as FatFs and DOS expect |
| `PartitionedDisk_Test.FiveOrMorePartitionsUseEbrChain` | the oracle finds every logical partition |
| `PartitionedDisk_Test.RoutesReadsAndWrites` / `.TableAndGapsReadOnly` | |
| `PartitionedDisk_Test.Fat16PlusFat32` | S-5 through `FatVolumeReader` per partition |

### 3.8 C8 — S3 and S4

| Test | Checks |
|---|---|
| `GraftCommit_Test.CommitThenReopenEqualsMerged` | the base alone equals the composite before the commit |
| `GraftCommit_Test.CrashAtEveryStepRecovers` | a fault injector stops the writer after each sector; reopening restores either the old or the new state, never a mix |
| `GraftCommit_Test.RefusesChdBase` / `.RefusesWhileInOtherSlot` | |
| `WriteBack_Test.PlanOnlyWritesNothing` | the host tree hash is unchanged after `--plan` |
| `WriteBack_Test.ModifyOwnedFolderFile` / `.CopyUpFromImageLayer` / `.CreateGoesToParentOwnerOrUpper` / `.RenameAcrossLayers` | the routing table of flatten-strategies §3 S4 |
| `WriteBack_Test.DeleteKeepAddsWhiteout` / `.DeleteTrash` / `.DeleteMoveKeepsRelativePath` / `.DeletePermanent` / `.DeleteIgnoreReappearsOnRebuild` | one test per `onDelete` policy (D-4): host state, descriptor state and the tree after a rebuild |
| `WriteBack_Test.TrashUnavailableFailsInPlan` | no fallback to `delete` |
| `WriteBack_Test.DeleteFromImageLayerIsWhiteout` | read-only owner: whiteout for keep / trash / move / delete, nothing for ignore |
| `WriteBack_Test.HostChangedIsConflict` / `.KeepBoth` | |
| `WriteBack_Test.IllegalHostNameEscapedAndRoundTrips` | `AUX`, a trailing dot |
| `WriteBack_Test.RebuildAfterWriteBackShowsSameTree` | the guest-visible tree is unchanged by the flatten |

## 4. Acceptance tests (real guests)

| ID | Test class / case | Machine, software |
|---|---|---|
| ACC-C1 | `ComposeAcceptance_Test.NedoOsBootsFromTwoFolders` | ZX-Evo, NedoOS `sd_boot.$C`, shadowed `term.com` |
| ACC-C2 | `ComposeAcceptance_Test.WildCommanderListsFilteredFat32` | ZX-Evo, Wild Commander |
| ACC-C3 | `ComposeAcceptance_Test.SprinterDssGraftedUtilFolder` | Sprinter, DSS on `dss_1_62_92.img` + folder |
| ACC-C4 | `ComposeAcceptance_Test.ProfiTwoPartitions` | Profi, PQ-DOS image + composed partition |
| ACC-C5 | `ComposeAcceptance_Test.EvoCdFromComposedIso` | ZX-Evo, ATAPI, NedoOS / ERS `AUTORUN.ZX` |
| ACC-C6 | `ComposeAcceptance_Test.FlatImageBootsAlone` | the ACC-C3 session flattened to `.img` and `.vhd` |
| ACC-C7 | `ComposeAcceptance_Test.DeltaSurvivesRestart` | re-create the emulator, insert, the guest reads its earlier write |

The guest check is screen text through the existing helpers (OCR-free, screen-memory based as in
the M1 ACC tests) or a file the guest writes, read back with `FatVolumeReader`.

## 5. Benchmarks

### 5.1 Binary, running, rules

- File: `core/benchmarks/emulator/io/compose_benchmark.cpp` (Google Benchmark, `-DBENCHMARKS=ON`).
- Run on a quiet machine, without lowered priority, per AGENTS.md:
  ```bash
  cmake -S . -B cmake-build-agent-release -G Ninja -DBENCHMARKS=ON   # once
  UNREAL_NICE=0 tools/build/build.sh core-benchmarks
  ./cmake-build-agent-release/bin/core-benchmarks --benchmark_filter='Compose|HostFolderFat' \
      --benchmark_repetitions=10 --benchmark_report_aggregates_only=true \
      --benchmark_out=scratch/bench/media-compose/run.json --benchmark_out_format=json
  python3 tools/bench/plot-media-compose.py scratch/bench/media-compose/run.json \
      --out scratch/bench/media-compose/
  ```
- A/B for NFR-P1 and NFR-P7: master vs branch, interleaved runs, medians of 10 repetitions,
  reported with the noise band (performance-guidelines §4).
- Fixture data for read benchmarks: a 64 MiB set of files (a 1 MiB file, 1 000 × 4 KiB files and
  one 16 MiB file). It is built once per process in scratch for host-folder modes. In-memory
  `MemoryDisk` images are used for image-source modes, so host page-cache effects stay out of the
  measurement. `MemoryDisk` is the *source* device, not the composite, so the composite's
  translation cost is still what is measured. A separate `/file` variant uses real files to show
  end-to-end numbers.

### 5.2 Modes compared

| Mode id | Medium under test |
|---|---|
| `raw` | `RawImage` of the same content (baseline, floor) |
| `hff` | `HostFolderFat` on master (C0 baseline) and on the branch (parity) |
| `c1f` | composite rebuild, 1 folder layer |
| `c8f` / `c64f` | composite rebuild, the same files spread over 8 / 64 folder layers |
| `cfat16` / `cfat32` | composite rebuild over a FAT16 / FAT32 image source |
| `cchd` | composite over a CHD hard-disk source (zlib, lzma) |
| `ciso` | composite FAT over an ISO source |
| `graft` | FAT32 base image + 1 folder layer (grafted) |
| `part2` | partitioned disk: FAT16 passthrough + FAT32 composite |
| `isot` | ISO target over folder + FAT sources (2 048-byte reads through `CdImage`) |
| `+sw{N}` | any of the above with `SessionWriteMap` holding N changed sectors (0, 1 K, 100 K) |

### 5.3 Benchmark families

| Family | Parameters | Metric (counters) |
|---|---|---|
| `BM_ComposeSeqRead/<mode>` | 1 MiB and 16 MiB files, sector by sector | ns / sector, MB/s (`bytes_per_second`), allocations / read (`HeapCounter`) |
| `BM_ComposeRandRead/<mode>` | 100 000 random data sectors (fixed seed) | ns / sector |
| `BM_ComposeMetaRead/<mode>` | FAT sectors, directory sectors, boot / FSInfo | ns / sector |
| `BM_ComposeDirWalk/<mode>` | `FatVolumeReader` listing of the whole tree | ms per walk |
| `BM_ComposeBuild/entries/layers` | entries 1 K … 1 M (×4), layers 1 / 4 / 16 / 64, synthetic trees | ms; `Complexity(benchmark::oNLogN)` |
| `BM_ComposeBuildMemory/entries` | same | bytes (counter from the pool / tree / layout sizes) |
| `BM_ComposeFragmented/extentsPerFile` | 1 … 4 096 | ns / sector (seq and random) |
| `BM_GraftBuild/baseEntries` | base 1 K … 1 M entries, fixed 100-file upper layer | ms |
| `BM_RebuildBuild/baseEntries` | the same content as rebuild | ms (contrast with graft) |
| `BM_Attribute/changedSectors` | 10 … 1 M changed sectors on a 100 K-entry volume | ms |
| `BM_Flatten/<format>` | `img`, `vhd`, `chd-zlib`, `compact` of a 256 MiB composite | MB/s, peak working set |
| `BM_SessionMapRead/changedSectors` | 0 … 1 M changes, read hit / miss | ns / sector (decides if `std::map` stays before H1) |

### 5.4 Charts

Generated by `tools/bench/plot-media-compose.py` (matplotlib; reads Google Benchmark JSON; writes
SVG and PNG plus a Markdown table of the numbers). Every chart has the run's git commit, host CPU
and date in its footer, and log scales where noted.

| Chart | X | Y | Series | Expected shape (the claim it tests) |
|---|---|---|---|---|
| **C1** build scalability | entries (log) | build time (log) | layers 1 / 4 / 16 / 64 | parallel lines of slope ≈ 1 (n log n); layer count shifts only slightly (NFR-P5) |
| **C2** read vs layers | layers 1 … 64 | ns / sector | seq, random, metadata | flat (NFR-P2) |
| **C3** read vs fragmentation | extents per file (log) | ns / sector | seq, random | seq flat (last-hit), random ∝ log k (NFR-P4) |
| **C4** memory | entries (log) | MiB (log) | rebuild, graft (upper only), ISO | linear; 100 K mark under the 32 MiB line (NFR-M2) |
| **C5** mode comparison | mode | MB/s (bar) | seq read, random read, each ±session map | composite modes within 10% of `hff`; `raw` as the ceiling |
| **C6** graft vs rebuild build | base entries (log) | build time (log) | graft, rebuild | graft flat, rebuild rising (NFR-P6) |
| **C7** flatten and attribution | changed sectors (log) / format | ms / MB/s | attribution; img, vhd, chd, compact | attribution sub-linear thanks to subtree skipping; img ≥ 80% raw copy (NFR-P8, P9) |
| **C8** change-layer cost | changed sectors (log) | ns / sector | hit, miss | log growth of `std::map`; input to H1 |

### 5.5 Results (filled in as phases land)

| NFR | Target | Measured | Commit | Chart |
|---|---|---|---|---|
| NFR-P1 parity data / metadata | ≤ 1.10× / ≤ 1.05× | — | — | C5 |
| NFR-P2 64 layers vs 1 | ≤ 1.15× | — | — | C2 |
| NFR-P3 allocations per read | 0 | — | — | — |
| NFR-P5 build 100 K entries | ≤ 1.5 s | — | — | C1 |
| NFR-P6 graft independent of base | flat | — | — | C6 |
| NFR-P7 non-composite media | no change (noise band) | — | — | A/B table |
| NFR-P8 flatten img | ≥ 80% raw copy | — | — | C7 |
| NFR-P9 attribution 10 K / 100 K | ≤ 500 ms | — | — | C7 |
| NFR-M2 memory 100 K entries | ≤ 32 MiB | — | — | C4 |

## 6. Tooling to add

| Tool | Purpose |
|---|---|
| `tools/bench/plot-media-compose.py` | JSON → charts C1-C8 + Markdown results table; `--compare a.json b.json` for A/B overlays |
| `core/tests/_helpers/isoimagebuilder.h` | `ScratchIsoImage`: deterministic ISO 9660 + Joliet images for tests |
| `core/tests/_helpers/composefixture.h` | synthetic trees, descriptor builders, `FatVolumeWriterForTests` |
| `.recipe/media/compose-media.md` | recipe: write a descriptor, insert, inspect layers / changes, flatten |
