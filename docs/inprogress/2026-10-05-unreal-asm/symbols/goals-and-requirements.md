# Symbol exchange: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Replaces (internally)** | the file parsing inside `LabelManager` ([label-manager.md](../../../emulator/design/debugger/label-manager.md)); `LabelManager` stays as the facade the rest of the emulator calls |
| **Coordinates with** | the debugger model ([protocol.md](../../2026-09-28-debugger-model/protocol.md) §3.13 `Label`, §4.3 `labels_import`), the GS debugger firmware profiles ([2026-09-27-gs-debugger](../../2026-09-27-gs-debugger/)), the TUI debugger's label import ([TDD-DBG-01](../../2026-09-24-tui-debugger/TDD-DBG-01_unreal-speccy-debugger-tui.md) §11), the debugger additions E7 ([2026-10-04-debugger-additions](../../2026-10-04-debugger-additions/TODO.md)) |
| **Next** | [architecture.md](architecture.md), [formats.md](formats.md), [tdd.md](tdd.md) |

## 0. Problem

A **symbol** is a name for a place in a program: `PRINT-A-1` for ROM address `#0010`, `PLAYMUS` for the music
player at `#C000` in RAM page 3, `SCREEN` for `#4000`. With symbols, a disassembly reads like source, a breakpoint
can be set on `PLAYMUS+12`, and a call trace says `CALL PLAYMUS` instead of `CALL C000`.

Symbols come from many places, and each place writes them its own way:

| Where symbols come from | Example | Form |
|---|---|---|
| A cross assembler's output | sjasmplus `.sym`, `.sld`, `.lst`; z88dk `.map`; pasmo `--equ` | plain text, one format per tool |
| Another emulator or debugger | Unreal `user.l`, VICE labels, MAME comments, DeZog | plain text |
| A disassembler project | IDA, Ghidra | their own databases; text or script exports |
| A ZX assembler running **inside** the emulated machine | TASM, ALASM, XAS, STORM, GENS, MASM | **tokenized**: the source and the label table are binary structures in the machine's RAM or in a file on a TR-DOS disk |
| Our own disassemblies | `data/symbols/*.map`, `docs/disasm/...` | our MAP text format |
| A ROM or firmware we recognize | the 48K ROM, Sprinter BIOS 3.04, a GS firmware | a symbol file chosen by the ROM's SHA-256 |
| The user | typing a name in the debugger | interactive |

Today the emulator handles a small part of this:

- `LabelManager` reads five text formats (MAP, SYM, VICE, SJASM, Z88DK), picked **by file extension** only.
- A label has a 16-bit address and one optional page. It has no size, no source line, no scope, and no record of
  where it came from.
- Writing labels out is limited to `SaveLabels` in our own formats. There is no way to give the names back to IDA,
  Ghidra, an assembler or another emulator.
- `ListingParser` (sjasmplus `.lst` source-level stepping) is a separate island with its own model.
- Names such as `PRINT-A-1` from the ROM maps are not valid in any assembler, so an export would break the target
  tool. Nothing renames them.
- Two files that name the same address differently are merged silently: the last one wins.
- The original Unreal debugger's "import labels from XAS / ALASM in memory" (Ctrl+A) existed in Unreal 0.37.1 and
  was dropped later (only the declarations remain); it bound every value to fixed RAM pages ([prior-art.md](../prior-art.md)
  P7).

### 0.1 Worked example: one game, three tools

A user reverse-engineers a game.

1. They disassemble it in **IDA**, name 300 routines and tables, and export the names.
2. In the emulator they **import** that export, run the game, and find 40 more routines with the debugger, the call
   trace and the PC history. They name those in the emulator.
3. They **export** all 340 names back to IDA (an IDC / Python script), and as an **sjasmplus** include
   (`name EQU #addr`), so the rebuilt game assembles with the same names.
4. A friend sends a **ALASM** source of a routine on a TR-DOS disk. The emulator **imports** its labels straight
   from the disk file (tokenized), or from the label table of ALASM running in the emulated machine.

Every step today needs hand-written scripts, and the page of each name (`#C000` in page 1 or in page 3?) is lost on
the way.

## 1. Goals

