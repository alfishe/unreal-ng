# Recipe: Working with the ZEUS Assembler in Unreal-NG

ZEUS (Simon Brattel and Neil Mottershead, Crystal Computing 1983) keeps its source in memory as numbered lines
and assembles straight into memory. This recipe runs four versions in unreal-ng, gets a source in and the code out,
and moves sources between ZEUS and the host. Every step below was run on 2026-10-07 (unreal-ng release build, own
instance). Not covered: the PHT 3.6 shell's other tools (its file manager is used here only to start ZEUS 1.1), tape
saves from inside ZEUS.

> **Transport:** MCP first (`target` = the id `emulator_manage create` returned). The WebAPI section is what the
> host-side tool `tools/unreal-asm/assemble-in-emulator.py` (profiles `zeus1983`, `zeus11`, `zeusgg`, `zeus7e`)
> sends.

## Versions and where they are

Paths are relative to the ZX collection's `software/programming/zeus/` folder (its README lists sources and
SHA-256).

| Version | File | Machine | Starts | Keyword table |
|:--|:--|:--|:--|:--|
| ZEUS 1983 (Crystal) | `ZEUS.ZIP` → `ZEUS.TAP` (BASIC loader + `zeus.1` CODE 57344,7000) | `48K` | the tape's loader runs it at 57344 | 1983: `DEFB DEFM DEFS DEFW` |
| ZEUS (GG, "ZEUS with B-disk v20.04.96") | `archives/ZEUS_GG.zip` → `ZEUS_GG.SCL` (`ZEUS.B` + `ZEUS.C`) | `PENTAGON` | `RUN "ZEUS"` from TR-DOS | `DB DM DS DW`, `INCBIN` |
| ZEUS 1.1 beta (YRIC, MIPh&T 1993; the ZEUS of PHT 3.6 and of the ADS 2.0 sources) | `PHT_ZEUS.LZH` → `zeus.$c` (hobeta, CODE 57344,6775) | `PENTAGON` in 48 BASIC | `RANDOMIZE USR 57344` | `DB DM DS DW`, `INCLUDE PLACE` |
| ZEUS v7.E (ZKSoft) | `archives/ZEUS72ZK.zip` → `ZEUS72ZK.SCL` (`ZEUSv7.E.B`) | `PENTAGON` | `RUN "ZEUSv7.E"` from TR-DOS | as 1.1 |

The language is the same everywhere (manual: `archives/zxdb/Zeus.txt`, section 5) except where the table below
says otherwise; every row was checked by assembling in that version.

| | 1983 | GG | 1.1 | v7.E |
|:--|:--|:--|:--|:--|
| operators | `+ - & !`, strictly left to right | as 1983 | as 1983 | also `*` `/` and `%binary`, left to right; `/` rounds to the nearest (half down: `7/2` = 3, `11/4` = 3); a dividend smaller than the divisor is error 4 |
| leading `-` (`LD DE,-1`) | error 0 | — | — | — |
| `DEFS` / `DS n` | leaves memory as it was | leaves memory as it was | writes zeros | writes zeros |
| files | — | `INCBIN "name"` (quoted; `INCBIN"name"` is error 0) | `INCLUDE name` / `PLACE name`, both type `C` files, only when started from the PHT 3.6 shell (standalone: error A); `OPEN "name"` compiles to a disk file | `INCLUDE name` (a type `Z` source), `PLACE name` (a type `C` file) |

Common to all: statements separated by `:` each with its own optional label (`NOP:L3 NOP`); a label is any first
word that is not a keyword; keywords are upper case only; `"c` is a character (`LD A,"Z`); `DEFM /text/` takes any
delimiter, `:` and `;` inside are text, no closing delimiter runs to the end of the line; `V` / `NV` are `PE` / `PO`;
`DISP d` is an offset (after `ORG 30000` / `DISP 10000` the code goes to 40000 and runs at 30000, labels and `$` are
30000-based; a later `ORG` keeps the offset; `DISP 0` ends it); `ENT` marks the entry point for `X` and makes no code.

