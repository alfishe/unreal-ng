# unreal-asm: sjasmplus → XAS (part B, Z3)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/xas/xasbackend.cpp` (the `xas` backend), `src/sync/reader.cpp` (the XAS descriptors: idle rule, LOAD's variables), `PlanInject` |
| **Rules from** | XAS's own behaviour as the XAS frontend models it (research-xas-to-sjasmplus.md §2) and each XAS build run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`XasBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form in three versions, the XAS oracles, STORM, ZAsm and TASM programs; `SyncInject_Test.XasTakesAFileAndItsEditorVariablesAtItsCommandLine`); `tools/verification/unreal-asm/emulator/inject-oracle.py xas` on every build |

## 1. Example first

```text
sjasmplus                          XAS 7.447
        ORG #6000                          ORG   #6000
start:  ld a,1+2*3                 start   LD    A,2*3+1           ; the atom of + moved to the right
        ld hl,label+2                      LD    HL,label+2
        ld a,high label                    LD    A,label/256
        ld a,low (label+1)                 LD    A,label+1*256/256
        ld bc,10-(4+2)                     LD    BC,10-4-2
        ex af,af'                          EX    AF,AF
        DUP 3 / db #55 / EDUP              !ASSM 3 / DB #55 / !CONT
        DISP #9000 ... ENT                 WORK  #9000 ... WORK
        MACRO WAIT n ... WAIT 5            LD    B,5 / m1w DJNZ m1w    ; expanded, its label one per expansion
n = 0 / WHILE n < 3 / db n / n = n+1 / ENDW    DB 0 / DB 1 / DB 2      ; unrolled, the name's values written
```

## 2. What the backend writes

