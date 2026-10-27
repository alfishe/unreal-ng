# C4b — a graft enumerates its base lazily

**Status:** done, 2026-10-06 (as built: §7, measured: §8). Follow-up of [c4-graft.md](c4-graft.md) §8 ("the union still needs the whole base
enumerated, and that walk dominates both builds"). Owner request 2026-10-06.

Exit: a graft build reads only the base directories the upper layers touch (and their ancestors); its time grows
with the upper layers, not with the base (benchmark: a base of 20 000 files in 200 directories); provenance and
`media changes` give the same answers as today; a fallback to a rebuild still sees the whole base.

## 1. What the build needs from the base (survey 2026-10-06)

| Consumer | Needs | Lazy |
|---|---|---|
| `UnionBuilder::Merge` | the children of directories on the upper layers' mount paths and inside their subtrees; whiteout and opaque targets | expand those paths first |
| `GraftBuilder::Walk` | per base directory: is each entry still in the union (a layer-0 child of the same name)? It reads the directory from the image itself | an unexpanded directory is untouched: its entry is kept, no recursion |
| free map, FAT patches, boot sector | the image (`ScanFree`, FAT sectors) | unchanged |
| S4 `deleted` paths, boot union paths (`BootFileData`) | `tree.Find` + file extents | expand those paths first |
| `Validate` (entry and 4 GiB limits) | rebuild limits | a graft skips it for unexpanded directories (they are valid FAT already) |
| `CompositeInfo` counts | files / bytes per layer | a listing walk of the base (no FAT chains), §3 |
| `GraftVolume::OwnerOf` (provenance: C6b attribution, write-back's owner layer) | the directory and file-data sectors of every base directory and file, with the directory's first cluster | indexed from the image on the first query, §4 |
| rebuild fallback (`build: auto`) | the whole tree | re-enumerate the base fully, merge again |

The cost today is `FatImageSource::Enumerate` of the base: a FAT chain walk per file (`ChainExtents`), a node and
extents per file, then a second full copy of the base into the union (`CopySubtree`), a full `SortTree` and `Count`.

## 2. Lazy enumeration

- `TreeNode` gains `unexpanded` and `baseCluster` (the directory's first cluster in the base image). An unexpanded
  directory has no children in the tree; `CopySubtree` carries both fields.
- `FatImageSource::Enumerate` with `lazy`: the root's entries are read (files with their extents), every
  subdirectory is a node `unexpanded` with its cluster. `FatImageSource::Expand(tree, node)` reads one directory the
  same way (its files with extents, its subdirectories unexpanded).
- The factory, when a graft is possible (`build` auto or graft, the bottom layer a FAT image at `/` taken from `/`),
  enumerates the base lazily, then expands the base paths the upper layers will touch: for every upper layer, its
  mount path and every directory path inside its tree (both case-folded by the FAT key), its whiteouts and opaque
  paths; the descriptor's `deleted` paths; the boot section's union paths. Each path is expanded component by
  component as far as the base has directories. The union is merged as today: an unexpanded node an upper layer does
  not touch stays unexpanded in the union.
- `GraftBuilder::Walk`: a union child that is an unexpanded layer-0 directory of the same name means "untouched,
  not walked"; the base entry is kept as it is.

## 3. Counts

The base layer's and the union's file and byte counts come from a listing walk of the base (`ListDirectory` per
directory, sizes from the entries, no FAT chains), done at build. The union's counts are the base's plus the upper
layers' minus what they replaced (the merge reports both).

## 4. Provenance on demand

`GraftVolume::OwnerOf` for a sector of the data area that no index names, while the tree has unexpanded
directories, builds once (at the first such query) a run index of the unexpanded subtrees from the image: per
directory, its cluster chain (role Directory, its own first cluster); per file, its chain (role FileData, the
directory's first cluster, layer 0). The node is "none" (`HasNode() == false`); ChangeAttributor needs the
directory cluster and the layer, and resolves paths through the `..` chain from the image (C6b).
The cost moves from every build to the first `media changes`, write-back or attributed save of a medium.

## 5. Fallback to a rebuild

When the graft fails (`build: auto`), the factory enumerates the base fully and merges again before the rebuild:
the rebuild sees exactly today's union.

## 6. Tests and benchmark

| Test | Checks |
|---|---|
| `GraftVolume_Test.*`, `ComposeGraft_Test.*` | unchanged, on the lazy path |
| `GraftLazy_Test.OnlyTouchedDirectoriesRead` | a base of 200 directories; an upper layer writes into two: the tree holds nodes for those two and the root only; the image's other directories were not listed (a counting device) |
| `GraftLazy_Test.SameSectorsAsAFullEnumeration` | lazy and full enumeration give byte-identical volumes (every sector) for the graft tests' bases |
| `GraftLazy_Test.ProvenanceOfUntouchedBaseFiles` | `OwnerOf` of a file sector deep in an unexpanded directory: FileData, layer 0, the right directory cluster; ChangeAttributor reports a guest modify there as `[base layer]` |
| `GraftLazy_Test.FallbackRebuildSeesEverything` | a graft that cannot fit falls back; the rebuild has every base file |
| `GraftLazy_Test.CountsMatchFull` | the base layer's and union's counts equal a full enumeration's |
| benchmark | `ComposeBuild/graft` with 2 000 and 20 000 base files, before and after |

## 7. As built (2026-10-06)

| Piece | Where | Differences from the design |
|---|---|---|
| Lazy nodes | `TreeNode::unexpanded` / `baseCluster` (`filetree.h`), carried by `CopySubtree` | - |
| Lazy enumeration | `FatImageSourceOptions::lazy`; `FatImageSource::Enumerate` returns the volume device; `FatImageExpander` (`fatimagesource.{h,cpp}`): `Expand`, `ExpandPath` (components by the FAT key), `ExpandAll`, `Count` | - |
| When | `CompositeMediumFactory::Build`: the bottom layer is a FAT image at `/` from `/`, the build is `auto` or `graft`, not an ISO target, and `CompositeBuildOptions::lazyBase` (default true; tests and the benchmark turn it off) | **Not lazy when the base layer has `include` / `exclude` filters**: a graft releases what they leave out, in every directory, so it must see every directory |
| Touched paths | every directory of every upper layer (under its mount), whiteouts, opaque paths, the descriptor's `deleted` and `.attributes` paths, the boot section's union paths | - |
| Merge guard | `UnionBuilder::MergeDir` fails ("merges into a base directory that was not read") rather than merging into an unexpanded directory | - |
| Graft walk | `GraftBuilder::Walk` keeps an unexpanded layer-0 directory's entry as it is and does not recurse; its first cluster goes to `GraftVolume::_unexpanded` | - |
| Provenance | `GraftVolume::IndexUnexpanded` at the first `OwnerOf` miss: directory clusters (with their place in the chain) and file runs of every unread subtree, read from the image. `SectorOwner::unlisted` (no node; `layer` 0, `dirCluster`, `offset` hold) and `SectorOwner::Known()`; `ChangeAttributor` uses `Known()` | A directory sector's offset counts its place in the chain (the full build counts clusters by number; equal unless the directory is fragmented) |
| Counts | **Deferred, not a listing walk at build**: the build records the unread directories' clusters (`uncountedBase`, `uncountedUnion`) and sets `CompositeInfo::countLater`; `CompositeInfo::CompleteCounts()` lists them at the first call (the `layers` reply, `mediacontrol.cpp`) and adds them in. A partitioned disk chains each graft partition's counts | The design counted at build: measured, the listing walk was 87 % of what the lazy build had left (§8) |
| Rebuild fallback | `ExpandAll` on the base, the union merged again (its report lines are not repeated), the boot plan made again, the base layer counted in full | - |

Known difference from a full read: a base file whose cluster chain is broken is left out of a full read, so the graft
used to release its directory entry; under an untouched directory it now stays as it is.

Tests as built (`core/tests/emulator/io/storage/compose/graftlazy_test.cpp`): `GraftLazy_Test.OnlyTouchedDirectoriesRead`
(40 base directories, an upper layer writes into two: 39 unexpanded directories, under 60 tree nodes, deferred
counts complete to the full ones), `SameSectorsAsAFullEnumeration` and `MountedLayerAndOpaqueDirectory` (lazy and
full builds: every sector, the counts, `ContentId`, and `OwnerOf` of every sector agree; untouched directories are
attributed `unlisted`), `FallbackRebuildSeesEverything` (a full FAT16 root forces the rebuild: every base file is
there), `PartitionCountsCompleteOnTheDisk`, `GuestChangeInAnUntouchedDirectoryIsTheBaseLayers` (`media changes` names
the base layer for a guest write under an unread directory). `ComposedLayout_Test.GraftOwners` now expects an
untouched base directory and its file as `unlisted` (no tree node).

## 8. Measured (2026-10-06, Linux container, `ComposeBuild` in `graftvolume_benchmark.cpp`)

The base has 2 000 files in 20 directories or 20 000 in 200; one upper file is grafted at the root. Means of 3.

| Variant | 2 000 files | 20 000 files |
|---|---|---|
| graft, before (the base read in full; `graftFull` now) | 2.40 ms | 22.0 - 22.9 ms |
| graft, lazy, counts by a listing walk at build (first cut) | 1.07 ms | 7.58 ms |
| **graft, lazy, counts deferred (as built)** | **0.44 - 0.46 ms** | **0.93 - 1.01 ms** |
| graft, lazy, plus `CompleteCounts` (the first `layers` reply) | 1.06 ms | 7.1 ms |
| rebuild (for scale) | 4.3 - 4.6 ms | 29.9 - 31.0 ms |

What is left of the lazy build is the free-cluster scan of the FAT (`ScanFree`, linear in the clusters) and the root
directory: a 25x faster graft at 20 000 files. The cost moved to the first `layers` reply (about 6 ms for 20 000
files, once) and to the first provenance query that lands in an unread directory.
