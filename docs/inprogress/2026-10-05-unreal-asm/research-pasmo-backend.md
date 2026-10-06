# unreal-asm: the pasmo backend (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Code** | `src/dialects/pasmo/pasmobackend.cpp`; `zxasm convert --to pasmo` |
| **Target** | [pasmo 0.5.5](https://pasmo.speccy.org/) by Julián Albo (its manual `pasmodoc.html` in the source archive) |
| **Checks** | `unreal-asm-tests` (`PasmoBackend_Test`, with `UNREAL_ASM_PASMO` the oracle programs too); `tools/unreal-asm/crosscheck.py` on real disks |
| **Result** | the oracle programs of STORM 1.3, ZAsm 3.15, TASM 5.0 and ALASM 5.09 and the General Sound 1.04 ROM (TASM 4.0) assemble with pasmo to the bytes the original assemblers built; on the collection's disks every main source both sjasmplus and pasmo assemble gives the same bytes (419 sources, 0 differences) |

## 1. Example first

```text
IR (from STORM)                     pasmo
ORG #9000,#8400 (run, place)        ORG #8400
                                    __UNREALASM_D DEFL #9000-$       ; no PHASE in pasmo: the difference
RUN     JR RUN                      RUN EQU $+__UNREALASM_D          ; a label is its run address
                                    JR 0+(RUN)-__UNREALASM_D         ; a relative jump goes to the physical target
        DW $                        DW ($+__UNREALASM_D)
-X+2, HIGH X+1                      (-X)+2, (HIGH X)+1               ; pasmo's unary operators bind loosely
2<3 (STORM: 1)                      (-(2 LT 3))                      ; pasmo's true is #FFFF
LD A,(IX+OFS)  (OFS = -122)         LD A,(IX+((OFS) AND #FF))        ; 16-bit unsigned words
RET (a label)                       L_RET                            ; mnemonics are reserved
IN F,(C) / OUT (C),0                DB #ED,#70 / DB #ED,#71
```

## 2. pasmo facts the backend relies on

| Fact (pasmodoc, checked with pasmo 0.5.5) | Writing |
|---|---|
| Precedence: `* / MOD SHL SHR`, then `+ -`, comparisons, the unary `NOT ~ ! + -`, `AND`, `OR XOR`, `&&`, `\|\|`, and `HIGH LOW` last: `-1+2` is -3, `HIGH #1234+1` is #12 | every unary operation and HIGH / LOW in parentheses; binary ones by priority |
| 16-bit unsigned words; comparisons give #FFFF | the 16-bit sources (ALASM, TASM, STORM, ZX-ASM) need no masks; STORM's true 1 is negated |
| An operand starting with `(` is taken for memory | `0+` in front of a value that starts with a parenthesis |
| `(IX+e)`: after `+` any byte 0..255, after `-` 0..128 | a numeric offset as is, a negated one with `-`, any other `((e) AND #FF)` |
| Names: a letter, `_ ? @ .` first; `$` inside a name is ignored; mnemonics, registers, operators and directives are reserved | such labels renamed `L_...` |
| `'text'` takes every byte, `''` is a quote | texts and one-character constants single-quoted (the file keeps the Spectrum code page) |
| No PHASE / DISP | the displacement written out: `__UNREALASM_D DEFL run-$` at DISP, 0 at ENT; labels `EQU $+__UNREALASM_D`, `$` as `($+__UNREALASM_D)`, JR / DJNZ targets `-__UNREALASM_D`; a file that may start inside a displacement defines the delta once (`IF !DEFINED`) |
| A DEFL name may not be used before its definition | a name assigned with `=` once in its assembly (the main source and what it INCLUDEs) is an `EQU`; ConvertProject counts definitions per assembly |
| `LOCAL` works in `MACRO` and `PROC`, not in `REPT` (redeclared in the second pass) | local blocks as `PROC ... ENDP` with `LOCAL`; a `REPT` body that defines labels becomes a macro called by `REPT` |
| `REPT` with `EXITM` inside `IF` | WHILE and REPEAT ... UNTIL as `REPT 65535` with an exit test |
| `DS n,fill` takes one fill byte | a fill sequence as `REPT n` / `DB` / `ENDM` |
| `INCBIN` takes the whole file | a part given by numbers is marked `; unreal-asm: slice offset,length`; `zxasm convert` writes that part to its own file and names it |
| A later `ORG` may write over earlier bytes (the memory image keeps the last) | TASM's INCBIN sector tail: written, then `ORG` back |
| No `IN F,(C)`, `OUT (C),0` | their bytes |
| No IFUSED, no memory reads while assembling, no pages, no SAVEBIN | reported; `IFUSED` blocks are assembled (`IF 1`) with a warning, SAVEBIN kept as a comment |

## 3. Results

| Check | Result |
|---|---|
| STORMT1 / STORMT2 (STORM 1.3) | equal (STORMT1 has `ORG run,place`: the written-out displacement) |
| ZXT1..ZXT3 (ZAsm 3.15) | equal (nested PHASE, macros, REPT labels); ZXT4 needs IFUSED and `.m` |
| T50PROG (TASM 5.0), constructs (ALASM 5.09) | equal |
| General Sound 1.04 ROM (TASM 4.0, PHASE across 20 INCLUDEs, INCBIN sector tails) | all 32 768 bytes equal |
| `crosscheck.py`: STORM and ZX-ASM disks of the collection | 88 equal, 0 differ; 22 need IFUSED, 2 refer to a redefinable name before it is defined |
| `crosscheck.py`: TASM and ALASM disks of the collection | 245 equal, 0 differ; 5 are TASM 4.12's SINUS (`.IF PASS`: an undefined name read as 0 in the first pass) |

pasmo 0.5.5 writes an empty file when the code spans the whole 64K: its output size is a 16-bit sum that wraps to 0
(the GS ROM conversion places TASM's variables at 0 and the ROM at #8000). `crosscheck.py` was run with a pasmo built
with that one line widened; the unit tests use programs that stay below.

## 4. Open items

| Item | Note |
|---|---|
| IFUSED, memory reads, pages | pasmo has none of them |
| A redefinable name used before its definition (`EM3D13=$ ... EM3D13=#3D13`, used first) | pasmo refuses; sjasmplus takes the previous pass's value |
| TASM 4.12's `.IF PASS` | pasmo refuses an undefined name in `IF` |
