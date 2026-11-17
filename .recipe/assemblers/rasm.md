# Recipe: rasm (Roudoudou's PC cross assembler) with unreal-ng

rasm (Edouard Berge, v3.3.3, MIT) runs on the PC; the emulator only runs what it builds. This recipe builds a rasm
project with rasm itself, converts it to sjasmplus with `zxasm` (the conversion assembles the source with unreal-asm's
port of rasm, so the result has rasm's bytes), takes its labels, and runs the result in unreal-ng. Every step was run on
2026-11-17 against rasm 3.3.3 (commit a5796b9).

Related: [README.md](README.md) (what every assembler recipe shares), the design
`docs/inprogress/2026-10-05-unreal-asm/rasm-engine.md`, the pieces `research-rasm-*.md` next to it.

## Where rasm is

The sources are in `emulators/rasm` (a checkout of the author's repository, kept read-only: build a copy). rasm's own
decrunch routines are in its `decrunch/` folder.

## Build rasm

```bash
cp -R <emulators>/rasm scratch/rasm-src && cd scratch/rasm-src
sed -i '' 's/-arch arm64 -arch x86_64/-arch arm64/; s/-mcpu=native//' makefile.MacOS   # one architecture, no -mcpu on clang
make -f makefile.MacOS                                                                 # ./rasm
./rasm -autotest                                                                        # rasm's own snippets: all OK
```

- Output names: `rasm main.asm` writes `rasmoutput.bin` (the code between the lowest and highest byte written), `-o name`
  gives `name.bin` / `name.sna`; `SAVE "file",start,size` writes the file and no `.bin`.
- `BUILDZX` writes a 48K `.sna`: banks 5 / 2 / 0 at #4000 / #8000 / #C000, each the first 16K of the 64K rasm `BANK`
  (`ORG #C000,0` puts code there); `BANKSET 0` makes bank 0 the whole 64K. `RUN pc,sp` pushes pc at sp.
- Labels are case-insensitive (rasm keeps them upper case); `CODE`, `SUB` and other directive names are not labels.

## Convert to sjasmplus

```bash
zxasm convert main.asm --codec rasm --from rasm --to sjasmplus -o main.sja
zxasm convert main.asm --project --codec rasm --from rasm --to sjasmplus -o outdir   # with the INCLUDEd files
```

`--codec rasm` is needed for sources without marks rasm alone has (most of them). The conversion is the program as
rasm assembled it:

- macros, `REPEAT` / `WHILE` and generated names expanded; `IF` / `SWITCH` reduced to what was assembled;
- instructions keep their labels where the label gives the value rasm wrote, else the number; `DEFB` / `DEFW` the same;
  everything else (strings, floats, `INCBIN` transforms, `SUMMEM` ...) as `DB` of rasm's bytes with the source line as
  a comment;
- rasm's enhanced instructions (`LD HL,SP`, `LD BC,DE`, `RLC BC` ...) are several Z80 instructions: `DB` of their bytes;
  aliases (`EXA`, `SL1`, `SHL`, `SHR`, `OTD`, `OTI`) become the standard names;
- crunched sections (`LZ48` / `LZ49` / `LZ4` / `LZX7` / `LZX0` / `LZX0B` / `LZEXO` / `LZAPU` / `LZSA1` / `LZSA2` ...
  `LZCLOSE`): the crunched bytes rasm wrote (bit-exact compressors), the labels inside as `EQU` of rasm's values;
- `SAVE`: `SAVEBIN` of the same range (a `DEVICE NOSLOT64K` is added); AMSDOS / HOBETA headers are reported;
- `BUILDZX`: `OUTPUT "rasmoutput.sna"` with rasm's 27-byte header and the 48K, so the `.sna` is rasm's byte for byte
  (sjasmplus' `SAVESNA` would write its own system variables and stack);
- CPC media (`BUILDSNA`, `BUILDCPR`, `BUILDROM`, `BUILDTAPE`, `EDSK`, `HFE`, `AMSDOS`, `RELOCATE` .rel files) are
  reported "not converted", never dropped silently.

A source rasm itself refuses (or a file of a project converted alone, whose labels are in other files) is converted line
by line with rasm's rules; the conversion says so ("does not assemble alone ... converted line by line").

## Convert to rasm

`zxasm convert file.asm --to rasm` writes rasm syntax from any dialect (the rasm backend,
`research-sjasmplus-to-rasm.md`): `SAVEBIN` becomes `SAVE`, `DEVICE` / `OPT` / `DISPLAY` are left out with a note;
pages (`ORG` with a page, `DEVICE` memory pages) and `SAVESNA` / `SAVETAP` / `SAVETRD` stay notes in the text. Check
with `tools/verification/unreal-asm/checks/rasmcheck.py` (rasm's 64K against sjasmplus'). rasm stops on code written
over code ("Assembling overwrite"), which sjasmplus allows: such projects do not build.

## Labels

`SymbolsFromProject` (symbols/fromsource.h) gives the labels with the values rasm gave them and their defining lines in
the source; names are the source's spelling with `.` and `@` of local labels as `_` (`start.loop` -> `start_loop`).

## Check the conversion

```bash
S=<emulators>/github/sjasmplus/sjasmplus
python3 tools/verification/unreal-asm/checks/rasmconvertcheck.py --zxasm <build>/bin/zxasm --sjasmplus $S
python3 tools/verification/unreal-asm/checks/dialectcheck.py rasm <emulators>/rasm --zxasm <build>/bin/zxasm \
    --rasm scratch/rasm-src/rasm --sjasmplus $S
```

The first takes rasm.c's 153 AUTOTEST snippets that write a binary (testdata/rasm/autotests), the second rasm's 13
decrunch routines; every one gives rasm's bytes (2026-11-17).

## Run the result in unreal-ng

A Spectrum program: load the `.sna` (WebAPI / MCP `media` load) or the binary at its address
(`POST /api/v1/emulator/{id}/memory/write`) on an instance of your own (`$PORT`, [README](README.md#your-own-instance)).

## Pitfalls

- **ORG inside a crunched section** is an error in rasm; code that runs elsewhere goes in its own section after the
  copy.
- **`%` is the modulo** (and `MOD`); `%101` is binary only at the start of a number.
- **`ORG a,b`**: code at `a`, bytes written at `b` of the current bank; in a 48K BUILDZX the bank's first 16K counts.
- **One file read alone** cannot be assembled when its labels live in other files: convert the project from its main
  source.
