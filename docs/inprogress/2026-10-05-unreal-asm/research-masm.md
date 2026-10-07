# unreal-asm: MASM source format research (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Codec** | `masm`, versions `1.0` (the demo), `1.1` (1.1, 1.3), `2.0`, `3.0` (`core/src/3rdparty/unreal-asm/src/codecs/masm/`) |
| **Authors** | KSA Software (Stanislav Kuzin) and \*AIG\* (Ilya Aniskovets), Moscow, 1995-96 |
| **Checks** | `unreal-asm-tests` (`MasmCodec_Test`), testdata `masm/` |
| **Result** | MASM 1.1's own source (5219 lines) and files typed in the demo, 1.1, 2.0 and 3.0 in unreal-ng decode to the text the editors show and encode back byte for byte; the canonical tokenizer writes every stored line (5279 of 5279) |

## 1. Example first

The line `        LD      A,(HL)` saved by MASM 1.1 is 12 bytes:

```
0A | 0A 08 | AA | 0A 05 | 80 | 2C 28 | 91 | 29 | 0A
len  8 blanks LD+blank 5 blanks A    ",("  HL    ")"  len
```

The same line saved by MASM 2.0 / 3.0 is `07 AA 04 80 2C 28 91 29 00`: a byte `01`-`1F` is 2-32 blanks (`07` = 8),
the line ends in `00`. Keywords are bytes `#80`+, shown in capitals; they are stored only when typed in capitals.

## 2. Versions found

All by KSA Software (Stanislav Kuzin) and \*AIG\* (Ilya Aniskovets), Moscow. "MASM80" on the Sprinter is a different
program: Microsoft M80 3.44 ported by Vasil Ivanov, whose
sources are plain CR LF text: no tokenized format exists for it.

