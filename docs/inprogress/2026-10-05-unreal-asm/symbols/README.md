# unreal-asm / symbols: one symbol model, codecs for every symbol file, labels from every source

Symbols (labels) from anywhere, in one model, out to anywhere:

- **in**: cross assemblers (sjasmplus, z88dk, pasmo), other emulators and debuggers (Unreal, VICE), disassemblers
  (IDA, Ghidra), ZX assemblers' **tokenized** sources and label tables (ALASM, XAS, STORM, GENS) from a file, a
  disk image or the **running** emulated machine, and ROM symbol bundles chosen by SHA-256;
- **one model**: name, address space (CPU view, ROM / RAM / cache page, device region, another CPU), kind, size,
  scope, source line, comment, aliases and provenance, in sets with priorities and explicit merge rules;
- **out**: assembler includes, other tools' label files, IDA / Ghidra / MAME scripts, and a lossless native file,
  with each target's name rules applied and every rename reported.

A module of the [unreal-asm](../README.md) cross-assembler library: tokenized sources are read by the library's source codecs ([../source-formats.md](../source-formats.md)); this module turns any decoded source, symbol file or live label table into symbols.

Replaces the file parsing inside `LabelManager` ([label-manager.md](../../../emulator/design/debugger/label-manager.md)),
which stays as the facade. Closes the debugger additions' label import E7
([2026-10-04-debugger-additions](../../2026-10-04-debugger-additions/TODO.md)).

## Documents

| File | Topic |
|---|---|
| [goals-and-requirements.md](goals-and-requirements.md) | **Start here.** Problem with a worked example, goals, non-goals, decisions D-1 / D-2, proposals for the owner (P-1…P-7), use cases, FR / NFR, acceptance, glossary |
| [architecture.md](architecture.md) | Component view, data model, import workflow, sequences (import, live scan, export, ROM bundle), decision trees DT-1…DT-4, threading, relation to existing code, **code placement** (mermaid flowchart, class, sequence and decision diagrams) |
| [formats.md](formats.md) | Format families (text, script, tokenized, live, native, bundle), lossiness matrix, catalog with detection and phases, examples side by side, name rules per target, the research plan for tokenized ZX assemblers, the native `*.usym.json` file |
| [prior-art.md](../prior-art.md) | Existing converters and tools, local (the owner's 2012 TASM detokenizer, its 2025 port, a Python 3.x decoder, test data) and public, compared; what each reveals about the formats; nothing is vendored (D-2) |
| [tdd.md](tdd.md) | Layers and the std-only rule, model structs, index, sources and sinks, `ICodec` (decode + encode per format), the tokenizer, tokenized codecs, live scanners, normalization and merge, export, bundles manifest, surfaces, memory budget, phases S0-S10 |
| [test-and-benchmark-plan.md](test-and-benchmark-plan.md) | Golden corpus from the real tools, unit tests per file, round trips, fuzzing, live checks, benchmarks and targets |

## In one picture

```mermaid
flowchart LR
    subgraph In["Sources"]
        A["text: sjasmplus · z88dk · pasmo · VICE · Unreal · MAP"]
        B["scripts: IDA · Ghidra"]
        C["tokenized: ALASM · XAS · STORM · GENS<br/>file · disk image"]
        D["live: label table in emulated RAM"]
        E["bundles: ROM SHA-256 → data/symbols"]
    end
    A --> T["Tokenizer"]
    B --> T
    C --> K["Detokenizer"]
    D --> L["LiveScanner"]
    E --> T
    T & K & L --> N["Normalizer + Merger<br/>space · names · policy · conflicts"]
    N --> S["SymbolStore per CPU<br/>sets · immutable index"]
    S --> U["disasm · breakpoints · call trace · PC history · skins · automation"]
    S --> X["Exporters + name rules"]
    X --> O["assembler includes · tool label files · IDA / Ghidra / MAME scripts · native .usym.json"]
```

## Where the code goes

`core/src/3rdparty/unreal-asm/src/symbols/` (namespace `unrealasm::symbols`, inside the [unreal-asm](../README.md) library): `model/ io/ codecs/ live/ bundles/` use only the standard
library, `adapters/` holds every emulator dependency; `LabelManager` stays the facade; `tools/symbols/symconv` is a
command-line converter built from the std-only part; bundles get `data/symbols/manifest.json`. Details:
[architecture.md](architecture.md) §9.

## Status

Design (2026-10-05). S1-S2 implemented (2026-10-06): the model, index, store, merge, the native file and the six label file codecs behind `LabelManager` (`core/src/3rdparty/unreal-asm/include/unrealasm/symbols/`, `src/symbols/`). See [TODO.md](../TODO.md).
