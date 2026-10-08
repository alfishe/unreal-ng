# unreal-asm: ASM80 → sjasmplus (ASM80 codec and frontend)

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Status** | Done: `src/codecs/asm80/asm80codec.{h,cpp}`, `src/dialects/asm80/asm80frontend.{h,cpp}`, tests `tests/asm80frontend_test.cpp`, test data `testdata/asm80/`, `testdata/dialects/asm80/` |
| **Assembler** | ASM80 / Asm80Win 2.02 ("Assembler 512 for Z80", Vyacheslav Mednonogov, Copper Feet, 1995-1999): a PC cross assembler for the Spectrum (games "UFO-2", "Black Raven"); the "Asm80Win 2.02 full pack" `ASM80CPP.RAR` of the KLUG BBS archive ([klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw)) holds asm80win.exe, the DOS ASM80.EXE, both programs' C++ sources, the manual `Asm80.txt` and its tests `TEST.A80` / `TEST2.A80` |
| **Sources of the rules** | the manual and the program's own source (`ASM80WIN/Asm80win.cpp`: `value()`, `label()`, `decode_star()`, the handlers `_t*` / `_def*`), each rule checked against asm80win.exe |
| **Oracle** | asm80win.exe under wine (a CrossOver bottle of its own) and a host build of `Asm80win.cpp` (`tools/verification/unreal-asm/oracles/build-asm80.py`), which gives the same bytes |
| **Result** | TEST.A80 (every instruction form, 1485 bytes), TEST2.A80 (mixed case), a constructs probe and a pages probe assemble with sjasmplus to the bytes asm80win.exe built; the AEDIT editor project (Sprinter CD, `sources/edit_sor`: four files with `*F` and `*B`) to the `ed.exe` its authors shipped, through sjasmplus, pasmo and z80asm alike; its 207 labels through `SymbolsFromProject` equal the label table of asm80win.exe's listing |
| **Note** | this is the "M80-style text dialect" of the TODO (`*D-`, `*Z80` option lines, `$` in names): ASM80's keys, not Microsoft M80 |

## 1. Example first

```text
ASM80                                   sjasmplus (converted)
Fill$   MAC                                     MACRO L_Fill_ _m0,_m1
        LD BC,=0*16                             LD BC,_m0*16
        LD HL,=1                                LD HL,_m1
        ENDM                                    ENDM
        Fill$ 1,$                       __ASM80_AT_x_5 L_Fill_ 1,__ASM80_AT_x_5
        DEFW -2*3/2                             DW ((-2*3)&#FFFF)/2
        IF N-10                                 IF ((N-10)&#FFFF)==0
        sub hl,de                               SUB 0
*F c:\speccy\aedit\window.a80                   INCLUDE "window.asm"
```

asm80win.exe passes `$` as the address of the call line (`LD HL,#6040` at `#6043`), builds `FD 7F` for `-2*3/2`
(the product is cut to 16 bits: 65530 / 2), takes the `IF` block when the expression is **0**, and assembles
`sub hl,de` to `D6 00`: SUB reads one operand and takes `hl` as an undefined label, whose expression is 0.

## 2. The language, as asm80win.exe assembles it

