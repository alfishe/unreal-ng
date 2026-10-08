# ZX assembler sources and symbols (unreal-asm)

unreal-ng reads and writes the sources of the Spectrum's own assemblers, converts them to a cross assembler's syntax,
and moves labels between the emulator, those sources and the tools around it (sjasmplus, pasmo, z88dk, IDA, Ghidra,
MAME). The work is done by the `unreal-asm` library (`core/src/3rdparty/unreal-asm`, standard C++20, no emulator
types) and its two command-line tools, `zxasm` (sources) and `symconv` (symbols). The emulator's debugger reads the
symbol formats through the same library.

Design and research: [docs/inprogress/2026-10-05-unreal-asm/](../inprogress/2026-10-05-unreal-asm/README.md).
Running the assemblers themselves inside the emulator: [.recipe/assemblers/](../../.recipe/assemblers/README.md).

## What it reads

| Assembler | Codec (`zxasm --codec`) | Versions | Converts to sjasmplus |
|---|---|---|---|
| TASM (Rst7, XL Design, KVA) | `tasm` | 2.0, 3.0-3.5, 4.0 / 4.4, 4.12, 5.0 beta, 5.5 beta | yes (3, 4.0, 4.12, 5.0) |
| ALASM (Alem, Alone Coder) | `alasm` | 3.8, 4.2 / 4.3, 4.42, 4.43 / 4.5, 4.44-4.46, 5.0, 5.05, 5.07-5.09 | yes |
| ZX-ASM / ZAsm | `zxasm` | 2.4-2.6, 3.0-3.10, Lite 1.07, 3.15-4.20 | yes |
| STORM | `storm` | 1.0beta, 1.2-1.3i | yes |
| MASM (AIG, KSA) | `masm` | 1.0 demo, 1.1, 2.0, 3.0 | yes (1.x, 3.0) |
| GENS (HiSoft Devpac) | `gens` | GENS1, GENS2-4 | yes |
| ZEUS | `zeus` | 1983, GG, 1.1 beta / v7.E, Primus Assembler 2.9 | yes |
| XAS | `xas` | 4.18, 5.05, 7.43, 7.43c, 9.07m, 9.10 | yes |
| ASM80 / Asm80Win (PC cross assembler) | `asm80` | 2.02 (text) | yes |
| PROMETHEUS (Proxima) | `prometheus` | the editor's save (records + symbol table) | yes |
| Laser Genius (Oasis Software) | `lasergenius` | the editor's tokenized text (tape blocks joined, the Beta Disk file); Phoenix paragraphs kept as bytes | yes |
| sjasmplus | `sjasmplus` | text | - |
| any text | `text` | CP866, KOI8-R, CP1251, UTF-8; any line end | - |

Every codec decodes the assembler's own file to UTF-8 text and encodes text back to the same bytes (byte for byte
for files the assembler wrote; the research documents give the corpus numbers). Containers: hobeta files
(`NAME.$A`), TR-DOS images (`.trd`, `.scl` through `tools/verification/unreal-asm/lib/zxdisk.py scl2trd`) and tape images (`.tap`,
`.tzx`).

The conversion goes through a neutral intermediate form; the sjasmplus output of every dialect is checked against
the original assembler running in the emulator: the converted source assembles to the same bytes (see the
`research-*-to-sjasmplus.md` documents). pasmo and z88dk's z80asm are second targets.

## zxasm

```bash
zxasm formats                                  # the codecs and their versions
zxasm detect   'GAME.$H'                        # which codec reads it, and why
zxasm decode   'GAME.$H' -o game.txt            # UTF-8 text (the version detected is printed)
zxasm encode   game.txt --codec alasm --version 5.07 -o 'GAME.$H'   # back to ALASM (a hobeta file)
zxasm files    disk.trd                        # the catalog, with the codec of each file
zxasm decode   disk.trd --file MAIN.A -o main.txt
zxasm convert  'GAME.$H' --to sjasmplus -o game.asm
zxasm convert  disk.trd --to sjasmplus -o out/ # the whole project: INCLUDE resolved, INCBIN files extracted,
                                                # ZX-ASM's LOADTAB tables read from the image
zxasm convert  project/ --codec asm80 --to sjasmplus -o out/   # a host directory (ASM80) as one project
zxasm check    'GAME.$H'                        # decode + encode: byte-exact?
```

Decoding without `--version` tries every version of the format and keeps the newest one that reproduces the file;
giving the version (from the catalog or the user) skips that: 12 (ZX-ASM) to 19 (ALASM) times faster.

## Symbols

The symbol formats (`symconv formats`):

| Format | What | Reads | Writes |
|---|---|---|---|
| `native` | the emulator's own `*.usym.json`, lossless (pages, kinds, scopes, source lines, comments) | yes | yes |
| `unreal-map`, `simple-sym`, `unreal-l`, `vice`, `sjasm-equ`, `z88dk-defc` | the files the debugger has always loaded (`.map`, `.sym`, Unreal's `user.l`, VICE, `.s`, `.z88`) | yes | yes |
| `sjasmplus-sym`, `sjasmplus-sld`, `sjasmplus-lst`, `cspect-map` | sjasmplus `--sym`, `--sld`, `--lst`, `CSPECTMAP` | yes | yes |
| `pasmo`, `z88dk-map` | pasmo's symbol file, z80asm's `-m` map | yes | yes |
| `ida-idc`, `ida-python`, `ghidra`, `mame` | scripts that set names in IDA, Ghidra (System.map), MAME (comments) | yes | yes |

```bash
symconv game.sym --to ida-python -o game.py          # any format to any format
symconv game.sld --to unreal-map --pages comment     # what the target cannot hold: fold | comment | drop
symconv source disk.trd --main MAIN --to unreal-map -o main.map   # the labels of a source project
symconv live 3:ram3.bin 6:ram6.bin --to native       # the label table of ALASM / XAS from RAM pages
```

- **Labels from sources** (`symconv source`): the labels a TASM, ALASM, STORM, ZX-ASM, MASM, GENS, ZEUS, XAS, ASM80, PROMETHEUS, Laser Genius or
  sjasmplus project defines, with the value each gets, its kind and its source line. The values come from a layout
  of the project's sjasmplus conversion (no bytes are built), checked against sjasmplus on the collection's disks.
  `symconv source` reads TR-DOS and tape images; an ASM80 project (host files) goes through the library's
  `SymbolsFromProject`.
- **Label tables in RAM** (`symconv live`): ALASM keeps its labels in RAM page 3, XAS in page 6; the scanner finds
  the table in a set of 16 KB pages and reads it. STS uses ALASM's table.
- A name the target cannot take is renamed by its rules and reported; a page symbol in a format without pages is
  folded to its CPU address, written as a comment, or dropped.

The debugger loads any of the text formats with `manage_symbols load_labels` / `POST /symbols/load`
([.recipe/analysis/symbols-listings-and-source-stepping.md](../../.recipe/analysis/symbols-listings-and-source-stepping.md));
a `.map` written by z80asm is recognized by its content.

## Speed

`unreal-asm-benchmarks` (configure with `-DBENCHMARKS=ON`, build the target) measures the codecs, the conversion and
the symbol module on the test corpus; the results and the targets are in
[test-and-benchmark-plan.md](../inprogress/2026-10-05-unreal-asm/test-and-benchmark-plan.md) §6.
