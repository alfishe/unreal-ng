# sjasmplus → ALASM: the backend and the injection (part B, Z0)

The ALASM backend writes the IR as ALASM 5.07 text (the spelling 5.07-5.09 share; `BackendOptions::targetVersion`
picks older keywords). `sync-inject` puts that text into a running ALASM 5.09 as its own text. This page records what
the backend relies on, how the injection decides that ALASM is idle, and how both were checked. The other direction is
[research-alasm-to-sjasmplus.md](research-alasm-to-sjasmplus.md); the design is
[asm-synchronizer.md](asm-synchronizer.md) part B (§13-§20).

## 1. Example first

A sjasmplus fragment and what `zxasm convert --to alasm` writes:

```text
COUNT   EQU SIZE*3+2            COUNT   EQU SIZE*3+2
        ld a,1+2*3                      LD A,1+(2*3)            ; ALASM computes left to right
        ld hl,(1+2)*3                   LD HL,0+(1+2)*3         ; "(" first would be memory
        ld a,high table                 LD A,96                 ; table is further down: its value
        ld bc,table+COUNT               LD BC,table+COUNT       ; a later label in + and -: kept
        ld a,COUNT%7                    LD A,COUNT-(COUNT/7*7)
        IF SIZE > 5                     IF0 5-SIZE+#8000/#8000  ; 0 when 5 < SIZE
main    nop                     main    NOP
.loop   djnz .loop              main_loop DJNZ main_loop
1       jr 1B                   _T1_1   JR _T1_1
        sub a,b                         SUB A                   ; sjasmplus repeats it per operand
                                        SUB B
        ld bc,de                        LD B,D                  ; a fake instruction, written out
                                        LD C,E
        rlc (ix+4),b                    DB #DD,#CB,4,0          ; no ALASM spelling: the bytes
        MACRO WAIT n                    MACRO WAIT
        ld b,n                          LOCAL                   ; .w belongs to each expansion
.w      djnz .w                         LD B,\0
        ENDM                    w       DJNZ w
                                        ENDL
                                        ENDM
```

`testdata/dialects/alasm-backend/oracle1.asm` holds these constructs and more. Its ALASM text, injected into ALASM
5.09 and assembled there, gives the 143 bytes sjasmplus builds from the original (§5).

## 2. How the backend is built

```mermaid
flowchart LR
    S["sjasmplus source"] --> L["layout (layout.h)<br/>every label's value,<br/>a hidden label per line for $"]
    S --> F["sjasmplus frontend"] --> IR["IR"]
    IR --> B["ALASM backend<br/>left to right, folding,<br/>names, macros, fakes"]
    L --> B
    B --> T["ALASM text"] --> C["alasm codec<br/>(5.07 tokens)"] --> I["sync-inject<br/>the text page"]
```

- **Values.** ALASM assembles in one pass and in 16-bit unsigned words. An expression ALASM cannot compute the
  source's way is written as its value. `Convert` / `ConvertProject` lay a sjasmplus source out first and hand the
  labels to the backend (`BackendOptions::labelValues`; `IBackend::WantsLabelValues`). The layout also gets a
  hidden local label on each line outside macros and repeats (`.__UNREALASM_L<file>_<line>`). Its value is `$` on
  that line. No such label goes before the first global label, because a `.label` there would itself become the
  parent.
- **The check is per operator.** An expression is kept as written when every part of it is congruent modulo #10000:
  `+ - * & | ^ ~`, high, low and ALASM's rotations. Division, right shifts and remainders need both operands to lie in
  0..#FFFF by the source's own arithmetic. Comparisons and logical operators exist only as IF conditions (§3). A
  label defined further down may stand only in `+` and `-` (ALASM's help). ORG, EQU, DS, DUP, IF and DISP take no
  later label at all.
- **Names.** sjasmplus' `parent.local` is written as `parent_local`, a temporary `1` as `_T1_<n>` (`1B` / `1F`
  resolved by line). A name that is an ALASM keyword in capitals is renamed: `L_EXA`, `L_E`. A macro's `.labels` and
  the labels of a repeated block go into a `LOCAL ... ENDL`.

## 3. ALASM facts the backend relies on

From ALASM 5.07's help (`al50help`) and checked in unreal-ng where marked.