| ID | Goal |
|---|---|
| G-1 | **One symbol model.** Every symbol from every source becomes the same record: name, where (address space + offset), kind, size, scope, source position, comment, and **provenance** (which import, which file, which line). Nothing a source carries is dropped on import if the model can hold it. |
| G-2 | **Codecs for every source family** (D-1: each reads and writes). Text formats (with a shared tokenizer), tokenized ZX assembler formats (sources and label tables, from a file or from a disk image), **live** label tables in the emulated machine's memory, ROM / firmware bundles chosen by hash. |
| G-3 | **Exporters to every target that takes symbols.** Assemblers (include files), emulators and debuggers (their label files), disassemblers (IDA, Ghidra scripts), our own lossless native file. A name the target cannot take is renamed by the target's rules, and the renames are listed. |
| G-4 | **Pages are kept.** A symbol is bound to a physical page (ROM 2, RAM 3, cache 0, a device region, the GS card's memory) when the source says so or the import is told so; a CPU-view symbol (`#4000`, any page) stays one. The same `#C000` in pages 1 and 3 are two symbols. |
| G-5 | **Lossless native file.** The emulator's own symbol file keeps every field, so export then import gives the same set (a round trip is an identity). |
| G-6 | **Merging is explicit.** Importing into a set that already has symbols follows a merge policy (keep, replace, keep both, report). Conflicts are reported with both sides, never resolved silently. |
| G-7 | **Detection by content.** The format is recognized from the bytes (with the extension as a hint), and the import says which format it took and how sure it was. The user can force a format. |
| G-8 | **Every surface.** Import, export, list formats, list and drop symbol sets: WebAPI + OpenAPI, CLI, MCP, Lua, Python and the Qt debugger, with docs and a recipe ([automation parity](../../../emulator/design/control-interfaces/command-interface.md)). |
| G-9 | **Fast where it is hot.** Address-to-name lookup is used by the disassembler on every line and by the call trace; it must stay a sorted-array search, never a scan. Loading 100 000 symbols takes well under a second. |
| G-10 | **Testable and extractable.** The model, the tokenizer and every format build and test without the emulator (no `EmulatorContext`), so the module can later become a standalone library like the planned unreal-media ([library-extraction](../../2026-10-05-media-multisource/library-extraction/README.md)). |

## 2. Non-goals (this design)

| ID | Non-goal | Why / where instead |
|---|---|---|
| NG-1 | Type information (structs, function signatures, local variables) | ZX tools rarely have it. The model keeps `size` and `kind`; a later `types` extension can be added. |
| NG-2 | Source-level debugging itself (step by line) | Already done by `ListingParser` + MCP `step_line` / `run_to_line`. This design only makes its line map an importer (SourceMap) of the same module. |
| NG-3 | Disassembling or reassembling | The module carries names; the disassembler and the assembler consume them. |
| NG-4 | Editing tokenized sources | Importers read tokenized ALASM / XAS / STORM sources for their labels. Exporting **into** a tokenized format is optional and only after the format research (formats.md §4). |
| NG-5 | Reading IDA `.idb` / `.i64` or Ghidra project databases directly | Closed / complex binary databases. IDA and Ghidra are reached through their script and text exports, in both directions. |
| NG-6 | A network symbol server | Not needed for ZX work. ROM bundles ship in `data/symbols/`. |

## 3. Decisions and proposals

### 3.1 Decisions (owner, 2026-10-05)

| ID | Decision |
|---|---|
| D-1 | **Every format is its own codec, and every codec reads and writes.** No import-only or export-only formats: a codec has a decoder (bytes → records) and an encoder (records → bytes), tested both ways. |
| D-3 | **The symbol module is part of the cross-assembler library `unreal-asm`** in `core/src/3rdparty/unreal-asm` ([../README.md](../README.md)); symbols are one consumer of its source codecs. (Replaces proposal P-1.) |
| D-2 | **Nothing is vendored.** Existing converters and tools (local and public, [prior-art.md](../prior-art.md)) are references for the formats only; every codec is a fresh implementation against these requirements, with its own tests. |

### 3.2 Proposals (for the owner's decision)

These are the design's proposals. The owner's answers go to §3.1, one question at a time.

| ID | Proposal | Recommended |
|---|---|---|
| P-1 | *(decided: D-3)* | — |
| P-2 | **The native file**: `*.usym.json` (one JSON document: header, sets, symbols). JSON over YAML: exact, fast, already parsed everywhere in the emulator. | yes |
| P-3 | **`LabelManager` stays as the facade**: its API keeps working for every caller; its parsing is replaced by the module's importers; its `Label` struct becomes a view of `Symbol`. | yes |
| P-4 | **Default merge policy** on import into a non-empty set: `keep both` (a second name for the same place becomes an alias), and **report** names that move (same name, other place). | yes |
| P-5 | **Tokenized formats in two steps**: first a research document per assembler (TASM 3/4, ALASM 4.42-5.0x, XAS 7.x, STORM, GENS 3/4, MASM), made by running each assembler in the emulator on a known source; then the codec. No format is coded from guesses. **TASM first**: it has the most prior art (the owner's 2012 converter's token table, TRD test data) ([prior-art.md](../prior-art.md)). | yes |
| P-6 | **Live scans are explicit**: scanning RAM for an assembler's label table runs only on request (`symbols import --live alasm`), never in the background. | yes |
| P-7 | **ROM bundles by hash**: `data/symbols/` gets a manifest that maps ROM page SHA-256 to symbol files; a machine loads the matching set at start and after a ROM change. | yes |

## 4. Use cases

| ID | Use case | Steps |
|---|---|---|
| UC-1 | Load an assembler's symbols | `symbols import game.sym` → detected as sjasmplus, 812 symbols in set `game.sym` (CPU view) |
| UC-2 | Load symbols for a paged program | `symbols import music.map --page ram3` → every symbol bound to RAM page 3 |
| UC-3 | IDA round trip | export from IDA (script) → `symbols import names.txt --format ida-names` → edit in the emulator → `symbols export ida.py --format ida-python` |
| UC-4 | Build with the same names | `symbols export names.inc --format sjasmplus-equ` → `INCLUDE names.inc`; `PRINT-A-1` becomes `PRINT_A_1` (rename list printed) |
| UC-5 | ALASM label table from the running assembler | ALASM is loaded in the emulated 128K; `symbols import --live alasm` finds the table, shows `412 labels in page 6 at #C000`, imports them |
| UC-6 | Labels from a tokenized source on a disk | `symbols import disk:A/GAME.A --format alasm-source` → labels with their source lines |
| UC-7 | ROM names by themselves | a 48K starts → the ROM's SHA-256 matches `48k_rom` in the manifest → set `rom:48k` (ROM page 0) is there without a command |
| UC-8 | Share a session's names | `symbols export session.usym.json` → another user imports it → identical set, provenance kept |
| UC-9 | Two sources disagree | importing `b.sym` over `a.sym`: `SCORE` is `#5B00` in one and `#5C00` in the other → conflict reported with both lines; the policy decides |
| UC-10 | GS card symbols | a GS firmware is recognized by hash → set `gs:rom105` in the card CPU's address space |

## 5. Functional requirements

| ID | Requirement |
|---|---|
| FR-1 | A symbol has: `name`, `location` (address space + offset), `kind` (code, data, const / equ, port, local, entry), `size` (0 = unknown), `scope` (global, local to a parent, module), `module`, `source` (file + line + column), `comment`, `aliases`, `enabled`, `provenance` (set id, importer, original text). |
| FR-2 | Address spaces: the CPU view of a CPU (`main`, `gs`), a physical page (`rom N`, `ram N`, `cache N` of a CPU's memory), a device region (`vram`, `cram`, `eeprom`, ... as in `DeviceMemory`), and an absolute numeric constant (an `EQU` that is not an address). |
| FR-3 | Symbols live in **sets**; a set has an id, a title, its origin (file, live scan, bundle, user), its import options and its enabled flag. The resolved view merges the enabled sets by priority. |
| FR-4 | Lookup: by name (exact, case rule of the set), by location (exact and nearest-below with offset: `PLAYMUS+12`), by range, by kind, by set, by text search. Page-aware: the CPU-view lookup asks which page each window shows now. |
| FR-5 | Import: from a host file, from a file inside a disk image (TR-DOS, FAT; `disk:A/NAME.EXT`), from a byte buffer (automation upload), from emulated memory (live scan), from a bundle. Options: format (auto or forced), target set, default address space / page, base offset, name filter, merge policy. |
| FR-6 | Detection: each format scores a probe (first 4 KB + extension); the best score above a threshold wins; ties and low scores are reported with the candidates. |
| FR-7 | Export: to a host file or a byte buffer; options: format, which sets, filter (space, page, range, kind), name rules of the target, what to do with symbols the target cannot hold (drop, comment, fold). The result lists renames and drops. |
| FR-8 | Merge policies: `keep` (existing wins), `replace` (new wins), `both` (alias), `fail` (stop on the first conflict); conflicts always listed. |
| FR-9 | Diagnostics: every import and export returns counts, warnings with source line numbers, and the detected format with its confidence; nothing is dropped without a diagnostic. |
| FR-10 | Persistence: the session's user edits and the list of imported sets survive a restart (the native file next to the session, owner decision for where). |
| FR-11 | Change notification: one `labels_changed` event per changed set (the debugger model's event), so skins redraw. |
| FR-12 | Compatibility: every current `LabelManager` caller and every current automation call (`labels`, `symbols load`, `/labels`, `load_labels`) answers as before. |

## 6. Non-functional requirements

| ID | Requirement | Target |
|---|---|---|
| NFR-1 | Address lookup cost | `O(log n)`, no allocation; ≤ 100 ns on 100 000 symbols (benchmark) |
| NFR-2 | Import speed (text) | ≥ 1 million lines per second for SYM / MAP; 100 000 symbols < 300 ms end to end |
| NFR-3 | Memory | ≤ 96 bytes per symbol plus its name text (interned) |
| NFR-4 | Thread safety | the emulation thread reads a consistent index while an import runs (copy-on-write swap of an immutable index) |
| NFR-5 | Determinism | the same input and options give the same set and the same export bytes |
| NFR-6 | Robustness | malformed input never crashes or hangs (fuzzed); a bad line is a diagnostic |
| NFR-7 | Portability | standard C++20 only in the model and formats; UTF-8 names; paths through `FileHelper` |

## 7. Acceptance

- Every format of [formats.md](formats.md) marked "phase 1" imports its golden corpus to the expected normalized
  JSON, and every exportable one exports byte-identical golden files.
- Native round trip: export → import is an identity on the whole corpus.
- Cross round trips (sjasmplus ↔ native ↔ IDA ↔ VICE) lose only what the lossiness matrix ([formats.md](formats.md)
  §2) says.
- The current `LabelManager` tests pass unchanged; the automation surfaces answer as before for the old calls.
- The benchmarks meet NFR-1 to NFR-3 ([test-and-benchmark-plan.md](test-and-benchmark-plan.md)).
- One tokenized assembler (the first researched, proposed ALASM) imports from a disk file and from a live session.

## 8. Glossary

| Term | Meaning |
|---|---|
| Symbol | A name for a location: an address, a page offset, a port or a constant. "Label" is the older word in this codebase; they mean the same here. |
| Address space | Where a location is: the CPU's 64K view, a physical page, a device region, or a plain number. |
| CPU view | The 16-bit addresses the CPU sees now; which page is behind `#C000` depends on paging. |
| Set | A group of symbols from one origin (a file, a scan, a bundle, the user), switched on or off together. |
| Provenance | Where a symbol came from: its set, the importer, the source file and line, and the original text. |
| Tokenized format | A program's source stored with keywords and mnemonics as byte codes (tokens) instead of text, and usually a separate label table: how ZX assemblers such as ALASM keep a source in memory and on disk. |
| Tokenizer | The shared lexer that splits a text line into tokens (numbers in every ZX notation, identifiers, strings, comments) for every text importer. |
| Detokenizer | The reverse for a tokenized format: turns its byte codes back into text or records. |
| Live scan | Finding an assembler's label table in the emulated machine's memory and reading it. |
| Name rules | What names a target accepts: characters, length, case, reserved words. |
| Bundle | A symbol file shipped with the emulator for a known ROM or firmware, chosen by SHA-256. |
