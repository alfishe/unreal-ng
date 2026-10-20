# unreal-asm: sjasmplus → MASM 1.1 / 1.3 (part B, Z3)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/masm/masmbackend.cpp` (the `masm` backend, on the `RetroWriter` core), `src/sync/reader.cpp` (the `masm-1.1` descriptor: idle rule with `idleZeroAt`, the form LOAD leaves) |
| **Rules from** | research-masm-to-sjasmplus.md §1 (MASM 1.1's help and source), MASM 1.1 run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`MasmBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, MASM 1.1's MT1, ZAsm, STORM, XAS and TASM 5.0 programs; `SyncInject_Test.Masm11TakesAFileAtItsPromptAsLoadLeavesIt`); `tools/verification/unreal-asm/emulator/inject-oracle.py masm11` |

## 1. Example first

```text
sjasmplus                          MASM 1.1
start:  ld hl,lab+2*3              START      LD HL,2*3+LAB      ; a chain left to right, the atom to the right
        ld a,high lab                         LD A,LAB/256
        ld a,low lab                          LD A,LAB&255
        ld bc,-lab                            LD BC,0-LAB
        ld a,lab ^ 3                          LD A,LAB+0@3       ; MASM reads LAB@3 as the name LAB@
        DUP 2 / nop / EDUP                    BEGIN 2 / NOP / END
        DISP #9000 ... ENT                    PHASE #9000 ... UNPHASE
        ex (sp),iy                            DB #FD,#E3         ; MASM 1.1 refuses EX (SP),IY
```

## 2. What the backend writes

| Source | MASM text |
|---|---|
| An expression | a chain left to right in which every operator takes a number, a name or `$` (no parentheses in MASM), regrouped where `+ - * & \| @` allow, as for XAS (research-sjasmplus-to-xas.md §2); `high` / `low` as `/256` / `&255`, `-x` as `0-x`, `~x` as `x@#FFFF`; `+0` before `@` after a name (MASM takes `@` for a character of the name: `LAB@#FF` is an error in MASM 1.1) |
| One MASM cannot chain, or one longer than its line holds | its value from the source's layout, with a note |
| A label | upper case, letters, digits, `_`, up to 10 characters (MASM's limit), no MASM keyword |
| `DUP n` ... `EDUP` | `BEGIN n` ... `END` for a body without labels (MASM's rule), assignments or blocks; else unrolled |
| `IF`, `WHILE`, `MACRO`, `DEFL` | decided, unrolled, expanded, written as values (MASM 1.x has no conditions) |
| `DB` / `DW` / `DS` | `DB` (strings of three or more characters quoted), `DW`, `DEFS n,fill` (MASM repeats the fill n times: a pattern of several bytes as `DB`) |
| `DISP` / `ENT`, `INCLUDE` / `INCBIN` | `PHASE` / `UNPHASE`, bare names of 8 characters |
| `IN F,(C)`, `SLL`, `EX AF,AF'`, IX / IY halves | `INF`, `SLI`, `EXA`, `XH XL YH YL`; `OUT (C),0` and `EX (SP),IY` as bytes |
| Lines | the label, the command from column 11; at most 31 characters (MASM's line buffer: a longer line breaks its editor's screen, seen in unreal-ng): a comment that does not fit goes on a line of its own, cut at 31; a code line that cannot be shortened is noted (MASM assembles it) |

## 3. Writing into MASM 1.1 (the injection)

MASM 1.1 keeps its text in a gap buffer from `#970B` (asm-synchronizer.md §7.12), but LOAD (`W`) leaves the file
as saved, its `FF` end included, at `#970B` with the gap start `(#96CC)` there and the gap end `(#96CE)` at `#FFFF`;
the editor builds the gap buffer when it is entered. The inject writes that form; the reader takes it
(`GapBufferParams::loadedForm` with a fixed start).

The idle rule: its key wait returns through `#85C8` in the menu and in the editor; the editor's 31-byte line buffer at
`#851A` is zeros only in the menu (`SyncDescriptor::idleZeroAt`). `A` then assembles the new text, `E` shows it.

MASM keeps no "changed" flag: a text in memory that is not empty and not the caller's own counts as changed, so the
first write over a loaded text needs force.

## 4. Results

| Check | Result |
|---|---|
| `MasmBackend_Test.ProgramsKeepTheirBytesThroughMasm` (host): every instruction form, MASM 1.1's MT1, ZAsm 3.15's ZXT1-3, STORM 1.3's STORMT1 / 2, XAS 7.447 `constrct`, 4.18 `cons418`, TASM 5.0's T50PROG → sjasmplus → MASM 1.1 → the MASM codec and back → the MASM frontend → sjasmplus | the original assemblers' bytes |
| MASM 1.1 in unreal-ng, the converted files loaded from its disk: `oracle1.asm` and every instruction form at `#6000` | 143 / 1173 bytes = sjasmplus' |
| `inject-oracle.py masm11`: the same written at its prompt, assembled with `A` | 143 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py masm --template` in MASM 1.1 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-75 of 100 lines | all words = sjasmplus' (found on the way: `3@LAB@5` lacked the `+0` after the name, the scan for a name looked through the operator `@`) |

## 5. Limits

- MASM 1.0 demo, 2.0 and 3.0 have inject rules (asm-synchronizer.md §7.12, 2026-10-16). MASM 3.0 refuses `@` (XOR,
  error 0): the backend's version `3.0` writes the value. The 2.0 copy has no compiler and the demo assembles nothing:
  their texts are checked by their own Save.
- A code line over 31 characters (a long name in a long expression whose value is not known) assembles but does not
  show whole in MASM's editor.
- No idle template snapshots for MASM yet.
