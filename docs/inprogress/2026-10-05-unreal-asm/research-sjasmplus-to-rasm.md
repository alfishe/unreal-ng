# unreal-asm: sjasmplus → rasm

| | |
|---|---|
| **Date** | 2026-11-17 |
| **Code** | `src/dialects/rasm/rasmbackend.cpp` (the `rasm` backend, on the `RetroWriter` core) |
| **Rules from** | RASM v3.3.3 built from source (its `rasm.c`), each rule tried in rasm against sjasmplus 1.23.1; research-rasm.md for the dialect |
| **Checks** | `unreal-asm-tests` (`RasmBackend_Test`; with `UNREAL_ASM_RASM` the programs of other assemblers through rasm against their own bytes); `tools/verification/unreal-asm/checks/rasmcheck.py` (sjasmplus sources and projects through rasm against sjasmplus' memory) |

`zxasm convert x.asm --codec sjasmplus --to rasm -o y.asm` (or `--project -o dir` for a main source with what it
INCLUDEs and INCBINs). Every frontend reaches it: a source of another dialect is first written as sjasmplus by the
sjasmplus backend (which knows each dialect's arithmetic, LOCAL blocks and macro parameters) and read again.

## 1. Example first

```text
sjasmplus                          rasm
        DW 7/2,1<2                         DW 7\2,(-(1<2))      ; "/" divides in floating point; true is 1
        DW ~5,1==2<3                       DW (-1-5),(-(1==(-(2<3))))
        ld a,high x / low x                LD A,hi(X) / LD A,lo(X)
        ld a,+(x+1)                        LD A,0+(X+1)         ; "(" first would read as memory
        DB %101                            DB 0b101             ; "%" is rasm's modulo
        IF fin>#8000 (fin later)           (decided while converting: rasm does not know fin there)
        DUP 2 / nop / EDUP                 REPEAT 2 / NOP / REND
        DISP #C000 ... ENT                 ORG #C000,$$ ... ORG $$
        .loop (after main)                 main_loop
FCH     nop                                FCH1    NOP          ; rasm reads FCH as #FC
        ex af,af' / in f,(c) / sli b       EX AF,AF' / IN F,(C) / SLL B
        SAVEBIN "out.C",#6000,143          SAVE "out.C",#6000,143
        END                                ;END                 ; rasm assembles on after END
```

## 2. What the backend writes

| Source | rasm text |
|---|---|
| An expression | rasm's operators with its priorities (`* \ %` 2, `+ -` 3, `<< >>` 4, every comparison 5, `&` 6, `^` 7, `\|` 8, `&&` 9, `\|\|` 10), parentheses where the source's order differs; division `\` (rasm's `/` divides in floating point); `~x` as `(-1-x)`; `high` / `low` as `hi()` / `lo()`; sjasmplus' `>>>` as `(x>>n&mask)`; byte swap as shifts; `$$$` as `$$` |
| A comparison or `!` used as a value | `(-(...))`: sjasmplus' true is -1, rasm's 1; in an `IF` as it is |
| An expression with a value outside 32 bits on the way (a 16-bit rotation `x*#10001>>15`) | its value: rasm computes in doubles and makes a value past 32 bits #7FFFFFFF / #80000000 |
| An instruction's operand wholly in parentheses | `0+(...)` (rasm would read memory); `(X)+1` stays: rasm reads it as a value |
| An index offset | its signed byte where known, `(IX-3)` |
| Numbers | `#hex`, `0b...` (rasm's `%101` is modulo), a printable character `'c'` (not `'` `"` `\`), else decimal |
| A label | letters, digits, `_` in the source's case; a sjasmplus local or module name joined with `_`; renamed when it is a rasm keyword, math function, register, `PI` / `ASSEMBLER_RASM`, or a name of hex digits ending in `H` (rasm reads `FCH` as `#FC`); names that differ only in case kept apart (rasm ignores case) |
| `IF` / `ELSE` / `ENDIF` | decided while converting when the layout gives the value; else `IF x` with later labels as their values (rasm computes an `IF`, `DEFS`, `REPEAT` count in the order of the text: a later label there is "not found") |
| `DUP n` ... `EDUP` | `REPEAT n` ... `REND` for a body without labels, assignments, blocks, ORG / DISP or macro calls; else unrolled (a label each pass); a count of 0 or less unrolled (to nothing) |
| `WHILE`, `MACRO`, `DEFL` / `=` | unrolled, expanded at the calls, written as values (the core) |
| `DB` / `DW` | the items as they are (rasm's `$` in a list is the item's own address, as sjasmplus'); runs of two or more printable characters in `"..."`, the rest as numbers |
| `DS n,fill` | `DEFS n,fill`; a pattern of several bytes as `REPEAT n` / `DB` / `REND`; STORM's cyclic pattern as `REPEAT` and the remainder |
| `ORG` | `ORG x` (a page in ORG: not converted) |
| `DISP` / `ENT` | `ORG x,$$` / `ORG $$` |
| `INCLUDE` / `INCBIN` | `INCLUDE "f.asm"` (a project's file under its name in the project), `INCBIN "f",offset,length` |
| `SAVEBIN "f",a,n` | `SAVE "f",a,n` (rasm saves when the assembly ends; with no length: to #10000); a DOS path's `\` as `/` |
| `END` | `;END` with a note: rasm goes on after END |
| `IN F,(C)`, `SLL`, `EX AF,AF'`, undocumented forms, Z80N | `IN F,(C)`, `SLL`, `EX AF,AF'`; Z80N instructions as bytes (the core) |
| `DEVICE`, `OPT`, `ASSERT`, `DISPLAY`, `SLDOPT` ... | left out (no bytes), each with a note |
| The text | UTF-8; string bytes above #7E as numbers (rasm refuses them in quotes without `-fq`) |

## 3. Results

| Check | Result |
|---|---|
| `RasmBackend_Test` (8 tests): every rule above on short texts | 8 / 8 pass; the whole `unreal-asm-tests`: 459 pass (the oracle tests of other assemblers skipped without their binaries) |
| `RasmBackend_Test.ProgramsAssembleToWhatTheOriginalsBuilt` (`UNREAL_ASM_RASM`): STORM 1.3's STORMT1 / STORMT2 (with INC), ZAsm 3.15's ZXT1-ZXT3, TASM 5.0's T50PROG, the ALASM constructs, `relocfake`, the GENS constructs → rasm | every one = the original assembler's bytes (ZXT1's 16-bit rotations needed the 32-bit rule above) |
| `rasmcheck.py` testdata: `testdata/sjasmplus/*.asm`, `testdata/dialects/sjasmplus/{relocfake,z5probe}.asm` | 5 / 5 equal |
| `rasmcheck.py --list corpus-ok.txt`: the 37 Z5 sjasmplus projects (`--project`) | 27 equal, 3 differ, 7 not built by rasm (2026-11-17; the causes in §4) |

## 4. Not converted (limits)

- **Code over code.** sjasmplus lets a later ORG write over bytes already assembled; rasm stops with "Assembling
  overwrite" / "ORG located in a previous ORG section". 7 corpus projects do this: the VDOS emulator of Wild
  Commander (3 copies: `DEFS 16384,#CF` for the ROM, then the RST entries over it), CobraSpectrum, FlexNavigator's
  `fn` / `fnloader` (several blocks at one address, each saved on its own), `zxfire` (a BASIC line patched in
  place). rasm has no option to allow it; splitting the blocks into rasm banks would change what `SAVE` writes.
- **Lua.** `LUA` ... `ENDLUA` (BasicLib's two examples: `sj.parse_code` writing the BASIC lines) is left out with a
  warning: the bytes it makes are missing (34 and 30 bytes).
- **INCHOB / INCTRD.** Not converted (a warning): `3color` includes its picture from a Hobeta file (5783 bytes). The
  project conversion copies only INCBIN files.
- **A page in ORG**, `DEVICE` memory pages, `SAVESNA` / `SAVETAP` / `SAVETRD`: rasm's `BANK` / `BUILDSNA` lay memory
  out their own way; written as a note.
- **Strings above #7E** are written as numbers: the text stays assemblable without rasm's `-fq`.
- **A value past 32 bits** is written as its value only when the layout gives it; with an unknown label in it, rasm
  saturates it to #7FFFFFFF.
