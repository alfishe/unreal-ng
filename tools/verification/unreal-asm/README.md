# unreal-asm: assembler automation scripts

Scripts that check the dialect conversion of the unreal-asm library (`core/src/3rdparty/unreal-asm`) against the
original assemblers and the cross assemblers: the bytes a converted source assembles to must equal the bytes the
original assembler built. The ones that prove general may move to the library's own `tools/` later. Python 3,
standard library only. The library's own unit tests are in `unreal-asm-tests`; these scripts run the corpus-sized and
emulator-driven checks behind them (results in the design folder's
[`research-*-to-sjasmplus.md`](../../../docs/inprogress/2026-10-05-unreal-asm/README.md), how to drive each assembler
by hand in [`.recipe/assemblers/`](../../../.recipe/assemblers/README.md)).

## Layout

```
unreal-asm/
  README.md                     this file
  lib/                          modules the scripts import (also usable alone)
    emulator.py                 WebAPI client: own instance, disk swap with TTD recording, keys, screenshots, memory
    zxdisk.py                   TR-DOS images: list a TRD / SCL catalog, extract a file as hobeta, add hobeta files to
                                a TRD, SCL to TRD, a file as hobeta (also a command line tool)
  emulator/                     the original assembler running in unreal-ng
    assemble-in-emulator.py     profiles tasm412, alasm509 (ALASM 5.05-5.09 disks), storm13, zasm315 (ZAsm 3.15;
                                3.2x ... 4.20 with --ram 512), masm11, xas7447, xas418, zeus1983, zeus11, zeusgg,
                                zeus7e, gens4 (tape): assembles a source and saves the bytes it built (the oracle for
                                a source with no binary on its disk)
    sync-dumps.py               profiles alasm509, alasm444, tasm412: golden RAM dumps for the asm-synchronizer, each
                                editor state with the file the assembler saved at that moment (testdata/sync)
  oracles/                      original assemblers built or run on the host
    build-asm80.py              ASM80 / Asm80Win 2.02 (PC cross assembler) from its own C++ source: the same bytes as
                                asm80win.exe (which also runs under wine: .recipe/assemblers/asm80.md)
  checks/                       the converted sources against cross assemblers and against what is on the disks
    roundtrip.py                every source of a set of TRD / SCL images to sjasmplus and back through the sjasmplus
                                frontend and backend (the text must not change); optionally assembles every file
    objcheck.py                 a converted ALASM project: every object of each main source's SAVEOBJ table built
                                with sjasmplus against the file ALASM saved on the same disk
    crosscheck.py               the main sources of a set of images to sjasmplus and to pasmo / z88dk (--targets),
                                each assembled, the bytes compared
    pasmocheck.py               pasmo's own sources through the pasmo frontend: pasmo against sjasmplus / pasmo / z80asm builds
    z80asmcheck.py              the cases of z88dk's z80asm test suite (z80asmcases.py) through the z80asm frontend
    zasmcheck.py                zasm's Test and Examples folders against zasm
    fantasmcheck.py             FantASM's tests against FantASM
    dialectcheck.py             zmac (testdata/zmac) and rasm (decrunch routines) against the real assemblers
    specasmcheck.py             Specasm programs: saimport + salink against the converted sjasmplus build
    z80nmatrix.py               the 29 Z80N instructions in each assembler, bytes against sjasmplus
    z80ncheck.py                the programs of a ZXSpectrumNextTests checkout built by sjasmplus (--zxnext=cspect), converted
                                with `zxasm convert --z80n`, built again: the files written must be equal
    symcheck.py                 the labels the symbol module takes from sources (symconv source) against sjasmplus
                                --sym over every main source of a set of images
    lstcheck.py                 the bytes of a sjasmplus listing against a memory dump, line by line (a converted
                                program against a running copy of it)
```

## Conventions