| Source | XAS text |
|---|---|
| An expression | a chain left to right in which every operator takes a number, a name or `$`: `a+(b+c)` as `a+b+c`, `a-(b+c)` as `a-b-c`, `a*(b*c)` as `a*b*c`, an atom of a commuting operator moved to the right; `high x` as `x/256`, `low x` as `x*256/256`, `-x` as `0-x` (or `x*#ffff`), `~x` as `x!#ffff`, `x<<n` as `x*2^n`, `x>>n` as `x/2^n`, a 16-bit rotation as `'L` / `'R` (7.x). These forms work in every version (4.18 has no `&L` / `&H`) |
| One it cannot chain (AND, OR, a remainder, a comparison, a grouping it cannot regroup) | its value from the source's layout (`BackendOptions::labelValues`), with a note; a warning where no value is known |
| A label | upper case, letters, digits, `_ ? @`, unique in its first 7 characters and no XAS keyword (`longname_b` → `longna1`, `ld` → `ld1`); shown in lower case as XAS's editor shows it. sjasmplus' `main.loop` → `main_lo`, temporary labels numbered |
| A name assigned with `=` / `DEFL` | XAS defines a name once: every use gets the value it has then; where that value is only known to XAS (`$` in it) each definition gets a name of its own with `EQU` |
| `IF` / `ELSE` / `ENDIF`, `IFDEF` | decided while converting when the value is known (only the taken branch is written); otherwise `IFNZ` / `IFZ` ... `!CONT` (7.43 / 7.447 only; `ELSE` as `!CONT` and the opposite test) |
| `DUP n` ... `EDUP` | `!ASSM n` ... `!CONT` (9.07m `.ASM` / `.END`); unrolled where XAS's single block level cannot hold it (inside another block, a body with labels, assignments or blocks) and always in 4.18 |
| `WHILE` | unrolled, the condition computed with the names' values as they stand |
| `MACRO` / a call | expanded: the arguments put in (an operand of a call, read by the source's frontend), the body's labels one name per expansion |
| `DB` with strings, `DW` | `DB "text",13` (7.x, 9.x); `DEFB` / `DEFM` lines in 4.x / 5.x (`DEFB` takes no strings there); a string of one or two characters as their bytes; `"`, `;`, `^`, `` ` ``, `{ \| } ~` and control characters as numbers (XAS's font draws the others as Russian letters, research-xas.md §6); a string in `DW` as its value |
| `$` in a `DB` / `DW` list | one item a line, `$` corrected by the item's offset (XAS's `$` is the item's own address) |
| `DS n,b` | `DS n,w` with the byte in both halves of the word (XAS repeats a word); a 2-byte pattern as the word, a longer one as `DB` |
| `DISP a` ... `ENT` | `WORK a` ... `WORK` |
| `INCLUDE` / `INCBIN` | `LTEXT` / `LCODE` (`LOADTEXT` / `LOADCODE` in 4.x / 5.x, `LTXT` / `LCOD` in 9.07m); an `INCBIN` of a part of a file is not converted |
| `IN F,(C)`, `OUT (C),0`, the undocumented `RLC (IX+d),B` copies | their bytes |
| Comments | Russian letters in capitals; А В Е К М Н О Р С Т Х З as the Latin letters XAS draws for them; other characters outside the font as `?` |

Two XAS bugs found by the oracle, written as bytes in the versions that have them: XAS 4.18 and 5.05 / 5.05SE assemble
`LD IY,(nn)` as `LD IX,(nn)` (`DD 2A`); XAS 4.18 assembles `LD LY,n` as `LD HY,n` (`FD 26`). And XAS 4.18's `!ASSM 3`
assembled its block once.

## 3. Writing into XAS (the injection)

XAS keeps its text at `#C000` of RAM page 3 and edits it in place (asm-synchronizer.md §7.11). Unlike ALASM it also
keeps state outside the text: the cursor line, the top line and a flag in instruction operands of its code, and the
text end, which the editor counts from the cursor when it is entered. XAS's LOAD sets them after reading the file
(`#98A4` in 7.447: `LD HL,(#C01F) / LD (top),HL / LD HL,(#C01D) / LD (cursor),HL / LD A,(#C021) / LD (flag),A`); the
text end is set by `LD HL,(cursor) / ... / CPIR / DEC HL / LD (end),HL` (`#82DF`). Both routines are in every build
once, so the inject finds them in the code and sets the four variables as LOAD would
(`FileImageParams::loadVariables`, `InjectPlan::pokes`):

| Build | cursor | top | flag | end |
|---|---|---|---|---|
| 4.18 | `#8093` | `#84B4` | `#8C45` | `#846C` |
| 5.05, 5.05SE | `#8095` | `#84B7` | `#8C5C` | `#846F` |
| 7.43c | `#8097` | `#84C3` | `#8CF9` | `#8478` |
| 7.447 | `#8097` | `#84E0` | `#8D19` | `#8495` |
| 9.07m | `#8092` | `#84BB` | `#8C33` | `#8470` |
| 9.10 | `#8092` | `#84BA` | `#8C56` | `#846F` |

The text is written only at XAS's command line (EXT; 9.x need the keys held 12 frames): from there XAS goes back to
the editor through the routine that reloads everything from those variables. The editor itself is busy (its cursor
line is a pointer into the text). The idle rule is the command line's return chain, read on the stack in each build
at the command line and in the editor (`SyncDescriptor::idleChain`):

| Build | Line input's return, command handler's | Editor's word there |
|---|---|---|
| 4.18 | `979F 8D85` | `8BE9` |
| 5.05, 5.05SE | `97A8 8D9E` | `8BFD` |
| 7.43c | `97D8 8E3B` | `8C9F` |
| 7.447 | `9DDC 8E5B` | `8CBF` |
| 9.07m | `95B5 8D7B` | `820C` |
| 9.10 | `9552 8D98` | `820B` |

## 4. Results

| Check | Result |
|---|---|
| `XasBackend_Test.ProgramsKeepTheirBytesThroughXas` (host): every instruction form as XAS 7.43, 4.18 and 9.10 text, XAS's own constructs (7.447 `constrct`, 4.18 `cons418`), STORM 1.3, ZAsm 3.15 and TASM 5.0 programs → sjasmplus → XAS → the XAS codec and back → the XAS frontend → sjasmplus | the original assemblers' bytes |
| `inject-oracle.py xas` in XAS 4.18, 5.05, 5.05SE, 7.43c, 7.447, 9.07m, 9.10: `oracle1.asm` (sjasmplus constructs, macros, WHILE, DUP, IF) | 143 bytes = sjasmplus' in each |
| the same with every instruction form at `#6000` | 1173 bytes = sjasmplus' in each |
| `expression-fuzz.py xas` in XAS 7.447 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-77 of 100 lines, 17-39 a seed written as expressions | all words = sjasmplus' (XAS takes more than 3 s for 100 such lines: the oracle's default wait read the memory before the end, the target waits 10 s) |

## 5. Limits

- A project: XAS holds one text, so the files a source INCLUDEs go as `LTEXT` to the disk (`zxasm inject` onto a disk
  writes them there); an inject of a project puts the main text into memory and the others onto the disk in the drive
  (2026-10-18; into a snapshot: `zxasm inject --disk`).
- A condition known only when XAS assembles works in 7.43 / 7.447 only (`IFNZ` / `IFZ`), and outside other blocks.
- Labels inside an expanded macro or an unrolled block have no layout value: an expression XAS cannot chain that uses
  one is not converted.
- No idle template snapshots for XAS yet (ALASM 5.09 has them, Z1).
