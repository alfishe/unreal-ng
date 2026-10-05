# Multi-source media: architecture

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Requirements** | [goals-and-requirements.md](goals-and-requirements.md) |
| **Details** | [tdd.md](tdd.md) (classes, data structures, algorithms), [fs-compatibility.md](fs-compatibility.md), [flatten-strategies.md](flatten-strategies.md) |

## 0. Summary

A **composite medium** is built from a descriptor in three steps:

1. **Resolve.** Every layer's source is opened once. Each source is a host folder, a FAT image
   partition or an ISO image, and each is seen through one interface, `IFileTreeSource`, as a tree
   of entries whose data is a list of **extents** in that source.
2. **Union.** The layers are merged bottom-up into one `UnionTree`, under the target file system's
   name rules. Shadowing, whiteouts and filters are applied here, **once**. After this step there
   are no layers at read time, only a tree whose files point into sources.
3. **Synthesize.** A target builder lays the tree out:
   - `FatSynthVolume` builds a fresh FAT16 / FAT32 volume (rebuild).
   - `GraftVolume` builds the base FAT image plus a sparse patch (graft).
   - `IsoSynthVolume` builds an ISO 9660 + Joliet volume.
   - `PartitionedDisk` builds an MBR over child volumes.

   Each result is an `IBlockDevice` (or a CD frame source), so the existing block stack, slots,
   TTD tap and export code use it unchanged.

Guest writes land in the **single change layer** on top, which is `SessionWriteMap` today. They are
attributed to layers and files **only when asked** (`media changes`, flatten), from the layout's
provenance and a re-read of the merged volume. The read path has no per-layer cost and no copies.

## 1. Where it sits

```mermaid
flowchart TB
    subgraph Surfaces["Surfaces (unchanged pattern)"]
        WEB["WebAPI /media + OpenAPI"]
        CLI["CLI media"]
        MCP["MCP media"]
        SCR["Lua / Python media_*"]
        QT["Qt media panel<br/>+ layers / changes view"]
    end
    Surfaces --> MC["MediaControl"]
    MC --> MM["MediaManager"]
    MM -->|"MediaSourceType::Composite"| CMF["CompositeMediumFactory (new)"]
    MM -->|"File / Folder / Blank"| OLD["existing factories<br/>RawImage, HostFolderFat, CHD, CdImage"]
    CMF --> STACK
    OLD --> STACK
    subgraph STACK["Block stack of a Medium"]
        direction TB
        TAP["MediaReadTap (TTD)"] --> HOLD["HostWriteHold (WriteThrough only)"]
        HOLD --> ACC["ReadOnlyGuard | SessionWriteMap → MediaChangeLayer (H1)"]
        ACC --> SRC["Source device<br/>composite or classic"]
    end
    STACK --> SLOT["Slot: SdCardSpi / AtaDisk / AtapiCdrom"]
```

The slots, `MediaReadTap`, `HostWriteHold`, the access layers and `BlockFormats` are unchanged. A
composite is just another source device at the bottom of the stack.

## 2. Component view

