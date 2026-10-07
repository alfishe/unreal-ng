# Recipe: TASM in unreal-ng

Run TASM (Turbo ASseMbler, TR-DOS) in the emulator: start it from its disk, open or type a source, assemble, run the
code, save it, list the labels, and move sources between TASM and the host. Every step was run on unreal-ng
(2026-10-07, `PENTAGON` model) with TASM 4.12; 4.0 was run through load, edit and assemble; the other versions were
started and their command lines read. The TASM language (left-to-right expressions, `{ } ^ [ ]` postfix operators,
4.12's macros) is in [research-tasm-to-sjasmplus.md](../../docs/inprogress/2026-10-05-unreal-asm/research-tasm-to-sjasmplus.md);
what the editor stores is in [research-tasm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-tasm.md).
Shared steps (own instance, entering TR-DOS, keys, host exchange, labels): [README.md](README.md).

## Versions and where they are

| Version | Image (collection: `software/programming/tasm4/unpacked/`) | Loader | Status here |
|---|---|---|---|
| 4.12 (Rst7, 1997) | `TASM_412/TASM_412.SCL` ([zxart](https://zxart.ee/releasefile/id:249305/TASM_412.ZIP)) | `TASM4.12` | load, edit, type, assemble, run, object save, symbol list, export to PC text, import from the host: all run |
| 4.12 for Pentagon 512 (ROM Corp.) | `TASM4512/TASM4512.SCL` ([zxart](https://zxart.ee/releasefile/id:249306/TASM4512.zip)) | `TASM4512` | starts on `PENTAGON` (title "tasm4.12+sts5.1 512k ONLY"), the 4.12 command line |
| 4.0 (XL Design, 1996) | `TASM4_0/TASM4_0.SCL` ([zxart](https://zxart.ee/releasefile/id:249304/TASM4_0.zip)) | `TASM4.0` | load, edit, assemble run |
| 4.4 (KVA, 1996) | `TASM4_4/TASM4_4.SCL` ([zxart](https://zxart.ee/releasefile/id:249307/TASM4_4.zip)) | `TASM 4.4` | starts; the 4.0 command line; the disk carries `GensTasm` and `ZeusTasm` converters |
| 3.0 (Rst7, 1994) | `TASM3_0/TASM3_0.SCL` ([zxart](https://zxart.ee/releasefile/id:249302/TASM3_0.zip)) | `TASM 3.0` | starts; a command line like 4.12's without Import/export |
| 3.2 | `TASM3_2/TASM3_2.SCL` ([zxart](https://zxart.ee/releasefile/id:249303/TASM3_2.zip)) | `TASM 3.2` | starts; the 3.0 command line |
| 3.5 ("FLASHVERSION") | `TASM_3_5/TASM_3_5.SCL` ([vtrd](https://vtrd.in/system/TASM_3_5.zip)) | `TASM+` | starts; a `>>>` prompt without the command list |
| 2.0 (Rst7, 1993) | `TASM2_0/TASM2_0.SCL` ([zxart](https://zxart.ee/releasefile/id:249301/TASM2_0.zip)) | `TASM128` | starts; the command line has `Print text` instead of `Import`; sources are plain text (CR LF, TABs) |
| 5.0 beta / 5.5 beta (XL Design) | not in the collection (the samples typed into them are in `software/programming/tasm5/`) | — | not run for this recipe |

TASM keeps its overlay at `#8000` (4.12) and its own code in banks 3, 4 and 6: assemble small programs below `#8000`
(the checks used `#7000`), or into a page with `.PAGE`. TASM 4.0 starts its banks from `#C000` of bank 4 (ZX Format
#3's description).

## MCP (preferred)

Disk with the source, TR-DOS, TASM 4.12 (the source added with `zxdisk.py add`, see [README.md](README.md)):

```text
media             {"action":"insert","slot":"A","path":"/abs/scratch/work412.trd","discard":true}
emulator_manage   {"action":"reset"}
type_input        {"action":"tap","key":"down"}                 # x4
type_input        {"action":"tap","key":"enter"}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"TASM4.12\""}}
type_input        {"action":"tap","key":"enter","frames":4}     # past the title (~8 s after RUN)
```

The command line `TASM128>` lists the commands; the capital letter is the key: **E**dit, **S**ave, **A**ssemble,
**N**ew name, mer**G**e file, **O**bject save, **C**atalog, **Q**uit, Load **F**ont, s**Y**mbol list, **R**un,
**I**mport/export; with Symbol Shift: Calculator, Monitor, Dos shell. `W` (not listed) chooses the work file.

Load a source as the work file and assemble it (the keyboard is inverted: type the name in the opposite case):

```text
type_input   {"action":"type","text":"w","delay_frames":6}
type_input   {"action":"type","text":"host","delay_frames":6}   # file HOST
type_input   {"action":"tap","key":"enter","frames":4}
type_input   {"action":"type","text":"a","delay_frames":6}
inspect_state {"aspects":["memory"],"address":28672,"length":8}
capture_media {"action":"screenshot","filename":"scratch/tasm-assembled.png"}
```

The screen shows `*** ASSEMBLE ***`, `Pass 1`, `Pass 2` with the line counts, `Total lines assembled`, `Label space
used` and `OK`; an error line is `PASS 1:<line> <code> <text>` (codes in ZX Format #3's description: 0 syntax, 1 a
label longer than 7, 6 a label defined twice, F a file not found). A file that is not on the disk answers
`New file...`.

`E` opens the editor on the work file. Leave it with Extend held long enough, then `Q`:

```text
type_input {"action":"combo","keys":["cs","ss"],"frames":12}      # the status line shows COMMAND (B,C,E,F,H,L,M,Q,R,S,W,X,Y,?,ENTER)
type_input {"action":"tap","key":"q","frames":4}
```

The other editor commands after Extend: `B` / `E` start / end of the text, `S` search, `X` search and replace, `R`
insert the buffer, `C` clear it, `F` the next assembly error, `L` the last changed line, `M` a remembered line, `W`
32 / 64 columns, `Y` find a label, `H` help, ENTER duplicate the line. In the text: CS+5..8 cursor, CS+3 / CS+4 page
up / down, CS+1 tab, CS+2 insert a blank, CS+9 delete right, CS+0 delete left, SS+W insert a line, SS+Q delete the
line, SS+E remember it; by the built-in help also SS+SPACE / SS+ENTER to start / end a key macro (not tried). The bottom line shows free memory, the buffer
size and `line / lines`.

Type a new source: `W`, `N` at the "not saved" question, a new name (`New file`), `E`, then one line per `type` +
ENTER (type the blanks yourself: the label in column 0, 8 blanks before a command):

```text
type_input {"action":"type","text":"        org #7000","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}
type_input {"action":"type","text":"start   ld a,2","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}
```

Then Extend + `Q`, `S` saves (`Saving file TEST1`, type `A`), `A` assembles, `R` runs from the last ORG (a `RET`
returns to TASM), `O` saves the object code (asks the name; start and length from Run / Len: `TEST1.C`, start
28672, 5 bytes in the check), `Y` lists the labels (`Printer (Y/N)?`: `n` lists them on the screen, value and
name).

### Export to PC text, import

`I` opens the Import/export menu: `3` imports a TASM 3.0 file, `2` a TASM 2.0 file, `a` / `p` export the work file
as Amiga / PC text. This menu wants the lower-case letter, which the inverted keyboard gives for Shift + the key:

```text
type_input {"action":"type","text":"i","delay_frames":6}
type_input {"action":"type","text":"P","delay_frames":6}      # arrives as p: "Save text to file:"
type_input {"action":"type","text":"sinpc","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}
```

The file (`SINPC.C`, start 32768) is plain text with CR LF line ends; take it with `media export` or a sector read.

### Help

TASM 4.12's manual is on its disk: `RUN "TASMHELP"`, CS+6 / CS+7 move between links, ENTER opens one, CS+3 / CS+4
page, `B` back. Chapters: main menu commands, editor commands, directives, the 6502 cross-assembler library, the SINUS
and SNAKE examples.

### TASM 4.0 / 4.4

`RUN "TASM4.0"` (4.4: `RUN "TASM 4.4"`), any key past the title. The command line (`TASM4.0>`): Edit, Assemble,
New_name, merGe_file, Import_2.0, Save, Obj_save, Load_Fnt, sYmbol_lst, Disk_drive, Quit, Run_prog, Catalog,
Beeper. `W` loads the work file, `E` edits, `A` assembles: the report is `*** END ASSM ***`, `Sym:` (room left for
labels), `Run:` (the last ORG), `Len:`. In the editor Extend (held, `frames: 12`) then `Q` leaves; the other Extend
keys in 4.0: `R` insert the buffer, `C` clear it, `S` search, `X` replace, `I` go on replacing, `B` / `E` start / end.

### TASM 2.0, 3.x

Same command letters; 3.0 / 3.2 have `Import 2.0 file`, 2.0 `Print text`. 2.0 saves its sources as plain text
(type `C`, start 38750): `zxasm decode` reads them as codec `tasm` version `2.0`.

## WebAPI

`tools/unreal-asm/emulator.py` wraps the calls (`run_trdos`, `tap`, `type`, `read`, `read_disk_file`,
`screenshot`); `tools/unreal-asm/assemble-in-emulator.py tasm412 <TASM_412.trd> <source.$A> <address> <length>
<out.bin>` runs a whole TASM 4.12 assemble and saves the bytes. The raw calls:

```bash
BASE=http://localhost:8901/api/v1
EMU_ID=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"PENTAGON"}' | jq -r .id)
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"/abs/scratch/work412.trd","discard":true,"immediate":true}'
# reset, TR-DOS from the menu, then:
curl -s -X POST $BASE/emulator/$EMU_ID/basic/run -H 'Content-Type: application/json' -d '{"command":"RUN \"TASM4.12\""}'
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/combo -H 'Content-Type: application/json' -d '{"keys":["cs","ss"],"frames":12}'
curl -s "$BASE/emulator/$EMU_ID/memory/read/28672?length=8" | jq -r .hexdump
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/export -H 'Content-Type: application/json' -d '{"path":"/abs/scratch/after.trd"}'
```

## Pitfalls

- **Extend for less than about 12 frames** types a letter into the text instead of opening the command line.
- **The first characters after SS+Q** (delete line) were lost or landed past the line's end: the line kept
  `ORG #7000` plus 49 blanks and stray letters, and TASM refused the line (error 8). Wait half a second after a
  line command, and check what the file holds with `zxasm decode`.
- **Names are case-sensitive** and the keyboard is inverted: `000load` on the disk is typed `000LOAD`.
- **`#8000-#BFFF` belongs to TASM 4.12's overlay**: TASM's own SINUS example assembles there; move ORG below
  (`#7000`) to read the bytes after assembling.
- The Import/export menu takes the letter in lower case (Shift + key), the main command line in either.
