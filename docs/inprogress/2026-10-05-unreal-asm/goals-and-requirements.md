# unreal-asm: goals and requirements

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Consumers** | [symbols](symbols/README.md) (debugger labels, symbol files), the emulator's disk import / export, the debugger's source view, the core assembler (`Z80TextAssembler`) |
| **Next** | [architecture.md](architecture.md), [source-formats.md](source-formats.md), [dialect-conversion.md](dialect-conversion.md), [tdd.md](tdd.md) |

## 0. Problem

ZX Spectrum programs were written in assemblers that ran on the Spectrum itself: TASM, ALASM, STORM, ZX-ASM, XAS,
GENS, Zeus, MASM. Each kept its source in **its own binary form**: mnemonics and registers as byte tokens, spaces
as run counts, lines framed by length bytes, sometimes whole expressions tokenized, sometimes the file stored back
to front. Thousands of sources survive only in these forms, on TR-DOS disk images.

Today a user who finds such a source has three problems:

1. **Reading it.** Each format needs its own detokenizer. The ones that exist cover a few formats, decode only, and
   disagree in places ([prior-art.md](prior-art.md)).
2. **Moving it between versions.** A TASM 3 source does not load in TASM 4 (other token numbers, other space runs);
   an ALASM 4 source and an ALASM 5 one differ too. Nobody writes the target form back.
3. **Building it today.** A modern cross assembler (sjasmplus, pasmo, z88dk) has another syntax: other directive
   names, other local-label rules, other number and string forms, other macro systems. Converting by hand is slow
   and error-prone.

And the emulator itself needs the same machinery: labels from any of these sources for the debugger
([symbols](symbols/README.md)), sources pulled off a disk image into readable text, edited text written back to a
disk in the assembler's own form.

### 0.1 Worked example

A user has `GAME.$H` (ALASM 4.4) on a TR-DOS disk.

1. `zxasm decode GAME.$H` → `GAME.alasm.asm`: exactly the lines ALASM shows on screen, CP866 comments as UTF-8.
2. `zxasm encode GAME.alasm.asm --format alasm-5.0` → `GAME.$H` that ALASM 5.0 loads (a sub-version conversion).
3. `zxasm convert GAME.$H --to sjasmplus` → `game.asm` for sjasmplus: directives and label forms written the way
   sjasmplus spells them, constructs sjasmplus lacks rewritten or expanded, anything left reported by line.
4. `sjasmplus game.asm` builds a binary **byte-identical** to the one ALASM builds from the original (the test
   oracle of [test-and-benchmark-plan.md](test-and-benchmark-plan.md)).
5. The debugger gets every label of the program with its address.

## 1. Goals

| ID | Goal |
|---|---|
| G-1 | **One codec per source format and sub-version**, decoding and encoding (decision D-1). Byte-exact round trip: `encode(decode(bytes)) == bytes` for every file the original assembler wrote. |
| G-2 | **Exact text.** Decoding gives the text the original assembler shows (its spelling, case and spacing), not a re-formatted approximation. |
| G-3 | **Format conversion within a dialect**: any format / sub-version of a dialect to any other (tokenized ↔ tokenized, tokenized ↔ text), with a report of what the target cannot hold (a TASM 4 directive in a TASM 3 target). |
| G-4 | **Dialect conversion through plugins**: frontends parse a dialect into a common Z80 source IR, backends write the IR in a dialect; N frontends + M backends instead of N × M converters; each plugin is a separate module of the framework. |
| G-5 | **Honest conversion**: what a target dialect cannot express is either rewritten by a documented rule (e.g. expanded), kept as a comment, or refused, per option; every case is reported with its line. |
| G-6 | **Verified by the originals**: the original assemblers running in the emulator are the oracle for decoding (what they show) and for conversion (the binary they build). |
| G-7 | **Consumers on one model**: symbols, assembling, source maps and the emulator's disk import / export all use the decoded document or the IR; no consumer has its own detokenizer. |
| G-8 | **Standalone**: standard C++ only, its own tests and benchmarks, a CLI (`zxasm`); the emulator reaches it through adapters (media, memory, debugger). |
| G-9 | **Every surface in the emulator**: decode / encode / convert / symbols on WebAPI + OpenAPI, CLI, MCP, Lua, Python and Qt, with docs and recipes (automation parity). |

