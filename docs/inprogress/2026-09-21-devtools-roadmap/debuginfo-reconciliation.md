# Phase 1 debug info against what unreal-asm already has

Status: reconciliation, 2026-10-09. Phase 1 of [prioritized-roadmap.md](prioritized-roadmap.md) plans a standalone
`libunreal-debuginfo` with parsers for SLD, `.lst`, `.sym`, `.map` and SDCC `.cdb` / `.adb`, and the R5 model
([unreal-ng-developer-toolchain-design.md](unreal-ng-developer-toolchain-design.md) §8.1). Since then the unreal-asm
library has built a symbol module with most of the parsers ([../2026-10-05-unreal-asm/symbols/](../2026-10-05-unreal-asm/symbols/README.md)).
This document lists what is there and what is not, so Phase 1 does not write a second parser.

## 1. R5 field by field

| R5 (§8.1) | In the code now | Where |
|---|---|---|
| `symbols[]`: module, main, local, value, compile page, traits | **Built.** `Symbol` has `name` (`module.main.local`), `module`, `parent`, `kind` (code / data / const / local), `location` (CPU view, ROM / RAM page + offset, device region), `window`, `traits` (SLD `+used` and the others kept for writing back), `source` (file, line), `provenance`. | `core/src/3rdparty/unreal-asm/include/unrealasm/symbols/symbol.h` |
| parsers: SLD | **Labels only.** The `sjasmplus-sld` codec reads `L` records (module brackets, structure and macro definitions skipped) and the page size from `Z` (`pages.size:`), and places each label on its physical page. `T` records are read only to tell code from data. `K`, `F`, `D` are skipped. It writes SLD back. | `src/symbols/codecs/crossasm/crossasmcodecs.cpp` |
| parsers: `.lst` | **Twice.** The `sjasmplus-lst` codec reads the listing's labels. `ListingParser` maps addresses to source lines for `/listing/*` and source stepping, but over a flat 64K (`_addressToLine`, 65536 entries; rows above #FFFF are not mapped). | codec: `crossasmcodecs.cpp`; lines: `core/src/debugger/listing/listingparser.h` |
| parsers: `.sym`, `.map` | **Built.** sjasmplus `--sym`, simple `.sym`, unreal-ng `.map`, z88dk `.map` / `-g` / `-s`, pasmo, CSpect, VICE, IDA, Ghidra, MAME and the native `*.usym.json`, each both ways. | `src/symbols/codecs/` |
| parsers: `.cdb` / `.adb` (SDCC) | **Missing.** | — |
| parsers: TRD / SCL / TAP / TZX | **Built** for the files on them: `ReadTrd`, `ReadTape`, `ReadHobeta`. | `include/unrealasm/containers.h` |
| `compile_memory_model` (SLD `Z`) | **Page size only.** Page count and slots are not kept. | SLD codec |
| `lines[]`: (compile page, offset, length) → (file, line, columns, macro origin) | **Missing as a model.** The only line table is `ListingParser`'s flat one. | — |
| `segments[]` with byte hashes, `content_hash` | **Missing.** | — |
| `types[]` (struct layouts, C types) | **Missing.** | — |
| `keywords[]` (SLD `K`) | **Missing** (skipped by the SLD codec). | — |
| `Binding`: compile page → physical page, relocation, process | **Missing.** Labels are bound by the page their file names; nothing relocates. | — |
| Lookup `(physical page, offset)` → line | **For labels, not lines.** `LabelManager` resolves a CPU address through the page mapped at its window when labels of several pages share it. | `core/src/debugger/labels/labelmanager.cpp` |
| Staleness detection | **Missing.** The ROM bundles are bound by page SHA-256, the same idea for ROM only. | `include/unrealasm/symbols/bundles.h` |
| Labels from a source with no debug file | **Built** (not in R5): `layout.h` computes the values of any dialect's labels without assembling. | `include/unrealasm/symbols/fromsource.h` |
| One surface layer | **Built** for symbols: `SymbolControl` behind WebAPI, CLI, MCP, Lua, Python and Qt. | `core/src/debugger/labels/symbolcontrol.h` |

## 2. Two statements in the roadmap that do not hold

- "Replace the legacy flat `ListingParser` in the Qt disassembler and GDB stub": the GDB stub does not use it (it
  has no symbol or line lookup), and the Qt disassembler takes labels from `LabelManager`. `ListingParser`'s
  consumers are `DebugManager` and the surfaces behind it: WebAPI `/listing/*`, CLI, MCP `manage_symbols`
  (`load_listing`, `source_at`, `step_line`, `run_to_line`), Lua and Python.
- "`libunreal-debuginfo` is a separate library": unreal-asm already is one, std-only, built and tested without the
  emulator, with the extraction goal written down (symbols G-10). A second library would split one model in two.

## 3. Recommendation

Phase 1 extends unreal-asm instead of starting `libunreal-debuginfo`:

1. **A line table in the library** (`unrealasm/debuginfo/` next to `symbols/`): `lines[]`, `segments[]` with byte
   hashes, `compile_memory_model`, `keywords[]`, under one `Module` whose symbols are a `SymbolSet`.
2. **SLD read in full** by the existing codec: `T` into `lines[]` with the page from `Z`, `K` into `keywords[]`, `Z`
   whole. `.lst` lines move from `ListingParser` into the library, page-aware.
3. **`ListingParser` becomes a facade** over the module, as `LabelManager` became over `SymbolStore`, so
   `/listing/*` and source stepping keep working and gain pages.
4. **Binding and staleness in the emulator**: compile page → physical page from the machine's paging, segment hashes
   compared at a coherent moment, the same way bundles match ROM pages.
5. **SDCC `.cdb` / `.adb`** as one more codec when a C project needs it.

What Phase 1 then adds is about half of what it planned: the parsers for `.sym`, `.map`, `.lst` labels, SLD labels and
the containers exist, with tests against the real tools' output.
