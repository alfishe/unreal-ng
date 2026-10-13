# unreal-asm: real sjasmplus projects into the retro targets (part B, Z5)

| | |
|---|---|
| **Date** | 2026-10-12 |
| **Code** | `src/dialects/sjasmplus/sjasmpluslower.cpp` (the lowering every retro backend runs on a sjasmplus program: `RetroWriter`, the ALASM backend), `src/layout/layout.cpp` (STRUCT laid out), `src/dialects/sjasmplus/sjasmplusfrontend.cpp` (DEFINE values, EXA / EXD) |
| **Checks** | `unreal-asm-tests` (`SjasmplusLower_Test`: the layout of STRUCT against sjasmplus' `--sym`, the words each target gets, a probe of every construct through all eleven retro targets and back, `testdata/dialects/sjasmplus/z5probe.asm`); `tools/verification/unreal-asm/checks/projectcheck.py` on real projects |

## 1. Example first

```text
sjasmplus                          TASM 4.12 (as written)
        STRUCT SPR,2               ;STRUCT SPR,2
p       POS                        SPR_P   EQU 2           ; a field: its offset
w       WORD #1234                 SPR_W   EQU 4
        ENDS                       SPR     EQU 12          ; the structure's name is its size
s2      SPR {5,6},#5678            S2      DEFS 2          ; an instance: its data, the values given
                                           DEFB 5 / DEFB 6 / DEFW #5678
                                   S2_W    EQU S2+4        ; ... and label.field
        MODULE main                ;MODULE main
start   LD HL,data                 MAIN_START LD HL,MAIN_DATA   ; the full names
        SHOW                               SHOW_MAIN               ; a macro using the module's names: its copy for the module
        ENDMODULE
        ALIGN 16                           DEFS n                  ; (16-$%16)%16, its value where the target has no %
        DEFINE COUNT 3 / LD B,COUNT        LD B,3
        IFUSED main.helper                 IF 1 (decided)
        DZ "hi" / DC "abc" / D24 / DD      DEFB / DEFW
```

## 2. What is lowered, and where

| Construct | Lowering |
|---|---|
| `MODULE name` / `ENDMODULE` | labels get their full names (`@main.start`, the layout's names); a name used inside the module means the most specific module's that is defined; a mark is left so a macro expanded inside finds the module's names (`kModuleMark`); a macro whose body uses the module's names gets a copy per module (`SHOW_main`), as sjasmplus expands macros as text where they are called |
| `STRUCT name[,offset]` ... `ENDS` | EQU of each field's offset (nested ones `SPR.p.x`) and of the size under the structure's name (sjasmplus: the name is the size, the offset included) |
| an instance (`label NAME values`) | its data: the offset's bytes as zeros, each field's default or the value given (`{}` flattened, an empty value keeps the default, a BLOCK takes none), then EQU of `label.field` |
| `LUA` ... `ENDLUA`, `INCLUDELUA` | left out with a warning (sjasmplus runs the script) |
| `ALIGN n[,fill]` | `DS (n - $ % n) % n[,fill]` (its value from the layout where the target lacks `%`) |
| `DZ`, `DC`, `ABYTE` / `ABYTEC` / `ABYTEZ`, `D24`, `DD` / `DWORD` | DB / DW items |
| `IFUSED` / `IFNUSED` | decided by the names the program uses |
| `SLOT`, `PAGE`, `MMU` | dropped with a note (the targets have one 64K memory) |
| `DEFINE name value` | the value put in where the name stands as a word, in the frontend (the layout and every backend see it) |
| `EXA`, `EXD` | `EX AF,AF'`, `EX DE,HL` (frontend) |
| `DEVICE`, `SAVEBIN`, `SAVESNA` ... `OUTPUT` | dropped with a note (as before) |

The layout lays out STRUCT (it had reported "STRUCT is not laid out" and lost the labels after it): fields take no
memory, an instance its size, and `label.field` gets its address. Checked against sjasmplus 1.21's `--sym` of the
probe (`SjasmplusLower_Test.StructsAreLaidOutAsSjasmplusDoes`).

## 3. Bugs found on the way (fixed)

| Where | Bug | Shown by |
|---|---|---|
| `RetroWriter` macro expansion | a temporary label in a macro body (`1 ... DJNZ 1b`) was looked up among the program's temporary labels: the jump was written as `JR 0` | Wild Commander's `emutrap.asm` (`TrapOUTJR`) |
| `RetroWriter` macro expansion | a string argument (`PAGEFILE #90,"kishki  C"` into `DB name`) became the number of its last two characters | The Link's loader |
| `RetroWriter` macro expansion (sjasmplus) | every label of a body got a name of the expansion's own; in sjasmplus only a `.label` does (`VORG = $` in a macro moves the one VORG) | Wild Commander's `emutrap.asm` |
| ALASM backend | the same string argument bug in its own macro calls; comments with characters outside CP866 failed its codec | the probe |
| TASM backend | a string holding a word that is a TASM keyword (`"needs a"`): TASM's editor stores the word as its token and shows and assembles it in capitals (research-tasm.md §4), so `a` became `A`; those letters are now written as numbers | The Link's loader |
| TASM backend | Russian letters in comments were kept, but TASM 3 / 4 use `#80-#F0` for their tokens: the codec refused the line and `zxasm convert` dropped it **silently**; comments are now ASCII | ZiFi's `zifi_driver.asm` |
| `zxasm convert` | the target codec's errors were not printed and did not fail the run (a line left out without a word) | the same |
| TASM backend | without `--to-version` the backend wrote 4.12 text but the codec encoded it with its newest table (TASM 5.5): `ORG` read back as `RETI` | `zxasm convert --to tasm` |

## 4. Results on real projects (`projectcheck.py`)

The corpus: 122 sjasmplus sources with ORG and one of MODULE / STRUCT / DEVICE / DEFINE / ALIGN / DZ / D24 / DD /
LUA / SAVEBIN / OUTPUT, from Wild Commander (original disassembly, improved), The Link, ZiFi, Cobra, pentevo,
NextPlayer, the Sprinter sources (romych, Flex Navigator), and sjasmplus' examples; Next-only sources left out. 37 of
them assemble alone with sjasmplus (the others are parts of larger builds, or need files the folders lack). Each
went to the target and back, file by file:

| Target | Memory equal | The converted text does not assemble | Bytes differ | Conversion refused |
|---|---|---|---|---|
| ALASM | 22 | 11 | 1 | 3 |
| TASM 4.12 | 16 | 16 | 5 | 0 |
| STORM 1.3 | 12 | 20 | 3 | 2 |
| XAS 7.447 | 12 | 18 | 3 | 4 |

Before the work (TASM): 6 equal. What remains, by cause:
- **TR-DOS names**: an INCLUDE / INCBIN of `wc_header.a80`, `pluginHeader.a80`, `rorat3d wmf.tga.pal` becomes an
  8-character name on the target (a warning); the files of a host project would have to be renamed onto the disk.
- **The check converts the main file alone**: its INCLUDEs stay sjasmplus, so names they define in a module or
  structure are not lowered with it; `--project` converts the folder as one project, which in turn meets the names.
- **DEFL across files**: a name assigned in one file and reassigned with `$` in another (Wild Commander's `VORG`)
  has no value in the second file's conversion.
- **LUA**: sjasmplus' examples build tapes with Lua (`fire.asm`, `3color.asm`): left out by design.

## 5. Open

- A project's files lowered together (MODULE, STRUCT and DEFL across INCLUDEs).
- Renaming a host project's files onto TR-DOS names for the targets that INCLUDE by disk name.
- Macro parameters named like registers (`MACRO T a` ... `LD A,a`): the frontend keeps registers in lower case,
  so `a` cannot be told from `A`.