| Version | Where | Binary (SHA-256 of the CODE file) | Keyword table | Catalog start of a saved file |
|---|---|---|---|---|
| 1.0 demo (1995, "MASM128>", Save works) | Spectrofon #15, `masmdemo.C` 5180 bytes, packed | read from RAM after it unpacked: page 2 offset `#0EA6` (address `#8EA6`) | `#80`-`#F3` | 37178 (`#913A`); the catalog length does **not** count the end byte `FF` |
| 1.1 (29.8.1995) | [MASM_11.ZIP](https://vtrd.in/system/MASM_11.ZIP) (21142 bytes, `939b4827…24c1`): `masm 1.1.C` 11776 bytes `135e5b02…2c96` | file offset `#2C5` (address `#C2C5`) | `#80`-`#F6` | 38667 (`#970B`) |
| 1.1, "MASM512>" build | `MASM1_1.LZH` (KLUG BBS): `MASM1_1.$C` | file offset 763; same table as above | `#80`-`#F6` | not run |
| 1.3 (help: 25.3.96) | [MASM1_3.zip](https://vtrd.in/system/MASM1_3.zip) (13324, `759899ab…2183`): `masm_1.3.C` 11776 `983f60b5…2c72` (5 bytes differ from 1.1: the version text) | `#2C5` | `#80`-`#F6` | as 1.1 |
| 2.0 TURBO (19.04.96) | [MASM2_0D.zip](https://vtrd.in/system/MASM2_0D.zip) (5543, `b378f003…2cf9`): `m2.C` 5473 `1b230104…896f`, packed | RAM page 2 offset `#06D9` (address `#86D9`, `LD HL,#86D9` in the lookup) | `#80`-`#FB` | 38106 (`#94DA`) |
| 3.0 MACRO (08.05.96) | [MASM30M.zip](https://vtrd.in/system/MASM30M.zip) (18929, `ee9b883f…1fbb`): `MMM1.C` 16384 `4af60a76…e005` | file offset 7193 (address `#DC19`) | `#80`-`#FE` | 37155 (`#9123`) |

P1's start values 38667 (= 1.1 / 1.3) and
38821 (`#97A5`, not seen) both have the high byte `#97`; the MASM 1.1 sources here carry 38662 / 38663 / 38701, so
the low byte varies (probably the text address of the build that saved them); `#97xx` is the 1.x signature.

## 3. File layout

**1.0 demo, 1.1, 1.3** (TASM 3's framing, which MASM imports, with one difference):

| Part | Bytes |
|---|---|
| line | `[n] [n body bytes] [n]` (the length repeated for scrolling back), n ≤ 64 characters of text |
| empty line | **one** `00` (TASM writes `00 00`) |
| end | `FF`; in a hobeta file the rest of the last sector is left-over memory (kept as the tail) |

No header. The type is `a`.

**2.0**: `FF`, then lines `[body] 00`, then `FF`. **3.0**: `FF lo hi FF`, then as 2.0; `lo hi` is the line the cursor
was on when the file was saved (saved at line 8, then again after moving up to line 5: `08 00`, `05 00`).

## 4. Keyword tables (code → keyword; a trailing blank is part of the keyword)

The table in the binaries is a list of words, each followed by `04` (no operand) or `08` (operand follows); the
list in MASM's own source (`M1+`, label `TAB_OPR`, line 398) is the same table. Common to all versions:

```
80 A    81 B    82 C    83 D    84 E    85 H    86 L    87 I    88 R    89 XH   8A XL   8B YH   8C YL   8D IX   8E IY   8F AF'
90 AF   91 HL   92 DE   93 BC   94 M    95 NC   96 NV   97 NZ   98 P    99 PE   9A PO   9B V    9C Z    9D SP   9E (below) 9F ORG_
A0 PHASE_ A1 UNPHASE A2 AND_ A3 ADC_ A4 SBC_ A5 ADD_ A6 SUB_ A7 XOR_ A8 OR_ A9 CP_ AA LD_ AB IM_ AC RST_ AD EI AE DI AF EXX
B0 EXA  B1 INF  B2 LDIR B3 LDDR B4 OTIR B5 OTDR B6 OUTI B7 OUTD B8 RETI B9 RETN BA INIR BB INDR BC CPIR BD CPDR BE NEG BF CPD
C0 CPI  C1 IND  C2 INI  C3 LDD  C4 LDI  C5 CCF  C6 CPL  C7 DAA  C8 HALT C9 NOP  CA RLA  CB RLCA CC RRA  CD RRCA CE SCF  CF RLD
D0 RRD  D1 EX_  D2 RET  D3 CALL_ D4 JP_ D5 PUSH_ D6 POP_ D7 INC_ D8 DEC_ D9 OUT_ DA IN_ DB DJNZ_ DC JR_ DD BIT_ DE RLC_ DF RRC_
E0 RL_  E1 RR_  E2 SLA_ E3 SRA_ E4 SLI_ E5 SRL_ E6 RES_ E7 SET_ E8 EQU_ E9 (below) EA (below) EB INCBIN_ EC INCLUDE_ ED DB_ EE DEFB_ EF DEFS_
F0 DEFW_ F1 DS_ F2 DW_
```

(`_` = the keyword's trailing blank.) Per version:

| Code | 1.0 demo | 1.1 / 1.3 | 2.0 | 3.0 |
|---|---|---|---|---|
| `9E` | `{$ ` | `{ ` | `*` | `{ ` |
| `E9` / `EA` | `BEGIN` / `END ` | `BEGIN ` / `END` | as 1.1 | as 1.1 |
| `F3` | `*AIG*'95 ` (last) | `DOWN ` | `DOWN ` | `DOWN ` |
| `F4`-`F6` | — | `UP ` `SYSTEM` `STOPKEY` | same | same |
| `F7`-`FB` | — | — | `MAC ` `ENDM` `IF ` `ELSE` `ENDIF` | `MAC` `ENDM` `BANK ` `BORDER ` `CLS` |
| `FC`-`FE` | — | — | — | `IF ` `ELSE` `ENDIF` |

3.0's `MAC` has no blank (its table entry is `MAC` + `08`; typed `mac 2` is stored `F7 20 32`). `{ ` / `*` / `{$ `
at `9E` is not used in any source.

## 5. Text inside a line

- **Characters**: `#20`-`#7F` as typed. `#60`-`#7F` are lower-case Latin in LAT mode; MASM's `F` command shows them as
  Russian capitals instead (help, section 5), the file cannot tell which. Lines hold at most 64 characters.
- **Case**: keywords are tokenized **only in capitals**. MASM starts with CAPS LOCK on: typed `ld a,(hl)` becomes
  `LD A,(HL)` (tokens); `LD A,B` typed with shift becomes `ld a,b`, stored as plain text and shown in lower case.
- **Blanks**: one blank is `20`; 2 or more are `0A n` (1.x) or one byte `n-1` (2.0, 3.0). A keyword with a trailing
  blank takes the first blank after it (`LD  A` = `AA 20 80`, `LD   A` = `AA 0A 02 80`). Trailing blanks are dropped.
  The editor stores blanks as typed (no automatic columns): `X  Y   Z` = `58 0A 02 59 0A 03 9C`.
- **Numbers, labels, comments, strings**: plain text; keywords are tokenized everywhere, comments included
  (`;LD A,B` = `3B AA 80 2C 81`), with these word rules (MASM 1.1, every printable character tried, file
  `typed-masm11__t1`):
  - the character **before** a keyword must not be a letter, digit, `_ . ? @ # "` (so `"LD A"` = `22 4C 44 20 80 22`:
    `LD` after the quote stays text, `A` after the blank is a token; `#A`, `.A`, `_A`, `1A` stay text);
  - the character **after** a keyword without its own blank must not be a letter, digit, `_ . ? @`
    (`A"`, `A#`, `A)`, `A,`, `A:` are tokens; `A.`, `A?`, `A@`, `A_`, `A1` text);
  - a keyword with a trailing blank needs a blank after it (`LD,` and `EQU` at the line end stay text);
  - the longest keyword wins (`AF'` before `AF`, `AF''` = `8F 27`).
- **1.0 demo**: no keywords between double quotes (`"LD A"` = text only). One typed file shows it.
- **2.0**: greedy, without word rules, everywhere except the label field (the first word of a line that starts at
  column 0): `;COMMENT` = `3B 82 4F 94 94 84 4E 54` (C, M, M, E are register tokens), `"LD A"` = `22 AA 80 22`.
  3.0 went back to the 1.x word rules (`COMMENT`, `ABC CBE`, `LDIRX` stay text).

The codec's `MasmCodec::EncodeBody` implements these rules per version.

## 6. Display

The 1.x editor uses a 64-column font (two characters per cell), 22 text lines, status line
`LINE:hhhh COL:hh LAT TXT:used/free BUFER:… FILE:name`; keywords are shown as in the table, everything else as stored.
2.0 / 3.0 use a small-capitals font (lower case looks like small capitals). The decoded text equals the screen:

The screens of 1.1 with its own source, of the boundary test lines and of the typed 2.0 / 3.0 / demo files were
compared with the decoded text.

## 7. Corpus and results

| File | Version | Lines | Origin |
|---|---|---|---|
| `MASM_SRC__LS2.$a`, `__M1.$a`, `__M2.$a` | 1.1 | 2009, 1493, 1717 | MASM 1.1's own source (`MASM_SRC.RAR`, KLUG BBS archive), byte copies |
| `typed-masm11__t1.$a` | 1.1 | 34 | typed in MASM 1.1 in unreal-ng, saved with SS+Enter |
| `typed-masmdemo__DM.$a` | 1.0 demo | 8 | typed in the demo, saved with `S` |
| `typed-masm20__NONAME.$a` | 2.0 | 9 | typed in 2.0 (some lines carry key-repeat garbage: `ZIM 3`, `IM 7`) |
| `typed-masm30__T3.$a` | 3.0 | 9 | typed in 3.0 |

A scan of every TRD / SCL / hobeta file at hand (archives opened up to three levels) found no
other MASM source: the scan found the 1.x framing only in `MASM_SRC`, and no 2.0 / 3.0 framing.

The codec on testdata `masm/`: **7 files, all byte-exact; 5279 lines, the canonical tokenizer reproduces all 5279
(100 %)**, the 5219 lines of the real sources included.

## 8. Version detection

1. 3.0: `FF lo hi FF` and the rest parses to `FF`. 2.0: starts with `FF` and parses (a 3.0 file also reads as 2.0;
   3.0 wins).
2. 1.x framing (`[n] … [n]`, `00`, `FF`): MASM 1.1 / 1.3, or the 1.0 demo when the catalog start is 37178. A file
   with a token above the version's last code (`F4`+ excludes the demo, `F7`+ excludes 1.x) drops that version.
3. The catalog: type `a`; start `#97xx` = 1.1 / 1.3, 37178 = demo, 38106 = 2.0, 37155 = 3.0. TASM 3 sources share
   the 1.x framing but are type `A` with start 39221 / 40872 and use `00 00` for empty lines; bytes alone can be
   ambiguous (a TASM file with tokens `#80`-`#E6` frames as MASM): use the catalog type.

## 9. Open questions

- 38821 (P1) and the 1.1 sources' 38662 / 38663 / 38701: the meaning of the low byte is not settled (a build's text
  address is the guess); no file with 38821 was found.
- The 1.0 demo string rule and 2.0's greedy rule rest on one typed file each; 2.0 / 3.0 have no real sources.
- Characters MASM 1.1 could not be made to type (`[ \ ] { | } ~`) are assumed to be separators.
- The "MASM512" build in `MASM1_1.LZH` was not run (same table).