| Fact | Writing |
|---|---|
| Left to right, parentheses only; `!` xor, `'x` / `.x` high / low, `x~` inverts the result so far | a right operand that is an operation in parentheses; a left one never |
| An operand starting with `(` is memory | a value is written `0+(...)` |
| "In every operation but + and - a label defined below is not allowed (one pass)" | folded to its value (labels from the layout) |
| IF0 takes the block on 0, IFN on not 0; before 5.03 IF0 is spelled IF | `==` / `!=` as a subtraction; `a<b` as `a-b+#8000/#8000` (0 when a<b, exact while a-b+#8000 lies in 0..#FFFF, checked with the values); `exist x` as `?x` |
| REPEAT ... UNTIL0 x runs the body until x is 0 (UNTIL before 5.07) | WHILE c as `IF0/IFN c`, `REPEAT`, body, `UNTIL0 c` (or `c-1` for a 0/1 test), `ENDIF` |
| Index halves HX LX HY LY; INF, SLI, EXA, `OUT (C),0` | written so |
| **emulator**: `ADD B` puts nothing down in 5.09 (one byte per `ADD x` / `ADD A,x` pair came out) | ADD / ADC / SBC always `A,x` |
| **emulator**: `DW "AB"` puts down `41 42`, the characters | a one or two character string in DW is written as the number |
| LOCAL / ENDL hides the labels inside (a label used before the block stays global) | macro and repeat labels |
| `DS n,a,b` repeats the pattern n times | sjasmplus' `DS n,v`; STORM's cyclic fill as DS + DB |
| ALASM 5.09 assembles #8000-#BFFF into its system page | the oracle sources ORG #6000 |

## 4. When ALASM is idle (the injection's precondition)

The text page and its header are those of part A (`FileImage`, the page IX+#0D names). ALASM's LOAD reads whole
sectors: after the file the page holds the rest of the last sector, then zeros (dump `alasm509-loaded`). Writing the
file and zeroing the rest is therefore what LOAD leaves, with "changed since saved" set in the header.

Writing is safe only while ALASM waits for a command. In the editor the cursor's pointers lead into the text, and
during ASSEMBLE, SAVE or a catalogue the text is in use. Memory flags did not tell the states apart: IX+1 is the
cursor column, and IX+#17 stays #FF at the prompt after the editor. The call chain does. In the frame wait
(`#907F`: `XOR A / LD HL,FRAMES / CP (HL) / JR Z`) the stack holds, ALASM 5.09, `alasm64` loader:

| State | Words from SP (#5FBE / #5FBC) |
|---|---|
| A> prompt (also with a command half typed) | `… 9043 8EF2 BF15 7FFD 9199 91E3 9707 2D2B` |
| editor | `… 9043 BE08 BED3 0008 9199 915D 9FD1 9733 2D2B` |
| editor's E> line | `… 9043 A537 BF15 0008 9199 91E3 A51D 9733 2D2B` |
| catalogue, ASSEMBLE | `#5FCE`/`#5FD0` = `879C 9733`, `D200 9733`, `0000 9733` |

`#91E3` returns from the line input to `#9707`, the command loop. A command (the editor too) runs with `#9733` in
that slot. The idle rule is therefore "the words #91E3, #9707 follow each other within 64 bytes above SP"
(`SyncDescriptor::idleChain`). Found by reading the stack paused in the wait loop in each state; breakpoints did not
fire in that instance (reported).

## 5. Oracle results

| Check | Result |
|---|---|
| Round trip on the host (`AlasmBackend_Test.ProgramsKeepTheirBytesThroughAlasm`): every instruction form (`instructions.asm`), ALASM 5.09's constructs, STORM 1.3 (STORMT1/2), ZAsm 3.15 (ZXT1-3), TASM 5.0 (T50PROG) → sjasmplus → ALASM → the alasm codec and back → the ALASM frontend → sjasmplus | the bytes of the original assemblers |
| `inject-oracle.py alasm509 … oracle1.asm 0x6000`: injected into ALASM 5.09, assembled by it | 143 bytes = sjasmplus' |
| the same with `instructions.asm` at ORG #6000 | 1173 bytes = sjasmplus' |
| `inject-oracle.py alasm509 --template` (Z1): `zxasm inject` into the .szx, .sna and .z80 templates, the snapshot loaded and assembled | 143 bytes = sjasmplus' in each; `instructions.asm` 1173 |
| `zxasm inject` onto a copy of ALASM's disk, `WORK ORACLE1` and `ASSEMBLE` in ALASM 5.09 | 143 bytes = sjasmplus' |

## 6. Limits and open items

- Snapshots and disks (Z1): `zxasm inject` writes the same text into an idle template snapshot (.szx / .sna / .z80) or
  onto a disk image of any format (unreal-disk); ALASM 5.09 assembled `oracle1.asm` from each to sjasmplus' bytes
  (asm-synchronizer.md §19).
- One text. A project's INCLUDEd files would go into more pages (ALASM keeps one text per page). Not built yet.
- Label values come only from sjasmplus sources. Another dialect's source is written without them: its later labels
  stay as they are, with a warning.
- A comparison used as a value (not in IF / WHILE) has no ALASM form: folded where the values are known, else a
  warning.
- The idle rule is known for 5.09 only. The other builds refuse injection unless `force` is given.
- The ALASM frontend reads `ADD B` as `ADD A,B` and `DW "AB"` as the word #4142. ALASM 5.09 does neither (§3).
  Recorded in TODO.