```mermaid
flowchart LR
    subgraph Descriptor["Descriptor layer"]
        DESC["ComposeDescriptor<br/>parse YAML/JSON, normalize, validate keys"]
    end

    subgraph Sources["Source layer: one instance per source"]
        POOL["SourcePool<br/>dedups by canonical path + options"]
        HFS["HostFolderSource<br/>FolderSnapshot, host files"]
        FIS["FatImageSource<br/>FatVolumeReader + extent walk"]
        IIS["IsoImageSource<br/>Iso9660Reader, Joliet"]
        DEV["opened devices<br/>RawImage / ChdImage / CdImage"]
        POOL --> HFS & FIS & IIS
        FIS --> DEV
        IIS --> DEV
    end

    subgraph Union["Union layer: build time only"]
        UB["UnionBuilder<br/>shadow, whiteout, opaque, filters"]
        NE["NameEquivalence<br/>FAT: LFN+8.3, case fold / ISO: d-chars, Joliet"]
        UT["UnionTree<br/>nodes + FileData extents"]
        VAL["TargetValidator<br/>limits, sizes, names"]
        UB --> NE
        UB --> UT --> VAL
    end

    subgraph Targets["Target layer: IBlockDevice / IFrameSource"]
        FSV["FatSynthVolume<br/>rebuild FAT16/32"]
        GV["GraftVolume<br/>base + SectorPatchMap + graft runs"]
        ISV["IsoSynthVolume<br/>ISO 9660 + Joliet"]
        PD["PartitionedDisk<br/>MBR/EBR + SubRangeDevice"]
        XM["ExtentReader<br/>run → extent → source, zero-copy"]
        FSV --> XM
        GV --> XM
        ISV --> XM
    end

    subgraph Prov["Provenance and flatten: on demand"]
        PM["ProvenanceMap<br/>LBA → owner, computed from layout"]
        CA["ChangeAttributor<br/>changed sectors + re-read → file ops"]
        FP["FlattenPlanner<br/>ops → layers → strategy"]
        FX["Flatten strategies<br/>S1 flat image, S2 delta, S3 base commit, S4 file write-back"]
        PM --> CA --> FP --> FX
    end

    DESC --> POOL
    DESC --> UB
    HFS & FIS & IIS --> UB
    VAL --> FSV & GV & ISV
    FSV & GV --> PD
    XM --> DEV
    XM --> HFS
    FSV & GV & ISV & PD --> PM
```

| Component | Responsibility | Lives |
|---|---|---|
| `ComposeDescriptor` | Parse and normalize the descriptor. Paths are made absolute; defaults are filled in; unknown keys are reported. | `core/src/emulator/media/compose/` |
| `SourcePool` | Open each distinct source once, share it between layers and partitions, and own its lifetime. | same |
| `IFileTreeSource` + 3 implementations | A source as a tree of entries with metadata, and file data as extents. | `core/src/emulator/io/storage/compose/` |
| `UnionBuilder`, `UnionTree` | Merge the layers into one tree under the target's name equivalence, and record the provenance of every node. | same |
| `TargetValidator` | Check the union against the target's limits before any layout work. | same |
| `FatSynthVolume` | `HostFolderFat`'s layout engine, generalized to read file data from any source by extent. `HostFolderFat` becomes a thin wrapper. | `.../storage/fat/` |
| `GraftVolume` | A FAT image as the base, a sparse map of patched metadata sectors, and runs of grafted clusters. | `.../storage/compose/` |
| `IsoSynthVolume` | An ISO 9660 Level 1/2 + Joliet layout over the union tree, given to `CdImage` as a cooked 2048 frame source. | `.../storage/cd/` |
| `PartitionedDisk`, `SubRangeDevice` | MBR / EBR synthesis and LBA range routing to child devices. | `.../storage/` |
| `ExtentReader` | The shared hot path: target run → file extent → source read into the caller's buffer. | `.../storage/compose/` |
| `ProvenanceMap`, `ChangeAttributor`, `FlattenPlanner` | Who owns a sector, what changed in which file, and where each change should go. | `.../storage/compose/` |
| `CompositeMediumFactory` | The glue to `MediaManager`: build the stack, content id, report, and save / flatten dispatch. | `core/src/emulator/media/` |

## 3. Data model

