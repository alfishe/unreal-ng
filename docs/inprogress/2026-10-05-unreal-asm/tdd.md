# unreal-asm: technical design

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Inputs** | [goals-and-requirements.md](goals-and-requirements.md), [architecture.md](architecture.md), [source-formats.md](source-formats.md), [dialect-conversion.md](dialect-conversion.md); the symbol module: [symbols/tdd.md](symbols/tdd.md) |

## 1. Rules

- Standard C++20 only; no emulator, Qt or drogon types (checked: the library builds as its own CMake target with
  only its own include paths).
- Namespace `unrealasm` (`unrealasm::codecs`, `unrealasm::ir`, `unrealasm::dialects`, `unrealasm::symbols`).
- Naming as in the emulator: PascalCase methods, camelCase fields, no underscores in file or class names; tests
  `<file>_test.cpp`.
- One folder per codec and per dialect plugin; a new format or dialect touches only its folder and one registry line.
- Every codec decodes and encodes (D-1); nothing vendored (D-2).

## 2. Layout

```text
core/src/3rdparty/unreal-asm/
├── CMakeLists.txt                      # target unreal-asm (static), unreal-asm-tests, zxasm (CLI)
├── README.md                           # what it is, MIT-style licence note, build
├── include/unrealasm/                  # public API
│   ├── bytes.h                         # ByteSource, ByteSink, CatalogHints
│   ├── document.h                      # SourceDocument, SourceLine, AttrBag, CodePage, LineEnd
│   ├── codec.h                         # ISourceCodec, CodecInfo, DetectResult
│   ├── registry.h                      # CodecRegistry, DialectRegistry
│   ├── ir.h                            # Program, IrLine, Label, Instruction, Operand, Directive, Expr
│   ├── dialect.h                       # IDialectFrontend, IDialectBackend, Capabilities, ConversionOptions
│   ├── convert.h                       # FormatConvert(), DialectConvert(), reports
│   ├── diagnostics.h
│   └── symbols/                        # the symbol module's public API (symbols/tdd.md)
├── src/
│   ├── core/                           # document, code pages (CP866, KOI8-R, CP1251 tables), diagnostics
│   ├── text/                           # the shared lexer helpers (numbers in every ZX notation, strings, comments)
│   ├── codecs/
│   │   ├── text/       textcodec.h / .cpp
│   │   ├── tasm/       tasm3codec, tasm4codec, tasmtokens (tables per sub-version)
│   │   ├── alasm/      alasm4codec, alasm5codec, alasmtokens, alasmheader
│   │   ├── storm/      stormcodec, stormexpr (tokenized expressions)
│   │   ├── zxasm/      zxasmcodec, zxasmtokens
│   │   └── xas/ masm/ gens3/ zeus/ ads/   (after research)
│   ├── ir/                             # nodes, builders, printers (debug), transforms
│   ├── dialects/
│   │   ├── sjasmplus/  frontend.cpp, backend.cpp, rules.cpp
│   │   ├── alasm/      frontend.cpp, backend.cpp, rules.cpp
│   │   ├── tasm/  storm/  zxasm/  pasmo/  z88dk/  gens/
│   ├── convert/                        # format conversion (DT-3), dialect conversion driver (DT-2)
│   └── symbols/                        # the symbol module (symbols/architecture.md §9)
├── tools/zxasm/                        # zxasm CLI: decode, encode, convert, detect, formats, dialects, symbols
├── tests/                              # unreal-asm-tests (GoogleTest from lib/), one file per source file
├── benchmarks/
└── testdata/                           # small fixtures; the big corpus lives in testdata/asm/ of the repository

core/src/debugger/asm/                  # emulator adapters: DiskFileSource / Sink, CatalogHints from TR-DOS,
                                        # the surfaces' shared functions (decode / encode / convert / detect)
```

## 3. Core types

```cpp
namespace unrealasm
{
enum class CodePage : uint8_t { Ascii, Cp866, Koi8r, Cp1251, Utf8 };
enum class LineEnd : uint8_t { Cr, Lf, CrLf };

struct AttrBag { CodecId codec = 0; std::vector<uint8_t> bytes; };   // codec-private; small

struct SourceLine { std::string text; AttrBag attrs; ByteRange origin; };

struct SourceDocument
{
    std::string name;
    DialectId dialect; CodecId format; std::string subversion;
    CodePage codePage = CodePage::Ascii; LineEnd lineEnd = LineEnd::Lf;
    std::vector<SourceLine> lines;
    AttrBag fileAttrs;
};

struct CatalogHints { char type = 0; uint16_t start = 0; uint16_t length = 0; std::string name; };

class ISourceCodec
{
public:
    virtual const CodecInfo& Info() const = 0;          // id "alasm4", title, dialect, sub-version range
    virtual int Detect(std::span<const uint8_t> bytes, const CatalogHints&) const = 0;   // 0..100
    virtual DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions&) const = 0;
    virtual EncodeResult Encode(const SourceDocument&, const EncodeOptions&) const = 0;   // bytes + diagnostics
    virtual LineCheck CheckLine(std::string_view text) const = 0;                        // DT-3
};
}  // namespace unrealasm
```

