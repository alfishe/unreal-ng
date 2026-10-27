# unreal-asm: sjasmplus → STORM (part B, Z3)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/storm/stormbackend.cpp` (the `storm` backend), `src/dialects/common/retrowriter.cpp` (the core it shares with the XAS backend), `src/sync/reader.cpp` (the STORM descriptors: idle rule, editor variables, the editor's changed flags), `PlanInject` |
| **Rules from** | STORM's own behaviour as the STORM frontend models it (research-storm-to-sjasmplus.md §2), STORM 1.3's own source (`XED`, `ED`, `MAIN`: LOAD, INIT, PGUP, the key handler), STORM 1.3 and 1.3i run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`StormBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, STORM 1.3's test programs, the XAS oracles, ZAsm and TASM programs; `SyncInject_Test.StormTakesAFileWhileItsEditorWaitsForAKey`); `tools/verification/unreal-asm/emulator/inject-oracle.py storm` |

## 1. Example first

```text
sjasmplus                          STORM 1.3
start:  ld a,1+2*3                 START   LD A,1+2*3
        ld a,high label            LABEL   ...  LD A,LABEL[
        ld a,low (label+1)                 LD A,(LABEL+1)]
        ld bc,10-(4+2)                     LD BC,10-(+4+2)        ; a part after - begins with an operator
        ld hl,-(label*2)*3                 LD HL,(LABEL*2~)*3     ; STORM 1.3 computes 2*3~*2 as -6
        ld hl,(label&7)+1                  LD HL,1                ; STORM 1.3 loses & ! | @ in parentheses: the value
        ld a,~5 & #0F                      LD A,5|#FFFF&#0F       ; | is STORM's XOR, ! its OR
        DUP 3 / ldi / EDUP                 .3      LDI            ; one line repeated
        DISP #9000 ... ENT                 ORG #9000,$ ... ORG $-#9000+#600B
        MACRO WAIT n ... WAIT 5            LD B,5 / M1W DJNZ M1W  ; expanded, its label one per expansion
