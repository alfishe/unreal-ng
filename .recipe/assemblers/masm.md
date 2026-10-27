# Recipe: MASM (Master Assembler 128K) in unreal-ng

Run MASM 1.1 by KSA Software and \*AIG\* (Moscow, 1995) on an emulated Pentagon 128K: put a source on its disk,
select it, edit it, assemble it, read the bytes it built, and move sources between MASM and the host
(`zxasm`, `zxdisk.py`, `symconv`). Every step here was run on 2026-10-07 against MASM 1.1. The other versions
(1.0 demo, 1.3, 2.0, 3.0) are described at the end with what was and was not checked.

Related: [run/manual-trdos-run.md](../run/manual-trdos-run.md) (TR-DOS), [input/keyboard.md](../input/keyboard.md)
(keys), [media/insert-disk.md](../media/insert-disk.md), the format research
`docs/inprogress/2026-10-05-unreal-asm/research-masm.md` and the conversion
`docs/inprogress/2026-10-05-unreal-asm/research-masm-to-sjasmplus.md`.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side
> pipelines (`tools/verification/unreal-asm/emulator/assemble-in-emulator.py masm11` drives exactly these steps over HTTP) or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## Where MASM is

The program disks are in the ZX collection under `software/programming/masm/` (`MASM_11.ZIP` → `MASM_11.SCL`:
`MASM 1.1.B` boot, `masm 1.1.C`, `sts3.2om.C`, `MASMHELP.W`; `MASM1_3.zip`, `MASM2_0D.zip`, `MASM30M.zip`;
`MASM_SRC.RAR`: MASM 1.1's own source). The source format is in `research/notes.md` there and in the research
document above. Machine: **Pentagon 128K** (MASM uses RAM pages 1, 3, 4, 6; "128k Speccy only").

## Preparing a disk (host)

MASM lists only its own sources (TR-DOS type `a`). A text source becomes a MASM file with the library's codec, then
a hobeta file, then goes onto a copy of the program disk:

```bash
zxasm encode prog.txt --codec masm --version 1.1 -o prog.bin            # MASM 1.1 tokenized text
python3 tools/verification/unreal-asm/lib/zxdisk.py hobeta prog.bin PROG.a 38667 'PROG.$a'   # type a, start 38667 (1.x)
python3 - <<'EOF'
import sys; sys.path.insert(0, 'tools/verification/unreal-asm/lib'); import zxdisk
img = zxdisk.scl2trd(open('MASM_11.SCL', 'rb').read())
open('scratch/masm.trd', 'wb').write(zxdisk.add(img, [open('PROG.$a', 'rb').read()]))
EOF
```

Keywords are tokenized only in capitals (MASM's own rule): write the source in capitals, labels as you like.
A file the source `INCLUDE`s or `INCBIN`s goes on the same disk (`INCLUDE M1+`, `INCBIN DAT`: bare names).

## Waiting: where MASM waits for a key

Drive MASM on a paused machine and wait in emulated time, never with host sleeps: a sleep that is long enough on an
idle host is too short on a loaded one, and a key then lands in the wrong screen. MASM 1.1 waits for every key in one
of three loops (found with `debug/pchist` and `disasm`, 2026-10-10):

| Where | PC | The loop |
|---|---|---|
| the title screen | `#983B` | `IN A,(#FE)` / `CPL` / `AND #1F` - any key |
| the menu, the editor, after `OK` | `#887D` | `HALT` / `OR (HL)` / `JR Z` on `LASTK` (`#5C08`), called from `#886E` |
| the Work file list | `#8B9C` | `LD A,(LASTK)` / `OR A` / `JR Z` |

Each key is then one pattern: run 2 frames (MASM finishes the step that reached the wait), tap the key (held 4 frames),
run 6 frames, `skip_until` the next wait's PC. `skip_until` runs the CPU until PC is there (a T-state budget, the
answer has `hit`), the same frames on every run. The whole assembly below takes about 2 s of host time instead of
the ~25 s of sleeps it used to. `basic/run` is the exception: it needs a running machine (resume, run, pause).
Screenshots right after a `skip_until` show the last presented frame: run one frame first.

## MCP (preferred)

Instance: `emulator_manage {"action":"create","model":"PENTAGON"}` (or an existing one; with several instances
pass its id as `target`, `"auto"` refuses). `skip_until` has no MCP action: `invoke_api` calls it, and its answer is
in `structuredContent.body` (`hit`, `pc`). Both blocks below were run line by line over MCP on 2026-10-10 (the key
lines with the pattern above): the bytes, then the edited file on the disk.

```text
control_execution {"target":"EMU_ID","action":"pause"}
load_software     {"target":"EMU_ID","path":"scratch/masm.trd","drive":"A"}
invoke_api        {"target":"EMU_ID","method":"POST","path":"/api/v1/emulator/{id}/reset"}
control_execution {"target":"EMU_ID","action":"run_frames","frames":100}            # the Pentagon boot menu
type_input        {"target":"EMU_ID","action":"tap","key":"down","frames":4}       # four downs: the menu's TR-DOS line
control_execution {"target":"EMU_ID","action":"run_frames","frames":12}            # 12 frames after each key
type_input        {"target":"EMU_ID","action":"tap","key":"down","frames":4}
control_execution {"target":"EMU_ID","action":"run_frames","frames":12}
type_input        {"target":"EMU_ID","action":"tap","key":"down","frames":4}
control_execution {"target":"EMU_ID","action":"run_frames","frames":12}
type_input        {"target":"EMU_ID","action":"tap","key":"down","frames":4}
control_execution {"target":"EMU_ID","action":"run_frames","frames":12}
type_input        {"target":"EMU_ID","action":"tap","key":"enter","frames":4}
control_execution {"target":"EMU_ID","action":"run_frames","frames":12}            # TR-DOS
control_execution {"target":"EMU_ID","action":"resume"}                            # basic/run needs it running
invoke_api        {"target":"EMU_ID","method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"MASM 1.1\""}}
control_execution {"target":"EMU_ID","action":"pause"}
invoke_api        {"target":"EMU_ID","method":"POST","path":"/api/v1/emulator/{id}/skip_until","body":{"pc":"0x983B","max_tstates":70000000}}
# then each key: run_frames 2, tap, run_frames 6, skip_until its wait (the table above)
type_input        {"target":"EMU_ID","action":"tap","key":"enter","frames":4}     # past the title -> #887D: the menu
type_input        {"target":"EMU_ID","action":"tap","key":"w","frames":4}         # Work file -> #8B9C: the type-a files
type_input        {"target":"EMU_ID","action":"tap","key":"enter","frames":4}     # the first one -> #887D
type_input        {"target":"EMU_ID","action":"tap","key":"a","frames":4}         # Assemble -> #887D: "Pass 1 … Pass 2 … OK"
type_input        {"target":"EMU_ID","action":"tap","key":"space","frames":4}     # any key after OK -> #887D: the menu
inspect_state     {"target":"EMU_ID","aspects":["memory"],"address":24576,"length":16}
```

The last call returned `0x6000: 3E 14 21 34 ED …` for the test program (`ORG #6000` …), the bytes MASM 1.1 wrote.
A program with `ORG #C000` or above is compiled into **RAM page 0**; read it as a page:

```text
invoke_api {"target":"EMU_ID","method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/0?offset=0&length=16384"}
```

Editing (the editor is the menu's `E`; from the menu, not from the `OK` after an assembly: that one takes the first
key). Every step waits at `#887D` with the same pattern; `type` waits until `keyboard/status` says
`sequence_running: false`:

```text
type_input {"target":"EMU_ID","action":"tap","key":"e","frames":4}            # the editor, the text from line 0
type_input {"target":"EMU_ID","action":"combo","keys":["symbol","w"],"frames":4}   # SS+W: insert a line
type_input {"target":"EMU_ID","action":"type","text":"x       nop"}          # CAPS LOCK is on: arrives as X NOP
type_input {"target":"EMU_ID","action":"combo","keys":["symbol","enter"],"frames":4} # SS+Enter: save to disk
type_input {"target":"EMU_ID","action":"combo","keys":["caps","symbol"],"frames":4}  # EXT: command mode
type_input {"target":"EMU_ID","action":"tap","key":"q","frames":4}            # Q: back to the menu
invoke_api {"target":"EMU_ID","method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}   # the file, rewritten
```

Checked 2026-10-10: the file's sector then starts `04 58 0A 07 C9 04` (the new `X NOP` line) before the old text.

## A host source into MASM 1.1 / 1.3

Write the source at its `MASM128>` prompt (not in the editor); `A` then assembles it, `E` shows it:

```text
asm_source  {"action":"sync_inject","path":"/abs/main.asm","assembler":"masm-1.1"}   # sjasmplus, converted by the MASM backend
type_input  {"action":"tap","key":"a","frames":4}                                     # Assemble
```

The source is written as MASM 1.1 text (left-to-right chains without parentheses, labels of 10 characters,
`BEGIN` / `END`, `PHASE`, lines of at most 31 characters, macros expanded:
[research-sjasmplus-to-masm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-sjasmplus-to-masm.md)) as LOAD leaves
a file. MASM keeps no "changed" flag: a text it holds is replaced only by force (or when it is the push watch's own).
`tools/verification/unreal-asm/emulator/inject-oracle.py masm11 <MASM_11.SCL> <source.asm> 0x6000 --wait 20` runs the whole
check.

## The text in memory (MASM 1.1)

2.0 and 3.0 (found in their code: asm-synchronizer.md §7.12): the same gap buffer from the address their files carry
(2.0 `#94DA`, 3.0 `#9123`) to `#FFFF`, lines ended by `00`; the gap start, the editor flag and the gap end are
operands of the gap-closing routine (2.0 `#92C6`+1 / +7 / +13, 3.0 `#8C24`+1 / +7 / +13). While a line is typed in it
is only in the line buffer (2.0 `#89D1`, 3.0 `#864F`, text); the text after it starts at `(#929F)` / `(#8BFD)`. Their
editor has no save: EXT `Q`, then `S`, ENTER at the prompt.

1.0 demo: 1.1's buffer from `#913A`, the gap `(#8663)` / `(#8665)`; in the editor the cursor line's record sits at the
gap start; the tail of the line buffer (`#85EE`-`#8620`) is blanks in the editor, zeros in the menu.

1.1 / 1.3: the text starts at `#970B`. While it is edited MASM keeps a gap: `(#96CC)` is the gap start, `(#96CE)` the gap end,
and the rest runs up to `#FFFF`, where the file's `#FF` end lies. In the editor the cursor line is out of the text (its
record ends at the gap end); after EXT `Q` it is back. The asm-synchronizer reads both states
([asm-sources.md](../analysis/asm-sources.md#the-source-an-assembler-holds-in-ram-asm-synchronizer)).

## WebAPI

The same sequence, run as it stands three times on 2026-10-10 (each `{"hit":true}`, the bytes `3E 14 21 34 12 C9`,
~2 s):

```bash
BASE=http://localhost:${PORT:-8090}/api/v1
api() { local body=${2:-'{}'}; curl -s -X POST "$BASE/emulator/$EMU_ID/$1" -H 'Content-Type: application/json' -d "$body"; }
frames() { api run_frames "{\"frames\":$1}" > /dev/null; }
# A key, held 4 frames, then run until MASM waits for the next key at $2 - the same frames on every run, whatever
# the host does. The 2 frames before the key let MASM finish the step that reached the wait.
key() { frames 2; api keyboard/tap "{\"key\":\"$1\",\"frames\":4}" > /dev/null; frames 6
        api skip_until "{\"pc\":\"$2\",\"max_tstates\":70000000}" | jq -c '{hit, pc}'; }

api pause > /dev/null
api media/A/swap '{"path":"'$PWD'/scratch/masm.trd","discard":true,"immediate":true}' > /dev/null
api reset > /dev/null; frames 100                                     # the Pentagon boot menu
for k in down down down down enter; do api keyboard/tap "{\"key\":\"$k\",\"frames\":4}" > /dev/null; frames 12; done
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r .text | grep TR-DOS   # * TR-DOS Ver 5.04T*
api resume > /dev/null; api basic/run '{"command":"RUN \"MASM 1.1\""}' | jq -c '{outcome}'; api pause > /dev/null
api skip_until '{"pc":"0x983B","max_tstates":70000000}' | jq -c '{hit, pc}'   # the title waits for a key
key enter 0x887D                       # the menu
key w     0x8B9C                       # Work file: the list of type-a files
key enter 0x887D                       # the first one (cursor keys move)
api memory/write '{"address":24576,"data":[170,170,170,170]}' > /dev/null   # #AA: unwritten stays visible
key a     0x887D                       # Assemble: "Pass 1 ... Pass 2 ... OK", then a key
frames 1; curl -s "$BASE/emulator/$EMU_ID/capture/screen?area=full&format=png&path=$PWD/scratch/masm.png" > /dev/null
key space 0x887D                       # back to the menu
curl -s "$BASE/emulator/$EMU_ID/memory/read/24576?length=140" | jq -r .hexdump
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/0?offset=0&length=16384" | jq '.data | length'
```

The whole sequence as one tool (source first on the disk, included files after it). The tool still waits with host
sleeps (`--wait` seconds for the assembly): give it more on a loaded host, or drive the steps above instead:

```bash
python3 tools/verification/unreal-asm/emulator/assemble-in-emulator.py masm11 MASM_11.SCL 'PROG.$a' 0x6000 140 scratch/prog.bin \
    --port $PORT --id $EMU_ID
python3 tools/verification/unreal-asm/emulator/assemble-in-emulator.py masm11 MASM_11.SCL LS2.\$a 0xC000 11665 scratch/own.bin \
    --extra M1+.\$a M2+.\$a --wait 50 --port $PORT --id $EMU_ID        # MASM's own source: ~45 s
```

## What MASM 1.1 does (checked)

| Thing | Behavior |
|---|---|
| Menu | capitals in the menu words are the keys: `E`dit, `A`ssemble, `N`ew name, `M`erge, new `F`ile, `O`bject save, `Q`uit to STS, quit in `B`asic, object `I`nfo, load `F`ont, `S`ave, `R`un, `I`mport TASM 3.0; `W` selects the work file (not shown) |
| Assembling | two passes, the line count in hex after each, `OK` or `line error-code`; codes from the help: 0 no such command, 1 impossible operand, 2 no label, 3 label exists, 4 bad label, 5 impossible action |
| Where the code goes | at the addresses the source names: below `#C000` into the visible 48K, `#C000`-`#FFFF` into RAM page 0; `PHASE` moves only the label addresses |
| Speed | MASM's own source (5219 lines, 3 files): ~45 s at 1x |
| INCBIN | the whole last sector of the file lands in memory, the address moves by the file's length |
| Labels | up to 10 characters, a colon after a label ignored; lower-case words are labels, never commands |

## Labels and sources on the host

```bash
zxasm decode 'PROG.$a' -o prog.txt                      # MASM file -> UTF-8 text (version detected)
zxasm convert scratch/masm.trd --to sjasmplus -o out/   # every source of the disk, sjasmplus syntax
symconv source scratch/masm.trd --main LS2 --to native  # the labels with values, source files and lines
```

The conversion assembles with sjasmplus to the same bytes MASM 1.1 builds (checked on a test program, an INCBIN and
MASM's own source, 11688 bytes).

## Other versions

| Version | Disk | Start | Checked |
|---|---|---|---|
| 1.0 demo | Spectrofon #15 (`SpFon_15.Trd`, boot `MASMdemo`) | `RUN "MASMdemo"`, any key | started (2026-10-07): the menu has no `W`; neither `W` nor `G` loaded a file, and `A` assembled nothing; no `DOWN` / `UP` / `SYSTEM` / `STOPKEY`. Its editor works (2026-10-10): what is typed shows in capitals (caps on) and is stored tokenized (`LD` = `#AA`, 1.1's framing); EXT `Q`, `N` + a name, `S` saves (type `a`, start `#913A`; the catalog length leaves out the `FF` it writes) |
| 1.3 | `MASM1_3.SCL`, boot `MASMv1.3` | `RUN "MASMv1.3"`, any key, `W`, Enter, `A` | run (2026-10-07): assembles as 1.1 (`DOWN HL` the same bytes); the binary differs from 1.1 in 5 bytes (the version text) |
| 2.0 TURBO | `MASM2_0D.SCL`, boot `m2` | `RUN "m2"`, any key, `W`, Enter | the editor works and saves (EXT `Q`, `S`, Enter); its sources are not 1.1's (`FF`, lines ended by `00`: `zxasm encode --codec masm --version 2.0`); `A` (menu, or Extend + `A` in the editor) returns without a word: this copy has no compiler ("Necessary file(s) absent!" in its binary) |
| 3.0 MACRO | `MASM30M.SCL`, boot `TSM` | `RUN "TSM"`, any key, `G` (merge file: the list, Enter), `A`; `W` works too | the editor saves as 2.0 (sources `FF lo hi FF` + 2.0's lines, `--version 3.0`); run: `BANK n`, `BORDER n`, `CLS [a]` emit fixed code; `NAME MAC` ... `ENDM` is skipped (nothing calls it: a word in the command field is a label, so `NAME` there gives error 3); `IF` stops with "!?Unknown error?!", `ENDIF` does nothing, `ELSE` jumps into the menu: research-masm-to-sjasmplus.md §4 |

## Pitfalls

- **Lines over 31 characters** (its line buffer) break MASM's editor screen; it assembles them.
- **`LAB@3`** is the name `LAB@` to MASM: XOR after a name needs `LAB+0@3`. **`EX (SP),IY`** is refused (error 0).

- **Several instances**: MCP `target:"auto"` refuses; pass the id. `assemble-in-emulator.py` without `--id`
  creates a new instance each run: pass `--id`.
- **The 64-column font**: `inspect_state` `screen_ocr` does not read MASM's screen; take screenshots.
- **`capture_media` path**: an absolute path came back URL-encoded and the file was written into the emulator's
  working directory as `%2FVolumes%2F…png` (2026-10-07); a relative name works (it lands in the emulator's working
  directory).
- **Keys typed at the menu are commands**: text typed while the menu is up runs its letters as commands; enter the
  editor first (`e`, then check a screenshot).
- **CAPS LOCK starts on**: `type` lower-case text to get capitals (tokens); capitals typed with Shift arrive as lower
  case and stay text, which MASM does not assemble.
