# unreal-asm: ZX-ASM → sjasmplus (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06; ENDA, `~text~` 2026-10-08 |
| **Code** | `src/dialects/zxasm/zxasmfrontend.cpp` (the `zxasm` frontend), the sjasmplus backend (`IFUSED` with ZX-ASM's meaning, `SAVEBIN`, `$$$`), `IFrontend::ParseInProject` (macros from the other files of a project) |
| **Syntax source** | ZX-ASM 3.10's manual (`compile.t` on the [3.10 disk](https://zxart.ee/releasefile/id:249317/ZASM_310.ZIP)) and the 3.3 ReadMe ([Z33_F9](https://vtrd.in/system/Z33_F9.zip)) |
| **Oracle** | ZAsm 3.15 ([ZASM315](https://zxart.ee/releasefile/id:249318/ZASM315.zip)) in unreal-ng: four test programs it assembled and saved with `SAVEOBJ` equal their conversions byte for byte; ZAsm 3.2x ([Z32X](https://zxart.ee/releasefile/id:249319/Z32X.zip)) on a Pentagon 512 for `ENDA` and `~text~` (§3.1), the `ENDA` probe also in 3.3 Final and 4.20 |
| **Checks** | `unreal-asm-tests` (`ZxasmFrontend_Test`, with `UNREAL_ASM_SJASMPLUS` the oracle programs too); `docs/inprogress/2026-10-05-unreal-asm/scripts/emulator/assemble-in-emulator.py zasm315` |

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
| `DB` texts in `"`, `'` or `~` (`~` through the XLAT table, §3.1); `DC` sets bit 7 of each text's last character (checked); `DS n,a,b` repeats the fill sequence n times (checked: 6 bytes); `DBW` (3.3) is `DB` + `DW` | `~text~` as the bytes the table gives (`DB #80,#81`), one character as a number |
| `ORG addr[,page]`; `PHASE` nests 5 deep and `UNPHASE` returns to the outer `PHASE` where it would be now (checked: `#C006` after the inner part) | `ORG`; nested `PHASE`: the outer address and the physical one kept in labels, `ENT`, `DISP`; `UNPHASE` back: `ENT`, `DISP outer+($-physical)` (sjasmplus does not nest `DISP`) |
| Macros: `NAME macro[:body]...endm`, parameters `=1`..`=n`, a parameter left out keeps the previous call's value (help: `FILL ,,6912,0`; checked), `IFP` asks whether parameters were passed, `EXITM` ends the expansion, labels inside are local to each call (checked); a macro must be defined before its call, often in a definitions file the sources `INCLUDE` (`ovldef`) | expanded at every call (the kept parameters make every call different); the definitions of the other project files are known (`ParseInProject`) |
| `REPT n ... ENDR`, labels local to each pass (checked); `REPL n` repeats the rest of the line | `DUP`; with labels and a count known when converting, written out pass by pass |
| `IF expr` (not 0), `IFDEF` / `IFNDEF`, `IFUSED X` / `IFNUSED X`: X used and **not defined so far** (help: libraries of routines compiled only when called; a routine the program defines itself, or takes from a label file, stays out) | `IF exist X`; `IFUSED` plus a `DEFINE` every definition of X sets (`__UNREALASM_DEF_X`), the answer in the redefinable label `__UNREALASM_IFU` |
| `INCLUDE "f1","f2"` and `INSERT` with drive letters; ZAsm shows a name with the type letter and the two bytes of the catalog start as extension (`ovlib.asm`, `A315.lbl`); a TR-DOS name may hold a dot itself (`a2.5_1`); `INSERT` copies the file's bytes (checked: no sector tail) | `INCLUDE` names the project file (the whole name when a project file has it, else without the extension); `INSERT` keeps the name, `zxasm convert` finds `FONT.fn1` as `FONT` of type `f` |
| `SAVEOBJ "file"[,start[,length]]`: start defaults to the last `ORG`, length to `$`-start | `SAVEBIN` (with `DEVICE`) |
| `ENT` (run address), `CREATE` (room for `$labels`) | comments |
| `LOADTAB "file"`: the XLAT table for `~text~` from then on (§3.1) | a comment; the table read from the project's data files (`BackendOptions::dataFiles`; `zxasm convert` takes them from the image) |
| `ENDA`: one assembly ends, the next starts after it with an empty label table (§3.1) | every part a `LOCAL` block: the backend keeps the parts' labels apart (`A1__L1`, `A1__L2`) |
| `MAKELAB`, `LOADOBJ`, `CHD`, `PROJECT`, `PUBLIC` | kept as text with a warning |

## 3. Oracle results (ZAsm 3.15 in unreal-ng)

| Program | Content | Result |
|---|---|---|
| ZXT1 (122 bytes) | postfix functions, unsigned division, IBM numbers, `DC`, `DS`, lists, `jrz` forms, `(bc)` ports, `exa`, `sli`, `xh` / `yl`, `REPT`, `REPL`, a macro reusing a parameter, nested `PHASE` | equal |
| ZXT2 (4 bytes) | labels in a macro called twice | equal |
| ZXT3 (10 bytes) | labels in `REPT` passes | equal |
| ZXT4 (320 bytes) | `IF` / `IFDEF` / `IFNDEF` / `IFUSED` / `IFNUSED`, `.m`, comments without `;`, `$` per statement, `INCLUDE`, `INSERT` of a 300-byte file | equal |

### 3.1 ENDA and `~text~` (ZAsm 3.2x on a Pentagon 512, 2026-10-08)

ZAsm 3.2x and later use the last 128K of a larger memory (3.2x ReadMe: "more than 128 kb, preferably 512-1024"): on a
128K machine they hang on their title, on a Pentagon 512 they run (`assemble-in-emulator.py zasm315 --ram 512`).

| Program | Content | Result |
|---|---|---|
| `enda1` (32 bytes) | `A1` and forward references before `ENDA`; after it `A1` defined again, forward references to `C1` / `B2` | ZAsm 3.2x, 3.3 Final and 4.20 build the same bytes; the conversion equal |
| (no file) | a label defined before `ENDA` used after it | ZAsm stops with an error on that line: the label table is empty |
| `xlt1` | `LOADTAB` of a 256-byte table (byte = code + 1), `~AB~`, `~a~`, `~A~+1` | equal: each character replaced by the table's byte, `~A~` a value |
| `xlt2` | `~AB~` before any `LOADTAB` (`#80 #81`), then an 80-byte table | equal: without `LOADTAB` ZAsm's own table applies; a code beyond a short table reads its sector's slack (0) |
| `xlt3` | `` ~0AZaz[]^_`{\|}#~ ``, `~АЯаяёЁ~` | equal: ZAsm's own table maps Latin to CP866 Russian and back, the rest unchanged |

ZAsm's own table is the same 256 bytes in the binaries of 3.10, 3.15, 3.2x, 3.3.05, 3.3.51, 3.3 Final, 3.4.04, 3.80.4,
4.0 x8, 4.20 and Lite 1.07 (ZX-ASM 2.4 / 2.6 have none); the frontend keeps a copy (`kXlat`). Testdata
`dialects/zasm32x/`.

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
| `~text~` and `LOADTAB` | done (2026-10-08, §3.1): ZAsm's own table, `LOADTAB` from the project's data files; a `LOADTAB` file the project lacks leaves `~text~` untranslated with a warning |
| `MAKE` (assemble into a disk file) | converted (2026-10-07): `ORG` and, where the section ends (the next `MAKE` / `ORG` / the end), `SAVEBIN` of it; checked in ZAsm 3.15: the label on the `MAKE` line keeps the address before it, the section's code goes only into the file (`zmk`: mk1.C, mk2.C and the SAVEOBJ file equal) |
| `ENDA` | done (2026-10-08, §3.1); ZAsm 3.15 refuses it (it comes with 3.2x). A part that `INCLUDE`s a label file `MAKELAB` wrote (the 3.2x ReadMe's use) needs that file in the project |
| ZX-ASM 2.x | the frontend reads its sources like 3.x (same syntax per its ReadMe); no 2.x oracle |
