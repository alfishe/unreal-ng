# Symbol exchange: architecture

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Requirements** | [goals-and-requirements.md](goals-and-requirements.md) |
| **Next** | [formats.md](formats.md), [tdd.md](tdd.md) |

## 1. In one paragraph

Bytes come from a **source** (a host file, a file inside a disk image, an upload, the emulated memory, a bundle).
A **detector** asks every registered **format** how well it recognizes them. The chosen format's **importer**
turns them into **records**: text formats through the shared **tokenizer**, tokenized ZX formats through their
**detokenizer**, live label tables through a **scanner**. A **normalizer** binds each record to an address space
and checks its name; a **merger** puts the records into a **set** of the **symbol store** by the merge policy.
Consumers (disassembler, breakpoints, call trace, PC history, debugger skins, automation) read the store's
immutable **index**. An **exporter** goes the other way: a filtered view of the store, the target's **name rules**,
then the target's writer.

## 2. Component view

```mermaid
flowchart LR
    subgraph Sources
        F["host file"]
        D["file in a disk image<br/>TR-DOS · FAT"]
        U["upload<br/>(automation bytes)"]
        M["emulated memory<br/>(live)"]
        B["bundle<br/>data/symbols + manifest"]
        E["user edits"]
    end

    subgraph Import["Import pipeline (no emulator types)"]
        R["ByteSource"]
        DET["Detector<br/>score every format"]
        T["Tokenizer<br/>(text formats)"]
        DT["Detokenizer<br/>(tokenized formats)"]
        SC["LiveScanner<br/>(label tables in RAM)"]
        REC["SymbolRecords<br/>+ Diagnostics"]
        N["Normalizer<br/>address space · name check"]
        MG["Merger<br/>policy · conflicts"]
    end

    subgraph Store["SymbolStore (per CPU)"]
        S1["sets: game.sym · rom:48k · user · live:alasm"]
        IX["immutable Index<br/>by address · by name"]
    end

    subgraph Export["Export pipeline"]
        V["View<br/>filter · sets · spaces"]
        NR["NameRules<br/>of the target"]
        W["Writer<br/>of the target format"]
    end

    F & D & U --> R
    B --> R
    R --> DET
    DET -->|"text"| T
    DET -->|"tokenized"| DT
    M --> SC
    T & DT & SC --> REC
    E --> MG
    REC --> N --> MG --> S1 --> IX
    IX --> C["Consumers<br/>disasm · breakpoints · call trace · PC history<br/>TTD · debugger skins · automation"]
    S1 --> V --> NR --> W --> O["file · bytes"]
    LM["LabelManager<br/>(facade, existing API)"] --- Store
```