- **Emulator port.** The WebAPI port is a parameter: `--port` (default 8090, the emulator's default). `--url`
  gives a whole address (another host), `UNREAL_ASM_EMULATOR_URL` does the same for every run. Start your own
  instance on ports of its own when other sessions use the defaults (below); stop it by its own PID.
- **Tools.** `UNREAL_ASM_ZXASM`, `UNREAL_ASM_SYMCONV` (the library's `zxasm` / `symconv` from `<build>/bin`),
  `UNREAL_ASM_SJASMPLUS`, `UNREAL_ASM_PASMO`, `UNREAL_ASM_Z80ASM` (+ `ZCCCFG`), or the matching options.
- **Outputs** go to `scratch/`. Commands below run from the repository root with
  `S=tools/verification/unreal-asm`.

## Example: a disk of ALASM sources with the objects ALASM built

```bash
export UNREAL_ASM_ZXASM=<build>/bin/zxasm UNREAL_ASM_SJASMPLUS=<path to sjasmplus>
$UNREAL_ASM_ZXASM convert TheLink.trd --to sjasmplus -o scratch/thelink   # every source + the files INCBIN names
python3 $S/checks/objcheck.py scratch/thelink TheLink.trd
#   GSTUNNE4.asm TUNNELZX.C     489 bytes  EQUAL
#   ...
#   objects equal: 18 of 19
```

## Example: many disks at once

```bash
python3 $S/checks/roundtrip.py scratch/roundtrip disk1.trd disk2.scl ... --assemble
# or the images listed in a file, one per line: --images-from scratch/images.txt
#   images: 6  sources: 99  round-trip differences: 0
#   main sources assembled: ...  without errors: ...
```

Only main sources are assembled (a file no other file of the image includes): the parts of a project assemble
through it.

## Example: the original assembler as the oracle

Start your own emulator instance (on ports of its own when other sessions use the defaults), then:

```bash
PORT=8090   # the default; another one for an instance of your own
UNREAL_WEBAPI_PORT=$PORT UNREAL_CLI_PORT=$((PORT+100)) UNREAL_MCP_PORT=$((PORT+200)) UNREAL_GDB_PORT=$((PORT+300)) \
UNREAL_DEZOG_PORT=$((PORT+400)) UNREAL_ZRCP_PORT=$((PORT+500)) <build>/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
python3 $S/emulator/assemble-in-emulator.py tasm412 TASM_412.TRD scratch/SIN7.\$A 0x7000 256 scratch/sin7.bin \
    --port $PORT
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
python3 $S/lib/zxdisk.py hobeta scratch/PROG.bin PROG.C 0xC00B scratch/PROG.\$C
python3 $S/emulator/assemble-in-emulator.py storm13 STORM_13.SCL scratch/PROG.\$C 0x8000 1024 scratch/prog.bin \
    --extra scratch/INC.\$C --port $PORT
```

ZAsm compiles into its own pages, so the source saves what it built (ZAsm 3.2x and later need more than 128K: add
`--ram 512` and the tool creates a Pentagon 512): it ends with
`saveobj "a:out.C",<address>,<length>` and the script reads `out.C` from the disk afterwards:

```bash
python3 $S/emulator/assemble-in-emulator.py zasm315 ZASM315.trd scratch/PROG.\$a 0x8000 320 scratch/prog.bin \
    --extra scratch/inc1.\$a --port $PORT
```

## Example: two targets against each other

```bash
export UNREAL_ASM_PASMO=<path to pasmo> UNREAL_ASM_Z80ASM=<path to z88dk-z80asm>
python3 $S/checks/crosscheck.py scratch/xc disk1.trd disk2.scl ... --targets pasmo,z88dk
#   equal: 245  no code: 5  pasmo errors: 5  skipped (sjasmplus errors or pages): 164
```

pasmo 0.5.5 writes an empty file when the code spans the whole 64K (its size wraps to 16 bits); such sources show as
"pasmo wrote nothing" unless pasmo is built with that one line widened.

## Example: a converted program against a running copy

```bash
sjasmplus --lst=scratch/build.lst scratch/build.asm
python3 $S/checks/lstcheck.py scratch/build.lst mem64k.bin --from 0x6000
#   90B3  listing C3 F1 FD   memory C3 15 FB   | PRBUF2  JP #FDF1
#   ...
#   bytes compared: 18928  differing: 32
```

## Example: labels from sources against sjasmplus

```bash
export UNREAL_ASM_ZXASM=<build>/bin/zxasm UNREAL_ASM_SYMCONV=<build>/bin/symconv UNREAL_ASM_SJASMPLUS=<path to sjasmplus>
python3 $S/checks/symcheck.py scratch/symcheck --images-from scratch/images.txt
#   differ 1/Marazm: 1 values, 0 missing, 0 extra (first: rnd)
#   ...
#   differ: ...  equal: ...  skipped (sjasmplus errors): ...  labels compared: ...
```

A program that runs while it assembles (ALASM's SNAKE waits for keys) is stopped by `--timeout` (sjasmplus) and by
the layout's own limit. Names that differ only in case share one host file and are skipped.

Write all outputs to `scratch/`.
