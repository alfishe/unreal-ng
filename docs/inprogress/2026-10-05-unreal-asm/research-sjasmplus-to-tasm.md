# unreal-asm: sjasmplus → TASM 4.12 (part B, Z3)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/tasm/tasmbackend.cpp` (the `tasm` backend, versions 4.12 / 4.0 / 3 by `targetVersion`), the `RetroWriter` core, `src/sync/reader.cpp` (the `tasm-4.12` descriptor: idle rule, the form LOAD leaves, the changed flag, the work name) |
| **Rules from** | TASM 4.12's help (`tasm412-hypertext`, unpacked from `tasm.ovl`), research-tasm-to-sjasmplus.md §2, TASM 4.12 run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`TasmBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, STORM, XAS, ZAsm and TASM 5.0 programs; `SyncInject_Test.Tasm412TakesAFileAtItsCommandLineAsLoadLeavesIt`); `tools/verification/unreal-asm/emulator/inject-oracle.py tasm412` |

## 1. Example first

```text
sjasmplus                          TASM 4.12
start:  ld a,1+2*3                 START   LD      A,1+(2*3)      ; left to right: the product in parentheses
        ld hl,(1+2)*3                      LD      HL,1+2*3       ; left to right is the source's grouping
        ld a,high label                    LD      A,LABEL/256    ; 4.12 has no { } (error 8)
        ld a,low (label+1)                 LD      A,LABEL+1&255
        ld bc,-label                       LD      BC,0-LABEL
        ld hl,lab<<2                       LD      HL,LAB*4
        IF ext == 5 ... ENDIF              .IF     EXT-5 ... .ENDIF   ; .IF assembles its part when the value is 0
        DISP #9000 ... ENT                 .PHASE  #9000 ... .UNPHASE
        DUP 2 / nop / EDUP                 NOP / NOP                ; no repeat directive: unrolled
```

## 2. What the backend writes