## Memory

ZEUS 1983 / 1.1 / GG run at 57344-65535 (v7.E's help: ZEUS at 56000, its label buffer below 55999). The source
lives at 32768 by default (`N` / `O` take another address). Code goes where `ORG` (+ `DISP`) says, so keep it clear
of the source and of ZEUS: 30000-32767 or 40000-55000 for small programs. A 20K program at #6000 (ADS 2.0) does not
fit beside its sources in 48K.

## MCP (preferred)

### ZEUS 1983 from its tape

```text
emulator_manage {"action":"create","model":"48K"}                      # -> id; state=paused: resume it
emulator_manage {"action":"resume","target":"<id>"}
load_software   {"target":"<id>","path":"<abs path>/ZEUS.TAP","play":true}
invoke_api      {"target":"<id>","method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"LOAD \"\""}}
inspect_state   {"target":"<id>","aspects":["screen_ocr"]}             # "ZEUS Z80 ASSEMBLER ... Crystal Computing" after ~8 s (fast load)
```

### Commands and typing

ZEUS reads commands and keywords in **capitals**. Switch CAPS LOCK on once (Caps Shift + 2) and then send
**lower-case** text: `type_input` sends an upper-case letter as Caps Shift + letter, which CAPS LOCK turns back into
a lower-case one (stored as plain text, not a keyword: the line fails to assemble).

```text
type_input {"target":"<id>","action":"combo","keys":["caps","2"],"frames":6}
type_input {"target":"<id>","action":"type","text":"n"}                # new empty source at 32768
type_input {"target":"<id>","action":"tap","key":"enter"}
type_input {"target":"<id>","action":"type","text":"i 10 10"}          # automatic line numbers 10, 20, ...
type_input {"target":"<id>","action":"tap","key":"enter"}
type_input {"target":"<id>","action":"type","text":"org 50000"}        # each line: text + enter
type_input {"target":"<id>","action":"tap","key":"enter"}
...                                                                    # ld a,"z / rst 16 / ret
type_input {"target":"<id>","action":"tap","key":"backspace"}          # x6: delete the offered number, then enter
type_input {"target":"<id>","action":"tap","key":"enter"}              #     ends automatic numbering
type_input {"target":"<id>","action":"type","text":"l"}                # list; "a" + enter assembles
```

Checked: the source at 32768 holds `0A 00 BF "50000" 00 14 00 B3 80 2C 22 5A 00 ...` (`#BF` = ORG, `#B3` = LD: the
keywords were stored as tokens), the code at 50000 is `3E 5A D7 C9`. Commands: `A` assemble (stops at the first
error and lists it: `Error n` + the line; codes in the manual's appendix 2), `L` list, `O addr` make the source at
`addr` current, `N addr` new source, `T` start and length of the source, `Q` quit to BASIC, `S` the symbol table.

### A source from the host, the code back

A ZEUS source is the bytes from 32768 to its `FF FF` (`zxasm decode` / `encode` below). Write it and make it current:

```text
invoke_api    {"target":"<id>","method":"POST","path":"/api/v1/emulator/{id}/memory/write","body":{"address":32768,"data":[10,0,191,...]}}
type_input    {"target":"<id>","action":"type","text":"o"}            # GG needs the address: "o 32768"
type_input    {"target":"<id>","action":"tap","key":"enter"}
type_input    {"target":"<id>","action":"type","text":"a"}
type_input    {"target":"<id>","action":"tap","key":"enter"}
inspect_state {"target":"<id>","aspects":["memory"],"address":40000,"length":623}
```

Checked with the Zeus Routines' `ZeusPrint` (ZXDB 19058): the 623 bytes at 40000 equal
`testdata/dialects/zeus/ZeusPrint.bin`. Fill the code range with a known byte first (`memory/write` of `#AA`) to see
what ZEUS did not write.

### Running the result

```text
type_input {"target":"<id>","action":"type","text":"q"}               # back to BASIC
type_input {"target":"<id>","action":"tap","key":"enter"}
invoke_api {"target":"<id>","method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RANDOMIZE USR 50000"}}
invoke_api {"target":"<id>","method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"PRINT USR 57344"}}   # ZEUS again
```

### Disk versions (v7.E shown; GG: `RUN "ZEUS"`)

```text
emulator_manage {"action":"create","model":"PENTAGON"}
emulator_manage {"action":"resume","target":"<id>"}
load_software   {"target":"<id>","path":"<abs path>/zeus7.trd","drive":"A"}     # the SCL converted to TRD, plus the files to INCLUDE / PLACE
invoke_api      {"target":"<id>","method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"ZEUSv7.E\""}}
inspect_state   {"target":"<id>","aspects":["screen_ocr"]}                     # "Z80 Assembler. Improved by ZKSoft."
```

then CAPS LOCK, `memory/write` the source, `o 32768`, `a` as above. Checked: a source with `INCLUDE inc1` (type `Z`
on the disk) and `DATA PLACE dat` (type `C`) built `01 3E 01 41 02 11 22 33 44 55 45` at 40000.

ZEUS 1.1 (`zeus.$c`): `PENTAGON`, menu item 48 BASIC (four items down is TR-DOS, three is 48 BASIC), write the
file's data (hobeta bytes 17..) to 57344, `basic/run` `RANDOMIZE USR 57344`, wait ~5 s before the first command.
Standalone it answers `INCLUDE` / `PLACE` with error A: their disk access is the PHT shell's.

### ZEUS 1.1 from the PHT 3.6 shell (INCLUDE, PLACE, OPEN)

Put the files of `PHT_ZEUS.LZH` on a TR-DOS disk (`zxdisk.py add`; at least `pht36.$b`, `zeus.$c`, `config.$c`)
with the files the source INCLUDEs and PLACEs, **all type `C`** (the help: "the extension is always *.C"; nothing may
follow the name on the line). `RUN "PHT 3.6"` starts the shell's file manager ("DOS 5.03 Tool"): Q / A move the
cursor, ENTER on a `C` file opens its File Functions, `G` there is Call Subroutine (a register line: type the PC),
ENTER calls it; `X` would be Exit to BASIC.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"PHT 3.6\""}}
type_input {"action":"tap","key":"a"}                      # the cursor to zeus.C (the second entry)
type_input {"action":"tap","key":"enter"}                  # File Functions
type_input {"action":"tap","key":"g"}                      # Call Subroutine
type_input {"action":"tap","key":"enter"}
type_input {"action":"type","text":"E000"}                 # PC = 57344, zeus.C's start
type_input {"action":"tap","key":"enter"}                  # ZEUS Z80 ASSEMBLER BETA version 1.1
```

Then CAPS LOCK, the source to 32768, `o 32768`, `a` as above. Checked: `INCLUDE inc1` + `DATA PLACE dat` (both type
`C`) built `01 3E 01 41 02 11 22 33 44 55 45` at 40000, as v7.E. `OPEN "name"` before `a` sends the code to a disk
file instead of memory ("Save File <NAME .C> ? (Enter for save)": ENTER): that is how a program larger than the
memory beside ZEUS is built. ADS 2.0 (`MAKE_ADS`: `ORG #6000`, `INCLUDE cc0` / `cc1` / `cc2`; CC2 PLACEs `$ads` and
`FONT$`) built this way to `ADSOBJ.C`, 20155 bytes, equal to its sjasmplus conversion.

