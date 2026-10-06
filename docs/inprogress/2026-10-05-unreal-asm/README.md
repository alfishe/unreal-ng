# unreal-asm: cross-assembler source library

A library that reads, writes and converts ZX Spectrum assembler sources in every form they exist in:

1. **Codecs**: one per source format and sub-version (TASM 3, TASM 4, ALASM 4.x / 5.x, STORM, ZX-ASM, XAS, GENS,
   plain text in its encodings). Each decodes the bytes into the exact text that assembler shows, and encodes that
   text back into the same bytes.
2. **Format conversion**: a source moves between formats and sub-versions of the **same dialect** through its text
   (TASM 3 file → TASM 4 file, tokenized ALASM → ALASM text file → tokenized ALASM 5).
3. **Dialect conversion**: separate **plugin modules** turn one assembler's syntax into another's through a common
   intermediate form (ALASM → sjasmplus, TASM → sjasmplus, GENS → pasmo, ...).
4. **Consumers** built on top: [symbols](symbols/README.md) (labels for the debugger, symbol files of other tools),
   assembling, source-level debugging, disk import / export in the emulator.

It lives in `core/src/3rdparty/unreal-asm` (owner decision 2026-10-05), next to `unreal-z80`: standard C++ only, its
own tests and a command-line tool; the emulator uses it through thin adapters.

## Documents

| File | Topic |
|---|---|
| [goals-and-requirements.md](goals-and-requirements.md) | **Start here.** Problem, goals, non-goals, decisions, open questions, use cases, requirements, acceptance, glossary |
| [architecture.md](architecture.md) | Layers, data model (source document, line, IR), the three pipelines (decode / encode, format conversion, dialect conversion), plugin model, consumers, decision trees, emulator integration (mermaid diagrams) |
| [source-formats.md](source-formats.md) | Every source format and sub-version: what is known, from where, how it is detected, what a byte-exact round trip has to keep |
| [research-storm.md](research-storm.md) | STORM 1.0beta … 1.3i: lines walked backwards, implied commands, number descriptors, packed labels, the 42-file corpus (phase A4) |
| [research-zxasm.md](research-zxasm.md) | ZX-ASM / ZAsm 2.4 … 4.20: text buffer with keyword pairs, the editor's rules, version detection, the 372-file corpus (phase A4) |
| [research-alasm.md](research-alasm.md) | ALASM 3.8 … 5.09: the file, lines and keywords from ALASM's own sources, the tables of every version, version detection, the 429-file corpus (phase A3) |
| [research-alasm-to-sjasmplus.md](research-alasm-to-sjasmplus.md) | ALASM → sjasmplus through the IR: the rules, the facts checked in ALASM 5.09 and sjasmplus 1.23, The Link's objects byte-equal (phase A5) |
| [research-tasm-to-sjasmplus.md](research-tasm-to-sjasmplus.md) | TASM 3 / 4.0 / 4.12 → sjasmplus: the rules, the GS 1.04 ROM rebuilt byte for byte, TASM 4.12 checked in the emulator (phase A5b) |
| [research-storm-to-sjasmplus.md](research-storm-to-sjasmplus.md) | STORM 1.3 → sjasmplus: several operands per instruction, postfix operators with priorities, built-in macros; STORM's own source and two programs assembled by STORM 1.3 in the emulator equal (phase A6) |
| [research-zxasm-to-sjasmplus.md](research-zxasm-to-sjasmplus.md) | ZX-ASM → sjasmplus: left to right with postfix functions, macros whose parameters persist, IFUSED libraries, nested PHASE; four programs assembled by ZAsm 3.15 in the emulator equal (phase A6) |
| [research-pasmo-backend.md](research-pasmo-backend.md) | IR → pasmo 0.5: pasmo's priorities, the displacement written out without PHASE, per-assembly EQU / DEFL; the oracle programs and the GS ROM equal through pasmo, 0 differences against sjasmplus on the corpus (phase A6) |
| [research-z88dk-backend.md](research-z88dk-backend.md) | IR → z88dk's z80asm: C's priorities on 32-bit words, a section per ORG, PHASE, the z80asm 2.3 workarounds; the oracle programs equal, 329 corpus sources equal to sjasmplus, the limits listed (phase A6) |
| [research-tasm.md](research-tasm.md) | TASM 3 / 4: the stream, the token table, the canonical tokenizer, what the real TASM 3.2 files show (phase A2) |
| [dialect-conversion.md](dialect-conversion.md) | The intermediate representation, frontend and backend plugins, the construct matrix, what cannot be converted, a worked ALASM → sjasmplus example |
| [prior-art.md](prior-art.md) | Existing converters and tools, local and public, compared; nothing is vendored |
| [tdd.md](tdd.md) | Library layout, namespaces, interfaces (`ISourceCodec`, `IDialectFrontend`, `IDialectBackend`), registry, CLI, emulator adapters, phases |
| [test-and-benchmark-plan.md](test-and-benchmark-plan.md) | Oracles (byte-exact round trips, the original assembler in the emulator, binary equality after conversion), corpus, tests, benchmarks |
| [memory-bridge.md](memory-bridge.md) | **P2, design only**: sources straight from the emulated machine's memory (per-assembler memory descriptors), export and conversion, background builds with hints, real-time labels |
| [symbols/](symbols/README.md) | The symbol module: one symbol model, symbol file codecs (sjasmplus, z88dk, VICE, IDA, Ghidra, ...), live label tables, ROM bundles |

## In one picture

```mermaid
flowchart LR
    subgraph Bytes["Source bytes"]
        T3["TASM 3 .$A"]
        T4["TASM 4 .$A"]
        AL["ALASM .$H"]
        ST["STORM .$C"]
        TX["text .asm<br/>CP866 · KOI8 · UTF-8"]
    end
    T3 & T4 & AL & ST & TX <-->|"codec<br/>decode · encode"| D["SourceDocument<br/>exact text in the assembler's own dialect<br/>+ codec attributes for byte-exact encode"]
    D <-->|"same dialect:<br/>format / sub-version conversion"| D2["SourceDocument<br/>another format of the dialect"]
    D -->|"frontend plugin<br/>(dialect → IR)"| IR["Z80 source IR<br/>labels · instructions · directives · expressions"]
    IR -->|"backend plugin<br/>(IR → dialect)"| D3["SourceDocument<br/>another dialect: sjasmplus · pasmo · ..."]
    IR --> SY["symbols<br/>labels · values (assembled)"]
    IR --> AS["assemble · source map"]
```

## Status

Design (2026-10-05). Decided (D-1…D-12, [goals-and-requirements.md](goals-and-requirements.md) §3): the library and
its place; one codec per format, decode and encode; nothing vendored; TASM 3 / 4 first, every other codec queued;
compiled-in plugins; a neutral IR; TASM → sjasmplus first, sjasmplus the first output target; UTF-8 on the host; the
`zxasm` CLI. Still open: the symbol module's P-2…P-7 ([TODO.md](TODO.md)).

Implementation on branch `unreal-asm` (master after the owner's review): A1-A4 the codecs of every version (text,
sjasmplus, TASM, ALASM, ZX-ASM, STORM), A5 the IR with ALASM → sjasmplus conversion checked against ALASM's own
binaries, A5b TASM → sjasmplus checked against the GS 1.04 ROM and TASM 4.12. Check scripts: `tools/unreal-asm/`. Progress per phase: [TODO.md](TODO.md).