| Source | TASM text |
|---|---|
| An expression | a chain left to right whose operators each take a term (a number, a name, `$` or a part in parentheses): `+ - * / & \|` (4.0 also `!` XOR); `-x` as `0-x`, `~x` as `#FFFF-x`, `x<<n` / `x>>n` as `x*2^n` / `x/2^n`; `high` / `low` as `/256` / `&255` (4.12, 3.x, 2.0; 4.0 the postfix `{ }`, which 3.0 refuses with error 8) |
| One TASM computes otherwise (a division of a value outside 0..#FFFF, a comparison, a remainder, XOR in 4.12) | its value from the source's layout, with a note |
| An operand that would start with `(` | `0+(...)`: TASM reads such an operand as memory (its 4.12 article) |
| A label | upper case, letters, digits, `_`, up to 24 characters (4.12 keeps any length); 7 in 4.0 / 3.x / 2.0 (4.0's error 1 "label field over 7 characters", its manual and seen in unreal-ng), no TASM keyword (its tokenizer stores a keyword as a token wherever it stands) |
| `IF` / `ELSE` / `ENDIF` | decided while converting when the value is known; else 4.12's `.IF` (its part assembled when the value is 0: `a==b` as `.IF a-b`, a plain value as `.IF x` / `.ELSE`), one level (TASM's do not nest); comparisons have no TASM form |
| `DUP`, `WHILE`, `MACRO`, `DEFL` | unrolled, expanded, written as values (the core; 4.12's `DEFMAC` is not used) |
| `DB` / `DW` / `DS` | `DEFB` (strings of three or more characters quoted, 4.12; 3 / 4.0 `DEFM`) / `DEFW` / `DEFS n,fill` (TASM repeats the fill n times: a pattern of several bytes as `DEFB`) |
| `DISP` / `ENT`, `INCLUDE` / `INCBIN` | `.PHASE` / `.UNPHASE`, `.INCLUDE NAME` / `.INCBIN NAME` (no quotes; 3 / 4.0 without the dot) |
| `IN F,(C)`, `OUT (C),0`, `SLL`, the undocumented copies | bytes in 4.12 ("they are not used that often, DEFB will do", its help); 4.0 `INF`, `SLI` |
| `LD HX,LX`, `LD HX,HX` (and IY) | bytes: TASM 4.12 assembles them with two prefixes (`DD DD 65`, seen in unreal-ng); the other `HX LX HY LY` forms as such |
| Lines | the label in column 0, the command from column 8, the operands from column 16, Russian letters kept in comments (CP866) |

## 3. Writing into TASM 4.12 (the injection)

TASM keeps its text in a gap buffer (asm-synchronizer.md §7.5), but only once its editor has been entered: LOAD (`W`)
puts the file as saved, its `FF FF` end record included, at the text start `(#8F59)` = `#A6EE` and leaves the other
pointers, the line count `#94D9` and the cursor line `#94CE` at 0; Edit builds the gap buffer from it. The inject
writes that form (byte by byte: the text runs from the `#8000` window into the page at `#C000`), and the reader takes
it too (`GapBufferParams::loadedForm`).

| What | Where | Set to |
|---|---|---|
| the file | `#A6EE` up | the file with its `FF FF` |
| top, gap start, gap end, line count, cursor line | `#8F5B`, `#8F5D`, `#8F5F`, `#94D9`, `#94CE` | 0 |
| changed since LOAD / SAVE | `#9BA6` (`#FF` after an edit, 0 after LOAD and SAVE; TASM asks to save before it drops a changed text) | `#FF` (`InjectOptions::markChanged`) |
| the work name | `#9C10` (8 characters, in the menu's `Work name:` text) | the title: Assemble takes the text in memory under it (without one it asks for a file to load) |

The idle rule: at `TASM128>` its key wait returns through `#9CE9`, `#80BC` (the editor: `#9EA8`, `#80BC`). An inject
is refused without force when `#9BA6` says the text changed and it is not the caller's own text.

TASM's stack is at `#617C`: code assembled from `#6000` overwrites it (the result changes from run to run, a large
one resets the machine). The checks use `#6200`.

## 4. Results

| Check | Result |
|---|---|
| `TasmBackend_Test.ProgramsKeepTheirBytesThroughTasm` (host): every instruction form, STORM 1.3's STORMT1 / STORMT2, XAS 7.447 `constrct`, 4.18 `cons418`, ZAsm 3.15 and TASM 5.0 programs → sjasmplus → TASM 4.12 → the TASM codec and back → the TASM frontend → sjasmplus | the original assemblers' bytes |
| TASM 4.12 in unreal-ng, the converted files loaded from its disk: `oracle1.asm` and every instruction form at `#6200` | 143 / 1173 bytes = sjasmplus' |
| `inject-oracle.py tasm412`: the same written at its command line, assembled with `A` | 143 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py tasm` in TASM 4.12 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 61 and 71-77 of 100 lines, 31-62 a seed written as expressions | all words = sjasmplus' |

## 5. Limits

- TASM 4.12's own SINUS-style sources (a macro calling itself, steered by values carried from its first pass to its
  second with `.IF PASS`) do not survive the expansion: they stay in TASM's own form.
- A project: TASM's `.INCLUDE` reads the disk; an inject writes one text.
- Every version has an inject rule (asm-synchronizer.md §7.5-§7.7). What the older ones refuse, each seen in unreal-ng
  with the injected `oracle1.asm` and written around by the backend: 4.0 / 4.4 a label of more than 7 characters
  (error 1) and `DEFS n,fill` (pass 1 never ends: the fill goes as `DEFB`); 3.x and 2.0 also the postfix `{ }`
  (error 8: `/256`, `&255`) and `HX` / `LX` / `HY` / `LY` (error 5: bytes). 2.0 takes 3.x's language as plain text.
- No idle template snapshots for TASM yet.
