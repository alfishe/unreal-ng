# unreal-asm: STORM → sjasmplus (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Code** | `src/dialects/storm/stormfrontend.cpp` (the `storm` frontend), the sjasmplus backend (comparisons that give 1, 16-bit unsigned words, `DS` patterns) |
| **Syntax source** | STORM 1.3's help (`STORMhlp`, an edition of the ZX Format #7 description) on the [STORM 1.3 disk](https://vtrd.in/system/STORM_13.ZIP), and STORM 1.3's own source (`STORM1_3.ZIP` from the KLUG BBS archive, [klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw)) |
| **Oracles** | STORM 1.3 itself: its own source converted and assembled by sjasmplus equals the released program in memory; two test programs assembled by STORM 1.3 in unreal-ng equal their conversions byte for byte |
| **Checks** | `unreal-asm-tests` (`StormFrontend_Test`, with `UNREAL_ASM_SJASMPLUS` the oracle programs too); `tools/unreal-asm/assemble-in-emulator.py storm13`, `tools/unreal-asm/lstcheck.py` |

## 1. Example first

```text
STORM 1.3                          sjasmplus
        LD HL,#C000,DE,#4000       LD HL,#C000 / LD DE,#4000     ; several operands: several instructions
        JR NZ,LOOP,EXIT            JR NZ,LOOP / JR EXIT
        OUT B,A,(#FE)              OUT (C),B / OUT (C),A / OUT (#FE),A
        LD HL,BC                   LD H,B / LD L,C                ; built-in macros
        EXA                        EX AF,AF'
.32     LDI                        DUP 32 / LDI / EDUP            ; ".n" repeats the line, .0 = 256 times
        LD DE,(MLINE')+6           LD DE,((MLINE<<8&#FFFF))+6     ; ' is a postfix "times 256"; (x)+6 is a value
        DW 1+2*3,2<3               DW 1+2*3,-(2<3)                ; priorities; a comparison gives 1, sjasmplus -1
        DS 7,#AA,#BB               DUP 3 / DB #AA,#BB / EDUP / DB #AA   ; 7 bytes of the pattern
        DB "@AEDFC825"             DB #AE,#DF,#C8,#25             ; a text of hex digit pairs
        ORG #5B00,#8B00            ORG #8B00 / DISP #5B00         ; runs at #5B00, is put at #8B00
```

## 2. STORM facts the conversion relies on

