# Symbol exchange: formats

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review; every row marked **verify** is checked against the real tool's output before its codec is written (golden files come from the tool, not from this table) |
| **Rule** | Every format is one codec that **decodes and encodes** (decision D-1); nothing is vendored, prior art is reference only (D-2, [prior-art.md](../prior-art.md)) |
| **Architecture** | [architecture.md](architecture.md) |

## 1. Families

| Family | What it is | How it is read | How it is written |
|---|---|---|---|
| **Text** | one symbol per line in a tool's text format | the shared tokenizer splits each line; the format's small line grammar picks the fields | the format's line writer, after the target's name rules |
| **Script** | a program for another tool that sets names (IDC, IDAPython, a Ghidra script, MAME debugger commands) | read like text: the codec recognizes the statement forms it writes and the common hand-written ones (`set_name(0x…, "…")`, `idc.set_name(…)`, `comadd …`) | generated from a template |
| **Tokenized** | a ZX assembler's own binary source and label table (TASM, ALASM, XAS, STORM, GENS, ...), in a file or on a disk image | the codec's detokenizer: a **label table** gives names and values; a **source** gives names and lines, and values from `EQU` or by assembling the detokenized text with the core's `Z80TextAssembler` (§4.1) | the codec's tokenizer writes the assembler's own form (the round trip is tested on files the real assembler made) |
| **Live** | the label table of an assembler or monitor running in the emulated machine | a scanner looks for the table in a read-only copy of the RAM pages and reads it with the tokenized decoder | — |
| **Native** | the emulator's own lossless file `*.usym.json` | JSON | JSON |
| **Bundle** | a native or MAP file shipped in `data/symbols/`, chosen by ROM SHA-256 through `data/symbols/manifest.json` | as its format | — |

## 2. What each format can hold (lossiness matrix)

`●` holds it, `○` partly (see the note), `–` cannot hold it. On export, what a format cannot hold follows DT-4 of
[architecture.md](architecture.md) §6 (fold, comment or drop) and is counted in the report.

| Format | name | CPU address | page | device / other CPU | kind | size | local scope | source line | comment | aliases |
|---|---|---|---|---|---|---|---|---|---|---|
| native `*.usym.json` | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |
| our MAP | ● | ● | ○ ROM page only (`ROMn:`) | – | ○ `(CODE)` etc. | – | – | – | ● | – |
| simple SYM / Unreal `user.l` | ● | ● | ○ `PP:XXXX` in `user.l` | – | – | – | – | – | – | – |
| sjasmplus `.sym` / `--exp` | ● | ● | – | – | ○ all `EQU` | – | ○ `main.loop` | – | – | – |
| sjasmplus `.sld` | ● | ● | ● page per line | – | ● | – | ● | ● | – | – |
| z88dk `.map` | ● | ● | – | – | ○ | – | ● local / public | ● | – | – |
| pasmo symbol output | ● | ● | – | – | ○ | – | – | – | – | – |
| VICE labels | ● | ● | – | – | – | – | – | – | – | – |
| MAME commands | ○ as comments | ● | – | – | – | – | – | – | ● | – |
| IDA IDC / IDAPython | ● | ● | ○ as a segment per page (script decides) | – | ○ code / data | ○ `create_data` | – | – | ● | – |
| Ghidra script | ● | ● | ○ overlay blocks | – | ○ function / label | – | – | – | ● | – |
| tokenized (ALASM, ...) | ● | ● | ○ the bank the assembler used | – | ○ | – | ○ | ○ | – | – |

## 3. Catalog

Phase numbers refer to [tdd.md](tdd.md) §10.