Everything inside "Import pipeline", "Store" and "Export pipeline" uses only the standard library. The emulator is
reached through three adapters ([§9](#9-code-placement)): a byte source for files inside disk images, a memory view
for live scans, and the `LabelManager` facade with the CPU's paging for page-aware lookup.

## 3. Data model

```mermaid
classDiagram
    class SymbolStore {
        +sets : vector~SymbolSet~
        +index() Index
        +Import(source, options) ImportReport
        +Export(view, format, options) ExportReport
        +Add / Update / Remove(symbol)
    }
    class SymbolSet {
        +id : string
        +title : string
        +origin : Origin
        +priority : int
        +enabled : bool
        +options : ImportOptions
        +symbols : vector~Symbol~
    }
    class Symbol {
        +name : InternedName
        +location : Location
        +kind : SymbolKind
        +size : uint32
        +scope : Scope
        +module : InternedName
        +source : SourceRef
        +comment : string
        +aliases : vector~InternedName~
        +enabled : bool
        +provenance : Provenance
    }
    class Location {
        +space : AddressSpace
        +offset : uint32
    }
    class AddressSpace {
        +cpu : CpuId
        +kind : CpuView | Rom | Ram | Cache | Device | Constant
        +page : uint16
        +region : InternedName
    }
    class SourceRef {
        +file : InternedName
        +line : uint32
        +column : uint16
    }
    class Provenance {
        +set : string
        +importer : FormatId
        +rawText : string
    }
    class Origin {
        +kind : File | DiskFile | Upload | Live | Bundle | User
        +where : string
        +sha256 : string
    }
    class Index {
        +byLocation : sorted vector
        +byName : hash map
        +Find(name) Symbol*
        +At(location) Symbol*
        +Nearest(location) (Symbol*, offset)
    }
    SymbolStore "1" o-- "*" SymbolSet
    SymbolSet "1" o-- "*" Symbol
    SymbolSet --> Origin
    Symbol --> Location
    Location --> AddressSpace
    Symbol --> SourceRef
    Symbol --> Provenance
    SymbolStore --> Index : builds
```

### 3.1 Address spaces, with examples

| Space | Written | Example | Meaning |
|---|---|---|---|
| CPU view | `cpu:main` | `PRINT_SCR = cpu:#0DAF` | whatever page window 0 shows: a symbol valid in any page |
| ROM page | `rom2` | `TRDOS_ENTRY = rom2:#3D2F` | ROM page 2 at offset `#3D2F`, wherever it is mapped |
| RAM page | `ram3` | `PLAYMUS = ram3:#0000` | RAM page 3, offset 0; at `#C000` when page 3 is in window 3 |
| Cache page | `cache0` | | ZX-Evo / TS-Conf cache RAM |
| Device region | `vram`, `cram`, `eeprom` | `PEN_PAPER5 = vram:#17F0` | memory a device owns (`DeviceMemory`) |
| Constant | `const` | `SCREEN_LEN = const:6912` | a number, not a place (an `EQU` used as a size) |
| Another CPU | `cpu:gs`, `gs.rom0` | `GS_PLAY = gs.rom0:#0123` | the General Sound card's CPU and memory |

A **CPU-view lookup** (the disassembler at `#C010`) first asks the machine which page window 3 shows (say RAM 3),
then looks up `ram3:#0010`, then `cpu:#C010`. Page symbols win over CPU-view symbols at the same place.

### 3.2 Kinds and scopes

| Kind | From | Example |
|---|---|---|
| `code` | a label on an instruction | `PLAYMUS` |
| `data` | a label on `DB` / `DW` / `DS` | `SCORE` |
| `const` | `EQU` / `=` of a number | `LINES EQU 24` |
| `port` | a port number (I/O map) | `AY_REG = port:#FFFD` |
| `entry` | a documented entry point (ROM maps) | `START` |
| `local` | a local label (`.loop`, `@1`, `1$`) | `PLAYMUS.loop` |

A `local` symbol keeps its parent (`scope.parent = PLAYMUS`), so exporters can write it in the target's own local
syntax or fold it to `PLAYMUS_loop`.

## 4. Import workflow

```mermaid
flowchart TD
    A["Import(source, options)"] --> B{"format forced?"}
    B -->|"yes"| F["that format"]
    B -->|"no"| C["probe: first 4 KB + extension + origin"]
    C --> D["every format: Detect(probe) → score 0..100"]
    D --> E{"best ≥ 60 and<br/>≥ 15 above the next?"}
    E -->|"yes"| F
    E -->|"no"| X["stop: 'ambiguous' / 'unknown'<br/>+ the candidate list"]
    F --> G{"family"}
    G -->|"text"| T["Tokenizer → line grammar of the format"]
    G -->|"tokenized"| K["Detokenizer → label table / source records"]
    G -->|"live"| L["LiveScanner over a MemoryView"]
    T & K & L --> R["SymbolRecords + Diagnostics"]
    R --> N["Normalizer:<br/>space = record's own, else options.space, else cpu:main<br/>offset += options.base<br/>name check (empty, too long, duplicate in the file)"]
    N --> M["Merger into the target set (options.policy)"]
    M --> I["new immutable Index → atomic swap"]
    I --> Z["ImportReport: format, confidence, counts,<br/>conflicts, warnings (with line numbers)"]
```

## 5. Sequences

### 5.1 Import a file

```mermaid
sequenceDiagram
    autonumber
    participant C as Caller (WebAPI / CLI / MCP / Lua / Python / Qt)
    participant LM as LabelManager (facade)
    participant ST as SymbolStore
    participant REG as CodecRegistry
    participant FMT as Codec (e.g. sjasmplus .sym)
    participant EMU as Emulator
    C->>LM: ImportSymbols("game.sym", {format: auto, set: "game.sym"})
    LM->>ST: Import(FileSource, options)
    ST->>REG: Detect(probe)
    REG-->>ST: sjasmplus-sym (score 95), z80asm-sym (40)
    ST->>FMT: Decode(source)
    FMT-->>ST: 812 records, 1 warning (line 77: duplicate)
    ST->>ST: normalize, merge (policy both)
    ST->>EMU: RunAtCoherentMoment: swap Index
    ST-->>LM: ImportReport
    LM->>EMU: post labels_changed {cpu: main}
    LM-->>C: report (format, 812 added, 0 conflicts, 1 warning)
```

The index swap runs at a coherent moment (`Emulator::RunAtCoherentMoment`), so the emulation thread never sees a
half-built index; the parsing itself runs on the caller's thread.

### 5.2 Live scan of an assembler's label table

```mermaid
sequenceDiagram
    autonumber
    participant C as Caller
    participant ST as SymbolStore
    participant SC as LiveScanner (alasm)
    participant MV as MemoryView (adapter)
    participant EMU as Emulator
    C->>ST: Import(LiveSource{scanner: alasm}, options)
    ST->>EMU: RunAtCoherentMoment
    EMU->>MV: snapshot every RAM page (copy, read-only)
    ST->>SC: Find(MemoryView)
    SC-->>ST: candidates [{page 6, #0000, 412 labels, score 90}]
    alt one candidate
        ST->>SC: Read(candidate)
        SC-->>ST: 412 records
    else several
        ST-->>C: candidates (the caller picks one: --at ram6:#0000)
    end
    ST-->>C: ImportReport
```

### 5.3 Export to another tool

```mermaid
sequenceDiagram
    autonumber
    participant C as Caller
    participant ST as SymbolStore
    participant NR as NameRules (sjasmplus)
    participant FMT as Codec (sjasmplus-equ)
    C->>ST: Export(view{sets: all, space: cpu+ram3}, "sjasmplus-equ", file)
    ST->>ST: resolve the view (enabled sets, filter)
    ST->>NR: check every name
    NR-->>ST: PRINT-A-1 → PRINT_A_1, IF → IF_ (reserved), 2 renames
    ST->>FMT: Encode(records, renames)
    FMT-->>ST: bytes
    ST-->>C: ExportReport (340 written, 2 renamed, 3 not representable: page symbols folded to comments)
```

### 5.4 ROM bundle at machine start

```mermaid
sequenceDiagram
    autonumber
    participant EMU as Emulator (start / ROM change)
    participant BM as BundleManager
    participant MAN as data/symbols/manifest.json
    participant ST as SymbolStore
    EMU->>BM: RomChanged(pages, SHA-256 per page)
    BM->>MAN: lookup each SHA-256
    MAN-->>BM: 48k_rom.map → rom0, 48k_variables.map → cpu
    BM->>ST: Import(Bundle, {set: "rom:48k", space: rom0, policy: replace})
    BM->>ST: drop bundle sets of ROMs no longer present
```

## 6. Decision trees

### DT-1: which address space a record gets

```mermaid
flowchart TD
    A["record"] --> B{"record carries a page?<br/>(ROMn:, PP:XXXX, bank field)"}
    B -->|"yes"| P["that page"]
    B -->|"no"| C{"options.space given?<br/>(--page ram3)"}
    C -->|"yes"| Q["options.space (+ options.base)"]
    C -->|"no"| D{"kind = const?"}
    D -->|"yes"| K["const"]
    D -->|"no"| E{"options.bindToCurrentPaging?"}
    E -->|"yes"| F["the page the window shows now<br/>(snapshot at import)"]
    E -->|"no"| G["cpu:main (CPU view)"]
```

### DT-2: merge one record into a set

```mermaid
flowchart TD
    A["record name N at location L"] --> B{"N already in the set?"}
    B -->|"no"| C{"another name at L?"}
    C -->|"no"| ADD["add"]
    C -->|"yes"| D{"policy"}
    D -->|"both (default)"| AL["add N; the two are aliases at L"]
    D -->|"keep"| SK["skip N (reported)"]
    D -->|"replace"| RP["replace the old name"]
    D -->|"fail"| FL["stop: conflict"]
    B -->|"yes, same L"| SAME["update fields that were empty"]
    B -->|"yes, other L"| MOVE{"policy"}
    MOVE -->|"keep / both"| KM["keep the old L, report 'moved'"]
    MOVE -->|"replace"| RM["move N to L, report"]
    MOVE -->|"fail"| FL
```

### DT-3: a name the export target cannot take

```mermaid
flowchart TD
    A["name"] --> B{"valid for the target?<br/>(charset · first char · length · case · reserved)"}
    B -->|"yes"| OK["as is"]
    B -->|"no"| C["mangle: invalid char → '_', leading digit → '_' prefix,<br/>reserved word → suffix '_', too long → cut + '_' + 4-hex hash"]
    C --> D{"collides with another name?"}
    D -->|"yes"| E["append _2, _3, ..."]
    D -->|"no"| F["renamed"]
    E --> F
    F --> G["listed in the ExportReport (and as a comment in the file when the format allows)"]
```

### DT-4: a symbol the export format cannot hold

```mermaid
flowchart TD
    A["symbol"] --> B{"format holds its space?<br/>(pages, devices, other CPU)"}
    B -->|"yes"| W["write"]
    B -->|"no"| C{"options.unrepresentable"}
    C -->|"fold (default)"| F["page symbol → CPU address if the page is mapped now,<br/>else skipped"]
    C -->|"comment"| CM["written as a comment line"]
    C -->|"drop"| DR["skipped"]
    F & CM & DR --> R["counted in the ExportReport"]
```

## 7. Threading

- Parsing, detection and export run on the **caller's thread** (an automation worker, the Qt thread).
- The store is changed under a mutex; each change builds a new immutable `Index` and publishes it with an atomic
  `shared_ptr` swap at a coherent moment. Readers (the disassembler on the emulation thread, automation threads)
  take the current pointer and never lock.
- A live scan copies the RAM pages it needs at a coherent moment and scans the copy on the caller's thread.

## 8. Relation to existing code

| Existing | Becomes |
|---|---|
| `LabelManager` (`core/src/debugger/labels/`) | facade over the per-CPU `SymbolStore`; `Label` is a view of `Symbol`; `LoadLabels` = `Import(format: auto)`; `SaveLabels` = `Export` |
| `LabelManager::ParseMapFile / ParseSymFile / ParseViceSymFile / ParseSJASMSymFile / ParseZ88DKSymFile` | five formats in the registry (same behavior, golden tests from today's parser output) |
| `ListingParser` (sjasmplus `.lst`) | stays the source-line map; its labels become the `sjasmplus-lst` importer; later a `SourceMap` beside the store |
| `data/symbols/*.map`, `data/symbols/sprinter/*.map` | bundles listed in `data/symbols/manifest.json` with their ROM hashes |
| GS firmware profiles (`symbol_file`) | a bundle in the `gs` CPU's store |
| MCP `manage_symbols`, WebAPI `/labels`, CLI `labels` / `symbols load`, Lua / Python `load_labels` | unchanged; new `import` / `export` / `formats` / `sets` beside them |
| Qt `LabelEditor` | gets Import... / Export... with a format list, a set list and the conflict report |

## 9. Code placement

The symbol module is one module of the [unreal-asm](../README.md) library (owner decision 2026-10-05: the library
lives in `core/src/3rdparty/unreal-asm`, next to `unreal-z80`). The library's layout is in
[../tdd.md](../tdd.md) §2; the symbol part:

```text
core/src/3rdparty/unreal-asm/
├── include/unrealasm/symbols/              # public headers of the module
└── src/symbols/                            # namespace unrealasm::symbols, std only
    ├── model/        symbol.h, symbolset, symbolstore, symbolindex, nameinterner, mergepolicy
    ├── codecs/       one folder per symbol file format, decode + encode (D-1):
    │   ├── native/  unrealmap/  simplesym/  unreall/  vice/  sjasm/  sjasmplus/  z88dk/  pasmo/  cspect/
    │   └── script/   ida/  ghidra/  mame/
    ├── fromsource/   labels from a decoded source (any source codec of the library) + optional assembling
    ├── live/         label-table scanners over a MemoryView (ALASM, XAS, STS), sharing table decoders with the
    │                 source codecs of the same assembler
    └── bundles/      data/symbols/manifest.json by SHA-256

core/src/debugger/symbols/                  # the emulator side (adapters, the only part that knows the emulator)
├── emulatormemoryview.cpp                  # MemoryView over Memory pages (coherent moment)
├── diskfilesource.cpp                      # ByteSource over a file in a TR-DOS / FAT image
├── pagingresolver.cpp                      # which page a window shows (CPU-view lookup)
└── bundlemanager.cpp                       # ROM change -> bundle sets

core/src/debugger/labels/labelmanager.*     # facade (kept)
data/symbols/manifest.json                  # bundles
```

Tokenized ZX formats are **not** in this module any more: they are the library's **source codecs**
([../source-formats.md](../source-formats.md)). The symbol module takes labels from any decoded source
(`fromsource/`), so every assembler the library can read gives symbols without a symbol-specific decoder.
