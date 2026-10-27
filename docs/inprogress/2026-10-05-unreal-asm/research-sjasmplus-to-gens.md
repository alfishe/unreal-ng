# unreal-asm: sjasmplus → GENS4 (part B, Z4)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/gens/gensbackend.cpp` (the `gens` backend), `src/dialects/common/chainwriter.cpp` (`ChainWriter`: left-to-right chains, shared with MASM), `RetroWriter::HalfRegisterBytes`, `src/sync/reader.cpp` (`Gens4Tape`: idle rule, `LinearParams::injectable`), `core/src/debugger/asm/sync/syncshared.cpp` (`CopyNow`) |
| **Rules from** | research-gens-to-sjasmplus.md §2 (HiSoft Devpac 3 / 4.1 manuals, GENS4 run in unreal-ng), GENS4 run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`GensBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, GENS4's constructs file, ZAsm, STORM, MASM, XAS and TASM 5.0 programs; `SyncInject_Test.Gens4TakesAFileAtItsPromptAsGLeavesIt`); `tools/verification/unreal-asm/emulator/inject-oracle.py gens4` |

## 1. Example first

```text
sjasmplus                          GENS4
start:  ld hl,lab+2*3              START   LD HL,2*3+LAB         ; a chain, the atom to the right
        ld a,high lab                      LD A,LAB&#FF00/256&255 ; GENS divides signed words
        ld a,low lab                       LD A,LAB&255
        ld a,5 ^ 3 / ld a,5 | 3            LD A,5!3 / LD A,5@3   ; ! XOR, @ OR
        ld a,ixh                           DEFB #DD,#7C          ; GENS has no undocumented instructions
        IF ext != 5 ... ENDIF              IF EXT-5 ... END
        db "text",0                        DEFM "text" / DEFB 0
        INCLUDE "lib.asm"                  *F lib
```

## 2. What the backend writes

| Source | GENS text |
|---|---|
| An expression | a chain left to right in which every operator takes a number, a name or `$` (GENS has no parentheses), regrouped where `+ - * & @ !` allow (`ChainWriter`, as for XAS and MASM); `?` the remainder, `@` OR, `!` XOR; `high` as `&#FF00/256&255` (GENS' `/` is signed), `low` as `&255`, `-x` as `0-x`, `~x` as `#FFFF-x` |
| One GENS computes otherwise (a division or a right shift of a value outside GENS' signed words, a comparison) | its value from the source's layout, with a note |
| A label | upper case, letters, digits, `_`, unique in its first 6 characters (GENS' rule, case counts) |
| `IF` / `ELSE` / `ENDIF` | decided while converting when the value is known; else `IF x` (true when not 0), `END`; one level |
| `DUP`, `WHILE`, `MACRO`, `DEFL` | unrolled, expanded, written as values (GENS4's `MAC` is not used) |
| `DB` / `DW` / `DS` | `DEFB`, `DEFM "text"` for strings of three or more characters, `DEFW`, `DEFS n` (zeros) or `DEFB` for another fill |
| `INCLUDE` | `*F name` |
| `DISP` / `ENT`, `INCBIN` | not converted (GENS has no displacement and no binary include): a warning |
| `IN F,(C)`, `OUT (C),0`, `SLL`, IX / IY halves, the undocumented copies | bytes: the halves through the H / L form's opcode (PROMETHEUS' instruction table, `RetroWriter::HalfRegisterBytes`) after `DD` / `FD` |

## 3. Writing into GENS4 (the injection)

GENS4 (tape, at 26000) keeps its text as records `[number lo] [number hi] text #0D` from a start word to an end word
that are operands its code rewrites (`(#67A8)`, `(#65C6)`; asm-synchronizer.md §7.13). `G` (load) leaves the file from
the start and the end word one past it. The inject writes the same (`LinearParams::injectable`). The idle rule: at its
`>` prompt its key wait returns through `#685D`, `#68DD`, `#67D9` (the line input of `I`: `#685D`, `#69C6`). GENS
keeps no "changed" flag: a loaded text is replaced by force or when it is the caller's own.

Found on the way: the live synchronizer copied only the pages a 48K machine counts (3) by number, 0-2, and missed the
page its `#4000` window maps (5): no 48K descriptor (GENS, ZEUS, Primus, Laser Genius, PROMETHEUS) read live. `CopyNow`
now always takes the pages the windows map.

## 4. Results

| Check | Result |
|---|---|
| `GensBackend_Test.ProgramsKeepTheirBytesThroughGens` (host): every instruction form, GENS4's constructs file, ZAsm 3.15's ZXT2 / ZXT3, STORM 1.3's STORMT2, MASM 1.1's MT1, XAS 7.447 `constrct`, TASM 5.0's T50PROG → sjasmplus → GENS → the GENS codec and back → the GENS frontend → sjasmplus | the original assemblers' bytes |
| GENS4 in unreal-ng, the converted files loaded from tape: `oracle1.asm` and every instruction form at `#6000` | 143 / 1173 bytes = sjasmplus' (`A,8000`: GENS' own table size is too small for 600 labels; some 4 minutes at 3.5 MHz) |
| `inject-oracle.py gens4`: the same written at its `>` prompt, assembled with `A,8000` | 143 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py gens --template` in GENS4 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-75 of 100 lines | all words = sjasmplus' (found on the way: a product above 32767 in absolute value is `*ERROR* 15`, its manual 2.5; such a product is written as its value) |

## 5. Limits

- A source with `DISP` / `PHASE` or `INCBIN` does not convert (GENS has neither).
- GENS4, GENS3M21, GENS 4.3t and GENS4B have inject rules (asm-synchronizer.md §7.13; the last three 2026-10-16).
- No idle template snapshots for GENS yet.