| Rule | Where in the source | Conversion |
|---|---|---|
| Fields separated by blanks / tabs; the label from column 0, no `:` after it (`decode()` wants a blank or `;` after a word) | `Pass1` | label as written |
| A label starts with `A`-`z` in ASCII order (letters and `[ \ ] ^ _` `` ` ``), then letters, digits, `$ _ #`; **16** characters count (2.01+); case counts | `label()`, `LAB_LEN`, `SYMBOL` | every spelling of a name becomes its defining spelling (`VeryLongLabelName01` / `…NameXYZ`: one label, checked: defining both is error 8); `$` / `#` names renamed for sjasmplus (`N$1` → `L_N_1`) |
| Mnemonics, registers, conditions in any case | `decode()` (`toupper`) | standard spelling |
| A command wins over a macro of the same name; any other word in the command field calls a macro | `Pass1` (`decode()` first) | macro call |
| Numbers: decimal, `#` hex, `%` binary (16-bit, wrapping); `"c"` one character in **double** quotes (the manual says single quotes; the program reads `"`) | `num()` | numbers reduced modulo 65536; character constant |
| Expressions: an optional sign, terms joined by `+ - * / % & \| ^` of equal priority, left to right; no parentheses | `value()` | the IR keeps the left-to-right tree; the backend adds parentheses |
| The terms are unsigned 16-bit, the accumulator 32-bit (`long res`); a product is cut to unsigned 16 bits (`res=(unsigned short)d`, error 31 out of -32768..65535); `/` and `%` signed on the accumulator; the result kept as a 16-bit word | `value()` | a product masked (`&#FFFF`) where a later `/` or `%` would see the difference; an `EQU` value masked (labels are unsigned words: `N EQU -1`, `N/2` = 32767) |
| An expression naming an undefined symbol is 0 (`ret=2`, `*i=0`); the output is still written | `value()`, manual "Errors" | a register's name used as a symbol (`hl`, `de`, …: ASM80 warns "the label's name is a register's") gives 0 with a warning |
| `AND CP OR SUB XOR` take `A`-`L` as registers; another register's name is that undefined label; the text after the operand is not read | `_t3com()` | `SUB 0` with a warning (AEDIT: `sub hl,de`, `sub de,bc`) |
| `JR` / `DJNZ`: the offset in 16 bits (`short i = target - address - 2`) | `_t12()`, `_t7()` | the target masked when it is an expression (`JR #FFFF&(near+#FFFF)`, TEST.A80) |
| `(IX+d)`, `(IX-d)`, `(IX)`: d one number or one label | `gIndex()` | indexed |
| `INF` = `IN F,(C)` (ED 70); `SLI`; `EX AF,AF` (any word after the comma) | `com[]`, `_t8()` | `IN (C)`, `SLI`, `EX AF,AF'` |
| `ORG e` (e defined earlier); `EQU` (defined earlier); `DEFS n[,b]` (one fill byte); `DEFB` / `DEFW` (`?` = a value of `rand()`); `DEFM "text"` (the rest of the line is not read) | `_org()`, `_equ()`, `_defs()`, `_defb()`, `_defw()`, `_defm()` | `ORG`, `EQU`, `DS`, `DB` / `DW`; `?` written as 0 with a warning; text after `DEFM`'s string warned |
| `DEFR n`: `rand()` seeded with the address; `TIME`: 20 characters of the date | `_defr()`, `_time()` | `DS` of zeros with a warning |
| `DISP e` … `ENDD`; `ORG` ends a `DISP`; labels get the displaced address; `$` is displaced; `DISP` holds across `*F` | `_disp()`, `_endd()`, `_org()` | `DISP` / `ENT` (an `ENT` before an `ORG` inside `DISP`) |
| `ENT e`: the run address of the output file | `_ent()` | a comment |
| `IF e` (true when e is 0), `IF a=b` (true when equal), `ELSE`, `ENDIF`; no nesting; e defined earlier | `_if()` | `IF ((e)&#FFFF)==0`, `IF (a)==(b)` (16-bit values) |
| `NAME MAC` … `ENDM`, parameters `=0`..`=9` taken as **values** at the call (`decode_macro()`: `$` is the call's address); no labels in the body; defined before use | `_mac()`, `decode_macro()` | `MACRO NAME _m0,…`; arguments as `+(expression)`; `$` in an argument becomes the call line's label (one is added) |
| Keys in column 0: `*F file` includes a text (its name to a blank or `;`), `*B file[,start[,length]]` a binary (without a length the address moves by the whole file's length), `*Pn` (hex) the page at `#C000`; `*L± *M± *C± *D± *E *H *S *G *GA` listing and symbol file, `*$ *Tn *Z80 *O` the output file's form | `decode_star()` | `INCLUDE "name.asm"` / `INCBIN "name",start,length` (names without drive and directory, in lower case: DOS names), `ORG addr,page` for an `ORG` at `#C000`+ after `*Pn`; the other keys comments |

## 3. Oracle results

| Program | Content | Result |
|---|---|---|
| `TEST.A80` (ASM80's own) | every instruction and operand form, `DEFB`/`DEFW`/`DEFS`/`DEFM` (CP866), a macro with four parameters, `*D+ *E *L+ *H *M+ *C±` | 1485 bytes at 40000, equal (`JR near+#FFFF` needed the 16-bit target) |
| `TEST2.A80` (ASM80's own) | mixed-case mnemonics and registers, odd spacing | 113 bytes, equal |
| `PROBE.A80` | expressions (left to right, `*` then `/`, negative `EQU`, `%`, `\|`, `^`, characters), labels with `$` / `#` and over 16 characters, `DEFS`, `DEFM` with `;`, macro with `$`, both `IF` forms, `DISP`/`ENDD`, `(IX)`, `INF`, `SLI`, `EX AF,AF`, `*B` with start and length, `sub hl,de`, `cp de` | 129 bytes at `#6000`, equal except the two `DEFB ?` bytes (MSVC `rand()`: the 3rd and 4th calls, pass 1 makes the first two) |
| `PAGES.A80` | `*O`, `*P3` / `*P4` + `ORG #C000`, `*P0` | the four blocks asm80win.exe wrote (`PAGES.B00`-`B03`) equal the converted source's bytes in RAM pages 2, 3, 4, 2 |
| AEDIT (Sprinter Team) | `editor.a80` with `*F` of window / graphic / setscr, `*B` of the font, a Sprinter EXE header, `sub hl,de` | 6917 bytes from `#7E00` equal the shipped `ed.exe` (sjasmplus, pasmo, z80asm); 207 labels equal the listing's table |

`tfmtest.a80` (TSFM tools) carries `*Z80` but is written for another assembler (`sp=#61ff`, `a=#bf`): ASM80
refuses it, so it is not ASM80 test data.

## 4. Projects on the host

ASM80 sources are host files: `zxasm convert <directory> --codec asm80 --to sjasmplus -o out` converts a directory as
one project (sources named in lower case without extension, as `*F` names them; the files `*B` names copied next to
the output). Detection alone picks only files with ASM80's marks (key lines, `MAC`, `=n`, `DISP`/`ENDD`, `DEFR`,
`INF`); a file without them reads as plain text, hence `--codec`.

## 5. Open items

| Item | Note |
|---|---|
| `DEFB ?` / `DEFW ?` / `DEFR` / `TIME` | values the conversion cannot know when it converts (MSVC `rand()` in assembly order, the date): zeros with a warning |
| `*B name,start` without a length | ASM80 moves the address by the whole file's length while reading only the rest (a warning; no source uses it) |
| ASM80 1.7 (DOS) | the operators were `@ ! ?` there (or, xor, mod; changed in 1.8 Win); no DOS-era source found, not read |