| Fact | Conversion |
|---|---|
| A line is `[label] statement : statement ... ;comment`; any number of statements | one IR statement each |
| An instruction takes any number of operands: `ADD A,A,A,B,HL,DE` is `ADD A,A` / `ADD A,B` / `ADD HL,DE`, `PUSH BC,DE` two pushes, `JR NZ,L1,L2` is `JR NZ,L1` / `JR L2`, `RET NZ,Z` two returns | grouped by what each instruction takes: `LD`, `EX`, `ADD`, `ADC`, `SBC`, `BIT`, `RES`, `SET` two operands; a condition and a target for `JP`, `JR`, `CALL`; one for the others. **Checked**: `RLC (IX+1),B` is `RLC (IX+1)` / `RLC B`, and `RES 0,(IX+1),B` is an error ("Operand expected"): no undocumented three-operand forms |
| `$` is the address of each virtual instruction, and of each `DB` / `DW` item (`DB ABC-$,LOOP-$` = two `DB`s; checked: the second item is one byte later) | a `DB` / `DW` list that uses `$` after its first item becomes one statement per item |
| Built-in macros (help): `IN D` = `IN D,(C)`, `IN (#FE)` = `IN A,(#FE)`, `OUT B` = `OUT (C),B`, `OUT (#FE)` = `OUT (#FE),A`, `LD HL,BC` = `LD H,B` / `LD L,C`, `LD BC,IX` = `LD B,HX` / `LD C,LX`, `EX HL,DE` = `EX DE,HL`, `ADD DE,HL` = `EX DE,HL` / `ADD HL,DE` / `EX DE,HL`, `EXA` and `EX AF,AF` = `EX AF,AF'`, `INF` = `IN F,(C)` | spelled out. **Checked**: `IN D,E,(#B7)` (in the help) and `IN (C)` / `OUT (C)` alone are errors in STORM 1.3 |
| `.n` in the label field assembles the line n times, `.0` 256 times | `DUP n` / `EDUP` around the line |
| Expressions on 16-bit words, carries ignored (`0-#20` = `#FFE0`) | `expressionBits = 16`, unsigned: division, remainder, `>>` and comparisons get `&#FFFF` operands |
| Priorities, high to low: postfix `[ ] ^ ` '` 8, `* / \` 7, postfix `~` 6, `<< >>` 5, `+ -` 4, `& ! \|` 3 (`!` is OR, `\|` XOR), `= < > <= >=` and postfix `@` 2; equal priorities left to right | parsed with these priorities; the backend writes sjasmplus parentheses where its priorities differ |
| Every unary operator is postfix and acts on what the operators of higher priority built: `[` high byte, `]` low byte, `^` round up to a multiple of 256 (`#AE18^` = `#AF00`), `` ` `` round down, `'` times 256, `~` negate (`2*3~` = -6), `@` logical not | `high`, `low`, `(x+#FF)&#FF00`, `x&#FF00`, `x<<8&#FFFF`, `-x`, `!x` |
| A comparison and `@` give 1 for true (chained: `1<2<3` = 1, `3>2>1` = 0, checked) | `Program::trueValue = 1`: the backend writes `-(a<b)` (sjasmplus gives -1), operands in STORM's own convention |
| An operand wholly in parentheses is memory; `0+(x)` and `(x)+6` are values (STORM's own source: `LD DE,(MLINE')+6` is the screen position line MLINE, column 6) | memory only when the `(` closes at the end |
| Numbers: `123`, `#AA`, `%101`, `"A"` a byte, `"AB"` a word with the first character high | numbers keep their spelling; `"AB"` its value |
| `DB "@AEDFC825"`: a text of hex digit pairs is those bytes; a one-character text is the character (the help lists `DB "A"` among the bugs; 1.3 builds it right, checked) | bytes |
| `DS n` n zeros; `DS n,a,b...` n bytes of the pattern repeated and cut (`DS 7,#AA,#BB` is 7 bytes; texts are patterns of their characters) | `DUP` of whole patterns and a `DB` of the rest; with a count known only when assembling (`DS N,1,2`), `IF (N)%2>=1` for the rest |
| `ORG run[,place]`: the code runs at `run` and is put at `place`; `$` is the run address | `ORG place` / `DISP run`; the next `ORG` ends the displacement (`ENT`), conditionally at a file's first `ORG` (an `INCL` may start inside a displaced `ORG`); a `$` in the addresses is kept before the `ENT` |
| `INCB` / `INCL` take several names, a text or a label's name; the 9th character is the TR-DOS type (default `C`); `INCL` does not nest | one `INCBIN` / `INCLUDE` each, `NAME.T` for another type. **Checked**: `INCB` moves the address by the file's length, the rest of its last sector is not copied (TASM's `INCBIN` copies it) |
| `_` before a command puts it in column 0 (40-character lines) | dropped |
| `REPT ENDR IF IFU IFNU IFD IFND ELSE EIF ENDM` are in 1.3's keyword table, announced for a later version (help: macros, conditional assembly) | kept as text with a warning |
| `PO PE P M` as the first operand imply **`JP`** (the codec, after this phase: research-storm.md §3) | `JP` |

## 3. Oracle results

| Source | Built by | Compared | Result |
|---|---|---|---|
| STORM 1.3's own source (`MAIN` with `ED`, `XED`, `DPC`, `ASM` and `INCB FONT`), 10 962 lines | STORM 1.2 / 1.3 (the released `STORM1.3`, unpacked in memory: `#6F19`-`#BFFF`) | 18 928 bytes with `lstcheck.py` | the code equal; 32 bytes differ: variables the program set while it started (cursor, colors, key state, buffers) and three `JP #FDF1` / `CALL #FD72` the published source has where the release jumps to `#FB15` / `#FA96` (the STS entry points of the published source differ from the release) |
| STORMT1 (49 lines: several operands, built-in macros, `.3`, `$` per item, `"@hex"`, priorities, postfix operators, comparisons, 16-bit words, `DS` patterns, `ORG run,place`, `INCB` of a 300-byte file) | STORM 1.3 in unreal-ng | 1 581 bytes from `#8000` | equal |
| STORMT2 (30 lines: `IN` / `OUT` forms, `LD` of register pairs, `SLI`, `INF`, `EX` forms, `RET` with several conditions, `DS` with a symbol count, chained comparisons, `~` and `@` priorities) | STORM 1.3 in unreal-ng | 112 bytes | equal |

Found on the way (fixed): the codec decoded the implied command of `PO PE P M` as `CALL`, as both reference decoders
do; STORM's binary has `JP` there (15 places in its own source, one with the author's comment `; JP`) and STORM's
editor shows `JP`. TR-DOS names with `/` (`SIN64/FF`) are written by `zxasm convert` as `SIN64_FF` (the host file
and the `INCBIN` name). A STORM file is detected only at its catalog start (`#C00B`, `#C003`) when the catalog is
known: zeros walk back as STORM lines, and a screen read as one.

STORM 1.3 has a bug of its own the conversion does not copy: `2*3~*2` gives -6 (the help: "expressions (AA<BB)*...
are buggy").

## 4. The corpus

The 42 STORM sources of the collection (research-storm.md §6: STORM's own, the Fighter demo, Global Commander GS,
Deja Vu #4, Adventurer #15, ZX Format #7): `roundtrip.py --assemble` converts 43 files with 0 round-trip differences;
39 of 42 main sources assemble alone, the others are a project part with its labels in another file and a source of
another assembler on the same disk whose `INCBIN` file is missing.

## 5. Open items

| Item | Note |
|---|---|
| `DW` values above 16 bits | sjasmplus warns and keeps the low 16 bits, as STORM does; the warning stays |
| STORM 1.0beta | no file found (research-storm.md §7); the frontend reads its lines like 1.3's |