## WebAPI

`BASE=http://localhost:8090/api/v1`, `ID` from `POST $BASE/emulator/start {"model":"48K"}`.

```bash
curl -s -X POST $BASE/emulator/$ID/memory/write -H "$J" -d '{"address":57344,"data":[...]}'      # ZEUS code (1983: the tape's CODE block)
curl -s -X POST $BASE/emulator/$ID/basic/run    -H "$J" -d '{"command":"RANDOMIZE USR 57344"}'   # outcome "started"
curl -s -X POST $BASE/emulator/$ID/keyboard/combo -H "$J" -d '{"keys":["cs","2"],"frames":6}'   # CAPS LOCK
curl -s -X POST $BASE/emulator/$ID/memory/write -H "$J" -d '{"address":32768,"data":[...]}'      # the source
curl -s -X POST $BASE/emulator/$ID/keyboard/type -H "$J" -d '{"text":"o","delay_frames":6}'
curl -s -X POST $BASE/emulator/$ID/keyboard/tap  -H "$J" -d '{"key":"enter","frames":4}'
curl -s -X POST $BASE/emulator/$ID/keyboard/type -H "$J" -d '{"text":"a","delay_frames":6}'
curl -s -X POST $BASE/emulator/$ID/keyboard/tap  -H "$J" -d '{"key":"enter","frames":4}'
curl -s "$BASE/emulator/$ID/memory/read/40000?length=623" | jq -r .hexdump
curl -s "$BASE/emulator/$ID/capture/screen?area=full&format=png&path=$PWD/scratch/zeus-assembled.png"
```

