# unreal-asm: architecture

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Requirements** | [goals-and-requirements.md](goals-and-requirements.md) |
| **Next** | [source-formats.md](source-formats.md), [dialect-conversion.md](dialect-conversion.md), [tdd.md](tdd.md) |

## 1. In one paragraph

A **codec** turns a source file's bytes into a **SourceDocument**: the exact lines the assembler shows, in its own
**dialect**, plus the attributes needed to write the same bytes again. Two documents of the same dialect convert
into each other by re-encoding the text with another codec (**format conversion**). To change the dialect, a
**frontend** plugin parses the document into the **IR** (a dialect-free Z80 source model) and a **backend** plugin
writes the IR in the target dialect (**dialect conversion**). **Consumers** (symbols, assembling, source maps, the
emulator's disk tools) work on documents or on the IR.

## 2. Layers

```mermaid
flowchart TB
    subgraph L1["1 · bytes"]
        BS["ByteSource / ByteSink<br/>file · buffer · (emulator) disk file · memory"]
        CH["CatalogHints<br/>TR-DOS type · start · name"]
        EN["encoding detectors (D-13)<br/>code page · line ends · text / binary<br/>reusable outside the library"]
    end
    subgraph L2["2 · codecs (one per format + sub-version)"]
        C1["tasm (3.x, 4.x)"] --- C3["alasm (3.8 … 5.09)"] --- C4["storm"] --- C5["zxasm"] --- C6["text (code page, line ends)"]
    end
    subgraph L3["3 · document"]
        SD["SourceDocument<br/>lines · dialect · format · code page · attributes"]
    end
    subgraph L4["4 · dialect plugins"]
        FE["frontends<br/>alasm · tasm · storm · zxasm · gens · sjasmplus · pasmo"]
        IR["IR<br/>Program · Line · Label · Instruction · Directive · Expr"]
        BE["backends<br/>sjasmplus · pasmo · z88dk · alasm · tasm · ..."]
    end
    subgraph L5["5 · consumers"]
        SY["symbols"]
        ASM["assemble (Z80TextAssembler · external tools)"]
        SM["source map"]
    end
    BS --> L2
    CH --> L2
    EN --> L2
    L2 <--> SD
    SD --> FE --> IR --> BE --> SD
    IR --> SY & ASM & SM
    SD --> SY
```

Rule: layers 1-5 use the standard library only. The emulator's disk images, memory and debugger are reached through
adapters outside the library ([§8](#8-emulator-integration)).

## 3. Data model

```mermaid
classDiagram
    class SourceDocument {
        +name : string
        +dialect : DialectId
        +format : FormatId
        +subversion : string
        +codePage : CodePage
        +lineEnd : LineEnd
        +lines : vector~SourceLine~
        +fileAttrs : AttrBag
    }
    class SourceLine {
        +text : string (UTF-8, exact)
        +attrs : AttrBag (codec-private)
        +origin : ByteRange
    }
    class AttrBag {
        +codec : CodecId
        +bytes : small blob
    }
    class Program {
        +dialect : DialectId
        +lines : vector~IrLine~
    }
    class IrLine {
        +pos : SourcePos
        +label : optional~Label~
        +body : Instruction | Directive | MacroCall | Raw | none
        +comment : optional~string~
    }
    class Label {
        +name : string
        +scope : Global | Local | Temporary | Module
        +parent : string
    }
    class Instruction {
        +mnemonic : Mnemonic
        +operands : vector~Operand~
    }
    class Directive {
        +kind : Org | Equ | Defl | Db | Dw | Ds | Include | Incbin | If | Else | EndIf | Macro | EndM | Rept | EndR | Disp | Ent | Module | EndModule | Align | Display | Other
        +args : vector~Expr~
        +text : string (Other)
    }
    class Expr {
        +node : Number | Symbol | Current | Unary | Binary | Call | String | Paren
        +spelling : NumberForm
    }
    SourceDocument "1" o-- "*" SourceLine
    SourceLine --> AttrBag
    SourceDocument --> AttrBag
    Program "1" o-- "*" IrLine
    IrLine --> Label
    IrLine --> Instruction
    IrLine --> Directive
    Instruction --> Expr
    Directive --> Expr
```

**Why two models.** The document is for **lossless** work: decoding and encoding, format conversion, showing the
source as the assembler does. The IR is for **meaning**: converting dialects, extracting symbols, assembling. A
frontend can always fall back to `Raw` for a line it cannot parse, so nothing is dropped silently.

**Attributes.** Everything a format stores that is not text lives in the codec's attribute bag: ALASM's `#FF`
editor flags, whether TASM wrote blanks as a run (`#0A n` / `#01 n`) or literally, ZX-ASM's per-token case and
trailing-space bits, file headers. Encoding with the **same** codec uses them for a byte-exact result; another codec
ignores them and writes its own canonical form.

## 4. Pipeline 1: decode and encode

```mermaid
sequenceDiagram
    autonumber
    participant U as Caller (CLI · emulator)
    participant R as CodecRegistry
    participant C as Codec (alasm-4.x)
    U->>R: Detect(bytes, catalog hints)
    R-->>U: alasm-4.x (score 92), alasm-5.x (70)
    U->>C: Decode(bytes)
    C-->>U: SourceDocument (812 lines, CP866, attrs) + diagnostics
    Note over U: edit the text, or just look
    U->>C: Encode(document)
    C-->>U: bytes (identical when nothing changed)
```

## 5. Pipeline 2: format conversion (same dialect)

```mermaid
flowchart LR
    A["bytes · tasm 3"] -->|"decode"| D["SourceDocument<br/>dialect tasm"]
    D --> V{"every line valid<br/>in tasm 4?"}
    V -->|"yes"| E["encode as tasm 4<br/>(canonical attributes)"]
    V -->|"no"| R["report lines<br/>(stop, or --force: keep as text / comment)"]
    R --> E
    E --> B["bytes · tasm 4"]
```

The text decides. A sub-version conversion is "decode with one codec, check the lines against the other, encode
with the other". The check is a light pass of the dialect's lexer (are all keywords known to the target?), not a
full parse, so it works before any frontend exists.

## 6. Pipeline 3: dialect conversion (plugins)

```mermaid
flowchart LR
    D1["SourceDocument<br/>alasm"] --> F["frontend: alasm<br/>lexer · parser"]
    F --> P["Program (IR)"]
    P --> X["transforms<br/>local-label renaming · macro expansion (opt) · number forms"]
    X --> B["backend: sjasmplus<br/>capabilities · writer"]
    B --> D2["SourceDocument<br/>sjasmplus (text)"]
    B --> REP["ConversionReport<br/>rewritten · commented · refused (by line)"]
```

- **Frontends** know one dialect's grammar (directives, label rules, expression syntax, macro syntax) and produce
  IR with source positions.
- **Transforms** are dialect-free IR passes shared by all pairs: rename labels to the target's rules, expand macros
  or repeat blocks when the target lacks them, change number spellings, resolve `$` / `*` current-address forms.
- **Backends** declare what they can write; for the rest the conversion options decide (DT-2).

N frontends and M backends give N × M pairs. Each frontend and each backend is its own module: a new dialect is one
frontend folder and one backend folder ([dialect-conversion.md](dialect-conversion.md)).

## 7. Decision trees

### DT-1: which codec reads these bytes

```mermaid
flowchart TD
    A["bytes + catalog hints"] --> B{"codec forced?"}
    B -->|"yes"| F["that codec"]
    B -->|"no"| C["every codec scores: signature · catalog type / start · structure walk"]
    C --> D{"best ≥ 60 and ≥ 15 above the next?"}
    D -->|"yes"| F
    D -->|"no"| E["report candidates; plain text if it decodes as text"]
```

### DT-2: an IR construct the backend cannot write

```mermaid
flowchart TD
    A["construct"] --> B{"backend has a rewrite rule?<br/>(e.g. DUP → REPT)"}
    B -->|"yes"| R["rewrite; report 'rewritten'"]
    B -->|"no"| C{"options.unsupported"}
    C -->|"expand (macro, repeat)"| E["expand in IR; report"]
    C -->|"comment (default)"| M["write the original line as a comment + a marker; report"]
    C -->|"refuse"| X["stop with the line"]
```

### DT-3: a line a format-conversion target cannot hold

```mermaid
flowchart TD
    A["line"] --> B{"all keywords known to the target codec?"}
    B -->|"yes"| W["encode"]
    B -->|"no"| C{"options"}
    C -->|"stop (default)"| S["report, write nothing"]
    C -->|"--force"| K["write the unknown keyword as plain text (target loads it as a label / error), report"]
```

## 8. Emulator integration

```mermaid
flowchart LR
    subgraph Emu["emulator (core)"]
        MED["media manager<br/>TRD · SCL · FAT · hobeta"]
        MEM["Memory pages"]
        DBG["debugger: LabelManager · source view"]
        SURF["WebAPI · CLI · MCP · Lua · Python · Qt"]
    end
    subgraph Ad["adapters (core/src/debugger/asm/)"]
        DF["DiskFileSource / DiskFileSink<br/>disk:A/GAME.H"]
        MV["MemoryView (live label tables)"]
        HINT["CatalogHints from the TR-DOS entry"]
    end
    LIB["unreal-asm<br/>codecs · documents · plugins · symbols"]
    MED --> DF --> LIB
    MED --> HINT --> LIB
    MEM --> MV --> LIB
    LIB --> DBG
    SURF --> LIB
```

| Surface (emulator) | Calls |
|---|---|
| WebAPI | `POST /asm/decode`, `/asm/encode`, `/asm/convert`, `GET /asm/formats`, `/asm/dialects`, `POST /asm/detect` (paths: host file, `disk:A/NAME.T`, or base64) |
| CLI | `asm decode / encode / convert / detect / formats / dialects` |
| MCP | tool `asm_source` with those actions |
| Lua, Python | `asm_decode{}`, `asm_encode{}`, `asm_convert{}`, ... |
| Qt | disk browser: "Open as source", "Export as text", "Convert to..."; the debugger's source view |
| standalone | `zxasm` CLI (no emulator) |

## 9. Code placement

`core/src/3rdparty/unreal-asm/` (owner decision D-3); full tree in [tdd.md](tdd.md) §2. The emulator adapters live in
`core/src/debugger/asm/` (and the symbol adapters in `core/src/debugger/symbols/`, [symbols/architecture.md](symbols/architecture.md) §9).
