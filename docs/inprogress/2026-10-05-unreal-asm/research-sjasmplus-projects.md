# unreal-asm: real sjasmplus projects into the retro targets (part B, Z5)

| | |
|---|---|
| **Date** | 2026-10-12 |
| **Code** | `src/dialects/sjasmplus/sjasmpluslower.cpp` (the lowering every retro backend runs on a sjasmplus program: `RetroWriter`, the ALASM backend), `src/core/dialects.cpp` (`ConvertProject` joins a sjasmplus project for them, `ProjectDiskNames`), `src/layout/layout.cpp` (STRUCT laid out, line addresses), `src/dialects/sjasmplus/sjasmplusfrontend.cpp` (DEFINE values, EXA / EXD, the syntax of §6), `tools/zxasm` (`convert --project`) |
| **Checks** | `unreal-asm-tests` (`SjasmplusLower_Test`: the layout of STRUCT against sjasmplus' `--sym`, the words each target gets, a probe of every construct through all eleven retro targets and back, `testdata/dialects/sjasmplus/z5probe.asm`, the syntax of §6, a project joined and cut, a long file cut into parts, disk names); `tools/verification/unreal-asm/checks/projectcheck.py --project` on real projects (§5) |

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

## 3. Bugs found on the way (the lowering, fixed)

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

## 4. A project written as sjasmplus reads it

sjasmplus reads an INCLUDE as text: a MODULE, a STRUCT, a name assigned with DEFL, a macro, a label renamed for the
target, all reach across the files. Lowered file by file they did not (VORG assigned in Wild Commander's `emu.asm` and
reassigned in `emutrap.asm`'s macros; `INFLATE_VARS` defined in `inflate.asm`, expanded in `unzip.asm`; `in` renamed
`IN1` in `BasicLib.asm`, used as `in` in the example). `ConvertProject` now joins a sjasmplus project for every backend
that lowers (`IBackend::JoinsSjasmplusProject`, the eleven retro ones):

- each main source (a file no other INCLUDEs) is written out with the text of every file it INCLUDEs (outside macro
  bodies) put after the INCLUDE line, between comment marks `UNREALASM@FILE:n` / `UNREALASM@END:n`;
- the joined text is laid out, lowered and written as one source, then cut at the marks into its files again; the
  INCLUDE lines stay, written by the backend with the files' names on the target's disk;
- diagnostics and labels get their file and line back; a file INCLUDEd twice and written otherwise the second time
  (another module) becomes a copy of its own (`NAME2`), its INCLUDE naming it;
- `BackendOptions::splitLines`: a file longer than that is cut into parts INCLUDEd where they stood, at lines outside
  any block (MACRO, DUP, WHILE, IF, STRUCT, LUA, DISP): `zxasm` turns it on when the target's format cannot hold a file
  (ALASM's 64K: NextPlayer becomes `NextPlay`, `NextPla2`, `NextPla3`).

`zxasm convert <main> --project --to <target> -o <dir>` takes a host project (`HostProject`: every INCLUDE relative to
the folder of the file that INCLUDEs it, `\` as a folder separator as sjasmplus takes it), the files its INCBINs name,
and for a retro target names every file for a TR-DOS disk (`ProjectDiskNames`: no folders, 8 characters, unique in
letters and digits: `_spg/download.tga.pix` -> `download`, `menu.tga.pal` / `menu.tga.pix` -> `menu` / `menu2`); the
INCLUDEs and INCBINs are written with those names (`BackendOptions::fileNames`), the renaming is listed, the codec's
errors are printed (a line a codec cannot hold was left out without a word).

The layout gives each line's address itself (`LayoutResult::lines`, and the lines of a macro a file's line expands),
where it had put a local label into the text: a line before the first label had none, and after an INCLUDE the
label's parent was the INCLUDEd file's last label, which the writer of the file could not know.

## 5. Results on real projects (`projectcheck.py --project`)

The corpus: 122 sjasmplus sources with ORG and one of MODULE / STRUCT / DEVICE / DEFINE / ALIGN / DZ / D24 / DD /
LUA / SAVEBIN / OUTPUT, from Wild Commander (original disassembly, improved), The Link, ZiFi, Cobra, pentevo,
NextPlayer, the Sprinter sources (romych, Flex Navigator), and sjasmplus' examples; Next-only sources left out. 37 of
them assemble alone with sjasmplus. Each went to the target as a project (`zxasm convert --project`) and back
(`zxasm convert <dir>`); the 64K memory sjasmplus builds from both must be equal:

| Target | Memory equal | Before this work | What differs |
|---|---|---|---|
| TASM 4.12 | 30 of 37 | 6 | the Lua ones (4), RELOCATE (1), TASM's INCBIN sector slack (2) |
| ALASM 5.09 | 29 | 22 | the Lua ones (4), RELOCATE (1), a macro mixing local and global labels (3) |
| STORM 1.3 | 32 | 12 | the Lua ones (4), RELOCATE (1) |
| XAS 7.447 | 32 | 12 | the Lua ones (4), RELOCATE (1) |

What is left, by cause:
- **Lua** (`3color.asm`, `fire.asm`, both BasicLib examples): the bytes come from Lua scripts sjasmplus runs (BasicLib's
  `LINE` / `LEND` / `NUM` macros are Lua). Left out with a warning: converting them needs a Lua interpreter with
  sjasmplus' `sj.*` API in the converter.
- **RELOCATE_START / RELOCATE_TABLE** (`relocation_main.asm`): sjasmplus builds the relocation table itself; not
  emulated yet (the layout would have to tell which word values depend on a label inside the block).
- **TASM's INCBIN** loads whole TR-DOS sectors: the rest of the last sector lands after the file (Flex Navigator
  includes several files at one ORG, a shorter one after a longer: TASM overwrites the longer one's tail, sjasmplus
  does not). A property of TASM on a disk, not of the conversion.
- **ALASM, a macro with local and global labels** (Wild Commander's `emutrap.asm`: a temporary label and `VORG = $`
  in one macro): ALASM's LOCAL block, which the local ones need, makes the global one local too, and ALASM's `@name`
  is another name (`JP shared` does not find `@shared`). Needs the macro expanded at its calls.

## 6. Bugs the projects showed (fixed)

| Where | Bug | Shown by |
|---|---|---|
| layout | a DISP inside a DISP moved the address; sjasmplus ignores it (and so does the lowering now, through IF branches) | Wild Commander's plugins |
| layout, lowering | a STRUCT field `BYTE` / `WORD` without a value took no room | Wild Commander's `wc_api.h.a80` |
| sjasmplus frontend | `1000b` / `00001000B` (binary outside a jump) read as a temporary label reference: written as 0 | `fdd.a80` |
| sjasmplus frontend, layout, writers | an instruction sjasmplus repeats for its operands (`ld b,X,hl,0`, `out (c),a,(c),b`, `rst 0,8`, `jr nz,$,z,$`) was written as it stood | ZX Evolution's plasma twister |
| sjasmplus frontend | `label+N` and `label+*` (smart SMC), `!label`, a macro on one line, `INSERT` / `BINARY`, `OUT n,A` without parentheses, a jump to a label named like a register (`djnz de`) | romych's DEMON, NextPlayer, Flex Navigator, pentevo's `tb.asm` |
| lowering | a one-line macro left every line after it inside the macro: MODULE and STRUCT were not lowered | NextPlayer |
| `RetroWriter` | an ALIGN inside DISP or in a macro, and after a file's lines before its first label, got 0 (no address known) | ZiFi, unzip |
| `RetroWriter` | a name assigned with `=` once was written as its uses' values and lost where it was used above its definition | NextPlayer's `HEADLEN` |
| `RetroWriter` | a sjasmplus macro's global labels got no target names (two of them cut to one 7-character name) | unzip in STORM |
| `RetroWriter` | the temporary labels of an unrolled DUP had one name for every pass | Flex Navigator |
| `RetroWriter` | `name =` with no value crashed the writer | Flex Navigator's `fn.mpj` |
| `RetroWriter` | a tab in a directive written as a comment moved its operands to the next field in XAS (`SAVEBIN "emu.bin"` became data) | Wild Commander's `emu.asm` |
| TASM, ALASM, STORM, XAS backends | Z80N instructions (`nextreg`, `swapnib` ...) written by name: none of them has them; now their bytes | NextPlayer |
| STORM backend | a string longer than a STORM line was written on one line | Flex Navigator |
| ALASM backend | a DUP assigning a name got a LOCAL block, which made the name local | Flex Navigator |
| ALASM backend, sjasmplus backend | a temporary label in a DUP: a LOCAL block ALASM keeps apart, which came back to sjasmplus as one label defined twice; now a numbered temporary label | the probe |
| XAS frontend | a name spelled in another file was not given the definition's spelling (XAS compares 7 characters without case) | Wild Commander's plugins |
| `zxasm convert <dir>` | `--to-version` was not passed on; the codec's errors were not printed; INCBIN of a host file got no sector slack | the check |
| backends | the folder of an INCLUDE / INCBIN was kept in its TR-DOS name (`_spg/dow`) | ZiFi |

## 7. Open

- Lua blocks (a Lua interpreter with sjasmplus' API in the converter).
- RELOCATE_START / RELOCATE_TABLE.
- ALASM: a macro mixing LOCAL labels and global ones, expanded at its calls.
- Macro parameters named like registers (`MACRO T a` ... `LD A,a`): the frontend keeps registers in lower case,
  so `a` cannot be told from `A`.
- A real editor holds less than the format: TASM, STORM and XAS take the 10000-line NextPlayer as one file here (their
  codecs check no size); `splitLines` would cut it for them too once their memory limits are known.
