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
| Power Assembler (Sergeyev) | `pasm` | 3.0 (text, CR LF) | yes |
| Laser Genius (Oasis Software) | `lasergenius` | the editor's tokenized text (tape blocks joined, the Beta Disk file); Phoenix paragraphs kept as bytes | yes |
| sjasmplus | `sjasmplus` | text | - |
| any text | `text` | CP866, KOI8-R, CP1251, UTF-8; any line end | - |

Every codec decodes the assembler's own file to UTF-8 text and encodes text back to the same bytes (byte for byte
for files the assembler wrote; the research documents give the corpus numbers). Containers: hobeta files
(`NAME.$A`), disk images of every format the emulator reads (`.trd`, `.scl`, `.fdi`, `.udi`, `.td0`, `.dsk` ...: their
TR-DOS files, through the emulator's disk library unreal-disk) and tape images (`.tap`, `.tzx`).

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
zxasm convert  C33_F9.trd --with Z33_F9.trd --to pasmo -o out/  # a set of disks: what the sources INCLUDE that their
                                                # disk lacks comes from the others (the first disk naming a file)
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

- **Labels from sources** (`symconv source`): the labels a TASM, ALASM, STORM, ZX-ASM, MASM, GENS, ZEUS, XAS, ASM80, PROMETHEUS, Laser Genius, Power Assembler or
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

### Labels from a source in the debugger

The debugger can take the labels straight from a source, with no label file:

- In the Disk files dialog, select the source and press **Import Labels**.
- Or in the label editor, choose **File > Import Labels from Source...** and pick a source file or an image. When an
  image holds several sources, a list asks which one is the main source.
- From scripts: `manage_symbols import_source` (MCP), `POST /symbols/import/source`, `symbols import-source <path>`
  (CLI), `symbols_import_source` (Lua), `emu.symbols_import_source` (Python).

**Where the values come from.** No assembler runs. The source is read as the assembler reads it:

- The address starts at `ORG`.
- Every instruction moves it by its length; `DB`, `DW`, `DS`, `ALIGN` and `INCBIN` by their sizes; `DISP` / `PHASE`
  change it the way they do in the assembler.
- The passes repeat until no label moves.

The values are the ones the assembler's label table would hold after assembling the project. On real disks they
match sjasmplus for 511 of 530 sources; the rest draw random numbers while they assemble, or hit a sjasmplus quirk.
Each label comes with its kind (code, data, constant), the file and line that define it, and the RAM page that
`ORG address,page` named.

**A source on a disk** is a project: the files it INCLUDEs and the files whose size INCBIN takes come from the same
disk. The disk is read as the emulator holds it now, unsaved edits included.

**Importing again** replaces the source's labels and does not add copies. The labels sit in their own set
(`source:disk:A/NAME`), which the label editor's Sets tab can switch off or drop.

**When the result is incomplete**, the report says which labels got no value and why. Typical causes are a missing
INCLUDE file and a label inside a block an `IF` leaves out. A missing INCBIN file counts as empty, so the labels after
it are short by its size.

**If you have the assembler's own table, use it instead.** A label file it wrote (sjasmplus `--sym`, a `.map`, a label
file on the disk) imports with Import..., and ALASM's or XAS's table in RAM after assembling imports with the live
scan. Details:
[.recipe/analysis/symbols-import-export.md](../../.recipe/analysis/symbols-import-export.md#labels-from-a-source).

## The source in the running machine

The debugger reads the source that ALASM (3.8c, 4.42-4.46, 4.5, 5.00-5.09), TASM (2.0, 3.0, 3.2, 3.5, 4.0, 4.4, 4.12), XAS (4.18-9.10) STORM (1.0beta, 1.3), ZX-ASM 2.4 / 2.5 / 2.6 / ZX ASM 3.0 / 3.10 / ZAsm 3.15 / 3.2x / Lite 1.07 / 3.3.02 / 3.3.51 / 3.3.Final / 3.4 / 3.80.4 / x64.1 / 4.0x8 / 4.x64 / 4.20 or MASM 1.0 demo / 1.1 / 1.3 / 2.0 / 3.0, GENS4 / GENS3M21 / GENS4B / GENS 4.3t, ZEUS 1983 / 1.1 / GG / v7.E, Primus 2.9, Laser Genius 1.04, PROMETHEUS 48K, Power Assembler 3.0 is editing in the machine, with no need to save
it to a disk first. You get it as text, as the file the assembler's own SAVE would write, or converted to sjasmplus,
pasmo, z88dk or ALASM:

```text
asm sync                                  # which assembler, its text, where the cursor is
asm sync extract --as dialect --to sjasmplus --output live.asm
```

The same is `asm_source` `sync_status` / `sync_extract` in MCP and `GET /asm/sync` in the WebAPI
([.recipe/analysis/asm-sources.md](../../.recipe/analysis/asm-sources.md#the-source-an-assembler-holds-in-ram-asm-synchronizer)).
The machine's memory is copied between two frames and never written. A line you are typing in TASM is part of the
text. ALASM keeps the line under the cursor apart until Enter, and the status says so.

`asm sync watch` keeps following the source while you type in the guest. Half a second after you stop typing, the
source is built on the host:

- its labels show in the debugger at once (the symbol set `live:sync:<assembler>`);
- `asm sync hints` lists the errors with their line numbers, for example `line 105: unknown symbol NOWHERE`;
- with `--output`, a host file (text, the assembler's format or sjasmplus) is written again after each build;
- a source that includes others (`INCLUDE`, XAS `LTEXT`, GENS `*F` ...) is built with them: they come from the other texts
  ALASM holds in memory, from the disk in drive A, and from host folders named with `--project` (for example `--project disk:A,/home/me/lib`); a file found
  nowhere is reported in the hints.

In the Qt debugger, the **Live source** button shows all of this in one window: the text with the guest's cursor line
highlighted, the errors and warnings on their lines and in a list below it, a status line, and the Watch, Inject...,
Push, Extract... and Convert... buttons.

`asm sync unwatch` stops the watch.

The other way works too, for ALASM (3.8c to 5.09), XAS (4.18 to 9.10), STORM (1.3, 1.3i), TASM 4.12, ZAsm 3.15, MASM 1.1, GENS4, ZEUS 1983, Primus 2.9, Laser Genius 1.04, PROMETHEUS 48K and Power Assembler 3.0 so far: `asm sync inject --path main.asm` writes a host source
(sjasmplus, or any dialect the converter reads) into the running ALASM as its own text, while ALASM waits at its `A>`
prompt (XAS: at its command line, EXT; STORM: while its editor waits for a key; TASM: at `TASM128>`; ZAsm: at its COMMAND prompt; MASM: at `MASM128>`; GENS4 and ZEUS: at their prompts; Laser Genius, PROMETHEUS and PASM: in their editors). The assembler then assembles it as if you had typed it. The files main.asm INCLUDEs go into ALASM texts of their own.
`zxasm convert --to alasm` gives the same text as a file (`--to-version` for another version: `--to tasm --to-version 4.12`). More assemblers are planned
([asm-synchronizer](../inprogress/2026-10-05-unreal-asm/asm-synchronizer.md)).

To edit on the host and assemble in ALASM, `asm sync push-watch main.asm` writes the file again on every save (and on
a save of a file it INCLUDEs). While ALASM is in its editor or assembling, the write waits for its command line. If
you changed the text inside ALASM since, the watch leaves it alone and reports a conflict (`--force` overwrites it).
`asm sync push-unwatch` stops it; in the Live source window it is the Push button.

Snapshots and disks work both ways without the emulator. `zxasm inject main.asm alasm-5.09.szx -o work.szx` puts
the source into a snapshot of ALASM waiting at its prompt (a ready template ships with the library's test data);
load `work.szx` and assemble. `zxasm inject main.asm disk.trd --assembler alasm-5.09` writes it as ALASM's file onto
a disk image of any format the emulator reads (TRD, SCL, FDI, UDI, TD0 ...). `zxasm probe work.szx` says which assembler a snapshot holds, and
`zxasm extract work.szx --to sjasmplus` takes its text out again. In the emulator, the `asm sync` verbs take
`--snapshot` for the same on a file.

## Speed

`unreal-asm-benchmarks` (configure with `-DBENCHMARKS=ON`, build the target) measures the codecs, the conversion and
the symbol module on the test corpus; the results and the targets are in
[test-and-benchmark-plan.md](../inprogress/2026-10-05-unreal-asm/test-and-benchmark-plan.md) §6.
