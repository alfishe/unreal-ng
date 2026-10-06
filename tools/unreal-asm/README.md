# unreal-asm checks

Scripts that check the dialect conversion of the unreal-asm library (`core/src/3rdparty/unreal-asm`) against the
original assemblers: the bytes a converted source assembles to with sjasmplus must equal the bytes the original
assembler built. Python 3, standard library only. The library's own unit tests are in `unreal-asm-tests`; these
tools run the corpus-sized and emulator-driven checks behind them (the results are in
`docs/inprogress/2026-10-05-unreal-asm/research-*-to-sjasmplus.md`).

| Script | What it does |
|---|---|
| `roundtrip.py` | Converts every source of a set of TRD / SCL images to sjasmplus, reads each result back through the sjasmplus frontend and backend (the text must not change), optionally assembles every file with sjasmplus; prints a summary |
| `objcheck.py` | For a converted ALASM project: builds every object of each main source's `SAVEOBJ` table with sjasmplus and compares it with the file ALASM saved on the same disk |
| `assemble-in-emulator.py` | Runs TASM 4.12, ALASM 5.09, STORM 1.3 or ZAsm 3.15 in an unreal-ng instance, assembles a source and saves the bytes it built (the oracle for a source with no binary on its disk) |
| `lstcheck.py` | Compares the bytes of a sjasmplus listing with a memory dump, line by line (a converted program against a running copy of it, e.g. an assembler's own source against the assembler unpacked in memory) |
| `emulator.py` | The WebAPI client the emulator script uses (own instance, disk swap with TTD recording, keys, screenshots, memory) |
| `zxdisk.py` | TR-DOS images: list a TRD / SCL catalog, extract a file as hobeta, add hobeta files to a TRD, SCL to TRD, a file as hobeta |

## Example: a disk of ALASM sources with the objects ALASM built

```bash
export UNREAL_ASM_ZXASM=<build>/bin/zxasm UNREAL_ASM_SJASMPLUS=<path to sjasmplus>
$UNREAL_ASM_ZXASM convert TheLink.trd --to sjasmplus -o scratch/thelink   # every source + the files INCBIN names
python3 tools/unreal-asm/objcheck.py scratch/thelink TheLink.trd
#   GSTUNNE4.asm TUNNELZX.C     489 bytes  EQUAL
#   ...
#   objects equal: 18 of 19
```

## Example: many disks at once

```bash
python3 tools/unreal-asm/roundtrip.py scratch/roundtrip disk1.trd disk2.scl ... --assemble
# or the images listed in a file, one per line: --images-from scratch/images.txt
#   images: 6  sources: 99  round-trip differences: 0
#   main sources assembled: ...  without errors: ...
```

Only main sources are assembled (a file no other file of the image includes): the parts of a project assemble
through it.

## Example: the original assembler as the oracle

Start your own emulator instance on its own ports (other sessions use the defaults), then:

```bash
UNREAL_WEBAPI_PORT=8095 UNREAL_CLI_PORT=8195 UNREAL_MCP_PORT=8295 UNREAL_GDB_PORT=8395 \
UNREAL_DEZOG_PORT=8495 UNREAL_ZRCP_PORT=8595 <build>/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
python3 tools/unreal-asm/assemble-in-emulator.py tasm412 TASM_412.TRD scratch/SIN7.\$A 0x7000 256 scratch/sin7.bin \
    --url http://localhost:8095
```

The script adds the source to a copy of the assembler's disk, fills the memory range with `#AA` (bytes the
assembler did not write stay visible), assembles, saves the range and a screenshot of the result. Choose an
address the assembler leaves alone: TASM 4.12 keeps its overlay at `#8000`, ALASM 5.09 compiles `#8000-#BFFF` into
its system page. ALASM picks the file by cursor: a first run without `--list-position` saves a screenshot of the
list to count the column and row on. Stop the instance afterwards (its own PID only).

STORM 1.3 clears the 48K memory when it starts and keeps its own code there while it runs, so the script reads the
bytes after quitting to BASIC (unwritten bytes read 0). The source is a STORM file, which `zxasm encode` writes from
text; files the source includes go along with `--extra`:

```bash
$UNREAL_ASM_ZXASM encode PROG.txt --codec storm --version 1.3 -o scratch/PROG.bin
python3 tools/unreal-asm/zxdisk.py hobeta scratch/PROG.bin PROG.C 0xC00B scratch/PROG.\$C
python3 tools/unreal-asm/assemble-in-emulator.py storm13 STORM_13.SCL scratch/PROG.\$C 0x8000 1024 scratch/prog.bin \
    --extra scratch/INC.\$C --url http://localhost:8095
```

ZAsm 3.15 compiles into its own pages, so the source saves what it built: it ends with
`saveobj "a:out.C",<address>,<length>` and the script reads `out.C` from the disk afterwards:

```bash
python3 tools/unreal-asm/assemble-in-emulator.py zasm315 ZASM315.trd scratch/PROG.\$a 0x8000 320 scratch/prog.bin \
    --extra scratch/inc1.\$a --url http://localhost:8095
```

## Example: a converted program against a running copy

```bash
sjasmplus --lst=scratch/build.lst scratch/build.asm
python3 tools/unreal-asm/lstcheck.py scratch/build.lst mem64k.bin --from 0x6000
#   90B3  listing C3 F1 FD   memory C3 15 FB   | PRBUF2  JP #FDF1
#   ...
#   bytes compared: 18928  differing: 32
```

Write all outputs to `scratch/`.
