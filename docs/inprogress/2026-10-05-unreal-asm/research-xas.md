# unreal-asm: XAS source format research (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Codec** | `xas`, versions `4.18`, `5.05` (5.05, 5.05SE), `7.43` (7.43, 7.447), `7.43c` (64 columns), `9.07m`, `9.10` (`core/src/3rdparty/unreal-asm/src/codecs/xas/`) |
| **Authors** | Max Petrov (hpm), later versions with Creator (Pavel Sokolov), STS and Mythos, 1996-1997 |
| **Checks** | `unreal-asm-tests` (`XasCodec_Test`), testdata `xas/` |
| **Result** | all 18 XAS sources found (demos, magazines, ZX Navigator, the XAS disks, one typed into XAS 7.447 in unreal-ng) decode to the editor's layout and encode back byte for byte with their sector slack; XAS's packer writes 4794 of 4913 lines from the text (the other 119 were written by the XASCII converter in lower case); the editors' screens (OCR) equal the decoded rows |

## 1. Example first

The XAS 7.447 line typed as `loop ld a,(ix+5)` is stored as 10 bytes plus its end byte:

```
4C 4F 4F 50 | AC | DF | 28 D3 2B 35 29 | 0D
"LOOP"        LD   A    "(" IX "+5)"     end of line
```

and the editor shows it as

```
loop    LD    A,(IX+5)
```

- Keywords and register names are single bytes `#80-#F6` (the table in §4); everything else is plain text.
- No blanks are stored. The screen puts the command at column 8 and the operands at column 14, and draws the
  commas between operands itself.
- Text outside strings and comments is stored in capitals (the editor upper-cases it) and shown in lower case.

## 2. Versions found

