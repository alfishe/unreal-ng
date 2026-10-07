# Recipe: GENS (HiSoft Devpac) in unreal-ng

Run the GENS assembler in the emulator: put it in memory and start it, load a source from a tape image or type it,
assemble, run the result, read the bytes out, and move sources and labels between GENS and the host. Every step
below was run on unreal-ng (2026-10-07) with GENS4 (tape, HiSoft Devpac 4) and, where noted, GENS3 (tape, 1983).
Not covered: the GENS language itself (see
[research-gens-to-sjasmplus.md](../../docs/inprogress/2026-10-05-unreal-asm/research-gens-to-sjasmplus.md)),
MONS, saving to tape from inside GENS.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side Python
> tools (`tools/unreal-asm/assemble-in-emulator.py` is one) or when MCP is unavailable
> ([_common/transports.md](../_common/transports.md)).

## Versions and where they are

| Build | Where | Code block | Status here |
|---|---|---|---|
| GENS4 V4.0 (tape) | `DEVPAC_4.TAP` in [HiSoftDevpacV4.tap.zip](https://spectrumcomputing.co.uk/pub/sinclair/utils/h/HiSoftDevpacV4.tap.zip) (ZXDB [8091](https://spectrumcomputing.co.uk/entry/8091); in the ZX collection under `software/programming/gens-mons/archives/zxdb/`) | tape block 5 (`gens4`, 10880 bytes, saved for 26000; block 7 is the 51-column `gens4-51`) | works: load, assemble, run, macros |
| GENS3 (tape, 1983) | `DEVPAC_1.TAP` in [HiSoftDevpacV3.tap.zip](https://spectrumcomputing.co.uk/pub/sinclair/utils/h/HiSoftDevpacV3.tap.zip) (same folder) | tape block 3 (`GENS3`, 8355 bytes); its BASIC loader puts it at 25444 after `CLEAR 25443` | works; no `MAC` / `ENDM` (`*ERROR* 02`) and no `C` command in this build |
| GENS4B V4.1 (TR-DOS) | `gens4b.$c` in `MONSGENS.LZH` (collection: `software/programming/gens-mons/`), hobeta, start 30000 | the file's 12174 bytes | works when started from the TR-DOS prompt: `X` catalogue, `G,,1:NAME` load, `P10,30,1:NAME` save, `A` with `*F 1:NAME` |

GENS runs from any address: put it where the program you assemble will not go. The editor's text follows GENS in
memory and the symbol table follows the text; code that would overwrite them stops the assembly with `Bad ORG!`.
GENS4 at 26000 leaves `#C000` and up free for small programs; at 45000 it leaves `#5B00`-`#AFC7` for the code.

Machine: **48K** (`model: "48K"`) for the tape builds. The TR-DOS build GENS4B runs on `PENTAGON`, started from the
TR-DOS prompt (its disk calls need TR-DOS's system variables).

## MCP (preferred)

Create a 48K instance and put GENS4 at 26000 (the bytes of tape block 5 without its flag and checksum byte, as a JSON
array). `create` starts the machine; `start` with a `model` made a 128k machine here and answered an error
(2026-10-07), so use `create`.

```text
emulator_manage {"action":"create","model":"48K"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/memory/write","body":{"address":26000,"data":[...10880 bytes...]}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RANDOMIZE USR 26000"}}
```

The screen shows the editor's command list and the `>` prompt. Set the macro buffer once per session (GENS4 says
"No Macro Space" at the first `MAC` otherwise): `C`, ENTER (include buffer unchanged), `2000`, ENTER.

```text
type_input {"action":"type","text":"C"}
type_input {"action":"tap","key":"enter"}
type_input {"action":"tap","key":"enter"}
type_input {"action":"type","text":"2000"}
type_input {"action":"tap","key":"enter"}
```

Load a source from a tape image (one text file, see [Sources to and from the host](#sources-to-and-from-the-host)):
`G,,` loads the first text file on the tape and appends it to the text in memory (`Z`, then `y`, empties the text
first).

```text
load_software {"path":"scratch/source.tap"}
type_input {"action":"type","text":"G,,"}
type_input {"action":"tap","key":"enter"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/tape/play"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/tape/info"}      # repeat until structuredContent.body.state is "ended"
```

Or type it: `I10,10` starts inserting at line 10 in steps of 10; each line is typed after the number GENS shows,
ENTER stores it; EDIT (Caps Shift 1) ends inserting. A line with no label starts with a blank. Give the editor about
a second after each ENTER: a blank typed earlier is lost and the next word becomes a label.

```text
type_input {"action":"type","text":"I10,10"}
type_input {"action":"tap","key":"enter"}
type_input {"action":"type","text":" ORG #C000"}
type_input {"action":"tap","key":"enter"}
...
type_input {"action":"tap","key":"edit"}
type_input {"action":"type","text":"L"}                 # list
type_input {"action":"tap","key":"enter"}
```

Assemble with `A`: GENS4 assembles at once with its defaults (no listing, code where ORG says). Options follow the
letter (manual, section 2.0, not exercised here): `A4` lists, `A32` skips the ORG check, `A1` lists the symbol table
at the end. It takes about a second per 400 bytes of text.

```text
type_input {"action":"type","text":"A"}
type_input {"action":"tap","key":"enter"}
capture_media {"action":"screenshot","filename":"scratch/gens-assembled.png"}
```

The screenshot shows `Pass 1 errors: 00`, `Pass 2 errors: 00`, `Table used: ...` and, with `ENT`, `Executes:
nnnnn`. Read the bytes (the address and the number of bytes):

```text
inspect_state {"aspects":["memory"],"address":49152,"length":64}       # structuredContent.memory.hexdump
```

Run the code from the editor with `R` (needs `ENT` in the source; a `RET` with the stack as it was returns to the
editor):

```text
type_input {"action":"type","text":"R"}
type_input {"action":"tap","key":"enter"}
```

`capture_media screenshot` takes a `filename` relative to the emulator's working directory; an absolute `path` was
saved under a percent-encoded name there (seen 2026-10-07).

### GENS3

Same calls with the code of `DEVPAC_1.TAP` block 3 at 25444 after `CLEAR 25443`:

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"CLEAR 25443"}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/memory/write","body":{"address":25444,"data":[...8355 bytes...]}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RANDOMIZE USR 25444"}}
type_input {"action":"tap","key":"enter"}                # "Buffer size?" (the include buffer): default
```

`A` then asks `Table size:` (ENTER: default) and `Options:`; give `4` for no listing (GENS3 lists by default, the
reverse of GENS4).

### GENS4B (TR-DOS)

Start it from the TR-DOS prompt: TR-DOS from the Pentagon menu ([run/manual-trdos-run.md](../run/manual-trdos-run.md)),
the code to 30000, `RANDOMIZE USR 30000` at the `A>` prompt. Started from 48K or 128K BASIC without entering TR-DOS
first, its disk commands hang or reset the machine (that was the earlier failure: TR-DOS's variables were not set up).

```text
media      {"action":"insert","slot":"A","path":"/abs/scratch/gens.trd","discard":true}
emulator_manage {"action":"reset"}
type_input {"action":"tap","key":"down"}                  # x4: TR-DOS
type_input {"action":"tap","key":"enter"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/memory/write","body":{"address":30000,"data":[...12174 bytes...]}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RANDOMIZE USR 30000"}}
```

The editor (V4.1, "1990 MOA B-Disk version") lists its commands. A disk file is named `n:NAME`, `n` = 1-4 for drives
A-D, the type is always `C` (the code reads the digit, the name, pads it to 8 blanks and asks TR-DOS for type `C`):

```text
type_input {"action":"type","text":"X"}                   # catalogue: "A:TEST <C> 1", free sectors
type_input {"action":"tap","key":"enter"}
type_input {"action":"type","text":"G,,1:TEST"}           # load TEST.C (appended to the text in memory)
type_input {"action":"tap","key":"enter"}
type_input {"action":"type","text":"P10,30,1:SAVED"}      # save lines 10-30 as SAVED.C (start = the text's address)
type_input {"action":"tap","key":"enter"}
```

Checked: `P` wrote back the same 35 bytes `G` read; a source with `*F 1:INC` assembled with `A` read `INC.C` from the
disk (`3E 01 06 02 C9` from `LD A,1` / `*F 1:INC` (`LD B,2`) / `RET`). `zxasm encode --codec gens --version 2` writes
such files (type `C`, numbered lines).

## WebAPI

The same steps; `tools/unreal-asm/emulator.py` wraps them (`write`, `tap`, `type`, `read`, `screenshot`), and
`tools/unreal-asm/assemble-in-emulator.py gens4` runs the whole oracle: GENS4 at `--at`, the source (hobeta or a
raw GENS file) on a tape image, `G,,`, `A`, the bytes read into a file.

```bash
BASE=http://localhost:8090/api/v1
EMU_ID=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' -d '{"model":"48K"}' | jq -r .id)
curl -s -X POST $BASE/emulator/$EMU_ID/memory/write -H 'Content-Type: application/json' -d @gens4.json   # {"address":26000,"data":[...]}
curl -s -X POST $BASE/emulator/$EMU_ID/basic/run -H 'Content-Type: application/json' -d '{"command":"RANDOMIZE USR 26000"}'
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/type -H 'Content-Type: application/json' -d '{"text":"G,,","delay_frames":6}'
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":4}'
curl -s -X POST $BASE/emulator/$EMU_ID/tape/load -H 'Content-Type: application/json' -d '{"path":"/abs/scratch/source.tap"}'
curl -s -X POST $BASE/emulator/$EMU_ID/tape/play
curl -s $BASE/emulator/$EMU_ID/tape/info | jq -r .state        # "ended" when the text is in
curl -s "$BASE/emulator/$EMU_ID/memory/read/49152?length=64" | jq -r .hexdump
curl -s "$BASE/emulator/$EMU_ID/capture/screen?area=full&format=png&path=/abs/scratch/gens.png"
```

```bash
python3 tools/unreal-asm/assemble-in-emulator.py gens4 DEVPAC_4.TAP 'gens/ISC11VRG__ISCOP.C.$C' 0x80E8 1235 \
    scratch/iscop.bin --at 45000 --url http://localhost:8095
