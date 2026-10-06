# unreal-asm: source formats and codecs

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft; every layout below is a **reference to confirm** on files the real assembler saved in the emulator (golden files come from the assemblers, not from this table) |
| **Rule** | One codec per format, decode **and** encode (D-1), covering every version of the format (D-15); nothing vendored (D-2) |
| **Sources** | [prior-art.md](prior-art.md) (L = local, P = public references) |

## 1. Catalog

| Codec id | Assembler / sub-version | Dialect | Detection | Known from | Confidence | Phase |
|---|---|---|---|---|---|---|
| `text` | any text source (code page CP866 / KOI8-R / CP1251 / ASCII / UTF-8; CR, LF, CRLF) | any (chosen or guessed) | decodes as text; code page by statistics | — | high | A1 |
| `tasm` | TASM 2.0 (`2.0`, text), 3.0-3.5 (`3`), 4.0 XLD / 4.4 KVA (`4.0`), 4.12 (`4.12`) | `tasm` | TR-DOS type `A` (2.0: type `C`, start 38750); version: start 39221 → 3, 40872 → 4.0, ≤ 4096 → 4.12, else from the bytes | every release's binary; [research-tasm.md](research-tasm.md) | confirmed on 127 real files | A2, A3 |
| `alasm` | ALASM 3.8, 4.2, 4.42, 4.5, 4.44, 5.07-5.09 | `alasm` | type `H`; signature `F3 76 C7 DD FD ED B0 D9` at `+#28`; version: the newest that re-tokenizes the file exactly | ALASM 5.09's own sources (P9), every release's binary; [research-alasm.md](research-alasm.md) | confirmed on 429 real files | A3 |
| `storm` | STORM 1.0beta (`1.0`), 1.2-1.3i (`1.3`) | `storm` | type `C` start #C00B (#C003 for 1.0beta) or `R` #C00B, the backward line walk reaching the start | STORM's own source, the FAR plugin port; [research-storm.md](research-storm.md) | confirmed on 42 real files | A4 |
| `zxasm` | ZX-ASM 2.4-2.6 (`2`), 3.0-3.10 (`3.0`), Lite 1.07 (`lite`), ZAsm 3.15-4.20 (`3.15`) | `zxasm` | type `C` at #A135-#A1DF / #2020 (2.x) / 35151 (3.0), `z` "as" (3.01), `a` "sm" (3.10+); else from the bytes | every release's binary, za_format.txt; [research-zxasm.md](research-zxasm.md) | confirmed on 372 real files | A4 |
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
| tasm (3, 4) | blanks as run vs literal, run lengths; unknown token bytes | trailing bytes after the end marker |
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
| R7 | (P2, for the [memory bridge](memory-bridge.md)) the assembler's memory management: where its source buffer, pointers, pages and label table live, per version | `research-<codec>.md` §memory |
| R6 | Golden corpus `core/src/3rdparty/unreal-asm/testdata/<codec>/` (D-14): probe files, real-world sources (L5 data, P5 sample, disks of the collection), expected text, binaries | tests |

## 4. Format conversions planned first

| From → to | Through | Expected losses |
|---|---|---|
| tasm 3 ↔ 4.0 ↔ 4.12 | `tasm` text | a keyword only one version has is written as text with a warning (4.0 `db`, 4.12 `defmac` on #97 where 3 / 4.0 have `defm`) |
| alasm 3.8 … 5.07 | `alasm` text | the same rule (5.x `DD` = hex bytes, 3.8-4.5 `DEFM` = a string; 5.x `IF0`, 4.x `IF`) |
| any tokenized → text | its dialect text | none in text; attributes dropped |
| text → any tokenized | the codec's canonical form | lines with keywords the table lacks (DT-3) |