```mermaid
classDiagram
    class IFileTreeSource {
        <<interface>>
        +Root() SourceNode
        +ReadData(FileData, offset, dst, bytes) bool
        +Identity() uint64
        +Describe() string
    }
    class HostFolderSource
    class FatImageSource
    class IsoImageSource
    IFileTreeSource <|.. HostFolderSource
    IFileTreeSource <|.. FatImageSource
    IFileTreeSource <|.. IsoImageSource

    class UnionNode {
        uint32 parent
        uint32 firstChild, childCount
        uint16 layer
        uint8 kind
        uint8 attributes
        int64 mtimeUtc
        uint64 size
        uint32 fileData
        NameRef targetName
    }
    class FileData {
        uint16 source
        uint8 storage
        uint32 firstExtent, extentCount
        uint64 bytes
    }
    class Extent {
        uint64 sourceLba
        uint32 sectors
        uint32 fileSectorStart
    }
    class UnionTree {
        vector~UnionNode~ nodes
        vector~FileData~ data
        vector~Extent~ extents
        NamePool names
        BuildReport report
    }
    UnionTree o-- UnionNode
    UnionTree o-- FileData
    FileData o-- Extent

    class IBlockDevice {
        <<interface>>
    }
    class FatSynthVolume
    class GraftVolume
    class PartitionedDisk
    class IsoSynthVolume
    IBlockDevice <|.. FatSynthVolume
    IBlockDevice <|.. GraftVolume
    IBlockDevice <|.. PartitionedDisk
    FatSynthVolume --> UnionTree
    GraftVolume --> UnionTree
    IsoSynthVolume --> UnionTree
```

**Key points**

- The tree is **flat arrays** (structure of indices, no per-node heap objects). Names live in one
  string pool. That gives the memory bound NFR-M2 and cache-friendly build passes.
- `FileData` says where a file's bytes are:
  - `HostFile`: a host path, read with the shared LRU of open streams.
  - `DeviceExtents`: sector extents on an opened device (FAT clusters or ISO blocks, coalesced).
  - `Zero`: a sparse or empty file.
- Every node keeps the **layer** it came from. Provenance at flatten time needs no other record.

## 4. Build workflow

```mermaid
flowchart TD
    A["insert / compose: descriptor path or inline JSON"] --> B["ComposeDescriptor::Parse<br/>normalize paths, defaults"]
    B -->|"bad keys / values"| R1["report, continue"]
    B --> C{"target.partition<br/>has partitions?"}
    C -->|"yes"| P["for each partition:<br/>passthrough → SubRangeDevice<br/>compose → recurse (no MBR)"]
    P --> PD["PartitionedDisk: MBR / EBR"]
    C -->|"no"| D["SourcePool: open each source once<br/>folder scan · FAT open · ISO open"]
    D -->|"unreadable"| E1["fail: UnreadableSource"]
    D --> F["each layer → SourceNode tree<br/>apply from / include / exclude / manifest"]
    F --> G["UnionBuilder: bottom → top<br/>shadow · merge dirs · opaque · whiteout"]
    G -->|"conflict: error"| E2["fail: BadRequest + conflicting paths"]
    G --> H["NameEquivalence: target names<br/>LFN + 8.3 / ISO + Joliet, collisions"]
    H --> I["TargetValidator<br/>sizes, counts, depth, cluster range"]
    I -->|"violation"| E3["fail: DoesNotFit + entry + limit"]
    I --> J{"strategy"}
    J -->|"auto: base is FAT, same type, fits"| K["GraftVolume::Build<br/>free-cluster map, patch dirs + FAT"]
    J -->|"rebuild / auto fallback"| L["FatSynthVolume::Build<br/>runs, dirs, FAT formula"]
    J -->|"iso9660"| M["IsoSynthVolume::Build<br/>PVD, SVD, path tables, dirs"]
    K --> N["ContentId = H(descriptor, identities, options)"]
    L --> N
    M --> N
    PD --> N
    N --> O["CompositeMediumFactory: stack + Medium + report"]
    O --> Q["MediaManager queues the insert at the frame boundary"]
```

