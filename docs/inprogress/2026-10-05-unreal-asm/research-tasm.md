# unreal-asm: TASM source format research, every version (phases A2, A3)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Codec** | `tasm`, versions `2.0` (text), `3` (3.0-3.5), `4.0` (4.0 XL Design, 4.4 KVA), `4.12` (`core/src/3rdparty/unreal-asm/src/codecs/tasm/`; one codec for every version, D-15) |
| **Programs** | [TASM 2.0](https://zxart.ee/releasefile/id:249301/TASM2_0.zip), [3.0](https://zxart.ee/releasefile/id:249302/TASM3_0.zip), [3.02](https://zxart.ee/releasefile/id:249303/TASM3_2.zip), [3.5](https://vtrd.in/system/TASM_3_5.zip), [4.0 XLD](https://zxart.ee/releasefile/id:249304/TASM4_0.zip), [4.4 KVA](https://zxart.ee/releasefile/id:249307/TASM4_4.zip), [4.12](https://zxart.ee/releasefile/id:249305/TASM_412.ZIP) (also [TRD](https://zxart.ee/releasefile/id:155527/TASMV4.12.trd.zip)); keyword tables read from each binary |
| **Corpus** | 127 distinct type-`A` sources: 24 TASM 3 (from [Legend of Kyrandia demo](https://zxart.ee/releasefile/id:168024/LegendOfKyrandiaDemo.trd.zip)), 91 TASM 4.0 / 4.4 (General Sound ROM 1.04 sources, Sprinter BIOS and 2D Studio sources, the TASM 3.02 disk's examples), 12 TASM 4.12 (the program's own examples, Kyrandia, [sobdemo](https://zxart.ee/releasefile/id:298066/sobdemo.zip), J7N's HD formatter) |
| **Result** | all 127 (and the TASM 2.0 file made in the emulator) decode and encode back **byte for byte**; the version from the catalog is never contradicted by the bytes; the canonical tokenizer alone writes 99.85 % of the lines exactly as TASM stored them (§5); the decoded text matches TASM's screen in 3.0, 4.0 and 4.12, and a file converted 4.0 → 4.12 opens in TASM 4.12 (§6.1) |

## 1. Example first

The first line of `000LOAD` (saved by TASM 4.0 / 4.4, catalog start 40872) is stored as 12 bytes:

```
0A | 0A 08 | BF | 0A 04 | 32 35 30 30 30 | 0A
len  8 blanks "org "  4 blanks  "25000"        len
```

and decodes to `        ORG     25000`: the opcode in column 8, the operand in column 16, keywords in capitals: exactly
what TASM shows (checked in the emulator, §6.1). TASM 4.12 stores the same line as `08 BF 04 "25000"`: it writes a
blank count directly (`08` = eight blanks).

## 2. Versions

"TASM" is three families with different authors (findings: the research agent's notes in the collection, the
binaries themselves):

| Version | Who, year | Catalog start of a saved source | Blanks | Keyword table |
|---|---|---|---|---|
| 2.0 | Rst7, 1993 | type `C`, 38750 | **plain text**: CR LF, TABs (§2.1) | none in the file (the binary's table, `#9B` DISP / `#9F` ENT, is for assembling) |
| 3.0-3.5 | Rst7 (3.5 with Alex Raider), 1994-95 | 39221 | `#0A n` | ends at `#E6` incbin (103 words) |
| 4.0, 4.4 | Sergey Pavlov / XL Design 1996; KVA 1996 | 40872 | `#0A n` | 3.x plus `#E7`-`#F0` sli inf lx hx ly hy db dm ds dw |
| 4.12 | Rst7 (Code Busters), 1997 | ≤ 4096 (the editor's line: 0, 17, 32, ...) | a byte `#02`-`#1F` is that many blanks; `#01 n` = n blanks | 3.x with `#97` defmac, `#9B` display, `#9F` endmac; ends at `#E4` |

The start values are words in each binary. TASM 2.0 sources are a different format altogether ("TASM 3.0's text is
not compatible with TASM 2.0", TASM 3.0's help, which imports 2.0 files).

### 2.1 TASM 2.0: text with editor tabs

No 2.0 source survives in any collection searched, so one was made: TASM 2.0 run in unreal-ng (Pentagon, TR-DOS,
`RUN "TASM128"`), a test source typed through the keyboard API, saved with `S` (TTD recorded). The file
(`testdata/tasm/T20SRC.$C`, type `C`, start 38750) is plain text:

- every line ends with CR LF;
- a blank run that reaches a tab stop (every 8 columns) is stored as one TAB per stop reached, the blanks after the
  last stop as blanks, everywhere (also inside strings and comments): `1234567 X` → `1234567` TAB `X`,
  `A` + 23 blanks + `B` → `A` TAB TAB TAB `B`, `ld a,7` + 6 blanks + `;` → TAB + 4 blanks, `X  Y` unchanged;
- no tokens; bytes above `#7F` are read as CP866 (not checked: the editor has no Russian input).

TASM 2.0 started with CAPS LOCK on: what was typed in capitals is stored in lower case; the file keeps what the editor
stored, the codec does not change case.

The owner's 2012 converter ("TASM4") used the 4.0 XLD table; the ZX-M8XXX references ("TASM 4.x": run byte `#01`,
defmac / display / endmac) describe 4.12, whose real files use direct counts (no `#01` in any of them).

## 3. The stream

| Part | Bytes | Notes |
|---|---|---|
| line record | `[n] [n body bytes] [n]` | the length is repeated after the body so the editor can scroll backwards; `n` ≤ 254; an empty line is `00 00` |
| end | `FF FF` | a length byte `FF` ends the source |
| after the end | — | in a hobeta file the rest of the last sector is left-over memory (kept by the container as its `tail`) |

Body bytes: `#20`-`#7E` ASCII, blanks as in §2, `#80`-`#F0` the version's keywords, shown in capitals (the tables in
the binaries are upper case); a keyword that is an instruction or directive carries its trailing blank (`#B3` =
`LD `), register and condition names do not (`#85` = `HL`); `#83` is `AF'`. Any other byte (and a keyword code the version does not have) decodes as U+F700 + byte.

## 4. Canonical tokenizer (how an edited or foreign line is written)

A line keeps its original body while its text does not change. An edited line, or a line from another version or
codec, goes through these rules (derived from the corpus, §5):

1. Blank gaps of two or more: `#0A n` runs (3, 4.0; up to 255) or direct counts (4.12; up to 31 per byte). A single
   blank stays literal.
2. **Every** whole word that is a keyword of the version, in any case, becomes its token, wherever it stands: label
   field (`include FILE` at column 0), operands, strings (`"(c) 2000"`) and comments (`;ld a,(hl)`). A word must not
   touch a letter, digit, `_`, `.`, `@`, `$`, `#` or `\` (TASM 4.12 macro parameters: `\r`, `\0`). Case: the files
   hold lower-case words that were tokenized and no upper-case keyword left as text outside character constants, so
   the match ignores case; the display is in capitals either way. A keyword with a trailing blank is used only when a blank
   follows and takes that blank. `af'` takes its apostrophe.
3. A character constant `"x"` stays as typed (`cp "a"`, `.IF KEY-"p"`).
4. A character TASM cannot hold is an error naming the line and the column; U+F700 + byte is written as that byte.

## 5. What the corpus shows

| Version (catalog) | Files | Lines | Canonical tokenizer exact | Version from the bytes alone |
|---|---|---|---|---|
| 3 | 24 | 1349 | all | 3 or 4.0 (no keyword tells them apart) |
| 4.0 / 4.4 | 91 | 38208 | 99.8 % | 4.0 for 50 files (they use db / dw / lx …), 3 or 4.0 for 40, one 4-byte file fits all |
| 4.12 | 12 | 4740 | all | 4.12 (direct blank counts) |

The lines the canonical tokenizer writes differently are all in the Sprinter BIOS / tools sources and TOOLS: some
comments and strings there keep keyword words as plain text (`in`, `or`, `bit`, `(l,h)`) while the rest of the same
files tokenizes them: those files were most likely produced by a text-to-TASM converter, not typed in TASM. Their
bytes are kept exactly.

**Version detection.** The catalog's start field decides when the file comes with one. Without it, every version
decodes and re-tokenizes each line; a line counts for a version when it comes back unchanged and holds no byte the
version gives no meaning (a direct blank count in a 3.x reading, `#ED` in a 3.x reading). All versions with the most
lines are reported (`DecodeResult::subversions`), the newest is chosen.

## 6. Test data (`testdata/tasm/`)

| File | Version | Why |
|---|---|---|
| `PRINTHL`, `APEAR` | 3 | Kyrandia, start 39221 |
| `000LOAD`, `CALLLOAD` | 4.0 | from the TASM 3.02 disk, but start 40872: saved by 4.0 / 4.4; `db` / `dw` |
| `TABLES_L`, `SGEN_ASM` | 4.0 | General Sound 1.04 sources: `db` / `dw` / `lx` |
| `EXAMPLES`, `SINUS`, `SNAKE` | 4.12 | TASM 4.12's own examples: `defmac` / `endmac`, direct counts, `"p"` constants |
| `ODNO` | 4.12 | start 71 |

### 6.1 Checked in the emulator

| TASM | File | What the screen shows |
|---|---|---|
| 3.0 | `PRINTHL` | the decoded text line for line: keywords in capitals, labels and operands as stored, columns 8 / 16 |
| 4.0 | `000LOAD` | the same, `DB` / `DW` as keywords |
| 4.12 | `000LOAD` converted 4.0 → 4.12 by the codec | opens without complaint, the same text; `db` / `dw` (no 4.12 keywords) as plain text, as the conversion warned |
| 2.0 | the typed test source | what §2.1 describes (the file was saved there) |

Run: an own emulator instance (Pentagon, TR-DOS, `RUN "<tasm>"`, `E` + the file name), TTD recorded; the screenshots
are kept with the research materials, not in the repository.

## 7. Open items

| Item | Note |
|---|---|
| 4.0 vs 4.4 | same start and table in the files seen; a file showing a difference would split the version |
