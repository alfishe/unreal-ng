# unreal-asm: TASM 3 / TASM 4 source format research (phase A2)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Codec** | `tasm`, versions `3` and `4` (`core/src/3rdparty/unreal-asm/src/codecs/tasm/`; one codec for every version, D-15) |
| **Sources** | prior-art L1 (the owner's `ZConverter_TASM4.cpp`, 2012), L2/L3, L5 (real TASM 3.2 files), P1 (ZX-M8XXX `asm-detok.js`); `TASM30.DOC` from the TASM 3.01 archive |
| **Status** | TASM 3: confirmed byte for byte on real files. TASM 4: **provisional**, from L1 and P1 only (no TASM 4 file found yet) |

## 1. Example first

The first line of `000LOAD` (TASM 3.2, a loader) is stored as 12 bytes:

```
0A | 0A 08 | BF | 0A 04 | 32 35 30 30 30 | 0A
len  8 blanks "org "  4 blanks  "25000"        len
```

and decodes to `        org     25000`. The opcode is at column 8 and the operand at column 16: that is how TASM
lays out the line on its screen. The old converter (L2) printed the same line with columns 12 and 22 because it
re-aligns the output. unreal-asm keeps TASM's own layout, so the text encodes back to the same bytes.

## 2. The stream

| Part | Bytes | Notes |
|---|---|---|
| line record | `[n] [n body bytes] [n]` | the length is repeated after the body so the editor can scroll backwards; `n` ≤ 254 |
| end | `FF FF` | a length byte `FF` ends the source; both real files end with exactly `FF FF` at the catalog's length |
| after the end | — | in a hobeta file the rest of the last sector is left-over memory (kept by the container as its `tail`) |

Body bytes:

| Byte | TASM 3 | TASM 4 |
|---|---|---|
| `#0A n` | `n` blanks | (a plain control byte) |
| `#01 n` | (a plain control byte) | `n` blanks |
| `#20-#7E` | ASCII | ASCII |
| `#80-#F0` | a token (table in `tasmtokens.cpp`, 113 entries) | the same table, except `#97` `defmac `, `#9B` `display `, `#9F` `endmac ` |
| anything else | kept as U+F700 + byte | same |

A token that is an instruction or directive carries its trailing blank (`#B3` = `ld `, `#BF` = `org `). Register and
condition names do not (`#85` = `hl`, `#BD` = `nz`); `#83` is `af'` with its apostrophe. A single blank is a literal
`#20`, two or more are a run.

## 3. What the real files show (L5)

| File | Catalog | Lines | Runs (length × count) | Literal double blanks |
|---|---|---|---|---|
| `000LOAD.$A` | type `A`, start 40872, length 2187 | 171 | 3×25, 4×55, 5×100, 6×8, 8×133 | none |
| `CALLLOAD.$A` | type `A`, start 40872, length 2083 | 184 | 2×3, 3×43, 4×49, 5×97, 6×7, 8×147, 12×2 | none |

- Labels are in column 0 and stay as written (upper case in these files). Mnemonics and registers are tokens, so they
  show in lower case. Operands that are labels or numbers stay as written.
- Comments after `;` hold plain ASCII.
- Every multi-blank gap is a run, including the gap before a comment (`ld b,91` + run 12 + `; SECTORS`).

## 4. Canonical tokenizer (how an edited or foreign line is written)

A line keeps its original body while its text does not change, which makes the round trip exact. An edited line,
or a line decoded by another codec, goes through these rules:

1. Two or more blanks become run bytes (chunks of at most 255). A single blank stays literal.
2. The label field (text from column 0 up to the first blank) is literal.
3. From `;` on, the rest is literal except for the blank runs. So is everything inside `'...'` or `"..."`.
4. Elsewhere, a whole lower-case word that matches a token becomes that token. The word must not have a letter,
   digit, `_`, `.`, `@`, `$` or `#` before or after it. A token with a trailing blank is used only when a blank
   follows, and it absorbs that blank. `af'` is matched with its apostrophe.
5. A character TASM cannot hold is an error naming the line and the column. Characters outside ASCII are an example;
   the exception is U+F700 + byte, which is written back as that byte.

**Check:** take the decoded text of both real files, drop every kept body, and encode with these rules alone. The
result equals TASM's own bytes (test `CanonicalTokenizerReproducesTasmsOwnBytes`). The tokenizer therefore writes
what TASM 3.2 writes, at least for these files.

## 5. Detection

| Evidence | Score |
|---|---|
| catalog type `A` and start 39221 / 40872 (TASM 3) or 1-4096 (TASM 4, P1) | 95 for the matching version, 20 for the other |
| no catalog: framing holds to the end marker; more `#0A` than `#01` bytes in bodies means TASM 3, otherwise TASM 4 | 80 (≥ 3 lines) or 60 for the matching version, 40 below that for the other |
| framing broken anywhere | 0 |

## 6. Sub-version conversion

TASM 3 ↔ TASM 4 (`EncodeOptions::subversion`) goes through the text. The kept bodies belong to the other version, so every line is tokenized
canonically with the target's run byte and table. The three TASM 4-only directive names (`defmac`, `display`,
`endmac`) have no TASM 3 token and are written as plain text. Converting TASM 3 → TASM 4 → TASM 3 gives TASM 3's
original bytes (test `SubVersionConversionThroughTheText`).

## 7. Open items

| Item | Why open |
|---|---|
| A real TASM 4 file | the TASM 4 table and run byte come from L1 and P1; with a sample, add `testdata/tasm4/` and the same tests as TASM 3 |
| Emulator oracle | load a file converted by `zxasm` into TASM 3.2 (on `TASM3_2.SCL.trd`) and compare the screen. For TASM 3 the byte equality already proves it; for TASM 4 this needs the program |
| TASM 2.0 sources | stored in another format (TASM 3 imports them); queued as a `tasm2` codec |
| Blanks inside strings and comments | the files have no multi-blank gap inside a string; rule 1 encodes them as runs like every other gap (TASM's editor compresses on line entry) — to confirm on the emulator |
