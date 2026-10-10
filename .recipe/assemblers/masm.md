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

## MCP (preferred)

Instance: `emulator_manage {"action":"create","model":"PENTAGON"}` (or an existing one; with several instances
pass its id as `target`, `"auto"` refuses).

```text
load_software {"target":"EMU_ID","path":"scratch/masm.trd","drive":"A"}
invoke_api    {"target":"EMU_ID","method":"POST","path":"/api/v1/emulator/{id}/reset"}
type_input    {"target":"EMU_ID","action":"tap","key":"down","frames":4}      # x4: the Pentagon menu's TR-DOS line
type_input    {"target":"EMU_ID","action":"tap","key":"enter","frames":4}
invoke_api    {"target":"EMU_ID","method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"MASM 1.1\""}}
# wait ~8 s: the title screen
type_input    {"target":"EMU_ID","action":"tap","key":"enter","frames":4}     # past the title: the menu
type_input    {"target":"EMU_ID","action":"tap","key":"w","frames":4}         # Work file: the list of type-a files
type_input    {"target":"EMU_ID","action":"tap","key":"enter","frames":4}     # the first one (cursor keys move)
type_input    {"target":"EMU_ID","action":"tap","key":"a","frames":4}         # Assemble: "Pass 1 … Pass 2 … OK"
capture_media {"target":"EMU_ID","action":"screenshot","path":"masm-assembled.png"}
inspect_state {"target":"EMU_ID","aspects":["memory"],"address":24576,"length":16}
```

The last call returned `0x6000: 3E 14 21 34 ED …` for the test program (`ORG #6000` …), the bytes MASM 1.1 wrote.
A program with `ORG #C000` or above is compiled into **RAM page 0**; read it as a page:

```text
invoke_api {"target":"EMU_ID","method":"GET","path":"/api/v1/emulator/{id}/memory/page/ram/0?offset=0&length=16384"}
```

Editing (the editor is the menu's `E`):

```text
type_input {"target":"EMU_ID","action":"tap","key":"e","frames":8}            # the editor, the text from line 0
type_input {"target":"EMU_ID","action":"combo","keys":["symbol","w"],"frames":4}   # SS+W: insert a line
type_input {"target":"EMU_ID","action":"type","text":"x       nop"}          # CAPS LOCK is on: arrives as X NOP
type_input {"target":"EMU_ID","action":"combo","keys":["symbol","enter"],"frames":4} # SS+Enter: save to disk
type_input {"target":"EMU_ID","action":"combo","keys":["caps","symbol"],"frames":4}  # EXT: command mode
type_input {"target":"EMU_ID","action":"tap","key":"q","frames":4}            # Q: back to the menu
invoke_api {"target":"EMU_ID","method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}   # the file, rewritten
```

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

`BASE=http://localhost:${PORT:-8090}/api/v1`; `tools/verification/unreal-asm/lib/emulator.py` wraps these calls.

```bash
curl -s -X POST $BASE/emulator/$EMU_ID/media/A/swap -H 'Content-Type: application/json' \
     -d '{"path":"'$PWD'/scratch/masm.trd","discard":true,"immediate":true}'
curl -s -X POST $BASE/emulator/$EMU_ID/reset
for k in down down down down enter; do
  curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap -d '{"key":"'$k'","frames":4}'; sleep 0.4; done
curl -s -X POST $BASE/emulator/$EMU_ID/basic/run -d '{"command":"RUN \"MASM 1.1\""}'; sleep 8
for k in enter w enter; do curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap -d '{"key":"'$k'","frames":4}'; sleep 2; done
curl -s -X POST $BASE/emulator/$EMU_ID/memory/write -d '{"address":24576,"data":[170,170,170,170]}'   # #AA: unwritten stays visible
curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap -d '{"key":"a","frames":4}'; sleep 6
curl -s "$BASE/emulator/$EMU_ID/memory/read/24576?length=140" | jq -r .hexdump
curl -s "$BASE/emulator/$EMU_ID/memory/page/ram/0?offset=0&length=16384" | jq '.data | length'
curl -s "$BASE/emulator/$EMU_ID/capture/screen?area=full&format=png&path=$PWD/scratch/masm.png"
```

The whole sequence as one tool (source first on the disk, included files after it):

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