`DecodeResult` = document + diagnostics (`{severity, line, byteOffset, message}`); unknown token bytes are decoded
to an escape (`\x9F` style inside the line text, configurable) and encoded back to the same byte.

## 4. Dialect plugins

```cpp
class IDialectFrontend
{
public:
    virtual DialectId Id() const = 0;
    virtual ParseResult Parse(const SourceDocument&, const ParseOptions&) const = 0;   // Program + diagnostics
};

class IDialectBackend
{
public:
    virtual DialectId Id() const = 0;
    virtual const Capabilities& Caps() const = 0;     // directive kinds, label rules, number forms, reserved words
    virtual WriteResult Write(const ir::Program&, const WriteOptions&) const = 0;      // SourceDocument (text)
};
```

`DialectConvert(document, targetDialect, options)` = frontend `Parse` → transforms (by the backend's capabilities
and the options) → backend `Write` → a `ConversionReport` (rewritten, expanded, commented, refused, renamed: each
with its source line).

## 5. Registry

`CodecRegistry::Builtin()` and `DialectRegistry::Builtin()` list every compiled-in codec and plugin (one line each).
Detection follows DT-1 of [architecture.md](architecture.md) §7.

## 6. CLI (`zxasm`)

```text
zxasm detect   <file|trd:IMAGE/NAME.T>
zxasm decode   <in> [-o out.asm] [--codec c] [--escape hex|keep]
zxasm encode   <in.asm> --codec c [-o out] [--force]
zxasm convert  <in> --to <codec|dialect> [-o out] [--unsupported comment|expand|refuse] [--macros keep|expand]
zxasm batch    <image.trd> --to text [-d outdir]          # every source on a disk
zxasm formats | dialects
zxasm symbols  <in> [--assemble] [--export f --format sym-codec]
```

The CLI reads TRD / SCL / hobeta itself through a small container reader inside `tools/zxasm` (not part of the
library API; the emulator uses its media manager instead).

## 7. Emulator integration

Adapters in `core/src/debugger/asm/`: `DiskFileSource` / `DiskFileSink` (read and write a file in a disk image
through the media manager, honoring write protection and the image's modified flag), `CatalogHintsFromTrdos`, and one
shared function per operation for the surfaces (WebAPI + OpenAPI tag `asm`, CLI `asm ...`, MCP tool `asm_source`,
Lua / Python `asm_*`, Qt disk browser actions). The symbol module's adapters: [symbols/tdd.md](symbols/tdd.md) §3, §7.

## 8. Phases

| Phase | Work | Ends with |
|---|---|---|
| A0 | Design (this folder); open questions Q-1 … Q-6 | decisions recorded |
| A1 | Library skeleton (CMake, tests, CLI), document model, code pages, `text` codec, registry, detection | round trip of text files in every code page and line end |
| A2 | `tasm3`, `tasm4` (research documents first), sub-version conversion | byte-exact on the corpus; TASM in the emulator loads the converted files |
| A3 | `alasm4`, `alasm5` (research: settle `#96` / `#9F`) | the same |
| A4 | `storm`, `zxasm` | the same |
| A5 | IR, transforms, `sjasmplus` frontend + backend, `alasm` frontend; ALASM → sjasmplus | binary equality on the corpus |
| A6 | `tasm`, `storm`, `zxasm` frontends; `pasmo`, `z88dk` backends; research codecs `xas`, `masm`, `gens3`, `zeus`, `ads` | per pair / codec |
| A7 | Emulator adapters and surfaces; Qt disk browser actions; recipe `.recipe/analysis/asm-sources.md` | live checks |
| A8 | Symbols on the library ([symbols/tdd.md](symbols/tdd.md) phases S1-S5 re-based; tokenized label tables come from the source codecs) | symbol acceptance |
| A9 | Benchmarks, results; docs `docs/features/asm-sources.md` | numbers meet NFR-1, NFR-2 |
