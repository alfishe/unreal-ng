# unreal-asm: GENS → sjasmplus (phase A8, GENS frontend)

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Status** | Done: `src/dialects/gens/gensfrontend.{h,cpp}`, tests `tests/gensfrontend_test.cpp`, test data `testdata/dialects/gens/` |
| **Sources of the rules** | HiSoft Devpac 3 and 4.1 manuals, section 2 (`HiSoftDevpacV3.txt`, `HiSoftDevpacV4.1.txt` of ZXDB entry [8091](https://spectrumcomputing.co.uk/entry/8091)); every rule below was then checked on GENS4 (and the noted ones on GENS3) running in unreal-ng |
| **Result** | a constructs file (104 bytes) and the five real GENS sources of `testdata/gens` assemble with sjasmplus to the bytes GENS4 built; their labels come through `SymbolsFromProject` with GENS' own line numbers |
| **File format** | [research-gens.md](research-gens.md) (numbered lines, blank compression, containers) |

## 1. Example first

```text
GENS4                                   sjasmplus (converted)
START:  LD   A,4+5*3-8                  START   LD A,(4+5)*3-8
        DEFW $,$                                DW $,$+2
        DEFW #8000/2                            DW (((#8000&#FFFF)^#8000)-#8000)/2
MOVE    MAC                                     MACRO MOVE _g0,_g1
        LD   HL,=0                              LD HL,_g0
        DEFB 2*=1                               DB 2*_g1
        ENDM                                    ENDM
        MOVE START,1+1                          MOVE START,+(1+1)
```

GENS4 built `3E 13` for the first line (19: no priorities), `00 C0 02 C0` for `DEFW $,$` at `#C000` (the location
counter moves per item), `00 C0` for `#8000/2` (signed words) and `04` for `DEFB 2*=1` with `1+1` (the argument is a
value, not text).

## 2. The language, as GENS4 assembles it

| Rule | Manual | Checked in the emulator | Conversion |
|---|---|---|---|
| Label from column 0; a `:` after it is skipped | label chars only | `LAB1: LD A,1` defines `LAB1` | label without the colon |
| Only the first 6 characters of a name count; case counts | 2.2, 2.4 | `ABCDEFG` defined, `DEFW ABCDEFH` gives its address | every spelling of a name becomes its defining spelling (also across the files of a project) |
| Label characters `0-9 $ A-z` (A-z: letters and `[ \ ] ^ _`, `#`), a letter first | 2.2 | `L[1]`, `two^5`, `a` are labels | renamed for sjasmplus where needed (`L_L_1_`, `L_two_5`, `L_a`) |
| Mnemonics, registers, conditions, directives in capitals | appendix 2 | `ld a,1` is `*ERROR* 02`; `LD A,a` loads the label `a` | lower-case mnemonics converted with a warning; lower-case register names stay labels |
| Expressions strictly left to right; `+ - * / ? & @ !` (`?` mod, `@` or, `!` xor); unary minus | 2.5 | `4+5*3-8` = 19, `17@%1000` = 25, `%1001101!%1011` = `%1000110`, `#3456?#1000` = `#456`, `-1` = `#FF` | the IR keeps the left-to-right tree; the backend adds parentheses |
| 16-bit two's complement words; numbers modulo 65536 | 2.5 | `70016` = 4480; `#8000/2` = `#C000`, `60000/2` = -2768, `0-7/2` = -3, `0-7?2` = -1, `#FFFF*2` = -2 | numbers reduced at parse; `/` and `?` on sign-extended 16-bit operands (new backend case: `expressionBits = 16`, signed) |
| `#` hex in capitals | 2.5 | `#ff` is `*ERROR* 10` | lower case accepted (no real source has it) |
| `"c"`: one character (`"""` is the quote) | 2.5 | `"A"+128` = `#C1`, `"y"-";"+7` = `#45`, `"""` = `#22` | a character constant |
| Spaces between terms and operators | 2.5 | `1 + 2`, `, 3` work | — |
| Text after an operand without `;` is no comment | 2.1 says comments may follow | `LD A,1 comment` is `*ERROR* 10` | operands end at a `;` outside character constants only |
| `$` in `DEFB` / `DEFW`: the address of the current item | 2.7 (the counter advances per expression) | `DEFW A1,$` at `#C000` gives `$` = `#C002` | `$` in item k becomes `$+k` (`$+2k` for words) |
| `ORG $` before any ORG | — | GENS4 started at `#8B87` in one session, `#CFBB` in another (after its text and symbol table) | kept as `ORG $` (sjasmplus: 0) with a warning |
| `DEFM` with any delimiter; the end of the line ends it | 2.7 | `DEFM /abc/`, `DEFM "d;e"` | a string |
| `DEFS n` | 2.7 | GENS4 writes zeros; GENS3 only moves the counter | `DS n` (zeros) |
| `ENT expr` | 2.7 | no code | a comment |
| `IF` / `ELSE` / `END` (END ends the condition; not nested) | 2.8 | `IF X-1` with X = 1 takes the ELSE part | `IF` / `ELSE` / `ENDIF`; an `END` without an open IF is dropped |
| `NAME MAC` … `ENDM`, parameters `=0`..`=31` | 2.6 | `2*=0` with argument `1+1` is 4 (a value); a label in the body is defined once (`*ERROR* 04` at the second call) | `MACRO NAME _g0,...` (as many as the body uses); a call passes `+(expression)`, a value in sjasmplus too |
| Macro buffer | editor `C` command | GENS4: "No Macro Space" until `C` sets it | — |
| `*F name` (drive `n:` optional) includes a file; other `*` commands shape the listing | 2.9 | checked in GENS4B: `*F 1:INC` read INC.C from drive A | `INCLUDE "name"`; the others become comments |

GENS3 (the 1983 tape build in `DEVPAC_1.TAP`) gives the same values for the probes it can assemble, but it has no
`MAC` / `ENDM` (`*ERROR* 02`) and no `C` command, asks `Buffer size?` when it starts, and lists the assembly unless
option 4 is given (GENS4: option 4 turns the listing on).

## 3. Oracle and results

GENS4 (`gens4` block of `DEVPAC_4.TAP`, HiSoft Devpac 4 on tape) ran in an own unreal-ng instance (48K): written at
26000 or 45000 (it runs from any address; the text and the symbol table follow it), started with
`RANDOMIZE USR`, the macro buffer set with `C`, the source loaded from a tape image with `G,,`, assembled with `A`,
the bytes read from memory filled with `#AA` beforehand. `tools/verification/unreal-asm/emulator/assemble-in-emulator.py gens4` does
these steps.

| Source | Bytes compared | Result |
|---|---|---|
| `testdata/dialects/gens/constructs.gens` (every construct of §2) | 104 at `#C000` | equal |
| `gens/ISC11VRG__ISCOP.C` | 1235 from `#80E8` | equal |
| `gens/PF212__BOOT.A` (two ORGs) | 106 from `#5E88` | equal |
| `gens/ZX_NET__ZX_NET1` | 192 from `#AFC8` | equal |
| `gens/HISOFT-C__64-A95` | 521 from `#F8A2` | equal |
| `gens/WINDOW__WINDOW` (`ORG $`: the converted source given GENS4's start `#CFBB`) | 1916 from `#CFBB` | equal |
| GENS3: the constructs file without its macros | 87 at `#C000` | equal except the 3 bytes of `DEFS 3` (GENS3 leaves memory as it was) |

`roundtrip.py --assemble` over the GENS files of the collection (one TR-DOS image holds one; the other sources are
the five of `testdata/gens`): 6 sources, 0 round-trip differences, 6 assembled by sjasmplus without errors.

Labels: `SymbolsFromProject` on the constructs file gives the values sjasmplus 1.24 wrote with `--sym` for the
conversion (`testdata/symbols/fromsource/gens-constructs.sym`), under the GENS names (`a`, `two^5`) and GENS' line
numbers (`SourceLine::number`).

## 4. Open points

- GENS4B (`MONSGENS.LZH`, "1990 MOA B-Disk version") works when started from the TR-DOS prompt (code at 30000,
  `RANDOMIZE USR 30000`); from BASIC without TR-DOS set up its disk commands hung or reset the machine. It names a
  file `n:NAME` (`n` = 1-4, drives A-D) and always uses type `C`: `G,,1:NAME` loads, `P10,30,1:NAME` saves the same
  bytes, `*F 1:NAME` includes; checked with `testdata/dialects/gens/GMAIN.$C` + `GINC.$C` (`3E 01 06 02 C9`, equal
  to the sjasmplus conversion; `ImageProject` takes the one-line INC that detection alone cannot tell).
- A value beyond 16 bits stays one in sjasmplus where GENS keeps the low 16 bits (`X EQU #FFFF+2` then `DEFS X`);
  no real source does this.
- The pasmo and z88dk backends have no signed 16-bit division yet: a GENS source converted to them divides as they do.
