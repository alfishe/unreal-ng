# Symbol exchange: technical design

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Inputs** | [goals-and-requirements.md](goals-and-requirements.md), [architecture.md](architecture.md), [formats.md](formats.md) |
| **Tests** | [test-and-benchmark-plan.md](test-and-benchmark-plan.md) |

## 1. Namespaces and layers

| Layer | Directory | May use | Must not use |
|---|---|---|---|
| model | `core/src/3rdparty/unreal-asm/src/symbols/model/` | std | anything of the emulator |
| io | `.../symbols/io/` | std, model | the emulator |
| codecs | `.../symbols/codecs/` | std, model, io | the emulator (one folder per codec) |
| live | `.../symbols/live/` | std, model, io, codecs (table decoders) | the emulator (it sees a `MemoryView`) |
| bundles | `.../symbols/bundles/` | std, model, codecs | the emulator |
| adapters | `.../symbols/adapters/` | everything above + emulator, memory, media | — |
| facade | `core/src/debugger/labels/labelmanager.*` | adapters, model | codecs directly (goes through `SymbolStore`) |

A CMake check (`symbols-std-only`) compiles `model/ io/ codecs/ live/ bundles/` as a separate object library with
only the standard include paths, so a stray emulator include fails the build. `tools/symbols/symconv` links that
library alone.

## 2. Model

```cpp
namespace symbols
{
enum class CpuId : uint8_t { Main = 0, Gs = 1 };                 // more CPUs append
enum class SpaceKind : uint8_t { CpuView, Rom, Ram, Cache, Device, Constant, Port };

struct AddressSpace
{
    CpuId cpu = CpuId::Main;
    SpaceKind kind = SpaceKind::CpuView;
    uint16_t page = 0;            // Rom / Ram / Cache
    NameId region = kNoName;      // Device: "vram", "cram", "eeprom", ...
    // "cpu:main", "rom2", "ram3", "cache0", "vram", "const", "port", "gs.rom0" - Parse / Format below
};

struct Location { AddressSpace space; uint32_t offset = 0; };

enum class SymbolKind : uint8_t { Unknown, Code, Data, Const, Port, Entry, Local };

struct Scope { NameId parent = kNoName; bool global = true; };

struct SourceRef { NameId file = kNoName; uint32_t line = 0; uint16_t column = 0; };

struct Provenance { SetId set = 0; FormatId importer = 0; std::string raw; };   // raw: the original line / entry

struct Symbol
{
    NameId name;
    Location location;
    SymbolKind kind = SymbolKind::Unknown;
    uint32_t size = 0;                 // 0 = unknown
    Scope scope;
    NameId module = kNoName;
    SourceRef source;
    std::string comment;
    std::vector<NameId> aliases;       // usually empty: small-vector in the implementation
    bool enabled = true;
    Provenance provenance;
};
}  // namespace symbols
```

- **Names are interned** (`NameInterner`: one `std::string` per distinct name, `NameId` = 32-bit index). Names are
  UTF-8 and compared byte-wise; a set has a case rule (`exact` default, `fold` for tools that ignore case).
- **`SymbolSet`**: `id`, `title`, `Origin {kind, where, sha256}`, `priority` (higher wins in the resolved view),
  `enabled`, the `ImportOptions` it was made with (so a set can be re-imported after the file changed), and its
  `std::vector<Symbol>`.
- **`SymbolStore`** (one per CPU, owned by the debug manager, behind `LabelManager`): sets, the interner, a mutex
  for changes, and `std::atomic<std::shared_ptr<const Index>>` for readers.

### 2.1 The index

```cpp
struct Index
{
    // Every enabled symbol with an address, sorted by (space, offset, priority desc); a page-aware lookup asks a
    // resolver which page a CPU window shows and searches that space first, then CPU view
    std::vector<IndexEntry> byLocation;        // {space key (32-bit), offset, symbol ref}
    std::unordered_map<NameId, SymbolRef> byName;   // + a folded-case map for "fold" sets
    const Symbol* At(Location) const;          // exact
    Nearest NearestBelow(Location, uint32_t maxDistance) const;   // "PLAYMUS+12"
    Range InRange(Location from, Location to) const;
};
```

Built once per change (`O(n log n)`), read lock-free; `IndexEntry` is 16 bytes. A `PagingResolver` (adapter)
answers "window → space" for CPU-view lookups at the moment of the call.

### 2.2 As implemented (S1, 2026-10-06)

Naive first, measured in S10 (the budget of §9 is the target there):