Disk versions: `POST /media/A/swap {"path":...,"discard":true,"immediate":true}`, `POST /reset`, menu: `keyboard/tap`
`down` x4 + `enter` (TR-DOS), `basic/run` `RUN "ZEUSv7.E"`. Wait for `keyboard/status` `sequence_running: false`
between keys. The same steps as one call per run:

```bash
python3 tools/unreal-asm/assemble-in-emulator.py zeus1983 ZEUS.TAP PROG.bin 40000 623 scratch/prog.bin --url http://localhost:8095
python3 tools/unreal-asm/assemble-in-emulator.py zeus7e ZEUS72ZK.SCL 'PROG.$Z' 40000 64 scratch/prog.bin --extra 'inc1.$Z' 'dat.$C'
```

## Sources between ZEUS and the host

```bash
zxasm decode PROG.bin --codec zeus                         # text (5-digit numbers dropped; --version 1983 | gg | pht)
zxasm encode prog.txt --codec zeus --version pht -o PROG.bin   # lines numbered 10, 20, ... when the text has none
python3 tools/unreal-asm/zxdisk.py hobeta PROG.bin PROG.Z 32768 'PROG.$Z'   # a TR-DOS file for v7.E's INCLUDE
python3 tools/unreal-asm/zxdisk.py add disk.trd out.trd 'PROG.$Z'
zxasm convert PROG.bin --codec zeus --version pht --to sjasmplus -o prog.asm
symconv source ads.trd --main make_ads --to native             # labels with values: the disk's sources, and its files
                                                               # under the names PLACE / INCBIN use (else "INCBIN file not found")
```

`--version` matters for text written for v7.E: its bytes are also valid 1983 bytes, and only the v7.E reading
accepts `*` `/` `%`. The sjasmplus conversion assembles to ZEUS's bytes except where `DEFS` / `DS` of 1983 / GG left
memory untouched (sjasmplus writes zeros there).

## Pitfalls

- **Lower-case text arrives without CAPS LOCK** (`o` → `Error`): ZEUS has no lower-case commands. With CAPS LOCK on,
  upper-case text arrives lower-case. Watch the echo: a command shows as typed.
- **A new emulator from `emulator_manage create` is paused**: `basic/run` answers 409 until `resume`.
- **Paths for `load_software` are read by the emulator process**: pass absolute paths.
- **Error 4 in v7.E on a division** whose dividend is smaller than the divisor (`1/3`): not a missing comma.
- **First error stops assembly**: the code before it is in memory, the rest is not.
- **`capture_media` `screenshot` with `path`** saved the file URL-encoded relative to the emulator's working folder
  (seen 2026-10-07); the WebAPI `capture/screen?path=` writes where asked.
