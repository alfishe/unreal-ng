# unreal-asm: ZX-ASM → sjasmplus (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Code** | `src/dialects/zxasm/zxasmfrontend.cpp` (the `zxasm` frontend), the sjasmplus backend (`IFUSED` with ZX-ASM's meaning, `SAVEBIN`, `$$$`), `IFrontend::ParseInProject` (macros from the other files of a project) |
| **Syntax source** | ZX-ASM 3.10's manual (`compile.t` on the [3.10 disk](https://zxart.ee/releasefile/id:249317/ZASM_310.ZIP)) and the 3.3 ReadMe ([Z33_F9](https://vtrd.in/system/Z33_F9.zip)) |
| **Oracle** | ZAsm 3.15 ([ZASM315](https://zxart.ee/releasefile/id:249318/ZASM315.zip)) in unreal-ng: four test programs it assembled and saved with `SAVEOBJ` equal their conversions byte for byte |
| **Checks** | `unreal-asm-tests` (`ZxasmFrontend_Test`, with `UNREAL_ASM_SJASMPLUS` the oracle programs too); `tools/unreal-asm/assemble-in-emulator.py zasm315` |

## 1. Example first

```text
ZX-ASM                              sjasmplus
        ld a,10+20*2                LD A,(10+20)*2            ; no priorities: 60
        dw #1200+#34.b              DW #1200+(#34&#FF)        ; a function acts on the operand before it
        jrz Loop:callnz Sub         JR Z,Loop / CALL NZ,Sub
        push af,bc,de               PUSH AF / PUSH BC / PUSH DE
        in a,(bc)                   IN A,(C)
FILL    macro:ld hl,=1:ld bc,=2:endm   (expanded at each call)
        FILL #4000,6912             LD HL,#4000 / LD BC,6912
        FILL ,768                   LD HL,#4000 / LD BC,768   ; a parameter left out keeps the last call's value
Print      Вход: A - код символа    Print  ; Вход: ...        ; Russian text is a comment without ";"
        saveobj "a:out.C",#8000,100 SAVEBIN "out.C",#8000,100
```

## 2. ZX-ASM facts the conversion relies on

| Fact | Conversion |
|---|---|
| Statements separated by `:`, a label in column 0, `Label:` is `Label` (marked for MAKELAB); a label starts with a letter, `_` or `$` | as such; column 0 holding anything else is documentation typed in the editor: kept as text |
| A comment after `;`, or from the first character outside ASCII (help: Russian comments and pseudographics need no `;`; checked: `nop  Комментарий`) | comment |
| Left to right without priorities, parentheses first (`10+20*2` = 60, checked); `+ - * / \` (remainder), `!` XOR, `\|` OR, `&` AND; 16-bit unsigned words (`(0-1)/2` = #7FFF, checked) | parentheses by tree; `expressionBits = 16`, unsigned |
| Numbers `123`, `#FF`, `%101`, `12h`, `0ffh`, `101b`; `"A"` / `'A'` a character; `$` the address of the statement (checked: `ld a,1:ld b,$-Print` uses the second statement's address) | numbers keep their spelling |
| Postfix functions on the operand right before them (checked: `#1200+#34.b` = #1234, `10+20.h` = 10): `.b` low byte, `.h` high byte, `.e` bytes swapped, `.l` / `.r` 8-bit rotation, `.L` / `.R` 16-bit rotation, `.c` CPL, `.n` NEG, `.s` bit 7 set, `.m` the word at that address while assembling | `&#FF`, `high`, swap, rotations spelled out, `^#FFFF`, `-x&#FFFF`, `\|#80`, `{x}` (with `DEVICE`) |
| `jrz` / `jpnc` / `callnz` / `retc`: the condition written with the instruction | `JR Z,...` |
| `PUSH`, `POP`, `INC`, `DEC` take lists; `exa` is `EX AF,AF'`; `(bc)` is `(c)` in `IN` / `OUT`; `sli`, `xh xl yh yl`, `out (c),0` | one instruction each; the standard names |
| The index offset is a byte: `(iy+#fe)` is `(iy-2)` | negative offset |
| `DB` texts in `"`, `'` or `~` (`~` translated by the LOADTAB table); `DC` sets bit 7 of each text's last character (checked); `DS n,a,b` repeats the fill sequence n times (checked: 6 bytes); `DBW` (3.3) is `DB` + `DW` | `~text~` kept with a warning |
| `ORG addr[,page]`; `PHASE` nests 5 deep and `UNPHASE` returns to the outer `PHASE` where it would be now (checked: `#C006` after the inner part) | `ORG`; nested `PHASE`: the outer address and the physical one kept in labels, `ENT`, `DISP`; `UNPHASE` back: `ENT`, `DISP outer+($-physical)` (sjasmplus does not nest `DISP`) |
| Macros: `NAME macro[:body]...endm`, parameters `=1`..`=n`, a parameter left out keeps the previous call's value (help: `FILL ,,6912,0`; checked), `IFP` asks whether parameters were passed, `EXITM` ends the expansion, labels inside are local to each call (checked); a macro must be defined before its call, often in a definitions file the sources `INCLUDE` (`ovldef`) | expanded at every call (the kept parameters make every call different); the definitions of the other project files are known (`ParseInProject`) |
| `REPT n ... ENDR`, labels local to each pass (checked); `REPL n` repeats the rest of the line | `DUP`; with labels and a count known when converting, written out pass by pass |
| `IF expr` (not 0), `IFDEF` / `IFNDEF`, `IFUSED X` / `IFNUSED X`: X used and **not defined so far** (help: libraries of routines compiled only when called; a routine the program defines itself, or takes from a label file, stays out) | `IF exist X`; `IFUSED` plus a `DEFINE` every definition of X sets (`__UNREALASM_DEF_X`), the answer in the redefinable label `__UNREALASM_IFU` |
| `INCLUDE "f1","f2"` and `INSERT` with drive letters; ZAsm shows a name with the type letter and the two bytes of the catalog start as extension (`ovlib.asm`, `A315.lbl`); a TR-DOS name may hold a dot itself (`a2.5_1`); `INSERT` copies the file's bytes (checked: no sector tail) | `INCLUDE` names the project file (the whole name when a project file has it, else without the extension); `INSERT` keeps the name, `zxasm convert` finds `FONT.fn1` as `FONT` of type `f` |
| `SAVEOBJ "file"[,start[,length]]`: start defaults to the last `ORG`, length to `$`-start | `SAVEBIN` (with `DEVICE`) |
| `ENT` (run address), `CREATE` (room for `$labels`) | comments |
| `MAKE`, `MAKELAB`, `LOADTAB`, `LOADOBJ`, `CHD`, `PROJECT`, `PUBLIC`, `ENDA` | kept as text with a warning |

## 3. Oracle results (ZAsm 3.15 in unreal-ng)

| Program | Content | Result |
|---|---|---|
| ZXT1 (122 bytes) | postfix functions, unsigned division, IBM numbers, `DC`, `DS`, lists, `jrz` forms, `(bc)` ports, `exa`, `sli`, `xh` / `yl`, `REPT`, `REPL`, a macro reusing a parameter, nested `PHASE` | equal |
| ZXT2 (4 bytes) | labels in a macro called twice | equal |
| ZXT3 (10 bytes) | labels in `REPT` passes | equal |
| ZXT4 (320 bytes) | `IF` / `IFDEF` / `IFNDEF` / `IFUSED` / `IFNUSED`, `.m`, comments without `;`, `$` per statement, `INCLUDE`, `INSERT` of a 300-byte file | equal |

Found on the way: ZAsm 3.15 has `DBW` among its tokens but its compiler refuses it (3.3 and later). Fixed on the way:
the tasm and zxasm detectors took screens and magazine texts for sources (TASM saves type `A` only; ZAsm 3.10+
keeps two extension characters in the start field, so a start that is no such pair is no ZAsm text).

## 4. The corpus

The ZX-ASM disks of the collection (73 images, research-zxasm.md §6): `roundtrip.py --assemble` converts 582 files with
0 round-trip differences; 200 of 445 main sources assemble alone. Most of the others `INCLUDE` the overlay definitions
`ovldef` that live on the ZAsm program disk, not on theirs (177), are the program documentation ZAsm keeps as sources
(`ReadMe`, `About`, `compile.t`), are project parts, or are texts of other assemblers saved in the ZX-ASM format.

## 5. Open items

| Item | Note |
|---|---|
| `~text~` and `LOADTAB` | the XLAT table is a file the source loads; not applied |
| `MAKE` (assemble into a disk file) | sjasmplus `OUTPUT`; not converted yet |
| `ENDA` (end the assembly here, start again after it) | kept as text |
| ZX-ASM 2.x | the frontend reads its sources like 3.x (same syntax per its ReadMe); no 2.x oracle |
