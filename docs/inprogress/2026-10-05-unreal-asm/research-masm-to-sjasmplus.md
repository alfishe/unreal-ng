# unreal-asm: MASM → sjasmplus (the `masm` frontend)

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Frontend** | `src/dialects/masm/masmfrontend.{h,cpp}`, tests `MasmFrontend_Test` |
| **Sources of the rules** | MASM 1.1's help (`MASMHELP`, in `MASM1_1.LZH`), 1.3's help (`MASMHELP.W` on `MASM1_3.SCL`), and MASM 1.1's own source (`testdata/masm/MASM_SRC__*`), whose assembler routines are quoted below by label |
| **Oracle** | MASM 1.1 running in unreal-ng (`tools/unreal-asm/assemble-in-emulator.py masm11`), testdata `dialects/masm11/` |
| **Result** | a program with every construct (140 bytes), an INCBIN (257 bytes) and MASM 1.1's own source (LS2 with M1+ and M2+: 11688 bytes) assemble through the sjasmplus conversion to the bytes MASM 1.1 built; the source's 711 labels lay out as sjasmplus 1.24 gives them |

## 1. The language (MASM 1.x)

The help says MASM "supports every operation TASM uses" (TASM 3.0, which MASM was first written in), with changes.
What the frontend reads, and where it comes from:

| Construct | Rule | Source |
|---|---|---|
| Label | column 0, up to 10 characters; letters, `_ ? @` first, digits and `.` after; a colon ends it and is ignored (`INCLUD1:CALL INCLUST`) | help §2; `SKIN`, `DIKL` |
| Keywords | tokens, typed only in capitals; a lower-case word is text: `ld a,b` is "no command" | research-masm.md §5 |
| A keyword in column 0 | a command (`INCLUDE M1+`), never a label | the tokenized line |
| Expressions | left to right, no priorities, no parentheses, no unary minus, 16-bit words: `+ - * / & |` and `@` = XOR; `/` unsigned, by zero an error | help §6.7; `BITE`, `CALC`, `DIVIS`, `MULT` |
| Numbers | `#FF` (capital hex digits), `%101`, `255`, `0FFH`; `"AB"` a word of the last two characters, `""` a quote; `$` the logical address of the line | `TREAK`, `HOXOR`, `BIN`, `KOB`, `TEK1` |
| `(IX±e)` | `+e` below #80, `-e` up to #80: the whole e is computed, then negated (`(IX-2+1)` = -3); `(IX)` = `(IX+0)` | `IX_IY`, `DAL1` |
| Conditions, registers | `NV` / `V` = PO / PE; `XH XL YH YL`; `EXA` = EX AF,AF'; `INF` = IN F,(C) | the keyword table, `TAB_ASM` |
| `DEFB` / `DB` | strings in quotes and byte expressions (`"Z"+1`) | `DB_1` |
| `DEFS` / `DS` | `count[,bytes…]`: the byte list repeated count times, zeros without one; count 0 an error | help §6.2; `DS_1` |
| `DEFW` / `DW`, `EQU`, `ORG` | as usual; ORG sets both addresses | `ORG_1` |
| `PHASE e` / `UNPHASE` | PHASE sets the logical address only (also inside a PHASE), UNPHASE copies the physical one back | `PH_1`, `UN_1` |
| `INCLUDE name` / `INCBIN name` | bare names, 8 characters; INCLUDE reads a type-`a` file, INCBIN a type-`C` file; INCBIN loads whole sectors (checked: the rest of the last sector lands after the file, the address moves by the length) | `INCLUST`, `INCLUD1`, `INCBIN1`; MT2 in the emulator |
| `BEGIN n` … `END` | the block n times, up to 8 deep, no labels inside | help §6.3; `BEG_AS`, `END_ASM` |
| `DOWN rr`, `UP rr` (rr = HL, DE, BC) | the next / previous screen line: `INC H / LD A,H / AND 7 / JR NZ,$+12 / LD A,L / ADD A,32 / LD L,A / JR C,$+6 / LD A,H / SUB 8 / LD H,A` (UP: `LD A,H / DEC H …`, `SUB 32`, `ADD A,8`) | `TAB`, `TAB2`, `KP1` |
| `SYSTEM`, `SYSTEM+` | `DI / LD IY,#5C3A / LD A,#3F / LD I,A / IM 1`, + adds `EI / RET` | `TAB_SYS`, `SYS_1` |
| `STOPKEY [address]` | `XOR A / IN A,(#FE) / CPL / AND #1F` then `JP Z,address`, or `JR Z,$-6` without one | `STOP_TB`, `STOP` |