#   1235 bytes from #80E8 written to scratch/iscop.bin; check scratch/iscop.assembled.png for errors
```

## Sources to and from the host

- **Read a GENS file** (hobeta from a TR-DOS disk, or the data of a tape `P` save): `zxasm decode FILE.$C -o
  file.txt` gives the text (`gens 2: 165 line(s)`); `zxasm files disk.trd` lists a disk and names the codec of each
  file; `--file NAME.T` takes one file from a disk image.
- **Write a GENS file**: `zxasm encode file.txt --codec gens -o file.gens` (lines numbered 10, 20, ... when the
  text has no numbers); `python3 tools/unreal-asm/zxdisk.py hobeta file.gens NAME.C 37066 NAME.$C` wraps it as
  hobeta, `zxdisk.py add disk.trd out.trd NAME.$C` puts it on a disk.
- **A tape image GENS loads with `G`**: a standard header of type 3 (CODE) with the name and the text length, then
  one data block with the line records (number low, number high, text, `#0D`). `assemble-in-emulator.py gens4`
  writes such a tape (`oracle.tap`) next to its output.
- **Convert to sjasmplus**: `zxasm convert FILE.$C --to sjasmplus -o file.asm`; the conversion assembles to the bytes
  GENS4 builds (checked on six programs). `ORG $` before any ORG is kept and reported: GENS starts after its text
  and symbol table, sjasmplus at 0.
