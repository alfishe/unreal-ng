# unreal-asm: the z88dk backend (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Code** | `src/dialects/z88dk/z88dkbackend.cpp` over the writer it shares with pasmo (`src/dialects/common/classicbackend.cpp`); `zxasm convert --to z88dk` |
| **Target** | z80asm of [z88dk 2.3](https://github.com/z88dk/z88dk/releases/tag/v2.3) (`z88dk-z80asm`, build 22110); its keyword list from `src/z80asm` (`keyword.def`, `scan_def.h`) |
| **Checks** | `unreal-asm-tests` (`Z88dkBackend_Test`, with `UNREAL_ASM_Z80ASM` the oracle programs too); `docs/inprogress/2026-10-05-unreal-asm/scripts/checks/crosscheck.py --targets z88dk` |
| **Result** | the STORM 1.3, ZAsm 3.15 (ZXT2, ZXT3), TASM 5.0 and ALASM 5.09 oracle programs assemble with z80asm to the bytes the original assemblers built; on the collection's disks 329 main sources build to the same bytes as with sjasmplus, the others hit the z80asm limits of §3 |

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
| `$hex`, `%bin`, `'c'` (`'\''`), double-quoted texts read C escapes | `$` prefix; `\\` and `\"` escaped in texts |
| A label ends with `:`; ~500 keywords of all the CPUs it knows (`abc`, `mov`, `test`, `copy`, `line` ...) are refused or read as keywords | such labels renamed `L_...` (`jp ret` silently assembled `jp 0`) |
| One ORG per section; a section's ORG must be a constant; `$` and labels count from their section's start | every constant ORG opens a section (`s_<file>_<n>`); an ORG computed from labels or `$` moves on with `DEFS (address)-$` in the section (a move back is z80asm's error); an ORG followed by nothing but labels and EQUs makes those labels EQUs |
| PHASE / DEPHASE, DEPHASE also outside PHASE; PHASE takes constants only | DISP / ENT; a nested PHASE's way back (ZX-ASM), an address computed from labels, has no form: z80asm reports it |
| Inside PHASE `$` comes out with the section's ORG added (`DW $` at PHASE #5D3B in ORG #6000 gives #BD3B), labels are right | `$` replaced by a label put before the statement (not in a macro, where it would repeat) |
| A JR to a label further on inside PHASE resolves wrongly unless the PHASE is closed later | a PHASE the file leaves open is closed at its end |
| IFDEF / IFNDEF see labels and EQUs defined before (not DEFL, not later ones) | a test for a name this file never defines: IFDEF / IFNDEF; a name it defines: decided when converting (defined outside the block that tests it, like sjasmplus' `exist`) |
| IF is decided in the first pass: a label defined further on reads as 0 | a warning names the label |
| `REPT n ... ENDR`; EXITM leaves the pass, but every pass is still expanded | fill sequences as REPT; WHILE / REPEAT ... UNTIL as REPT with an exit, at most 1024 passes |
| `LOCAL` works in MACRO, not in REPT; no PROC | a REPT body with labels becomes a macro; a local block outside a macro gets its labels numbered |
| `BINARY "f"` takes the whole file | a part given by numbers cut out by `zxasm convert`, as for pasmo |
| `IN F,(C)`, `OUT (C),0`, `SLL`, `IXH` ... | as such |
| Each section is written to its own binary (`<out>_<section>.bin`), its address `__<section>_head` in the map | `crosscheck.py` and the unit tests put them together |

## 3. Limits (z80asm cannot express)

| Case | Seen in |
|---|---|
| TASM's INCBIN sector tail (written, then the address moves back) | most TASM 4 projects: the tail is reported, the bytes after the file differ |
| `ORG $-2` and other moves back within a section | two STORM sources of Adventurer #15 |
| An ORG that goes back and forth through a redefinable pointer (ALASM's SAVEOBJ 2.1 macro) | SAVEOBJ |
| The return from a nested PHASE (ZX-ASM) | ZXT1 |
| IF on a label defined further on | one ALASM source |
| TASM 4.12's `.IF PASS` | SINUS |
| IFUSED, memory reads while assembling, pages | as for pasmo |
