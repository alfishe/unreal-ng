# unreal-asm: sjasmplus → ZX-ASM / ZAsm 3.15 (part B, Z3)

| | |
|---|---|
| **Date** | 2026-10-11 |
| **Code** | `src/dialects/zxasm/zxasmbackend.cpp` (the `zxasm` backend, on the `RetroWriter` core), `src/sync/reader.cpp` (the `zasm-3.15` descriptor: idle rule, `LinearParams::resetFrom` / `resetTo`, `PlanInjectLinear`) |
| **Rules from** | research-zxasm-to-sjasmplus.md §2 (ZX-ASM 3.10's manual, ZAsm 3.15 run in unreal-ng), ZAsm 3.15 run in unreal-ng |
| **Checks** | `unreal-asm-tests` (`ZxasmBackend_Test`; with `UNREAL_ASM_SJASMPLUS` a round trip of every instruction form, ZAsm 3.15's own test programs, STORM, XAS and TASM 5.0 programs; `SyncInject_Test.Zasm315TakesAFileAtItsCommandPromptAsLoadLeavesIt`); `tools/verification/unreal-asm/emulator/inject-oracle.py zasm315` |

## 1. Example first

```text
sjasmplus                          ZAsm 3.15
start:  ld a,1+2*3                 START   LD A,1+(2*3)         ; left to right: the product in parentheses
        ld a,high label                    LD A,LABEL.h         ; functions on the operand before them
        ld a,low (label+1)                 LD A,0+(LABEL+1).b   ; "(" first would read as memory
        ld bc,-label                       LD BC,LABEL.n
        ld a,~5 / ld a,5^3                 LD A,5.c / LD A,5!3
        IF ext != 5                        IF EXT-5             ; true when not 0
        DUP 2 / nop / EDUP                 REPT 2 / NOP / ENDR
        DISP #9000 ... ENT                 PHASE #9000 ... UNPHASE
        ex af,af' / in f,(c)               EXA / INF
        SAVEBIN "out.C",#6000,143          SAVEOBJ "out.C",#6000,143
```

## 2. What the backend writes

| Source | ZX-ASM text |
|---|---|
| An expression | a chain left to right whose operators each take a term: `+ - * / \ & \| !`; `high` / `low` / `-x` / `~x` / byte swap as the functions `.h` `.b` `.n` `.c` `.e` on a term; `<<` / `>>` as `*2^n` / `/2^n`; an operand that would start with `(` gets `0+` |
| One ZX-ASM computes otherwise (a division of a value outside 0..#FFFF, a comparison, a 16-bit rotation of a computed value) | its value from the source's layout, with a note |
| A label | upper case, letters, digits, `_`, up to 24 characters, no ZX-ASM keyword (its tokenizer stores keywords as tokens) |
| `IF` / `ELSE` / `ENDIF` | decided while converting when the value is known; else `IF x` (true when not 0): `a!=b` as `IF a-b`, `a==b` as `IF a-b` / `ELSE`; comparisons have no ZX-ASM form |
| `DUP n` ... `EDUP` | `REPT n` ... `ENDR` for a body without labels, assignments or blocks (ZX-ASM makes labels local to each pass, sjasmplus does not); else unrolled |
| `WHILE`, `MACRO`, `DEFL` | unrolled, expanded, written as values (the core) |
| `DB` / `DW` / `DS` | `DB` (strings of three or more characters quoted; `"`, `'`, `~` and bytes above `#7F` as numbers: ZX-ASM takes a byte above `#7F` for the start of a comment) / `DW` / `DS n,fill` (ZX-ASM repeats the fill n times: a pattern of several bytes as `DB`) |
| `DISP` / `ENT`, `INCLUDE` / `INCBIN`, `SAVEBIN` | `PHASE` / `UNPHASE`, `INCLUDE "f"` / `INSERT "f"`, `SAVEOBJ` |
| `IN F,(C)`, `SLL`, `EX AF,AF'`, IX / IY halves | `INF`, `SLI`, `EXA`, `XH XL YH YL` |

The ZX-ASM codec reads `zxasm` text in the versions 3.0 / lite / 3.15 (`zxasm convert --to zxasm --to-version 3.15`).

## 3. Writing into ZAsm 3.15 (the injection)

ZAsm keeps one buffer from `(#8829)` to `(#8837)` (asm-synchronizer.md §7.9): a table of words from `#8829` (the
start, the cursor line, the marks, the end), the text from `#884C` in page 2, its part from `#C000` in RAM page 6
(mapped only while ZAsm needs it). LOAD leaves every word but the end at the start. The inject writes the same: the
file from the start (above `#C000` into page 6 whatever is mapped), the end word one past it, the other words at
the start (`LinearParams::resetFrom` / `resetTo`).

The idle rule: at its COMMAND prompt (CS+SS in the editor) its key wait returns through `#C404`, `#E630`, `#E6F0`
(its code runs in the page at `#C000`). `A` there assembles the new text. Back in the editor (ENTER) the screen is
redrawn from the new text; the inject sets the cursor's line number (`#8827`) and column (`#8630`) to 0 as LOAD leaves
them, or the editor would go on from the old line (2026-10-17).

ZAsm keeps no "changed" flag (none found in memory between an edit and SAVE): a text in memory that is not empty and
not the caller's own counts as changed, so the first write into a loaded text needs force.

## 4. Results

| Check | Result |
|---|---|
| `ZxasmBackend_Test.ProgramsKeepTheirBytesThroughZxAsm` (host): every instruction form, ZAsm 3.15's ZXT1-ZXT3, STORM 1.3's STORMT1 / STORMT2, XAS 7.447 `constrct`, 4.18 `cons418`, TASM 5.0's T50PROG → sjasmplus → ZAsm 3.15 → the ZX-ASM codec and back → the ZX-ASM frontend → sjasmplus | the original assemblers' bytes |
| ZAsm 3.15 in unreal-ng, the converted files loaded from its disk with a `SAVEOBJ`: `oracle1.asm` and every instruction form at `#6000` | 143 / 1173 bytes = sjasmplus' |
| `inject-oracle.py zasm315`: the same written at its COMMAND prompt (the 9203-byte text runs into page 6), assembled with `A` | 143 / 1173 bytes = sjasmplus' |
| `expression-fuzz.py zasm --template` in ZAsm 3.15 (2026-10-19): random `DW` expressions (labels above and below, numbers, unary `- ~ high low !`, every binary operator, three levels), seeds 71-85 of 100 lines | all words = sjasmplus' (found on the way, each measured in ZAsm 3.15: its `\|` ORs the low bytes only and keeps the left high byte, `1\|#8000` is 1; its `*` takes the left operand's bits from the top and stops at the first carry with the value so far, `7*#4000` is #8000 (100 products); `(x).c.b` is a syntax error, now `((x).c).b`; those operations are written as their values) |

## 5. Limits

- The editor shows the template's work name (`STUB`) after an inject: SAVE writes under it.
- Every version has an inject rule (asm-synchronizer.md §7.9-§7.10, 2026-10-16). ZX ASM 3.0 itself (codec version
  `3.00`: 3.0's table) has no `&` `|` `!`, no `.` functions, no `REPT`, `EXA`, `OUT (C),0` or `DS n,fill`, each tried
  alone in unreal-ng: the backend writes `high` as `/256`, `-x` / `~x` as `0-x` / `#FFFF-x`, the rest as values, `REPT`
  unrolled, `EX AF,AF'`, bytes and `DB` lists. ZX-ASM 2.x (`2`) the same and no parentheses (their parts as values).
  3.10 has all of them.
- A project: ZAsm's `INCLUDE` reads the disk; an inject writes one text.
- No idle template snapshots for ZAsm yet.