```

## 2. What the backend writes

| Source | STORM text |
|---|---|
| An expression | STORM's operators with its priorities (postfix `[ ] ~ @` 8 / 6 / 2, `* / \` 7, `<< >>` 5, `+ -` 4, `& ! \|` 3, comparisons 2), parentheses where they differ from the source's; `high x` as `x[`, `low x` as `x]`, `-x` as `x~`, `~x` as `x\|#FFFF`, `!x` as `x@`, `a&&b` as `a@@&(b@@)`; a comparison negated for sjasmplus' -1 (`a&&b` and a negated comparison hold a part in parentheses STORM cannot compute: written as their values, see below); a postfix part on the left of an operator parenthesized (STORM 1.3's `2*3~*2` bug); `x&#FFFF` as `x` (STORM's words are 16 bits) |
| One STORM computes otherwise (a division, a remainder, `>>` or a comparison of a value outside 0..#FFFF, a rotation) or one longer than the line holds | its value from the source's layout (`BackendOptions::labelValues`), with a note |
| A part in parentheses holding `& ! \| @` or a comparison, or an index offset `(IX+d)` with one outside parentheses | its value with a note, else the text with a warning: in parentheses STORM 1.3 computes only `+ - * / \ << >>` and `~ [ ]` (`1+(6&3)` = 7, `1+(0@)` = 1, `1+(1<2)` a syntax error, `(6!1)` "3 Subscript wrong"; checked 2026-10-19 with the lines typed in STORM's editor) |
| A part in parentheses after `- / & \| >> >= >` | `(+...)`: STORM stores such a part beginning with an operator (its own source: `(+C2-KEYTXT)`) |
| A label | upper case, letters, digits, `_` (a letter first: a line starting `_` and a command is a command in column 0), up to 7 characters, unique, no STORM keyword |
| A name assigned with `=` / `DEFL`, `IF`, `WHILE`, `MACRO` | as for XAS (research-sjasmplus-to-xas.md §2), STORM 1.3 having no conditions: a condition known only when assembling is written as taken, with a warning |
| `DUP n` ... `EDUP` | `.n` before the body when it is one line of instructions or data without a label (n up to 256, `.0`), else unrolled |
| `DB` | strings of three characters or more quoted; one or two characters, `"`, control characters, bytes above `#7F` and `@` before hex digit pairs as numbers; a character as its number (STORM 1.3's first pass counts `DB "A"` as two bytes: the labels after it come out too high, its help lists `DB "A"` among the bugs) |
| `$` in a `DB` / `DW` list | one item a line, `$` corrected by the item's offset |
| `DS n,b...` | `DS n,b...` |
| `DISP a` ... `ENT` | `ORG a,$` ... `ORG $-a+place` (the place from the source's layout); a run address with `$` in it under a name of its own (`RUN EQU ...`) |
| `INCLUDE` / `INCBIN` | `INCL "NAME"` / `INCB "NAME"`, the TR-DOS type as the 9th character (`"NAME.B"` → `"NAME    B"`) |
| `IN F,(C)`, `SLL`, `EX AF,AF'` | `INF`, `SLI`, `EXA`; `OUT (C),0` and the undocumented copies (`RLC (IX+d),B`) as bytes |
| Lines | the label, the command from column 8, a comment right after the operands (`ADD HL,DE; (ETEXT)`: STORM's codec reads no blank before it); at most 40 characters: a comment that does not fit goes on a line of its own, cut at 40; a wider statement gets an EQU of its longest expression (`S1 EQU (VALUE_E-VALUE_4-2)*256+#18`, `LD HL,S1`), before the line, or after the line of the last name it holds when one is defined further down (STORM's EQU takes no later label, an instruction takes a later EQU; then without `$`); NextPlayer 8 wide lines to none, the 37 projects 25 to none; Russian letters kept (CP866), other characters as `?` |

## 3. Writing into STORM (the injection)

STORM keeps its text in RAM page 6 (asm-synchronizer.md §7.8): `(#C000)` one past an `#FF` after the text, the name at
`#C002`, an `#FF` at `#C00A`, the text from `#C00B`. It is a one-window editor: there is no command line to wait at,
and the line under the cursor lives in a line buffer. STORM's own LOAD (source `XED`, `LOAD3`) puts `#FF` before the
text, `ABLINE` (the cursor line's address) at its start, `#FF` after it, `ETEXT`, then calls `INIT` (block marks off,
`WNAME` from `TXNAME`, the cursor line into the buffer) and redraws.

The inject writes the page the same way and lets STORM do the rest through its own PGUP (control key 4): with
`ABLINE` on the second line, `NLINE` 1 and the cursor on the first screen line (`PY` = 1), PGUP walks one line back to
the text's start and calls `PSCR0`, which redraws the screen from there and sets `ABLINE`, `AELINE` and the line
buffer. The key is given as the key handler gives one: `LASTK` 4 and bit 0 of `VAR` (`RAWKEY`, which ONGET reads, is
rewritten by the interrupt every frame; `LASTK` only on a new key). Besides: `MBSTAT` 0 (no block, not changed),
`CACHEST` 0, `WNAME` = the name, bit 1 of `VAR+4` (the line buffer is in the text: PGUP does not write it back).

| Variable (STORM 1.3's source) | 1.3 | 1.3i | Found by |
|---|---|---|---|
| `ABLINE`, `NLINE` | `#B8EF`, `#B8ED` | `#B8C5`, `#B8C3` | `SCRP0` (`LD IY,0 / LD (ABLINE),HL ... LD (NLINE),IY`) |
| `MBSTAT`, `WNAME` | `#BF95`, `#AB35` | `#BF95`, `#AB0B` | `INIT2` |
| `CACHEST` | `#BF94` | `#BF94` | `INIT` |
| `VAR` (`PY` = +2, flags +0 / +4) | `#B8E2` | `#B8B8` | `START` (`LD IX,VAR`) |
| `LASTK` | `#BFC2` | `#BFC2` | the main loop `INP4` |

The idle rule: the main loop's `CALL WTKPR` returns to `#87F5` (1.3) / `#87CB` (1.3i), on the stack while the editor
waits for a key (`SyncDescriptor::idleAlternatives`: one descriptor for both). The page has no "changed" field, the
editor's variables do (`FileImageParams::changedVariables`): bit 4 of `MBSTAT` (a line written since LOAD / SAVE, the
`*` STORM shows; SAVE clears it) and bit 1 of `VAR+4` clear (the cursor line typed in, not yet in the text). Either
refuses an inject without force, unless the text is the caller's own (the push watch).

## 4. Results

| Check | Result |
|---|---|
| `StormBackend_Test.ProgramsKeepTheirBytesThroughStorm` (host): every instruction form, STORM 1.3's STORMT1 / STORMT2, XAS 7.447 `constrct`, 4.18 `cons418`, ZAsm 3.15 and TASM 5.0 programs → sjasmplus → STORM → the STORM codec and back → the STORM frontend → sjasmplus | the original assemblers' bytes |
| `inject-oracle.py storm` in STORM 1.3: `oracle1.asm` (sjasmplus constructs, macros, WHILE, DUP, IF), every instruction form at `#6000` and at `#8000` | 143 / 1173 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py storm` in STORM 1.3 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), 15 seeds of 100 lines, half of them written as expressions | all words = sjasmplus' (found on the way: sjasmplus takes a shift count modulo 32, `1<<33` = 2, `14>>224` = 14: the layout and the writers' values gave 0) |
| `expression-fuzz.py storm13i` in STORM 1.3i (2026-10-19): the same, seeds 71-75 | all words = sjasmplus' |
| `inject-oracle.py storm` in STORM 1.3 (2026-10-19): `z5probe.asm` (STRUCT, MODULE, ALIGN, DZ / D24 / DD, IFUSED, DEFINE, EXA / EXD, temporary labels), the real loaders of The Link (stage 1, stage 2, src) and romych's `cmdtest` | 94 / 466 / 1172 / 1477 / 198 bytes = sjasmplus' |
| the same in STORM 1.3i (`inject-oracle.py storm13i`, 2026-10-17) | `oracle1.asm` 143 bytes, every instruction form at `#6000` and at `#8000` (1173) = sjasmplus' |

STORM 1.3i's Quit restarts the machine: the 48K memory the 1.3 check reads after it is empty (an earlier note took
that for lost bytes). While it runs, 1.3i keeps the code it builds in its pages: `#6000`-`#6FFF` in RAM page 7,
`#7000`-`#7FFF` in page 3, `#8000`-`#BFFF` in page 4, `#C000`-`#FFFF` in page 0, each from the page's start;
`storm13i_finish` reads it there.

## 5. Limits

- STORM 1.0beta has an inject rule (2026-10-16): `START`, `INIT` and `INP4` match its code as they are (`LASTK` is the
  system variable `#5C08`), its `SCRP0` has `LD D,0` where 1.3 has `XOR A / LD D,A`; no `INIT2` (no name, no `MBSTAT`).
- An empty text is written, but STORM's screen is not redrawn (PGUP needs a line before the cursor).
- The `*` STORM shows for a changed text stays on screen until STORM prints its status line again.
- A project: STORM holds one text; `INCL` files go to the disk.
- No idle template snapshots for STORM yet.