- names are plain `std::string`s, not interned; a local symbol's scope is its `parent` name (empty = global);
- the index holds the sets it was built from (`shared_ptr`), so a reader's symbols stay valid while the store
  changes; the store publishes a new index under a small mutex that guards only the pointer copy (C++20
  `std::atomic<std::shared_ptr>` is not in every standard library the project builds with);
- the index's name maps keep each set's priority, so a higher-priority set wins a name across case rules;
- a record's own space is anything but the main CPU view (DT-1); `base` is added to every offset except constants.

## 3. Sources and sinks

```cpp
class ByteSource { public: virtual std::string_view Name() const = 0;           // for diagnostics
                           virtual std::optional<std::string> Extension() const = 0;
                           virtual bool ReadAll(std::vector<uint8_t>& out, std::string& error) = 0; };
class ByteSink   { public: virtual bool Write(std::span<const uint8_t>, std::string& error) = 0; };
```

Implementations: `FileSource` / `FileSink` (paths through `FileHelper`, UTF-8), `BufferSource` / `BufferSink`
(automation uploads and downloads), `DiskFileSource` (adapter: `disk:A/NAME.EXT` through the TR-DOS catalog or
`FatVolumeReader::ReadFile`; a TR-DOS file read by name is a small addition to `TrdosCatalog`).

## 4. Codecs

Every format is one codec with a decoder and an encoder (decision D-1); a codec that cannot encode is not accepted.
Nothing is vendored (D-2): each codec is written fresh from its research document and the golden files.

```cpp
enum class Family : uint8_t { Text, Script, Tokenized, Live, Native };
struct Capabilities { uint32_t holds = 0; };   // bit per lossiness-matrix column (decode and encode both exist)

class ICodec
{
public:
    virtual FormatId Id() const = 0;                    // "sjasmplus-sym"
    virtual std::string_view Title() const = 0;          // "sjasmplus symbol file (--sym)"
    virtual Family GetFamily() const = 0;
    virtual Capabilities Caps() const = 0;
    virtual const NameRules& Rules() const = 0;          // for export (DT-3)
    virtual int Detect(const Probe& probe) const = 0;    // 0..100
    virtual ImportResult Decode(std::span<const uint8_t> bytes, const ImportContext& ctx) const = 0;
    virtual ExportResult Encode(const ExportView& view, const ExportContext& ctx, ByteSink& out) const = 0;
};
```

- `Probe`: the first 4 KB, the extension, the origin kind (a TR-DOS file brings its type letter and name).
- `ImportResult`: `std::vector<SymbolRecord>` (a `Symbol` with names still as strings, plus the source line) and
  `std::vector<Diagnostic>` (`{severity, line, column, message}`).
- `ImportContext`: default space, base offset, case rule, name filter, the current paging snapshot (for
  `bindToCurrentPaging`), limits (max symbols, max line length).
- `ExportView`: the resolved symbols to write, already filtered; `ExportContext`: unrepresentable policy (DT-4),
  header comment on / off, line ending (`\n` default, `\r\n` for tools that need it).
- **Registry**: codecs register in one table (`CodecRegistry::Builtin()`); `Detect` asks all of them, sorts by
  score, applies the thresholds of [architecture.md](architecture.md) §4 (60, and 15 above the next).

### 4.1 The tokenizer

One lexer for every text format, line by line, no allocation per token (tokens are `string_view`s into the line):

| Token | Accepts | Example |
|---|---|---|
| `Number` | decimal `1234`; hex `#C000`, `$C000`, `0xC000`, `C000h`, `0C000H`; binary `%1010`, `0b1010`, `1010b`; octal `17q` / `17o` only when the format enables it | value + width + spelling |
| `Ident` | the format's identifier charset (a per-format table, default `A-Z a-z 0-9 _ . ? ! @ $ -`, never starting with a digit unless the format allows) | `PRINT-A-1`, `main.loop`, `@@1` |
| `String` | `"..."` and `'...'` with `\` escapes when the format enables them | `"name"` |
| `Punct` | `: = , ( ) [ ] { } \|` and `+ -` when not part of an ident | |
| `Comment` | from the format's comment starters to the end of the line (`;` default; `//`, `#` as the format says) | `; plays one frame` |
| `Space` | skipped, but counted for column numbers | |

A format describes its line grammar with a few helpers over the token stream (`Expect(Ident)`, `Optional(Punct,
':')`, `Number()`), so each text format is a short file and every format reports the same kind of diagnostics with
line and column. Lines longer than the limit (default 4096) are a diagnostic, never a crash.

### 4.2 Tokenized formats

