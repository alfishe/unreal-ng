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
  (ALASM's 64K: NextPlayer becomes `NextPlay`, `NextPla2`, `NextPla3`) or its editor cannot (2026-10-19, measured by
  each editor's own LOAD of longer and longer files in the emulator: TASM 4.12 / 4512 22801 bytes, its text from
  `#A6EE` below `#FFFF`, 4.0 / 4.4 24655 (`#9FA8`), 3.0-3.5 26305 (`#9935`), 2.0 26529, and a longer file loads "OK"
  over itself; STORM 1.3 16369 bytes in RAM page 6, "File too big" from 16373;
  XAS 7.447 64 sectors at `#C000` of RAM page 3, the 65th wrapping over `#0000` without a word). NextPlayer goes to each
  of the three in parts of 1000 lines, and back to the same memory.

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
| TASM 4.12 | 31 of 37 | 6 | the Lua ones (4), TASM's INCBIN sector slack (2) |
| ALASM 5.09 | 33 | 22 | the Lua ones (4) |
| STORM 1.3 | 33 | 12 | the Lua ones (4) |
| XAS 7.447 | 33 | 12 | the Lua ones (4) |
| ZX-ASM 3.15 (2026-10-17) | 33 | 17 | the Lua ones (4) |
| PROMETHEUS 48K (2026-10-17) | 33 | 13 | the Lua ones (4) |
| MASM 1.1 (2026-10-17) | 31 | 18 | the Lua ones (4), MASM's INCBIN sector slack as TASM's (2) |
| GENS 4 (2026-10-17) | 30 | 12 | the Lua ones (4), DISP (3: GENS assembles code where it lies) |
| ZEUS 1983 / v7.E (2026-10-17) | 30 / 30 | 12 / 20 | the Lua ones (4), `DS 16384,#CF` (3: more DEFB than ZEUS's text holds, its DEFS leaves memory as it was) |
| Laser Genius 1.04 (2026-10-17) | 33 | 19 | the Lua ones (4) |
| Primus 2.9 (2026-10-17) | 25 | 12 | the Lua ones (4), a second ORG back (Wild Commander's `emu.asm` ×3, Cobra, Flex Navigator ×2: Primus takes one ORG; one ahead is a hole), DISP (relocation, pentevo's `plasmatw`: Primus has none) |
| PASM 3.0 (2026-10-17) | 26 | 19 | the Lua ones (4), a second ORG back (`emu.asm` ×3, Cobra, Flex Navigator ×2: PASM takes one ORG), the relocation example (its DISP table) |

What is left, by cause:
- **Lua** (`3color.asm`, `fire.asm`, both BasicLib examples): the bytes come from Lua scripts sjasmplus runs (BasicLib's
  `LINE` / `LEND` / `NUM` macros are Lua). Left out with a warning (not in scope: no Lua in the converter).
- **TASM's INCBIN** loads whole TR-DOS sectors: the rest of the last sector lands after the file (Flex Navigator
  includes several files at one ORG, a shorter one after a longer: TASM overwrites the longer one's tail, sjasmplus
  does not). A property of TASM on a disk, not of the conversion.

