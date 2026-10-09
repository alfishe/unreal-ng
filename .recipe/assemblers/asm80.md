# Recipe: ASM80 / Asm80Win (PC cross assembler) with unreal-ng

ASM80 ("Assembler 512 for Z80", Copper Feet, 1995-1999) runs on the PC, not in the Spectrum: the emulator only runs
what it builds. This recipe builds an ASM80 project with the original assembler (under wine) or with its host build,
converts it to sjasmplus with `zxasm`, takes its labels with `symconv`, and runs the result in unreal-ng. Every step
was run on 2026-10-08 against Asm80Win 2.02.

Related: [README.md](README.md) (what every assembler recipe shares), the conversion
`docs/inprogress/2026-10-05-unreal-asm/research-asm80-to-sjasmplus.md`.

## Where ASM80 is

The ZX collection keeps the "Asm80Win 2.02 full pack" in `software/programming/asm80/ASM80CPP.RAR` (KLUG BBS):
`asm80win.exe` (Win32 console), `ASM80.EXE` (DOS 1.7), the C++ sources of both, the manual `Asm80.txt` (CP866), the
tests `TEST.A80` / `TEST2.A80`, `BIN2TRD.EXE` (binaries and hobeta files to a TRD). Unpack it with `unar` (7-Zip
leaves the RAR members empty).

## Build with the original (wine)

CrossOver's wine runs asm80win.exe in a bottle of its own (do not reuse another program's bottle):

```bash
B=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
$B/cxbottle --bottle unreal-asm-oracle --create --template winxp64      # once
cd scratch/asm80-project                                                  # the .a80 files and the binaries *B loads
$B/wine --bottle unreal-asm-oracle --wait-children "$PWD/asm80win.exe" EDITOR.A80 </dev/null
```

- Give wine the full host path of the exe; `asm80win.exe NAME.A80` alone in the command fails ("cannot execute").
- A project that names its files with a drive (`*F c:\speccy\aedit\window.a80`, AEDIT) needs them there: copy them
  into the bottle's `drive_c/speccy/aedit/` and pass `--workdir 'C:\speccy\aedit'`.
- Output: `NAME.bin` (the code from the last `ORG` on), `NAME.lst` (listing and label table), or what the keys ask
  for: `*O` one file per `ORG` (`.B00` …), `*$` hobeta, `*Tn` TAP, `*Z80` a snapshot. Messages are CP866.

## Build with the host port

`tools/verification/unreal-asm/oracles/build-asm80.py` builds `Asm80win.cpp` as a host tool with what the Win32
build had (unsigned char, 32-bit long, MSVC `rand`); it gives the same bytes as the exe:

```bash
python3 tools/verification/unreal-asm/oracles/build-asm80.py <unpacked>/ASM80WIN/Asm80win.cpp scratch/asm80
cd scratch/aedit && A80_ROOT=$PWD/root ../asm80 EDITOR.A80 </dev/null    # c:\dir\f opens $A80_ROOT/dir/f
```

## Convert to sjasmplus

```bash
zxasm convert scratch/aedit --codec asm80 --to sjasmplus -o scratch/aedit-sj   # the directory as one project
zxasm convert TEST.A80 --to sjasmplus -o test.asm                              # one file (detected by its * keys)
```

The directory form names every source in lower case without extension (as `*F` names them) and copies the files
`*B` loads next to the output. `--codec asm80` is needed for files without ASM80's marks (key lines, `MAC`, `=n`,
`DISP`/`ENDD`, `DEFR`, `INF`). The converted project assembles with sjasmplus (or pasmo / z80asm through
`--to pasmo` / `--to z88dk`); give it a harness that saves the range ASM80 wrote:

```text
        DEVICE ZXSPECTRUM128
        INCLUDE "editor.asm"
        SAVEBIN "editor.bin",#7E00,6917
```

What does not convert exactly is warned: `DEFB ?` / `DEFW ?` / `DEFR` (random bytes), `TIME` (the date), text after
`DEFM`'s string, register names used as labels (`sub hl,de` is `SUB 0` in ASM80 and in the conversion).

## Labels

`SymbolsFromProject` lays the project out (`symconv source` takes TR-DOS and tape images; for a host directory use
the library call, or convert and run sjasmplus `--sym`). AEDIT's 207 labels equal the table at the end of ASM80's
listing (`NAME.lst`: name, `#hex`, decimal, `Main` / `Pg#nn` / `Def in EQU` / `Def in DISP` / `Byte`).

## Run the result in unreal-ng

A Spectrum program: load the binary at its address (WebAPI `POST /api/v1/emulator/{id}/memory/write`, or a TRD made
with `BIN2TRD.EXE` / `tools/verification/unreal-asm/lib/zxdisk.py add`) on a machine of its own on ports of your
own (`$PORT`, [README](README.md#your-own-instance)). AEDIT is a Sprinter EXE (`EXE` header, 640x256 mode), for the
`SPRINTER` model; running it was not part of this check.

## Pitfalls

- **`char` is unsigned in asm80win.exe** (project option `/J`): a host build with signed `char` rejects every
  immediate operand ("invalid operand").
- **16 characters of a name count**: `VeryLongLabelName01` and `VeryLongLabelName02` are one label (error 8 when
  both are defined).
- **`IF e` is true when e is 0** (as in ALASM); `IF a=b` when equal; no nesting.
- **`"c"`, not `'c'`**: the manual says single quotes, the program reads double quotes.