| Version | Where | Program file (size, SHA-256 of the file data) | Archive (size, SHA-256) |
|---|---|---|---|
| 4.18 | [vtrd.in XAS418.zip](https://vtrd.in/system/XAS418.zip) → `XAS418.SCL` | `XASo.B` 12032, `f40a6b4299b934d3c982d5a42767921b7ac8cac28799a37d5e7f1180bda54c99` | 18153, `f6181466c4a721f0186253ab2acb1050d94316bacec9156efa066daf78949250` |
| 5.05 | KLUG BBS `XAS505.$Z` (ZXZIP; unpacked with ZXUNZIP 1.0a under dosbox-x) | `XAS505.$B` 14353 (hobeta), `b94d68b9141361fa628fb4e6f831b38905f28f940d08174ae786428b88e683ff` | 35089, `ffd4cdba5ae5c1b8e45de94854701d49504be53f430164924843da6e09f27e08` |
| 5.05SE | [vtrd.in XAS505SE.zip](https://vtrd.in/system/XAS505SE.zip) | `XAS505SE.B` 14336, `929aab99637117b84acecb4e413492f1024f0baadc92dda55542673d06711bd0` | 29294, `5c07723f56c6f1d477bb3204fb0f342e9bfa8eda28b8dac856d1e7bc04d1dddc` |
| 7.43 | KLUG BBS `XAS7_43C.ZIP` | `XAS_7_43.$B` 15633 (hobeta), `a4a47f890d39e69d2188f94ad3a0e0b9965a371fe19e2ce2a2eb58f44d94c186` | 57393, `d0a83ffbf58f18d15bc24c53a7b387b2eff95b36a8f3c857352f82ce20477337` |
| 7.43c (64 columns) | same archive | `XAS7_43C.$B` 15121 (hobeta), `b04220425ab19ef66739886d5a4dc20b8cc39db650e9f545ce3edd21d767edac` | (same) |
| 7.447 | [vtrd.in XAS7_447.ZIP](https://vtrd.in/system/XAS7_447.ZIP) | `XAS7.447.B` 16128, `0370cfc1ee09ef44fc09d3bd6adb9ea04c8063d1ae9bbf5ddfb335c6dcb55e6c` | 24257, `21f10793d7cb386b927f2b775ac69b04198369713b52f5ec1b35172781704b3a` |
| 9.07m (Mythos recompile, 64 columns) | [vtrd.in XAS907M.zip](https://vtrd.in/system/XAS907M.zip) | `XAS9.07m.B` 13824, `d3c427a1aa8beb92faa51e88f3e38f60464dce5670ced03be02b34790b0f2560` | 23638, `3a12901b352dcb925479401cd6ab5f2312092b548361a16f15b68b5a977ea4a1` |
| 9.10 (STS "64sm", 64 columns) | [vtrd.in XAS9_10.zip](https://vtrd.in/system/XAS9_10.zip) | `XAS9.10.B` 16640, `737493e1e3e839af7d201fde3a21daa2a3f03b6299a53198d5a095ed6e638325` | 44634, `205bdd94c9272fcfad532e27dbb90f7cf45b82b664735704254daedffd4a410d` |

The program files are packed; the tables and routines below were read from RAM after each program had started
(the 64 KB address space). Addresses are Z80 addresses in that state.

| Version | Token table | Row formatter | Line packer | Title of a new text |
|---|---|---|---|---|
| 4.18 | #8697 | #8A4C | #87E8 | `XAS by Max Petrov (HPM) 3.091` |
| 5.05 / 5.05SE | #869A | #8A60 | #87EB | `XAS by Max Petrov (HPM) 5.05 ` |
| 7.43 | #8722 | #8AFC | #887E | `by Max Petrov & Creator v7.40` |
| 7.43c | #8722 | #8AFC (65 columns) | #887E | `by Max Petrov & Creator v7.43` |
| 7.447 | #8748 | #8B22 (row copy #86D8, token printer #8BEE) | #889A (implied command #9A80) | `by Max Petrov & Creator v7.44` |
| 9.07m | #9355 | — | #87EC | `XAS 9.07 ReCompiled by Mythos` |
| 9.10 | #92F2 | #8A5C | #87F2 | `XAS by Max Petrov,64sm by STS` |

Related tools on vtrd.in: ALL2XAS5, ALSM2XAS, T2XASC20 (TXT→XAS), XASCII (ASCII⇄XAS
converter by SMT, with its own XAS source), XASTC101 (XAS→TXT), ZASMXASH (ZASM→XAS). Not analyzed beyond XASCII.

## 3. The file

Catalog: type `X`, start #5341 (the bytes `AS`) for a source; XMACROS (the macro file of 4.x / 5.x) has start
#5361 (`aS`) and the 4.x / 5.x file list marks it `M`. The length field is not used by XAS (0 in most files;
1260 in Read Me, 8018 in one ZXNAV file); the sector count covers the text.

The text is loaded as it is at #C000 and edited in place there.

| Offset | Size | Meaning |
|---|---|---|
| 0 | 29 | title, blank-padded (a new text gets the program's template, table above) |
| 29 | 2 | address of the cursor line when the file was saved (#C023 = the first line) |
| 31 | 4 | editor state (cursor column / row and flags; bit 7 of byte 34 = "text changed", cleared on save). Kept as it is |
| 35 | 1 | #01: the start sentinel (the editor walks lines backwards until it) |
| 36 | ... | lines |
| end | 1 | #00: end of text. Bytes after it are whatever filled the last sector (kept by the codec) |

A line is its bytes followed by one end byte:

| End byte | Line |
|---|---|
| #0D | normal |
| #0C | marked red |
| #09 | marked green |
| #01-#08, #0A, #0B | also end a line (shown unmarked); never written by XAS |

The two marks are the editor's line markers. The screen colors row *n* after the *n*-th line, so the colors slip
when a line takes two rows (an XAS display bug).

## 4. Token table

Same codes in every version; names stored in RAM as bit-7-terminated strings (4.x-7.x) or 5-byte blank-padded
entries (9.x).

`80` LDIR · `81` LDDR · `82` LDI · `83` LDD · `84` CPIR · `85` CPDR · `86` CPI · `87` CPD
`88` INIR · `89` INDR · `8A` INI · `8B` IND · `8C` OUTI · `8D` OTIR · `8E` OUTD · `8F` OTDR
`90` RETI · `91` RETN · `92` NEG · `93` RLD · `94` RRD · `95` PUSH · `96` POP · `97` ADD
`98` SUB · `99` ADC · `9A` SBC · `9B` AND · `9C` OR · `9D` XOR · `9E` CP · `9F` INC
`A0` DEC · `A1` BIT · `A2` RES · `A3` SET · `A4` RLC · `A5` RRC · `A6` RL · `A7` RR
`A8` SLA · `A9` SRA · `AA` SLI · `AB` SRL · `AC` LD · `AD` EX · `AE` IN · `AF` OUT
`B0` IM · `B1` RST · `B2` DJNZ · `B3` JP · `B4` JR · `B5` CALL · `B6` RET · `B7` EXX
`B8` CPL · `B9` DAA · `BA` RLCA · `BB` RRCA · `BC` RLA · `BD` RRA · `BE` NOP · `BF` HALT
`C0` DI · `C1` EI · `C2` SCF · `C3` CCF · `C4` ORG · `C5` ENT · `C6` EQU · `C7` WORK
`D0` BC · `D1` DE · `D2` HL · `D3` IX · `D4` IY · `D5` SP · `D6` AF · `D7` (C)
`D8` B · `D9` C · `DA` D · `DB` E · `DC` H · `DD` L · `DE` (HL) · `DF` A
`E0` (BC) · `E1` (DE) · `E2` HX · `E3` LX · `E4` HY · `E5` LY · `E6` I · `E7` R
`E8` NZ · `E9` Z · `EA` NC · `EB` PO · `EC` PE · `ED` P · `EE` M · `F1` (SP) · `F2` AF'

| Code | 4.18, 5.05 | 7.43, 7.43c, 7.447 | 9.10 | 9.07m |
|---|---|---|---|---|
| `C8` | DEFB | DB | DB | DB |
| `C9` | DEFW | DW | DW | DW |
| `CA` | DEFM | DM | DM | DM |
| `CB` | DEFS | DS | DS | DS |
| `CC` | !ASSM | !ASSM | !ASSM | .ASM |
| `CD` | !CONT | !CONT | !CONT | .END |
| `CE` | LOADTEXT | LTEXT | LTEXT | LTXT |
| `CF` | LOADCODE | LCODE | LCODE | LCOD |
| `EF` | !ON | !ON | !ON | .ON |
| `F0` | !OFF | !OFF | !OFF | .OFF |
| `F3` | — | USEL | USEL | USEL |
| `F4` | — | IFNZ | — | — |
| `F5` | — | IFZ | — | — |
| `F6` | — | MAKE | — | — |

`#80-#CF` and `#F3-#F6` are commands, `#D0-#F2` registers / conditions / switches.

## 5. What a stored line contains (the packer)

The editor edits the screen row (42 characters; 64 in 7.43c / 9.x) and packs it when Enter is pressed
(7.447 #889A). The packer is the canonical encoder (`XasCodec::EncodeBody`):

1. Blanks and commas between items are skipped and not stored.
2. A keyword is recognized (case-insensitive, first match in table order) only when followed by a blank, a comma,
   a `;` or the line end. `LDIR` is not `LD`+`IR`; `NOP:NOP` is text.
3. Label field: text up to a blank. A second text item before the command is refused.
4. Command: a command token. A register in command position is refused by 4.x / 5.x; 7.x rewrites the edit line
   first (`HL,1` becomes `LD HL,1`, #9A80); 9.x inserts the command byte (LD / OUT / JP, #8780).
5. Operands: register tokens and text items. Two text items in a row are refused (they would run together), so
   `LD (LAB),5` cannot be entered; `(IX+d)` / `(IY+d)` is stored as `(` + `IX`/`IY` token + text and does not
   count, so `LD (IX-2),5` gives `AC 28 D3 2D 32 29 35`.
6. Commas are stored only in the operand lists of `DB DW DS` (and `PUSH POP` from 5.05): `DB 1,2,3` keeps its
   commas, `PUSH BC,DE,HL` is `95 D0 D1 D2`. 7.x drops a comma followed by a blank or comma, which leaves two
   text items in a row, so `DB 1, 2` is refused by 7.x.
7. Text items: copied up to a blank, comma, `;` or the end; `a-z` become `A-Z`, `.` becomes `#` (the
   README's "`.` = `#`").
8. Strings: `"` up to the next `"`, at least one character (`DB """` is the string `"`). From 5.05 a letter or
   digit right after the closing quote continues the string (`"AB"CD"`). String bytes keep their case.
9. Comment: from `;` to the end as typed; trailing blanks removed. Blanks before `;` are not stored, so a comment
   always follows the last item directly (`EQU 1; Define`) or starts the line.

21 test lines typed into XAS 7.447 gave the stored bytes the packer computes: every accepted line matched; the
three lines XAS refused (`db 1, 2,,3`, `a1 a2 nop`, `ld (lab),5`) are the ones the packer refuses. (Strings and comments came out in capitals because the emulated keyboard types capitals.)

The older packer copied in XASCII's source (labels `LL87F3`-`LL89AB`, an XAS version before 4.18) kept the case,
took `:` as a statement separator (stored as #0E) and had no "two text items" check. No found file uses #0E.

## 6. What the editor shows (the row formatter)

XAS 7.447 #8B22, the same algorithm in every version, with these parameters:

| Version | Bytes per row | Tab rule | Lower case |
|---|---|---|---|
| 4.18, 5.05 | 43, one row per line | label < 8 → command at 8, else at 14 / one blank; operands at 14 | yes |
| 7.43, 7.447 | 43, a longer line continues on the next row | as 4.x | yes |
| 7.43c | 65 | label < 8 → command at 15, else 23; operands at 23 | yes |
| 9.07m, 9.10 | 65 | label < 14 → 14, else 22; operands at 22 | no |

Per row (state: field counter 3, lower-case mask on):

- A byte below #0E ends the row.
- `;` copies the rest of the row as it is (no case change, tokens shown as raw font glyphs). It ends a string too:
  a `;` inside quotes starts the comment on screen (the stored string is intact).
- At the start of each item (a token, or a text run after a token) the counter goes down: 3→2 pads to the first
  tab stop, 2→1 pads to the second (at least one blank), then each further item boundary draws a `,`.
- A token is printed by name. After `DB` (#C8) letters keep their stored case.
- `(` followed by a token prints `(`, the token, then copies up to `)` as it is. A `,` is drawn before the `(`
  when the byte before it is not a token.
- Text: `A-Z` shown as `a-z`, except between quotes; the quote toggles only at the start of an item.
- The row is cut at the byte limit; 7.x shows the next 43 bytes of a long line on the next row, starting a new
  label field (a line of exactly 43 bytes gets an empty second row); 4.x shows only one row (a line over 44
  bytes shows junk at its end).
- Column 42 (7.x) is behind the frame; the screen shows columns 0-41.

The font has Russian letters on the codes that do not look like Latin ones: #10 Д, #11 Ж, #12 И, #13 Й, #14 Л,
#15 П, #16 У, #17 Ф, #18 Ц, #19 Ч, #1A Ы, #1B Ь, #1C Э, #1D Ю, #1E Я, #1F Ъ, #7B Ш, #7C Щ, #7D Б, #7E Г; #7F ©,
#60 £, #5E ↑. Russian А В Е К М Н О Р С Т Х З are typed as the Latin letters / `3`. On the 6-pixel font Y = У and
Ш = Щ look the same, on the 4-pixel font also И = W and Ь = b.

Unknown bytes: a token past the table end prints whatever follows the table (7.447: #F7 nothing, #F8 `!y`,
#FA `m`...; 9.x #F4: two #00 and `E`); the codec shows it as the private-use character U+F700 + byte. #0E and #0F are glyphs (a checker block and a
Г-like sign). Bytes #80+ in a comment or inside a `(IX` copy are raw font glyphs (U+F700 + byte in the text). #00 inside the text ends
it for the screen.

## 7. The decoded form

One text line per stored line: the line as the editor lays it out (§6), lower case included, without the row cut.
Encoding packs each line with XAS's packer, so a decoded line is exactly what XAS shows, and typing it into XAS gives
the same bytes. What the text cannot say is kept beside it:

| Kept in | What |
|---|---|
| a line's attributes | its end byte when it is not #0D (the red / green mark), and the stored bytes when the packer would not write them for the text (XASCII's lower-case labels) |
| the document's attributes | the header (title, cursor line, editor state, 35 or 36 bytes), bytes after the last line end, the #00 and the sector slack |

XAS ignores the catalog length (0 in most files), so the codec reads the file's whole sectors: containers hand the
bytes after the length to the codecs as `CatalogHints::slack`, and the hobeta / TRD readers accept a length field
larger than the file (XAS left 8018 on a 15-sector file). A new text gets the version's title template, the cursor on
the first line (#C023) and a clear editor state.

## 8. Detection

- Catalog type `X`, start #5341 (`AS`), or #5361 (`aS`) for a macro file.
- Content: byte 35 = #01; up to the first #00 no byte below #0E other than #09 / #0C / #0D; the last byte before
  the #00 is one of those three.
- Version: the title, if it is still a template (§2); otherwise a text with #F3-#F6 is 7.x (#F4-#F6 exist only in
  7.x). The codes cannot tell 4.x, 5.x and 7.x apart otherwise; only the names of #C8-#CF differ.

One file in the corpus (`P#14VRZZ__KERNEL.$X`, from a Plutonium #14 ZXZIP archive) lacks its first byte: the
title is 28 bytes and the sentinel sits at offset 34. The codec accepts that (a 35-byte header).

## 9. Corpus and results

Search: every TRD / SCL / FDI image, hobeta file, ZIP / RAR / 7z / LZH archive (nested) and every ZXZIP `.$Z`
archive (40, unpacked with ZXUNZIP under dosbox-x) at hand: files of type `X`/`x` or with start `AS` / `aS`. TD0 (58)
and UDI (3) images were not read. Testdata `xas/` holds every file found (`*.txt` = the expected decoded text):

| File | Source | Lines | Packer reproduces |
|---|---|--:|--:|
| `CCINTROS__KISHKOID`, `CCINTROS__matrix` | Chaos Constructions 2000 intros disk (`CCINTROS.ZIP`) | 321, 333 | all |
| `OBERON#5__mandelbr` | Oberon #5 (`OBERON#5.ZIP`) | 313 | 313 |
| `P#14VRZZ__KERNEL`, `P#14VRZZ__OOPSfix` | Plutonium #14 (`P#14VRZZ.$Z`, ZXZIP) | 296, 434 | all |
| `SNG_2APP__DAINGY` | SNG #2 appendix disk | 359 | 359 |
| `XAS505SE__XMACROS` | [XAS 5.05SE disk](https://vtrd.in/system/XAS505SE.zip) | 87 | 87 |
| `XAS7_43C__Read_Me` | XAS 7.43 archive (`XAS7_43C.ZIP`) | 182 | 182 |
| `XAS7_447__Read_Me` | [XAS 7.447 disk](https://vtrd.in/system/XAS7_447.ZIP); bytes 3328-3583 (its 14th sector) hold an earlier piece of the text, a damaged copy of the 7.43 file | 182 | 182 |
| `XASCII__XASCII` | [XASCII 1.1 disk](https://vtrd.in/system/XASCII.zip) (made by the converter, lower case) | 1291 | 1172 |
| `ZXNAV1_3__KERNEL`, `PT3daem`, `SCT.src`, `SET.SRC` | ZX Navigator 1.3 (`ZXNAV1_3.ZIP`) | 127, 60, 377, 275 | all |
| `ZXN__KERNEL` | `ZXN.ZIP` | 114 | 114 |
| `Zxf_04Ap__XMACROS`, `Xas_help` | ZX Format #4 appendix (= the XAS 4.18 / 5.05 files) | 52, 79 | all |
| `typed-in-xas7447__noname` | **typed into XAS 7.447 in unreal-ng and saved** | 31 | 31 |

Archives without a link are from the KLUG BBS archive ([klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw)). Duplicates
not kept: XMACROS and Xas help on the XAS 4.18, 5.05 and 5.05SE disks (= the ZX Format #4 copies).

- decode → encode byte-exact: **18 of 18 files** (header and sector slack included).
- lines the packer writes exactly from their text: **4794 of 4913 (97.6 %)**; the 119 others are all XASCII lines
  the converter stored with lower-case labels or operands (the editor would store capitals). `zxasm check` also
  counts the red / green marked lines as kept (their mark is not in the text).
- screen check (OCR of the emulator screen against the decoded rows cut at the row width, every 19-line window):
  XAS 7.447: 3373 rows of the 11 7.x files, 0 differences; XAS 4.18: 1544 rows of the 7 4.x files, 0;
  XAS 9.10: all 18 files, 4913 rows, 1 difference (XASCII's `IFNZ`, a 7.x-only token: 9.10 prints table junk);
  XAS 7.43c: all files, no difference in rows 1-16 (rows 17-19 are covered by its status lines).
- None of 470 TR-DOS images of other software has a file the codec takes for XAS.

## 10. Open questions

- The 7.x implied-command rewrite (#9A80) and the 9.x one (#8780) are only partly read; the packer refuses a
  register in command position instead of rewriting it.
- The meaning of the four editor-state bytes at offset 31 and of the TR-DOS length field.
- 4.x display of lines longer than 44 bytes (junk from the row buffer) is not modeled.
- 9.07m was not checked on screen (its tables and packer match 9.10 apart from the dot words).
- The XAS version whose packer XASCII copied (`:` → #0E) was not found.
- TD0 and UDI images were not searched.