Two more are done (§7): sjasmplus' relocation table, and ALASM's macros with local and global labels.

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
| ZX-ASM backend | SAVEOBJ's operands were written as typed: the names not in ZX-ASM's capitals (`Start`), so the text came back to sjasmplus with names it did not define | The Link's loader |
| ZX-ASM frontend | an index offset made with a 16-bit function (`(IY+100.n+X)`) came back as 65436+X: now the low byte as a signed one | NextPlayer |
| sjasmplus backend | a SAVEBIN alone got DEVICE ZXSPECTRUM4096 (it sets the screen's attributes): NOSLOT64K | The Link's loader |
| MASM backend | a Russian letter in a comment was kept: MASM's bytes #80 and up are its keywords, the codec refused the line | ZiFi, Wild Commander, FAT32 driver, DEMON, Flex Navigator (15 projects) |
| GENS backend | `*F name` (INCLUDE) was put in the command column: GENS reads it only from the first one | every project with an INCLUDE |
| GENS, ZEUS, Laser Genius backends | `^` and `` ` `` in a string, a character or a comment: the Spectrum's set has the arrow and the pound there (no ASCII byte): numbers, or those characters in comments | NextPlayer, Cobra, unzip, DEMON, Flex Navigator |
| GENS, ZEUS 1983, Laser Genius, PROMETHEUS backends | INCBIN not converted: the file's bytes as DB when the project holds it (`RetroWriter::IncbinAsData`) | ZiFi, DEMON, Flex Navigator, NextPlayer |
| ZEUS frontend | a DM delimited by `"` was read as a character `"h` first, so a `:` in it split the line | ZiFi's URLs |
| project joiner | ZEUS 1983 / GG and PROMETHEUS have no INCLUDE: the project was cut into files again anyway; now it stays one file (`IBackend::Includes`) | 15 projects |
| `RetroWriter` | `$` in a DISP block was unknown: the line's address from the source's layout | sjasmplus' relocation example |
| sjasmplus frontend | `EX HL,DE` (sjasmplus: EX DE,HL), `ADD A,(n)` and `LD H,(n)` (no such Z80 forms: sjasmplus assembles the number) were kept as written | DEMON, ZiFi, NextPlayer, Cobra |
| PROMETHEUS backend | a DS of up to 4096 zeros is written as DEFB (PROMETHEUS' DEFS leaves memory as it was; Flex Navigator clears a place with DS #400) | Flex Navigator |
| `zxasm convert <dir>` | `--version` was not given to the decoder: a Primus text read as ZEUS (DISK as DISP) | DEMON, Flex Navigator |
| projectcheck.py | the folder of a DOS-style `bin\name` OUTPUT; the device a target text needs is the original's (the memory a device sets up is not the program's) | Flex Navigator, The Link |
| Laser Genius frontend (2026-10-17) | a PUT is global in Laser Genius, but each file of a project is converted alone: an included file's own PUT moved its bytes inside the includer's DISP (sjasmplus keeps one across INCLUDE). The includer now defines `__LG_PUTIN` while its PUT is in force, the included file's first PUT ends that DISP first (`IFDEF` / `ENT` / `UNDEFINE`) | Wild Commander's `emu.asm` (×3) |
| PASM frontend (2026-10-17) | "without ORG the code goes to 24576" (`IF $==0`) moved an included file whose includer had `ORG 0`: an ORG now defines `__PASM_ORG` and the rule applies only without it | Wild Commander's `plasmatw` (×3), `trdump`, `setime` |
| PASM backend (2026-10-17) | a relative jump in a displaced block counted from the line's start, not its statement's (`call ESC : jr nz,.esc` was 3 bytes off): `RetroWriter::statementAddress` | Wild Commander's `trdump`, `setime` |
| pasmo backend (2026-10-19) | a Z80N word's low byte written `&255`: pasmo reads `&FF` as a hexadecimal number; now `AND 255` | `tests/relocate` |
| lowering, every backend (2026-10-19) | an EQU the lowering writes became the parent of the `.labels` after it in TASM / ALASM / pasmo (`!` now keeps the parent); an EQU's own `.t` was resolved with the new parent, sjasmplus uses the previous one | `tests/relocate`, the probe |
| lowering, layout (2026-10-19) | a nested STRUCT's field values were dropped; `TEXT len,{...}` and D24 / DWORD fields took no room or wrong bytes (the last byte fills the rest, an instance's `{...}` overwrites only the bytes it gives); a DWORD constant over 16 bits reached pasmo, which refuses it | `relocation_high_1` |
| lowering, layout (2026-10-19) | ABYTE's offset ended at the first blank; sjasmplus takes the longest expression before one (`ABYTE 123 high label2-123`) | `relocation_high_1` |
| layout (2026-10-19) | HIGH relocation recorded a byte of a moving word (`LD B,label`), NEXTREG's register number, Z80N `PUSH nn` (big-endian, HIGH only) at the low byte, missed `EQU high label` uses and ABYTE / DB bytes of a high byte | `relocation_high_1`, `relocation_high_2` |
| sjasmplus frontend, layout, writers (2026-10-19) | sjasmplus' fake instructions (`LD BC,(IX+d)`, `LDI A,(HL)`, `SUB DE,BC`, `LD HL,IX` ...): only a few were expanded, and the layout sized and relocated them as one instruction; now the full list of sjasmplus' manual, shared by every writer and by the layout part by part (`SjasmplusFake`) | `relocation_fake_instructions` |
| pasmo backend (2026-10-19) | the undocumented `RLC (IX+d),B` / `SET b,(IX+d),r` copies: pasmo has none of them; now their bytes | `relocation_fake_instructions` |
| every backend (2026-10-19) | an IF on a memory read while assembling (`IF {addr}`): pasmo / z88dk left its ENDIF alone, TASM wrote the condition as 0 and dropped the block; now each writes the block as taken with a warning (pasmo `IF 1`) | `relocation_mem_read` |
| Primus and PASM backends (2026-10-17) | a second ORG ahead of `$` is a hole up to it (Primus' DEFS; PASM had its zeros); after a second ORG left out the text's addresses part from the source's, so no hole is made from there (`emu.asm` grew past 64K) | NextPlayer |

## 7. The relocation table and ALASM's mixed macros

**RELOCATE_START [HIGH] ... RELOCATE_END, RELOCATE_TABLE [offset]** (sjasmplus builds the table itself). The layout
does it as sjasmplus does: a regular label defined in a block is relocatable, an EQU of a relocatable value too; a
value is relocatable when moving every relocatable label (and `$` in the block) by #1700 moves it by as much (`norel`
keeps a label still). Each instruction's value field (a word: JP / CALL nn, LD rr,nn, LD (nn),r, Z80N's ADD rr,nn) and
each DW item in a block that moves is recorded at its address (physical; displaced when the block began inside a DISP;
a DISP opened in the block makes its own labels and `$` absolute); with HIGH, a word's high byte or a byte holding a
high byte. A STRUCT's BYTE / WORD fields keep their default values, so an instance's fields (the defaults or the
values it gives) are recorded too. `relocate_count` / `relocate_size` are the table's size; RELOCATE_TABLE puts the
words down (minus the offset). The writers put the table as DW of its addresses and `relocate_count` /
`relocate_size` as numbers. DG / DEFG (bits) and DH / DEFH (hex pairs) are laid out and written as DB.

Checked against sjasmplus' own `tests/relocate` (2026-10-19) as whole programs: each test converted to pasmo, built,
and its 64K memory compared with sjasmplus' (`DEVICE NOSLOT64K`, so no device fills the screen). 23 of 30 are the same
bytes, tables included. The other seven are not conversions: six are sjasmplus' own error cases (`0000_region` wraps
past 64K, `disp_inside`, `disp_outside`, `norel_operator`, `table_sub_offset`,
`temporary_labels_consistency`), and `mem_read` reads memory while assembling (`{addr}`), which no target has. The
rules the conversion keeps are in `testdata/dialects/sjasmplus/relocfake.asm` (pasmo oracle test).

**ALASM, a macro with local and global labels** (Wild Commander's `emutrap.asm`: a temporary label and `VORG = $` in
one macro). ALASM's LOCAL block, which the local labels need, makes the global one local too, and ALASM's `@name` is
another name (`JP shared` does not find `@shared`, emulator). The lowering puts such a macro in at each of its calls
for ALASM (`LowerSjasmplus(..., expandMixedMacros)`): its arguments put in (a string one in DB as a string), its
.labels named for each expansion (`__M1_l`, `!` keeping the parent of the .labels after it), its temporary labels one
per line, its global labels and assignments as they are.

## 8. Open

- Lua blocks stay left out with a warning (not in scope).