| Codec id | Family | Decode | Encode | Detection (content first, extension as hint) | Status | Phase |
|---|---|---|---|---|---|---|
| `native` | native | ● | ● | JSON with `"format": "unreal-symbols"` | done (S1) | S1 |
| `unreal-map` | text | ● | ● | lines `[ROMn:\|RAMn:]HHHH  NAME  (TYPE) ; comment`, often a `---` banner; a bank prefix or a `(TYPE)` sets it apart | done (S2) | S2 |
| `simple-sym` | text | ● | ● | lines `HHHH NAME [(TYPE)] [; comment]` (also `sos.l`); the extension tells it from the other `HHHH NAME` formats | done (S2) | S2 |
| `unreal-l` | text | ● | ● | lines `HHHH name` (the linear RAM address: page `HHHH >> 14`) / `PP:HHHH name` (RAM page `PP`); RAM pages only (checked in Unreal's `MON_LABELS::load`) | done (S2) | S2 |
| `vice` | text | ● | ● | lines `al C:HHHH .name` | done (S2) | S2 |
| `sjasm-equ` | text | ● | ● | lines `NAME EQU $HHHH ; (TYPE)` (also reads sjasmplus' `NAME: EQU 0x0000HHHH`) | done (S2) | S2 |
| `z88dk-defc` | text | ● | ● | lines `DEFC name = $HHHH ; (TYPE)` (what LabelManager read) | done (S2) | S2 |
| `z88dk-map` | text | ● | ● | lines `name = $HHHH ; ...` **verify** the field list against z88dk's current output | new | S3 |
| `sjasmplus-sym` | text | ● | ● | lines `NAME: EQU 0x0000HHHH` (`--sym`, `--exp`), sorted by name; checked on sjasmplus 1.24 output, written back byte for byte | done (S3) | S3 |
| `sjasmplus-sld` | text | ● | ● | first line `\|SLD.data.version\|1`, then `file\|line\|deffile\|defline\|page\|value\|type\|data` (sjasmplus documentation, version 1); `L` lines give module / main / local and traits (`+equ`, `+local`), `T` lines mark code, `Z` the page size | done (S3) | S3 |
| `sjasmplus-lst` | text | ● | ● | the listing: a line defining a label gives its address (the source starts 18 characters after the address), `EQU` with a plain number its value, `MODULE` / `ENDMODULE` the prefix; no pages | done (S3) | S3 |
| `pasmo` | text | ● | ● | lines `NAME<TAB>[<TAB>]EQU 0HHHHH` (the third command-line argument; `--public` writes only PUBLIC names), sorted by name; a PROC's LOCAL label is named by pasmo (`00000000`); checked on pasmo 0.5.5, written back byte for byte | done (S3) | S3 |
| `ida-python` | script | ● | ● | — | new | S4 |
| `ida-idc` | script | ● | ● | `set_name(` / `MakeName(` lines | new | S4 |
| `ghidra` | script / text | ● | ● | lines `name address [type]` for Ghidra's symbol-import script **verify** | new | S4 |
| `mame` | script | ● | ● | — (debugger command file: `comadd HHHH,name`) **verify** | new | S4 |
| `cspect-map` | text | ● | ● | sjasmplus `--cspectmap` output **verify** | new | S4 |
| `tasm` | tokenized source | ● | ● | TR-DOS type `A` / `.$A`; line records `[len][bytes][trailer]`, tokens `#80-#F0`, `#0A n` = n spaces, `#FF` end (owner's 2012 converter; **verify** the trailer byte and TASM 3 vs 4 tables) | prior art | S6 |
| `alasm` | tokenized + live | ● | ● | after research | research | S7 |
| `xas` | tokenized + live | ● | ● | after research | research | S8 |
| `storm`, `gens`, `masm`, `zxasm` | tokenized | ● | ● | after research | research | S9 |
| `sts` (labels kept by the STS monitor) | live | ● | ● | after research | research | S9 |

Tools without a symbol format in this table (for example Fuse) are not targets until someone asks; the registry
makes adding one a single file.

### 3.1 Text formats, by example

The same three symbols in each text format the emulator reads today or will write (names as each tool needs
them):

| Format | `PRINT-A-1` (ROM 0, `#0010`) | `PLAYMUS` (RAM 3, `#0000`, shown at `#C000`) | `SCREEN` (`#4000`, any page) |
|---|---|---|---|
| native | `{"name":"PRINT-A-1","space":"rom0","offset":16,"kind":"entry"}` | `{"name":"PLAYMUS","space":"ram3","offset":0,"kind":"code"}` | `{"name":"SCREEN","space":"cpu:main","offset":16384,"kind":"data"}` |
| unreal-map | `ROM0:0010  PRINT-A-1  (CODE)` | `C000  PLAYMUS  (CODE)` (page lost: folded) | `4000  SCREEN  (DATA)` |
| unreal-l | — (RAM pages only) | `03:C000 PLAYMUS` | — (RAM pages only) |
| vice | `al C:0010 .PRINT_A_1` | `al C:C000 .PLAYMUS` (folded) | `al C:4000 .SCREEN` |
| sjasmplus-sym | `PRINT_A_1: EQU 0x00000010` | `PLAYMUS: EQU 0x0000C000` (folded) | `SCREEN: EQU 0x00004000` |
| ida-idc | `set_name(0x0010, "PRINT_A_1");` | `set_name(0xC000, "PLAYMUS");` (or in a `RAM3` segment) | `set_name(0x4000, "SCREEN");` |

The rows marked **verify** are written down from documentation and memory; the golden files produced by the real
tools decide.

### 3.2 Name rules per target (DT-3)

| Target | Allowed | First char | Case | Max length | Reserved |
|---|---|---|---|---|---|
| native, unreal-map, simple-sym, unreal-l | any printable except blank | any | kept | none | none |
| sjasmplus / sjasm / pasmo | `A-Z a-z 0-9 _ . ? ! # @` **verify** | letter, `_`, `.` (local) | kept | none | instructions, registers, directives |
| z88dk | C identifier + `_` | letter, `_` | kept | none | — |
| VICE | **verify** | `.` prefix written by the exporter | kept | — | — |
| IDA | C identifier + `@ $ ? .` **verify** | not a digit | kept | 511 **verify** | — |
| Ghidra | no blanks **verify** | — | kept | — | — |

## 4. Tokenized ZX assemblers: research plan

> **Moved to the library (2026-10-05, D-3).** Tokenized sources are now the [unreal-asm](../README.md) library's
> **source codecs** ([../source-formats.md](../source-formats.md)); the symbol module takes labels from any decoded
> source (`fromsource/`) and keeps only the **live label-table** scanners. This section stays as the background of
> that decision.

**What tokenized means here.** ZX assemblers keep their source in RAM in a compact binary form: mnemonics,
registers and often whole operands are single-byte codes (tokens), labels are numbers into a separate **label
table**, and line numbers or lengths are binary. The same form is saved to disk (a TR-DOS file, often type `C` or
the assembler's own letter). Reading labels from such a file or from the running assembler means knowing three
things per assembler and version: where the label table is, how one entry is laid out (name encoding, value,
flags, link to the next entry), and how the source refers to labels.

### 4.1 A source is not a label table

A tokenized **source** file holds label *names* and the lines that define them, but not their addresses: those exist
only after the assembler ran. A source codec therefore gives, per label: name, defining line, kind (from `EQU` /
`DEFB` / an instruction), and a value only where the line itself says it (`EQU 24`). With `--assemble` the
detokenized text goes through the core's two-pass `Z80TextAssembler`, whose symbol table supplies the addresses (and
reports what it could not assemble: macros, includes it cannot reach). A **label table** (in RAM after assembling,
or saved by the assembler) has the addresses directly.

### 4.2 Research

**Nothing here is coded from guesses** (proposal P-5). The references found ([prior-art.md](../prior-art.md): the
Unreal 0.37.1 table scans, ZX-M8XXX's detokenizer, H2ASM, the ZAsm View spec, our ALASM script) settle part of the
layouts and disagree in places, so each format is still researched and confirmed on files the real assembler made:

| Step | What | Output |
|---|---|---|
| R1 | Collect the assembler releases and their manuals (local ZX collection: ALASM 4.2-5.0, XAS 5.05 / 7.43, STORM 1.3, GENS 3 / 4, MONS-GENS, ZEUS-GENS, MASM, ZX ASM docs) | a materials index outside the repo |
| R2 | Run the assembler in the emulator (TTD recording on), type or load a known source: labels of every kind (global, local, `EQU`, long names, names with every allowed character), assemble | a session file |
| R3 | Dump RAM before / after defining each label; diff; find the table and the entry layout; confirm with a second source | `research-<assembler>.md` in this folder: table location rule, entry layout with offsets, name encoding, value width, flags, chain / hash / sorted, end marker, version differences |
| R4 | Save the source to a TR-DOS disk; read the file; find the same structures | the file layout in the same document |
| R5 | Golden corpus: the disk files and RAM dumps of R2 / R4 with the expected native JSON | `testdata/symbols/<assembler>/` |
| R6 | Importer (file + live scanner) against the corpus | code |

What is already known: **TASM** has a token table and a line layout in the owner's 2012 converter, plus TRD test
data ([prior-art.md](../prior-art.md) §1). From the Unreal 0.37 manual: XAS 7 keeps its labels in bank 6 (bank `#46` on a Pentagon with
more than 128K); ALASM 4.42-5.0x can be anywhere in 128K RAM (pages 1-7, so a scan); with the STS monitor, STS's
labels are in bank 7 (`#47`). These rules decide where the live scanners look first; the layouts come from R3.

**Detection of a tokenized file** uses what the research finds: the TR-DOS file type and name extension, a header
signature, and a table that parses consistently (every entry's name in the allowed character set, values in range,
the chain ending where the table says).

**Live scanning** (proposal P-6) runs on request only. The scanner copies the RAM pages at a coherent moment and
scores candidate positions; with several candidates the caller picks one (`--at ram6:#0000`).

## 5. The native file `*.usym.json`

```json
{
  "format": "unreal-symbols",
  "version": 1,
  "generator": "unreal-ng 2026-10-05",
  "sets": [
    {
      "id": "game.sym",
      "title": "Game symbols (sjasmplus)",
      "origin": { "kind": "file", "where": "game.sym", "sha256": "3f1c..." },
      "priority": 100,
      "enabled": true,
      "symbols": [
        { "name": "PLAYMUS", "space": "ram3", "offset": 0, "kind": "code", "size": 412,
          "module": "music", "source": { "file": "music.asm", "line": 12 },
          "comment": "plays one frame", "aliases": ["MUS_FRAME"],
          "provenance": { "importer": "sjasmplus-sld", "raw": "|music.asm|12||3|49152|F|PLAYMUS" } },
        { "name": "PLAYMUS.loop", "space": "ram3", "offset": 7, "kind": "local",
          "scope": { "parent": "PLAYMUS" } }
      ]
    }
  ]
}
```

Rules: offsets are numbers (no hex strings: exact and fast); a missing field means "unknown" (not zero); `space`
uses the spellings of [architecture.md](architecture.md) §3.1; unknown fields are kept on import and written back on
export (forward compatible).
