# unreal-asm: source formats and codecs

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft; every layout below is a **reference to confirm** on files the real assembler saved in the emulator (golden files come from the assemblers, not from this table) |
| **Rule** | One codec per format and sub-version, decode **and** encode (D-1); nothing vendored (D-2) |
| **Sources** | [prior-art.md](prior-art.md) (L = local, P = public references) |

## 1. Catalog

| Codec id | Assembler / sub-version | Dialect | Detection | Known from | Confidence | Phase |
|---|---|---|---|---|---|---|
| `text` | any text source (code page CP866 / KOI8-R / CP1251 / ASCII / UTF-8; CR, LF, CRLF) | any (chosen or guessed) | decodes as text; code page by statistics | — | high | A1 |
| `tasm3` | TASM 3.x | `tasm` | TR-DOS type `A`, start 39221 / 40872 (P1) | P1, L4 (heuristic), L5 data | medium | A2 |
| `tasm4` | TASM 4.x | `tasm` | type `A`, start ≤ 4096 (P1) | L1 (owner, 2012), P1 | medium-high | A2 |
| `alasm4` | ALASM 4.2-4.46 | `alasm` | type `H`; signature `F3 76 C7 DD FD ED B0 D9` (P1) | P1, P2, L8 | medium (P1 / P2 disagree on `#96`, `#9F`) | A3 |
| `alasm5` | ALASM 5.0-5.09 | `alasm` | as above; version by the token table in use | P1, P5 sample | medium | A3 |
| `storm` | STORM 1.x | `storm` | type `C` start `#C00B` / `#C003`, or type `R` start `#C00B` (P1) | P1 | medium | A4 |
| `zxasm` | ZX-ASM 2.5 / 3.x, ZAsm 3.01 / 3.10 | `zxasm` | types `a` / `z` / `C` with P1's start rules | P3 (written spec), P1 | medium-high | A4 |
| `xas` | XAS 5 / 7 | `xas` | type `X` / `x`, start bytes `AS` (P1) | none public (P4 binary) | research | A6 |
| `masm` | MASM (Spectrum), MASM80 (Sprinter) | `masm` | type `a`, start 38667 / 38821 (P1) | none public | research | A6 |
| `gens3` | GENS 3 / 4 compressed | `gens` | — | the Devpac manual (no byte layout) | research | A6 |
| `zeus` | Zeus tokenized | `zeus` | — | none | research | A6 |
| `ads` | ADS (a TASM-family editor) | `tasm` | — | P1 | medium | A6 |

Text-only dialects (no tokenized form; reached through `text`): `sjasmplus`, `sjasm`, `pasmo`, `z88dk` (z80asm),
`zmac`, `gens` (text), `zeus` (text).

## 2. What each tokenized format does (from the references)

| Feature | TASM 3 | TASM 4 | ALASM | STORM | ZX-ASM |
|---|---|---|---|---|---|
| Line framing | `len, content, len` | `len, content, len` | `len (incl. itself), content`; 0 ends | stored back to front, a trailing byte with the length in its low 6 bits | text lines ending `#0D` |
| File end | `#FF` length | `#FF` length | length 0 | the file start | end of file |
| Header | — | — | 64-byte TR-DOS-like header, name + source length at `+#21` (L8); signature (P1) | — | — |
| Space runs | `#0A n` | `#01 n` | bytes `#01-#1F` (L8) / `< #10` (P1) = n blanks | — | `#06 n` (n & `#7F`) |
| Tokens | `#80-#F0` with the trailing blank inside the token | the same range, some numbers reassigned (`DEFMAC`, `DISPLAY`, `ENDMAC`) | mnemonic table in the first token position; operand / register table later in the line | `#09-#8B`, operators, **expressions fully tokenized** (8 number forms, nested sub-expressions, 6-bit packed label names) | `#02-#05 t` prefix + token t `#20-#C8`; prefix bits: upper case, trailing blank |
| Text escapes | — | — | `#10` = CP866 text to the line end (P1); CP866 inside comments and strings (L8) | — | — |
| Editor marks | — | — | `#FF` line flag (L8: start and end of lines) | — | — |

### 2.1 What a byte-exact round trip must keep (attributes)

| Codec | Per line | Per file |
|---|---|---|
| tasm3 / tasm4 | blanks as run vs literal, run lengths; unknown token bytes | trailing bytes after the end marker |
| alasm | `#FF` flags and their position; run vs literal blanks; token vs literal spelling of a keyword typed in a comment | the header (name, length field, any unused bytes) |
| storm | the exact packing of each expression (number form, sub-expression structure) | — |
| zxasm | per token: case bit, trailing-blank bit; run vs literal blanks | line-end style |

When the same codec encodes a document it decoded, it uses these attributes; a document from another codec or from
edited text gets the codec's **canonical** form (runs for indentation, tokens for every keyword the table has,
upper case where the assembler shows upper case).

## 3. How each codec is confirmed (research steps, per assembler)

| Step | What | Output |
|---|---|---|
| R1 | The assembler releases and manuals (local ZX collection; the P references) | materials index outside the repo |
| R2 | In the emulator (TTD recording on): type or load a **probe source** with every construct of the dialect (every mnemonic and register, every directive, local / temporary labels, long names, every number form, strings with quotes, comments in Russian), save it to a TR-DOS disk | the probe file as the assembler wrote it |
| R3 | Compare the file with the references' layouts; resolve conflicts (ALASM `#96`, `#9F`); find what the references do not say | `research-<codec>.md` in this folder |
| R4 | Screen captures of the assembler showing the probe source (OCR of the emulator screen) | the expected decoded text |
| R5 | Assemble the probe in the assembler; keep the binary | the binary oracle for dialect conversion |
| R6 | Golden corpus `testdata/asm/<codec>/`: probe files, real-world sources (L5 data, P5 sample, disks of the collection), expected text, binaries | tests |

## 4. Format conversions planned first

| From → to | Through | Expected losses |
|---|---|---|
| tasm3 ↔ tasm4 | `tasm` text | TASM 4-only directives into TASM 3 (refused or forced as text) |
| alasm4 ↔ alasm5 | `alasm` text | directives new in 5.x into 4.x |
| any tokenized → text | its dialect text | none in text; attributes dropped |
| text → any tokenized | the codec's canonical form | lines with keywords the table lacks (DT-3) |