```cpp
class ITokenizedDecoder           // per assembler and version, written after the research (formats.md §4)
{
public:
    virtual int ScoreTable(std::span<const uint8_t> bytes, size_t at) const = 0;   // is a label table here?
    virtual ImportResult ReadTable(std::span<const uint8_t> bytes, size_t at, const ImportContext&) const = 0;
    virtual ImportResult ReadSourceFile(std::span<const uint8_t> file, const ImportContext&) const = 0;
    virtual bool WriteSourceFile(const std::vector<std::string>& lines, std::vector<uint8_t>& out) const = 0;  // tokenize
};
```

The tokenized codec for a file uses `ReadSourceFile` (labels with their source lines; addresses from `EQU` or, with
`--assemble`, from `Z80TextAssembler` over the detokenized text) and its encoder writes the assembler's own form; the
live scanner uses `ScoreTable` + `ReadTable` on the RAM copy.

**As implemented (2026-10-07).** Sources are no symbol codec: the library's source codecs decode them, and
`SymbolsFromProject(files, main, options)` (`include/unrealasm/symbols/fromsource.h`) gives their labels. The values
come from `layout::Layout` (`include/unrealasm/layout.h`, `src/layout/`) over the sjasmplus conversion, not from
`Z80TextAssembler` ([formats.md](formats.md) §4.1). The label tables (`ScoreTable` / `ReadTable`) and the live
scanners stay as designed, after their research.

### 4.3 Live scanning

```cpp
class MemoryView { public: virtual size_t PageCount(SpaceKind) const = 0;
                           virtual std::span<const uint8_t> Page(SpaceKind, uint16_t) const = 0; };
class ILiveScanner { public: virtual std::vector<Candidate> Find(const MemoryView&) const = 0;  // {space, offset, count, score}
                             virtual ImportResult Read(const MemoryView&, const Candidate&, const ImportContext&) const = 0; };
```

`EmulatorMemoryView` (adapter) copies the RAM / cache pages at a coherent moment; the scan runs on the copy. Each
scanner first looks where the assembler's documented rule says (XAS: bank 6 / `#46`; STS: bank 7 / `#47`), then
scans every page when the rule does not hold.

## 5. Normalization and merge

1. **Space** by DT-1 ([architecture.md](architecture.md) §6).
2. **Offset** `+= options.base`; a page offset must be < 16 KB, a CPU-view offset < 64 KB, a device offset < the
   region size: otherwise a diagnostic and the record is skipped.
3. **Name**: empty → skipped; longer than 1024 bytes → cut + diagnostic; duplicate inside one file at the same
   place → one symbol; at another place → diagnostic, the first wins inside the file.
4. **Merge** by DT-2 with the policy (`both` default, `keep`, `replace`, `fail`); every conflict becomes a
   `Conflict {name, old location, new location, old set, new line}` in the report.
5. **Publish**: build the index, swap it at a coherent moment, post `labels_changed {cpu}`.

## 6. Export

1. Resolve the view: enabled sets (or the ones asked), by priority; filters (space, page, range, kind, set, name
   pattern).
2. Representability (DT-4) per symbol against the format's capabilities.
3. Name rules (DT-3) with a collision-free mangling table; the table goes into the report and, when the format has
   comments, into the file header.
4. Sort for the target (by address for most; by name where the tool wants it) and write.
5. Report: written, renamed (old → new), folded, commented, dropped.

## 7. Bundles

`data/symbols/manifest.json`:

```json
{ "format": "unreal-symbols-manifest", "version": 1,
  "bundles": [
    { "id": "rom:48k", "match": { "page_sha256": "…" }, "file": "48k_rom.map", "space": "rom0" },
    { "id": "sysvars:48k", "match": { "model": ["48K", "128K", "PENTAGON"] }, "file": "48k_variables.map", "space": "cpu:main" },
    { "id": "rom:sprinter-3.04-setup", "match": { "page_sha256": "…" }, "file": "sprinter/bios304-setup.map" }
  ] }
```

`BundleManager` (adapter) runs at machine start and after a ROM change (`RomChanged` with the page hashes the ROM
code already computes for its signatures): it imports the matching bundles as sets with origin `bundle` and drops
bundle sets whose ROM is gone. A user can switch a bundle set off; that choice is remembered.

## 8. Surfaces

The current calls stay as they are (FR-12). New ones:

| Surface | Import | Export | Formats | Sets |
|---|---|---|---|---|
| WebAPI | `POST /symbols/import {path \| disk \| base64, format, set, space, base, policy}`; `POST /symbols/import/live {scanner, at}` | `POST /symbols/export {path \| (download), format, sets, filter, names}` | `GET /symbols/formats` | `GET /symbols/sets`, `PUT /symbols/sets {id, enabled, priority}`, `DELETE /symbols/sets?id=` (a set id holds a path, so not in the URL path) |
| CLI | `symbols import <file\|disk:A/F.A> [--format f] [--set s] [--page ram3] [--base n] [--policy p]`, `symbols import --live alasm [--at ram6:#0000]` | `symbols export <file> --format f [--sets a,b] [--page ...]` | `symbols formats` | `symbols sets`, `symbols set <id> on\|off`, `symbols drop <id>` |
| MCP | `manage_symbols` actions `import`, `import_live` | `export` | `formats` | `sets`, `set_enable`, `drop` |
| Lua | `symbols_import{...}` | `symbols_export{...}` | `symbols_formats()` | `symbols_sets()`, ... |
| Python | `emu.symbols_import(...)` | `emu.symbols_export(...)` | `emu.symbols_formats()` | `emu.symbols_sets()`, ... |
| Qt | Label editor: Import... (format auto / list, page, policy; the report with conflicts) | Export... (format list, sets, the rename list) | — | a "Sets" tab: enable, priority, drop |

All surfaces call one function each in the facade (`LabelManager::ImportSymbols / ExportSymbols / Formats / Sets`),
which returns the same report structure (`StateNode`), so every surface answers with the same fields.

Built (2026-10-08): `SymbolControl` (`core/src/debugger/labels/symbolcontrol.h`) has the verbs `formats`, `detect`,
`sets`, `import`, `export`, `set` and `drop`, with the TTDControl pattern (a verb, options by name, a reply with an
error code and a StateNode body). The WebAPI, CLI, MCP, Lua and Python call it. Import takes a `path` only so far:
`disk`, `base64`, the live scan, export's `filter` / `names` and the Qt dialogs are still to come. The OpenAPI
gets a `symbols` tag; the recipe is `.recipe/analysis/symbols-import-export.md`.

## 9. Memory and speed budget

| Item | Size |
|---|---|
| `Symbol` without its strings | 64 bytes (aliases and comment empty in the common case) |
| interned name | its bytes + 16 |
| `IndexEntry` | 16 bytes |
| 100 000 symbols, names 10 bytes | ~ 10 MB |

Text import: one pass over the bytes, tokens as views, one record per line; 100 000 lines ≈ 3 MB parse in
< 100 ms on the development machine (measured in [test-and-benchmark-plan.md](test-and-benchmark-plan.md)).

## 10. Phases

> **Re-based on the library (2026-10-05).** The symbol work is phase A8 of [../tdd.md](../tdd.md) §8; S1-S5 below
> keep their content; S6-S9 (tokenized assemblers) are now the library's codec phases A2-A6, and the symbol module only
> adds `fromsource/` and the live scanners.

| Phase | Work | Ends with |
|---|---|---|
| S0 | Design (this folder), owner decisions P-1…P-7 | decisions recorded |
| S1 | Model, interner, index, store, merge, native JSON format, `symbols-std-only` build check | unit tests; native round trip |
| S2 | The five existing formats + `unreal-l` as codecs (their encoders are new); `LabelManager` becomes the facade | golden tests = today's parser output; every existing test passes unchanged |
| S3 | Tokenizer; sjasmplus `.sym` / `.sld` / `.lst` labels; pasmo | golden corpus from the real tools |
| S4 | Name rules, DT-3 / DT-4; codecs IDA IDC + Python, Ghidra, MAME, CSpect (both ways); `symconv` tool | golden files both ways; cross round trips |
| S5 | Surfaces (WebAPI + OpenAPI, CLI, MCP, Lua, Python, Qt), bundles + manifest, recipe | live checks, MCP / Qt tests |
| S6 | TASM (3.x / 4.x): research from the prior art (token tables, line layout, version differences) then the codec (decode, encode, `--assemble`) | corpus from TRD test data + files made by TASM in the emulator |
| S7 | ALASM: research (R1-R5), codec and live scanner | corpus + tests |
| S8 | XAS (research, codec, scanner in bank 6 / `#46`) | corpus + tests |
| S9 | STORM, GENS, MASM, ZX ASM, STS (research each first) | corpus + tests |
| S10 | Benchmarks and the results table; docs (`docs/features/symbols.md`) | numbers meet NFR-1…3 |

The debugger additions' E7 ("label import") closes with S7 + S8 (the two formats the TUI's import menu names).