- **Labels**: `symconv source FILE.$C --to <format>` gives the labels with their values, kinds and GENS' own line
  numbers (`symconv source ... --to native`); `--main NAME` picks the source on a disk image.

## Errors and pitfalls

| Seen | Meaning | What to do |
|---|---|---|
| A source line with `*ERROR* nn` under it, the assembly paused | an error in that line (Devpac 4.1 manual, appendix 1: 02 mnemonic not recognised, 04 a symbol defined twice, 10 an 8-bit value is larger, 19 nested conditions) | fix the source; a reset and a fresh start of GENS is the reliable way back |
| `basic/run` answers 409 "the emulator is not running" | the machine is paused (seen once after a reset) | `emulator_manage {"action":"resume","target":"auto"}` |
| `basic/run` answers 409 "other keyboard input is still being typed" | keys queued while the machine was paused are still waiting | `type_input {"action":"release_all"}`, let frames run, retry |
| `*ERROR* 02` at `ld a,1` | mnemonics, registers and conditions are capitals only | write them in capitals |
| `*ERROR* 10` at `#ff` | hex digits in capitals only | `#FF` |
| `*ERROR* 10` at `LD A,1 comment` | a comment needs `;` | `LD A,1 ;comment` |
| `*ERROR* 04` at the second call of a macro | a label in a macro body is defined at every call | no labels in macros called twice |
| `No Macro Space` | the macro buffer is 0 | `C`, ENTER, a size, ENTER |
| `Bad ORG!` | the code would overwrite GENS, its text or its symbol table | put GENS elsewhere or the code higher (`A32` skips the check at your own risk) |
| The text appears twice | `G` appends | `Z`, `y` before `G` |
| `K Invalid colour` on a BASIC screen | GENS crashed (seen while it printed an error line) | reset and start GENS again |
| Two labels clash that look different | only the first 6 characters count | rename |
| `$` in `DEFW A,$` is not the line's address | GENS moves `$` per item | expected (the sjasmplus conversion writes `$+2`) |
