# Recipe: XAS (the TR-DOS assembler) in unreal-ng

Goal: run XAS (Max Petrov / Creator, 1996-97) in the emulator, load a source, assemble it and read the bytes it
built; exchange sources with the host; get the labels of an XAS project. Everything below was run on XAS 7.447 and
4.18 (the oracle of the XAS -> sjasmplus conversion, `docs/inprogress/2026-10-05-unreal-asm/research-xas-to-sjasmplus.md`).
XAS 9.10 was run on 2026-10-07 too: its command line needs Caps Shift + Symbol Shift held 12 frames
(`type_input combo ["cs","ss"]` with `frames: 12`), then `A` assembles as in 7.447; see the pitfalls.

Prerequisite: a running PENTAGON instance (the default model; 128K, TR-DOS). For the generic steps see
[_common/setup.md](../_common/setup.md), [input/keyboard.md](../input/keyboard.md) and
[run/basic-inject-extract.md](../run/basic-inject-extract.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred; the TR-DOS `RUN` and the memory write ride
> `invoke_api`. Use [WebAPI](#webapi) inside host-side Python pipelines (`tools/verification/unreal-asm/` speaks HTTP) or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## Where the programs are

In the ZX collection under `software/programming/xas/`: the program disks `research/work/trd/XAS7_447.trd`
(`XAS7.447` + STS 5.1 + `Read Me`), `XAS418.trd` (`XASo` + `XMACROS` + `Xas help`), `XAS9_10.trd`, ...; the
archives they come from are in `vtrd/` (XAS 4.18, 5.05SE, 7.447, 9.07m, 9.10) and `XAS7_43C.ZIP`. The source format
and every source found are in `research/` (`notes.md`, `corpus/`).

## What XAS does (as run)

| | XAS 7.447 | XAS 4.18 |
|:--|:--|:--|
| Start | `RUN "XAS7.447"` from TR-DOS | `RUN "XASo"` |
| First screen | the disk's XAS sources in two columns, `Read Me` first | the same list (`XMACROS`, `Xas help`, then yours) |
| Text in memory | from #C000 (the file as it is: 36-byte header, then lines) | the same |
| Assemble | `CAPS SHIFT`+`SYMBOL SHIFT` (EXT), then `A` | the same |
| Code goes to | its ORG address in the 64K view (#6000 is free); `WORK` moves only the labels | the same |
| After assembling | the report: `Label Table Made, Last Address`, `Object Length`, then each error with its line number (`Syntax`, `Label Exists`, `Fatal Error`); assembling goes on after an error, the value is 0 | `Label Table Done` |
| Labels | 7 significant characters, case-insensitive | 7 |

## MCP (preferred)

```text
# 1. the program disk with your source added (host side, see "Sources to and from the host")
media           {"action":"swap","slot":"A","path":"<abs>/scratch/xas/work.trd","discard":true,"immediate":true}

# 2. reset, choose TR-DOS in the Pentagon menu (4 x down, enter), run XAS
emulator_manage {"action":"reset"}
type_input      {"action":"tap","key":"down","frames":4}          # x4
type_input      {"action":"tap","key":"enter","frames":4}
invoke_api      {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"XAS7.447\""}}
capture_media   {"action":"screenshot","filename":"scratch/xas/list.png"}   # the file list (wait ~8 s first)

# 3. pick the file (cursor keys move in the two columns) and load it
type_input      {"action":"tap","key":"right","frames":4}
type_input      {"action":"tap","key":"enter","frames":4}

# 4. mark the target memory so unwritten bytes show, then assemble: EXT, A
invoke_api      {"method":"POST","path":"/api/v1/emulator/{id}/memory/write","body":{"address":24576,"data":[170,170,170,170]}}
type_input      {"action":"combo","keys":["caps","symbol"],"frames":4}
type_input      {"action":"tap","key":"a","frames":4}
capture_media   {"action":"screenshot","filename":"scratch/xas/assembled.png"}   # the report (wait ~4 s)

# 5. the bytes XAS built
inspect_state   {"aspects":["memory"],"address":24576,"size":16}
#   [memory] 16 bytes at 0x6000: 0x6000: DD 7E 00 FD 77 FD DD 46  07 08 DD 7C FD 68 E5 DD
```

## WebAPI

`tools/verification/unreal-asm/lib/emulator.py` wraps these calls (`Emulator(url, id)`; `insert_disk`, `run_trdos`, `tap`, `type`,
`write`, `read`, `screenshot`):

```bash
B=http://localhost:${PORT:-8090}/api/v1/emulator; ID=<EMU_ID>; J='Content-Type: application/json'
curl -s -X POST $B/$ID/media/A/swap -H "$J" -d '{"path":"<abs>/scratch/xas/work.trd","discard":true,"immediate":true}'
curl -s -X POST $B/$ID/reset
curl -s -X POST $B/$ID/keyboard/tap -H "$J" -d '{"key":"down","frames":4}'      # x4, then "enter"
curl -s -X POST $B/$ID/basic/run   -H "$J" -d '{"command":"RUN \"XAS7.447\""}'
curl -s -X POST $B/$ID/keyboard/tap -H "$J" -d '{"key":"right","frames":4}'     # then "enter": load
curl -s -X POST $B/$ID/memory/write -H "$J" -d '{"address":24576,"data":[170,170,170,170]}'
curl -s -X POST $B/$ID/keyboard/combo -H "$J" -d '{"keys":["cs","ss"],"frames":4}'
curl -s -X POST $B/$ID/keyboard/tap -H "$J" -d '{"key":"a","frames":4}'
curl -s "$B/$ID/memory/read/24576?length=16" | jq -r .hexdump
```

`tools/verification/unreal-asm/emulator/assemble-in-emulator.py xas7447` / `xas418` runs these steps end to end (the source and the files
it loads added to a copy of the program disk, the range filled with #AA, the report saved as a screenshot):

```bash
python3 tools/verification/unreal-asm/emulator/assemble-in-emulator.py xas7447 XAS7_447.trd 'proj.$X' 0x6000 0x20 scratch/xas/proj.bin \
    --extra 'inc.$X' 'dat.$C' 'dat2.$Z' --port $PORT --id <EMU_ID>
#   32 bytes from #6000 written to scratch/xas/proj.bin; check scratch/xas/proj.assembled.png for errors
```

The script the conversion was checked with ran exactly this per test program, on an instance of its own (ports as in
[README](README.md#your-own-instance); MCP then answers on `http://localhost:$((PORT+200))/mcp`).

## Sources to and from the host

```bash
zxasm encode prog.txt --codec xas --version 7.43 -o prog.bin   # text -> an XAS file (4.18 / 5.05 / 7.43 / 9.10 ...)
zxasm decode 'prog.$X'                                         # an XAS file (hobeta, or --file NAME.X in a .trd) -> text
python3 tools/verification/unreal-asm/lib/zxdisk.py add XAS7_447.trd work.trd 'prog.$X' 'inc.$X' 'dat.$C'
zxasm convert work.trd --to sjasmplus -o out/                  # every source of the disk -> sjasmplus, LCODE files extracted
symconv source work.trd --main prog --to sjasmplus-sym         # the labels with the values XAS gives them
```

An XAS source on disk is type `X` with start #5341 (`AS`) and length 0 (XAS ignores it); a hobeta header for it:
name, `X`, `41 53`, `00 00`, `00`, sector count, checksum. LTEXT names another XAS source, LCODE a code file (`"name"`
= type C, `"name.Z"` another type, `"B:name"` drive B).

## Saving and the text in memory

EXT then `S` shows `Save text NAME`; Enter keeps the name. XAS writes whole sectors straight from `#C000` (the length
field stays 0). The asm-synchronizer reads the same text from RAM page 3 without a save
([asm-sources.md](../analysis/asm-sources.md#the-source-an-assembler-holds-in-ram-asm-synchronizer)). In unreal-ng,
9.07m and 9.10 save as well since the WD1793 status fix of 2026-10-09 (they wrote nothing before). After each disk
operation 9.x issues STEP `#20`, which leaves the head one cylinder past the track register; its next operation misses
the first sector (about a second, 4 revolutions), reads the ID under the head and seeks again: a pause, not an error.

## Pitfalls

- **XAS 9.10**: Extend for 4 frames types letters into the text: hold it 12 frames. Its file list shows only files
  with `AS` in the catalog start field (what XAS and `zxasm encode` write); after a Pentagon reset it reopens the
  text left in RAM instead of showing the list (start a new machine). A string of several characters in an LD / DW
  operand puts its leading characters into the code (research-xas-to-sjasmplus.md §4).

- **Typing into the text instead of a command.** EXT (`caps`+`symbol`) followed by the command letter works right
  after loading a file; in other editor states the letter went into the text as a new line (seen after an assemble
  report). Reload the file (reset and step 2) rather than trying to recover.
- **Writing the text into #C000 while the editor runs** confuses it (its pointers keep the old text): put the source
  on the disk and load it through the file list.
- **`capture_media` with an absolute `filename`** wrote a file named after the URL-encoded path into the emulator's
  working directory; a relative `filename` (resolved against the emulator's working directory) works.
- **Errors do not stop XAS**: a wrong line assembles as 0 and is listed in the report; always read the report
  screenshot.
- **Version differences seen**: 4.18 has no `'L` / `&H`, no `PUSH HL,IX`, no port without parentheses (`OUT PORT,A`
  is a Fatal Error) and no `!ASSM n` repeat; it names the data directives DEFB / DEFW / DEFM / DEFS.
- **The instance may disappear in a long session**: here it stopped without a crash log after another session rebuilt
  the app it ran from; running a copy of the app from your own `scratch/` avoided it.
