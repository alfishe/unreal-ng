# unreal-asm: the z88dk backend (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Code** | `src/dialects/z88dk/z88dkbackend.cpp` over the writer it shares with pasmo (`src/dialects/common/classicbackend.cpp`); `zxasm convert --to z88dk` |
| **Target** | z80asm of [z88dk 2.3](https://github.com/z88dk/z88dk/releases/tag/v2.3) (`z88dk-z80asm`, build 22110); its keyword list from `src/z80asm` (`keyword.def`, `scan_def.h`) |
| **Checks** | `unreal-asm-tests` (`Z88dkBackend_Test`, with `UNREAL_ASM_Z80ASM` the oracle programs too); `tools/verification/unreal-asm/checks/crosscheck.py --targets z88dk` |
| **Result** | the STORM 1.3, ZAsm 3.15 (ZXT2, ZXT3), TASM 5.0 and ALASM 5.09 oracle programs assemble with z80asm to the bytes the original assemblers built; on the collection's disks 354 main sources build to the same bytes as with sjasmplus (2026-10-20; 351, 335 before), the other five (TASM 4.12's SIN7 / SINUS) write a table backwards in a macro, a z80asm limit of §3 (an error at once; before, their DEFL text grew and z80asm did not end) |

## 1. Example first

```text
IR                                   z80asm
        ORG #8000                    SECTION s_MAIN_1          ; one ORG per section
                                     ORG ($8000&$FFFF)
START   LD A,(1+2)*3                 START:                    ; a label ends with ":"
                                     LD A,0+(1+2)*3            ; C's priorities, "0+": not memory
        DW (0-1)/2  (ALASM)          DW ((0-1)&$FFFF)/2        ; 32-bit words: 16-bit unsigned masked
        DISP #C000                   PHASE ($C000&$FFFF)
        DW $        (in DISP)        __UNREALASM_PC_MAIN_1:    ; z80asm 2.3 adds the section's ORG to $ in PHASE
                                     DW __UNREALASM_PC_MAIN_1
        IF !exist make               IFNDEF make               ; a name another file defines
ABC     NOP                          L_ABC:                    ; ABC, MOV, TEST ... are z80asm keywords
        INCBIN "FONT"                BINARY "FONT"
```

## 2. z80asm facts the backend relies on

| Fact (checked with z80asm 2.3) | Writing |
|---|---|
| C's priorities, 32-bit signed words, comparisons give 1 | the sjasmplus-like tree; a 16-bit unsigned source's division, remainder, shift right and comparisons get `&$FFFF` operands, rotations a final mask; sjasmplus' -1 negated, STORM's 1 kept |
| A signed 16-bit source (GENS): two's complement words, numbers modulo 65536 | `/` and `%` on operands sign-extended `(((x&$FFFF)^$8000)-$8000)` (a number 0..#7FFF as is); an EQU, a DS count, an IF condition known to exceed #FFFF masked `&$FFFF` |
| `$hex`, `%bin`, `'c'` (`'\''`), double-quoted texts read C escapes | `$` prefix; `\\` and `\"` escaped in texts; the remainder written ` % ` with blanks (`7%100` is 7 and the binary number `%100`; fixed 2026-10-19) |
| A label ends with `:`; ~500 keywords of all the CPUs it knows (`abc`, `mov`, `test`, `copy`, `line` ...) are refused or read as keywords | such labels renamed `L_...` (`jp ret` silently assembled `jp 0`) |
| One ORG per section; a section's ORG must be a constant; `$` and labels count from their section's start | every constant ORG opens a section (`s_<file>_<n>`); an ORG computed from labels or `$` moves on with `DEFS (address)-$` in the section; a move back to an address the layout gives (`ORG $-2`; a pointer the source redefines: the next line's address) opens a section there, after the one it writes over (2026-10-20; in a macro or a loop, whose lines have an address per pass, it stays z80asm's error); an ORG followed by nothing but labels and EQUs makes those labels EQUs |
| PHASE / DEPHASE, DEPHASE also outside PHASE; PHASE takes constants only | DISP / ENT; a nested PHASE's way back (ZX-ASM), an address computed from labels, has no form: z80asm reports it |
| Inside PHASE `$` comes out with the section's ORG added (`DW $` at PHASE #5D3B in ORG #6000 gives #BD3B), labels are right | `$` replaced by a label put before the statement (not in a macro, where it would repeat) |
| A JR to a label further on inside PHASE resolves wrongly unless the PHASE is closed later | a PHASE the file leaves open is closed at its end |
| IFDEF / IFNDEF see labels and EQUs defined before (not DEFL, not later ones) | a test for a name this file never defines: IFDEF / IFNDEF; a name it defines: decided when converting (defined outside the block that tests it, like sjasmplus' `exist`) |
| IF is decided in the first pass: a label defined further on reads as 0; an address label is its offset in the section | an IF outside a macro whose names the layout knows (not redefinable) is decided when converting, the branch left out written as a comment line and its new DEFL names assigned the value the layout leaves; otherwise a warning names the label |
| DEFL is a text macro of the preprocessor: `X DEFL $` gives the `$` where X is used; DEFLs assigned from each other grow at every assignment (z80asm may not end) | `$` in a DEFL replaced by a label put before the statement (`LOCAL` in a macro); a DEFL written in exclusive IF branches counts once (an EQU); names assigned from each other: each value an EQU of its own (`LOCAL` in a macro) and the name standing for it, so the text no longer doubles (2026-10-20) |
| `REPT n ... ENDR`; EXITM leaves the pass, but every pass is still expanded | fill sequences as REPT; WHILE / REPEAT ... UNTIL as REPT with an exit, at most 1024 passes |
| `LOCAL` works in MACRO (also in its middle), not in REPT; no PROC | a REPT body with labels becomes a macro; a local block outside a macro gets its labels numbered |
| `BINARY "f"` takes the whole file | a part given by numbers cut out by `zxasm convert`, as for pasmo |
| `IN F,(C)`, `OUT (C),0`, `SLL`, `IXH` ... | as such |
| Each section is written to its own binary (`<out>_<section>.bin`), its address `__<section>_head` in the map | `crosscheck.py` and the unit tests put them together, in the map's order (a later section writes over an earlier one), the `s_tail_` sections first |
| TASM's INCBIN copies the rest of the file's last sector, then the address moves back | the tail (`BINARY "name.slack"`) in a section `s_tail_<file>_<n>` at the file's end, the code going on in a new section from there (2026-10-20) |

## 3. Limits (z80asm cannot express)

| Case | Seen in |
|---|---|
| A move back within a section in a macro or a loop (a warning) | SINUS (TASM 4.12's GENSINUS writes its table forwards and backwards through DEFLs) |
| The return from a nested PHASE (ZX-ASM) | ZXT1 |
| IF on a label defined further on inside a macro, or on a redefinable name | none in the collection |
| IFUSED, memory reads while assembling, pages | as for pasmo |