Everything up to `O` runs **off the emulation thread**. The folder scans and image opens can take a
long time on network folders. They support `cancelRequested` / `onProgress` as `FolderSnapshot`
does (BUGS.md #3). Only the final swap happens on the emulation thread at the frame boundary, as
today.

## 5. Read path

### 5.1 Rebuild volume (FAT target)

```mermaid
sequenceDiagram
    autonumber
    participant Slot as AtaDisk / SdCardSpi
    participant Tap as MediaReadTap
    participant CL as SessionWriteMap
    participant V as FatSynthVolume
    participant X as ExtentReader
    participant S as Source (RawImage / ChdImage / host file)

    Slot->>Tap: ReadSector(lba, dst)
    Tap->>CL: ReadSector(lba, dst)
    alt sector changed by the guest
        CL-->>Tap: copy from change map
    else unchanged
        CL->>V: ReadSector(lba, dst)
        alt metadata region (MBR, boot, FSInfo, FAT)
            V->>V: synthesize into dst (formula, no table)
        else directory cluster
            V->>V: memcpy from generated directory bytes
        else file data cluster
            V->>X: Read(run, sectorInFile, dst)
            X->>X: last-hit cache, else binary search run, extent
            alt DeviceExtents
                X->>S: ReadSector(sourceLba, dst)
            else HostFile
                X->>S: pread(offset, 512) into dst, zero-pad tail
            end
        else free cluster
            V->>V: zero dst
        end
    end
```

There is exactly **one copy**, from the source into `dst`, which is the read itself. The union, the
layers and the name mapping cost nothing here; they were resolved at build time.

### 5.2 Graft volume

```mermaid
sequenceDiagram
    autonumber
    participant CL as SessionWriteMap
    participant G as GraftVolume
    participant P as SectorPatchMap
    participant X as ExtentReader
    participant B as Base image device

    CL->>G: ReadSector(lba, dst)
    G->>P: find(lba)
    alt patched (FAT sector, directory sector, FSInfo)
        P-->>G: memcpy 512 from patch
    else inside a grafted cluster run
        G->>X: Read(graftRun, sectorInFile, dst)
        X->>X: source device or host file → dst
    else everything else
        G->>B: ReadSector(lba, dst)
    end
```

The patch map is a sorted vector of LBAs plus a slab of 512-byte sectors (built once, read only).
Grafted runs are a sorted vector. Both are binary-searched; the base needs no lookup at all.

### 5.3 ISO target

`IsoSynthVolume` is a `cd::IFrameSource` with `StoredFormat::Cooked2048`, so `CdImage` serves READ
(10), READ CD, TOC and the 512-byte `IBlockDevice` view exactly as for an `.iso` file. A 2048-byte
block of file data maps to four 512-byte source sectors (FAT or host file), or to one 2048-byte
block of an ISO source. Either way the bytes land directly in the caller's buffer.

### 5.4 Partitioned disk

```mermaid
sequenceDiagram
    autonumber
    participant CL as SessionWriteMap
    participant PD as PartitionedDisk
    participant C as child (SubRangeDevice / FatSynthVolume / GraftVolume)

    CL->>PD: ReadSector(lba, dst)
    alt lba == 0 or an EBR sector
        PD->>PD: synthesize partition table into dst
    else lba inside partition k
        PD->>C: ReadSector(lba - start[k], dst)
    else gap / alignment padding
        PD->>PD: zero dst
    end
```

## 6. Write path and provenance

```mermaid
sequenceDiagram
    autonumber
    participant Guest as Guest OS (FAT driver)
    participant Slot as AtaDisk
    participant CL as SessionWriteMap
    participant User as User / automation
    participant MM as MediaManager
    participant CA as ChangeAttributor
    participant PM as ProvenanceMap
    participant R as FatVolumeReader

    Guest->>Slot: WRITE SECTORS
    Slot->>CL: WriteSector(lba, src)
    CL->>CL: store in sparse map (drop if equal to source)
    Note over CL: sources untouched, no per-write attribution cost

    User->>MM: media changes sd.zc
    MM->>CA: Attribute(changeLayer, layout)
    CA->>PM: Owner(lba) for each changed sector
    PM-->>CA: Fat / Dir(node) / FileData(node, layer) / Free / BaseSector
    CA->>R: re-read merged volume (source + changes)
    R-->>CA: directory tree T1
    CA->>CA: diff T0 (union tree) vs T1<br/>created / modified / deleted / renamed
    CA-->>MM: ChangeSet: op, path, layer, bytes, sectors
    MM-->>User: JSON / table
```

Attribution is **pull-based**. The guest write path stays as it is today: one sparse-map insert.
The cost moves to the moment someone asks (NFR-P9).

## 7. Flatten workflow

```mermaid
flowchart TD
    A["media flatten slot --strategy X [--plan]"] --> B["ChangeAttributor → ChangeSet"]
    B --> C["FlattenPlanner: route each op<br/>(layer writable? copy-up? whiteout?)"]
    C --> D{"--plan"}
    D -->|"yes"| E["print plan: op, path, from layer → to target, bytes"]
    D -->|"no"| F{"strategy"}
    F -->|"S1 flat image"| G["BlockFormats::Write(whole stack)<br/>.img / .vhd / .chd, temp + rename"]
    F -->|"S1 --compact"| G2["re-synthesize FatSynthVolume from T1<br/>then write"]
    F -->|"S2 session delta"| H["write change map to descriptor.delta<br/>keyed by ContentId"]
    F -->|"S3 base commit"| I["journal overwritten base sectors<br/>write patch + grafted data + changes"]
    F -->|"S4 file write-back"| J["stage files in temp dir<br/>conflict check vs snapshot<br/>rename into host layers, whiteouts into descriptor"]
    G --> K["change layer kept (export) or emptied (save)"]
    G2 --> K
    H --> K
    I --> L["base is self-contained; upper layers emptied<br/>descriptor rewritten; rebuild"]
    J --> M["rescan + rebuild; change layer emptied"]
```

The strategies, their trade-offs and the routing table are in
[flatten-strategies.md](flatten-strategies.md).

## 8. Insert sequence (end to end)

```mermaid
sequenceDiagram
    autonumber
    participant UI as Surface (CLI / WebAPI / Qt)
    participant MC as MediaControl
    participant MM as MediaManager
    participant F as CompositeMediumFactory
    participant D as ComposeDescriptor
    participant SP as SourcePool
    participant UB as UnionBuilder
    participant T as Target builder
    participant EMU as Emulation thread

    UI->>MC: media insert ide0.master games.ucompose.yaml
    MC->>MM: Insert(slot, {Composite, path})
    MM->>F: Build(source, slotKind, access)
    F->>D: Parse + normalize
    D-->>F: descriptor + report
    F->>SP: Open(layer sources)
    SP-->>F: IFileTreeSource × N (deduplicated)
    F->>UB: Merge(layers, targetRules)
    UB-->>F: UnionTree + report
    F->>T: Build(tree, targetOptions)
    T-->>F: IBlockDevice + layout facts
    F-->>MM: Medium(stack, contentId, report)
    MM->>MM: same-source rule per layer, recording guard
    MM->>EMU: queue swap at frame boundary
    EMU-->>MM: inserted
    MM-->>MC: MediaResult (report: strategy chosen, shadowed, renamed, skipped)
    MC-->>UI: result
```

## 9. Threading, lifetime, identity

| Topic | Rule |
|---|---|
| Build | Off the emulation thread, cancellable, with progress (as the folder pipeline). |
| Read / write | Emulation thread only, as every block device today. The composite is immutable after the build except for the host-file LRU and the last-hit caches, which are per instance and need no locks. |
| Source lifetime | `SourcePool` holds `shared_ptr`s. The medium owns the pool. Eject destroys it and closes every file. |
| Attribution / flatten | Runs with the emulation paused (as `BlockFormats::Save` requires today) or on a frozen copy of the change map (H1 versions make that cheap). |
| `ContentId` | Hash of the normalized descriptor, each source's identity (`FolderSnapshot::Identity`, image `ContentId`) and the build options. Equal ids mean byte-identical media, so a persisted delta (S2) or a snapshot reference is valid. |
| TTD | Unchanged: `MediaReadTap` above the composite records and replays reads; guest writes are barriers as today. |

## 10. Relationship to the media history (H1-H5)

The composite needs only what `SessionWriteMap` has today: a sparse map of changed sectors and a
`Changes()` iterator. When H1 replaces it with `MediaChangeLayer` (versions, spill to disk), the
composite gains versioned attribution for free: `media changes --at v3`. The persisted delta (S2)
becomes the H1 spill file. Nothing in this design blocks H1 or depends on it.

## 11. What changes in existing code

| Code | Change |
|---|---|
| `HostFolderFat` | Layout engine moved into `FatSynthVolume`. `HostFolderFat::Build` becomes "one `HostFolderSource` layer → union → `FatSynthVolume`", with byte-identical output (parity test over the existing test corpus). |
| `FatVolumeReader` | New: enumerate a file's cluster chain as coalesced extents (`ChainExtents`). New: free-cluster bitmap (`ScanFree`). New: per-directory cluster list (graft patching). Read-only, no behavior change for current callers. |
| `MediaSourceType` | New value `Composite`. `formatHint` `"compose"`. |
| `MediaManager` / `MediaControl` | Dispatch to `CompositeMediumFactory`; new verbs `compose`, `layers`, `changes`, `flatten`. |
| `BlockFormats` | New writer: fixed VHD (`.vhd`): raw data + a 512-byte `conectix` footer with CHS geometry. |
| `IBlockDevice` | Optional, phase C9: `ReadSectors(lba, count, dst)` with a default loop, overridden by `RawImage` and the composite for bulk reads. A/B gated (NFR-P7). |
| Nothing else | Slots, peripherals, TTD tap, model switch, config parsing for other keys. |

## 12. Decision trees

Every policy of this design is a decision tree, drawn next to the rules it implements. This
section maps when each one runs and adds the two that belong to the medium's life cycle.

### 12.1 Which tree runs when

```mermaid
flowchart LR
    subgraph Build["insert / compose / rescan (off the emulation thread)"]
        direction TB
        B7["DT-7 layer × target supported?"] --> B1["DT-1 entry admitted?"]
        B1 --> B2["DT-2 merge policy"]
        B2 --> B3["DT-3 target names"]
        B3 --> B4["DT-4 rebuild / graft / ISO / partitions"]
        B4 --> B6["DT-6 target file system"]
        B6 --> B5["DT-5 boot structures"]
        B5 --> B13["DT-13 delta restore"]
    end
    subgraph Run["guest running"]
        direction TB
        R1["writes → change layer<br/>(no policy on the hot path)"]
    end
    subgraph Ask["media changes"]
        direction TB
        A8["DT-8 classify entries"]
    end
    subgraph Leave["save / eject / insert over / flatten"]
        direction TB
        L15["DT-15 disposition"] --> L9["DT-9 save strategy"]
        L9 --> L14["DT-14 S3 preconditions"]
        L9 --> L10["DT-10 S4 routing"]
        L10 --> L11["DT-11 delete policy"]
        L10 --> L12["DT-12 plan gate, conflicts"]
        L11 --> L12
        L9 --> L8["DT-8 (S3, S4)"]
    end
    Build --> Run --> Ask
    Run --> Leave
    L16["DT-16 rescan"] --> Leave
    L16 --> Build
```

| Tree | Policy | Where |
|---|---|---|
| DT-1 | entry admission: from, service files, links, include / exclude, manifest, limits | [tdd.md](tdd.md) §3.2 |
| DT-2 | merge: whiteout, opaque, shadow / keep-lower / error, directory merge | [tdd.md](tdd.md) §3.2 |
| DT-3 | target names: LFN / 8.3, ISO / Joliet, `onBadName` | [tdd.md](tdd.md) §3.3 |
| DT-4 | build strategy: partitions, ISO, graft or rebuild, `auto` fallbacks | [tdd.md](tdd.md) §6.3 |
| DT-5 | boot structures: boot layer, bottom source, relocation (D-6) | [tdd.md](tdd.md) §5 |
| DT-6 | target file system: slot `fsCompatibility` / `defaultFs`, `auto` | [fs-compatibility.md](fs-compatibility.md) §6 |
| DT-7 | which source may feed which target | [fs-compatibility.md](fs-compatibility.md) §3 |
| DT-8 | change attribution: create / modify / delete / rename / attributes | [flatten-strategies.md](flatten-strategies.md) §2 |
| DT-9 | save strategy: GUI dialog, automation policy, eject rule (D-7, D-8) | [flatten-strategies.md](flatten-strategies.md) §3 |
| DT-10 | S4 routing per operation | [flatten-strategies.md](flatten-strategies.md) §3 S4 |
| DT-11 | delete policy `onDelete` (D-4) | [flatten-strategies.md](flatten-strategies.md) §3 S4 |
| DT-12 | S4 plan gate: inconsistent guest FS, host names, conflicts | [flatten-strategies.md](flatten-strategies.md) §3 S4 |
| DT-13 | S2 delta on insert | [flatten-strategies.md](flatten-strategies.md) §3 S2 |
| DT-14 | S3 preconditions and journal recovery | [flatten-strategies.md](flatten-strategies.md) §3 S3 |
| DT-15 | eject / insert-over disposition of a composite | below |
| DT-16 | rescan with pending changes | below |

### 12.2 DT-15: a composite leaves its slot (eject, insert over it, model switch that drops it)

```mermaid
flowchart TD
    A["eject / insert over / detach"] --> R{"TTD recording?"}
    R -->|"yes, no endRecording"| RF["refuse: 'recording'"]
    R -->|"no / endRecording"| D{"change layer dirty?"}
    D -->|"no"| GO["remove; SourcePool closes every source"]
    D -->|"yes"| P{"disposition"}
    P -->|"none"| I{"interactive?"}
    I -->|"yes"| Q["Qt prompt: save (→ DT-9) / export / discard / cancel"]
    I -->|"no"| DR["refuse: 'dirty' (retry with a disposition)"]
    P -->|"save"| S9["DT-9 (on eject: S3 / S4 only with an explicit strategy)"]
    P -->|"export"| EX["S1 to exportPath"]
    P -->|"discard"| DS["drop the change layer (a delta file on disk is kept)"]
    S9 -->|"done"| GO
    S9 -->|"refused"| DR2["eject refused: medium stays, reason reported"]
    EX -->|"done"| GO
    EX -->|"failed"| DR2
    DS --> GO
    Q -->|"cancel"| STAY["medium stays"]
```

A model switch (M5) keeps a composite whose slot exists on the new model, changes included. When
the slot does not exist there, the switch applies DT-15 with the switch's own disposition.

### 12.3 DT-16: rescan (FR-54)

```mermaid
flowchart TD
    A["media rescan slot"] --> B["re-scan folder layers, re-open image layers"]
    B --> C{"new ContentId equals the current one?"}
    C -->|"yes"| N["nothing to do · report 'unchanged'"]
    C -->|"no"| D{"change layer dirty?"}
    D -->|"no"| R["rebuild; swap at the frame boundary (eject + insert for the guest)"]
    D -->|"yes"| P{"disposition given?"}
    P -->|"no"| I{"interactive?"}
    I -->|"yes"| Q["prompt: save (DT-9) / export / discard / cancel"]
    I -->|"no"| DR["refuse 'dirty': the old sectors would land on moved files"]
    P -->|"save / export / discard"| X["apply it (DT-15 branches), then rebuild"]
    Q -->|"chosen"| X
```

Changes cannot be carried across a rebuild with a different `ContentId`: every cluster may have
moved. Only S3 / S4 (which turn changes into source content first) or S1 (a copy) keep them.