## 2. Non-goals (this design)

| ID | Non-goal | Why |
|---|---|---|
| NG-1 | A new assembler | The core's `Z80TextAssembler` and the external cross assemblers assemble; the library feeds them. |
| NG-2 | Converting between CPUs (Z80 → 6502) | Z80 family only (Z80, the documented undocumented instructions, Z84C15 / Sprinter extensions where a dialect has them). |
| NG-3 | Pretty-printing / re-formatting styles | Backends write a plain, consistent style; style options can come later. |
| NG-4 | Disassembly to source | Separate (the disassembler); its output can enter as a text source of a chosen dialect. |
| NG-5 | Loading plugins as shared libraries at run time | Plugins are modules compiled into the library and registered at start (see open question Q-2). |

## 3. Decisions (owner, 2026-10-05)

| ID | Decision |
|---|---|
| D-1 | Every format is one codec that **decodes and encodes**; no read-only or write-only formats. |
| D-2 | **Nothing is vendored.** Prior art ([prior-art.md](prior-art.md)) is a reference for the formats only; every codec and plugin is a fresh implementation with its own tests. |
| D-3 | The work is a **cross-assembler source library**, `unreal-asm`, in `core/src/3rdparty/unreal-asm`. Symbols are one consumer of it. |
| D-5 | **The first codecs are TASM 3 and TASM 4** (answer to Q-1). |
| D-6 | **Plugins are compiled-in modules**: one folder each (code + tests), registered by one line; run-time loading may come later (answer to Q-2). |
| D-7 | **The IR is neutral**: designed from all dialects at once, not modeled on one (answer to Q-3). Every construct of every dialect of the catalog has an IR form or an explicit `Raw` fallback; no dialect's spelling is the IR's spelling. |
| D-8 | **Every codec of the catalog stays in the development queue**: TASM 3 / 4 first (D-5), then ALASM, STORM, ZX-ASM, XAS, MASM, GENS, Zeus, ADS at lower priority - none is dropped. |
| D-9 | **The first dialect pair is TASM → sjasmplus** (answer to Q-4). |
| D-10 | **The sjasmplus codec is the first output target implemented**: its text codec and dialect backend come first among the targets (owner, 2026-10-05; "sjasm" read as sjasmplus). |
| D-11 | **Decoded text on the host is always UTF-8**; the original code page (CP866 / KOI8-R / CP1251 / ASCII) is recorded in the document so encoding back is exact (answer to Q-5). |
| D-12 | **The CLI is `zxasm`** (`decode`, `encode`, `convert`, `detect`, `batch`, `formats`, `dialects`, `symbols`) (answer to Q-6). |
| D-13 | **Universal encoding detectors are separate classes of the library** with their own public API (code page, line ends, text vs binary), usable outside the codecs (the emulator's media and text tools, other libraries) (owner, 2026-10-05). |
| D-4 | Order: first **text conversion between formats and their sub-versions** (codecs), then **assembler syntax conversion** as separate plugin modules in the framework. |

## 4. Open questions (asked one at a time)

| ID | Question | Proposed answer |
|---|---|---|
| Q-1 | *(decided: D-5, TASM 3 + TASM 4)* | — |
| Q-2 | *(decided: D-6, compiled-in modules)* | — |
| Q-3 | *(decided: D-7, a neutral IR)* | — |
| Q-4 | *(decided: D-9, TASM → sjasmplus)* | — |
| Q-5 | *(decided: D-11, UTF-8)* | — |
| Q-6 | *(decided: D-12, `zxasm`)* | — |

## 5. Use cases

| ID | Use case |
|---|---|
| UC-1 | Decode a tokenized source from a host file or straight from a disk image (`disk:A/GAME.H`) to text |
| UC-2 | Encode edited text back to the assembler's form and write it to a disk image |
| UC-3 | Convert a source between sub-versions (TASM 3 ↔ 4, ALASM 4 ↔ 5) |
| UC-4 | Convert a whole disk of sources to text files in one command (batch, with a report per file) |
| UC-5 | Convert a source to another dialect (ALASM → sjasmplus) and build it with the modern tool |
| UC-6 | Labels of any source for the debugger (assembled for addresses) |
| UC-7 | Detect what a file is (format, sub-version, confidence) from its bytes and its TR-DOS catalog entry |
| UC-8 | Show a tokenized source in the emulator's debugger beside the running code (source map) |

## 6. Requirements

| ID | Requirement |
|---|---|
| FR-1 | `SourceDocument`: lines of exact text; the format and sub-version it came from; the original code page; per-line codec attributes (editor flags, space-run vs literal blanks, token case bits, ...) so encoding is byte-exact; file-level attributes (headers, signatures, trailing data). |
| FR-2 | Codecs report decode diagnostics with line and byte offset; unknown tokens are kept (as an escape) so nothing is lost. |
| FR-3 | Detection scores every codec on the bytes plus catalog hints (TR-DOS type letter, start address, name) and reports the candidates. |
| FR-4 | Format conversion checks every line against the target codec and reports lines the target cannot hold, before writing. |
| FR-5 | The IR holds: labels (global, local, temporary, module-scoped), instructions with operand expression trees, directives normalized to kinds, conditional blocks, macro definitions and calls, repeat blocks, includes, comments and blank lines, and a raw passthrough for what a frontend cannot parse. |
| FR-6 | Each IR node keeps its source position (document, line, column) for diagnostics and source maps. |
| FR-7 | Backends declare their capabilities (which IR constructs they can write) and the rewrite rules they apply; conversion options choose expand / comment / refuse for the rest. |
| FR-8 | The CLI and every emulator surface expose decode, encode, convert, detect and the format / dialect lists. |

| ID | Non-functional | Target |
|---|---|---|
| NFR-1 | Decode / encode speed | ≥ 50 MB/s on tokenized files (a whole 640 KB disk in < 20 ms) |
| NFR-2 | Dialect conversion | ≥ 200 000 lines/s |
| NFR-3 | Robustness | malformed input never crashes or hangs (fuzzed per codec) |
| NFR-4 | Determinism | same input and options → same bytes |
| NFR-5 | Portability | standard C++20, no emulator types, builds alone (its own CMake target) |

## 7. Acceptance

- Every codec: byte-exact round trip on its corpus of files saved by the real assembler; decoded text equals what
  the assembler shows (captured from the emulator).
- Every sub-version conversion: the target assembler, running in the emulator, loads the file and builds the same
  binary as the source version did.
- Every dialect pair: the converted source, built by the target tool, gives a binary byte-identical to the original
  assembler's (or the report explains each difference).
- The symbol module's acceptance ([symbols/goals-and-requirements.md](symbols/goals-and-requirements.md) §7).

## 8. Glossary

| Term | Meaning |
|---|---|
| Format | How a source is stored as bytes: a tokenized layout or a text file with an encoding and line ends. |
| Sub-version | A version of an assembler whose format or token table differs (TASM 3 vs 4, ALASM 4.2 vs 5.09). |
| Dialect | The assembler's language: mnemonic and directive spellings, number and string forms, label rules, macros. One dialect can have several formats (ALASM tokenized and ALASM as text). |
| Codec | Decodes one format into a `SourceDocument` and encodes it back. |
| Tokenized | Keywords, mnemonics and registers stored as byte codes; spaces as counts; lines framed by lengths. |
| IR | The intermediate representation: a dialect-free model of a Z80 source. |
| Frontend / backend | A plugin that parses a dialect into IR / writes IR in a dialect. |
| Oracle | The independent truth a test compares with: the original assembler running in the emulator. |