The 1.3 help gives DOWN as `SUB #E0 / JR NC`: the 1.3 binary differs from 1.1 only in its version text
(research-masm.md §2), and 1.1's table writes `ADD A,32 / JR C`, which the oracle confirms.

The 1.0 demo's keyword table ends before `DOWN` (`#F3` is its `*AIG*'95`): those four are labels there. 2.0 adds
`MAC ENDM IF ELSE ENDIF`, 3.0 also `BANK BORDER CLS`.

## 2. Conversion

- Expressions keep their order with parentheses (`2+3*4` → `(2+3)*4`); division masks to 16 bits as for TASM
  (`ir::Program::expressionBits = 16`, `unsignedArithmetic`).
- `$` inside a PHASE is the logical address, so `ORG` / `PHASE` with `$` inside an active PHASE keep their value in
  `__UNREALASM_PHASE=…` before the `ENT` (sjasmplus ignores a DISP inside a DISP). The symbol module leaves
  `__UNREALASM_` names out of a source's labels.
- `BEGIN` / `END` → `DUP` / `EDUP`; `DEFS n,list` → `DUP n` + `DB list`; the macro commands are written out.
- `INCBIN name` carries the sector slack (`NAME.slack` after the file, the address put back), as TASM's.
- A name sjasmplus reads as a register (`HX`) is renamed (`L_HX`).
- 2.0 / 3.0 directives stay as text with a warning.

## 3. Checks

| Check | Result |
|---|---|
| `MT1`: every construct of §1 (`testdata/dialects/masm11/MT1.$a`) | MASM 1.1 built 140 bytes at `#6000`; the conversion equal |
| `MT2` + `DAT.C`: INCBIN of 5 bytes | 257 bytes equal, the sector's rest included |
| MASM 1.1's own source (LS2 + M1+ + M2+) | MASM 1.1 assembled it in 45 s without errors: `#C000-#ED90` in RAM page 0 and `#6000-#6016`, 11688 bytes; the conversion equal |
| The released `masm 1.1.C` | equal up to `#D135` except its run-time variables; from there it holds code the archived source lacks (a newer build), so it is no oracle |
| Labels | `symconv source` / `SymbolsFromProject` on the source: the 711 labels of sjasmplus 1.24's `--sym` (`testdata/symbols/fromsource/masm11-LS2.sym`) |
| `roundtrip.py --assemble` (the 9 MASM files at hand) | 0 round-trip differences; the typed boundary-test files do not assemble (key-repeat garbage, as in MASM itself) |

No other MASM source exists in the collection (research-masm.md §7).

## 4. MASM 2.0 / 3.0

No documentation and no real source. Tried in MASM 3.0 (`RUN "TSM"`, `G` to load, `A`): plain programs assemble to
the addresses they name; `NAME MAC` (and `NAME MAC n`) is accepted, a call `NAME` (with or without an argument)
gives error 3; `IF 1`, `IF X` (X EQU 1), with or without ELSE, give "!?Unknown error?!" in pass 1. The binary's
messages ("Found macros:", "Macros inside!", "'ENDM' absent!", "Begin haven't end!") confirm named macros; the
syntax of the call and of IF is not known. Open.
