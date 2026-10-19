# Recipe: ALASM in unreal-ng

Run ALASM (Alem 1997, Alone Coder to 5.09) in the emulator: start it, load a source, assemble, list the labels, edit,
export and import plain text, and move sources between ALASM and the host. Every step was run on unreal-ng
(2026-10-07, `PENTAGON` model, 128K) with ALASM 5.09; 3.8c, 4.42, 4.44, 4.5, 5.07 and 5.08 were run through load and
assemble. The ALASM language (16-bit unsigned arithmetic, LOCAL blocks, `\0`-style macros) is in
[research-alasm-to-sjasmplus.md](../../docs/inprogress/2026-10-05-unreal-asm/research-alasm-to-sjasmplus.md), the
file format in [research-alasm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-alasm.md); the command
reference is ALASM's own help (`al50help` on the 5.0x disks, decode it with `zxasm decode`). Shared steps (own
instance, entering TR-DOS, keys, host exchange, labels): [README.md](README.md).

## Versions and where they are

| Version | Image (collection: `software/programming/alasm/`) | Loader | Status here |
|---|---|---|---|
| 5.09 (Alone Coder, 2011) | `x/ALASM509_STS75/ALASM509_STS75.TRD` ([alonecoder.nedopc.com](http://alonecoder.nedopc.com/zx/ALASM509_STS75.rar), [zxart](https://zxart.ee/releasefile/id:249271/ALASM509.zip)) | `alasm64` (64 columns), `alasm42`, `alasmatm` | load, assemble, symbol list, edit, export, import: all run |
| 5.08 | `x/ALASM508/ALASM508.TRD` ([zxart](https://zxart.ee/releasefile/id:406951/ALASM508.zip)) | `alasm64` | load, assemble |
| 5.05 (build 01728, 15.04.06) | `wdc/alasm505.zip` (binaries, ALASM's sources, change log), `vtrd/PT+ALASM.ZIP` ([vtrd](https://vtrd.in/system/PT+ALASM.ZIP)) | `alasm64` | load, assemble (2026-10-08) |
| 5.0 (2005) | `klug-bbs/ALASM5_0.RAR` | `alasm64` | load, assemble (2026-10-08) |
| 5.07 | `ALASMV5.07(AloneCoder).trd` in `zxart/ALASMV5.07(AloneCoder).trd.zip` ([zxart](https://zxart.ee/releasefile/id:155270/ALASMV5.07(AloneCoder).trd.zip)) | `alasm64` | load, assemble |
| 4.5 | `zxart/alasm45.scl` ([zxart](https://zxart.ee/releasefile/id:589336/alasm45.scl)) | `ALASM4.5` | load, assemble |
| 4.44 (Stall edition) | `ALASM444.SCL` in `zxart/ALASM444.ZIP` ([zxart](https://zxart.ee/releasefile/id:249270/ALASM444.ZIP)) | `al64_444` | load, assemble |
| 4.42 | `zxart/Alasm442.SCL` ([zxart](https://zxart.ee/releasefile/id:408359/Alasm442.SCL)) | `alasm442` | load, assemble |
| 3.8c (Alem, 1997) | `al38c_.scl` in `zxart/ALASM38c.zip` ([zxart](https://zxart.ee/releasefile/id:406950/ALASM38c.zip)) | `ALASM` | load, assemble; no `DB` (use `DEFB`) |
| 4.2 | `zxart/ALM.scl` ([zxart](https://zxart.ee/releasefile/id:574325/ALM.scl)), `klug-bbs/ALASM42.ZIP` | `ALM` (from a disk in the order ALM, sts6.2, alasm48) | load, assemble, sync inject (2026-10-18); a key closes its title screen; macro parameters `:0`..`:9` |

Each version saves sources in its own variant of the format (type `H`): give `zxasm encode --codec alasm --version`
`3.8`, `4.2` (also 4.3), `4.42`, `4.5` (also 4.43), `4.44` (4.44-4.46), `5.0`, `5.05` or `5.07` (5.07-5.09). ALASM 5.09 compiles `#8000-#BFFF` into its system page:
assemble below (the checks used `#6000`) or with `ORG addr,page`.

## MCP (preferred)

```text
media             {"action":"insert","slot":"A","path":"/abs/scratch/al509w.trd","discard":true}
emulator_manage   {"action":"reset"}
type_input        {"action":"tap","key":"down"}                 # x4
type_input        {"action":"tap","key":"enter"}
invoke_api        {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"RUN \"alasm64\""}}
capture_media     {"action":"screenshot","filename":"scratch/alasm-started.png"}
```

ALASM 5.09 tests the memory (`128 k mask#07`) and shows the `A>` prompt (the current drive). Commands are words:
type the capital letter, ALASM writes the whole word and a blank, then the parameter, then ENTER. So **no blank
after the letter**: `waltest` becomes `WORK ALTEST`; `w altest` becomes `WORK  ALTEST` and finds no file.

```text
type_input {"action":"type","text":"waltest","delay_frames":6}     # inverted keyboard: arrives as ALTEST
type_input {"action":"tap","key":"enter","frames":4}               # "Loaded Ok to #C6 ALTEST .H" (#C6 = the page)
type_input {"action":"type","text":"a","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}               # ASSEMBLE
inspect_state {"aspects":["memory"],"address":24576,"length":8}
```

The report: the line count, then `Symbols:#.... Post:#.... Macro:#.... RUN:#6000 $:#6008` (RUN = the last ORG, `$`
where it stopped); an error shows the line number, the error code and the line. `Illegal` = no text to assemble.

Commands (ALASM 5.0x help): **Q**uit [addr[,page]], **D**ebug (STS), **R**un [addr[,page]], **W**ork [drv:][name]
(no name: choose from the catalog with the cursor), **C**atalogue [drv:][mask], **N**ame, **E**dit [name], mer**G**e,
coun**T** expression, s**Y**mbol [mask|=value], **A**ssemble, **S**ave [+|!][drv:], **M**ove, **I**nfo, **P**age n,
dri**V**e d, **J**umb, **B**an [+]; overlay commands con**F**ig, imp**O**rt [drv:][mask], e**X**port [+][drv:]. BREAK
interrupts a command.

```text
type_input {"action":"type","text":"y","delay_frames":6}           # SYMBOL: "#6005 TAB", "#6000 START", "Found:#0002"
type_input {"action":"tap","key":"enter","frames":4}
```

`E` + ENTER opens the editor on the current page (status line: page, name, free memory, the buffer, line, size,
Ins/Ovr). Extend gives the editor's command line `E>`; `q` + ENTER returns to `A>`:

```text
type_input {"action":"type","text":"e","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}
type_input {"action":"combo","keys":["cs","ss"],"frames":12}
type_input {"action":"type","text":"q","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}
```

Editor commands after Extend (help): Quit, Assemble, counT, Begin / End, Search, sYmbol, Xreplace, coDe, Restore,
Clear, Ins/ovr, Undo, Labels. In the text: SS+I next search, SS+SPACE delete the line into the buffer, SS+Q / SS+E
home / end, SS+W copy the line, CS+SPACE / CS+1 tab or Rus/Lat, CS+2 insert a blank, CS+9 / CS+0 delete,
SS+ENTER choose a text in memory.

### Plain text out and in

`x` + ENTER exports the current text; the overlay asks `Load overlay (Y/N)?` (`y`). The file has the same name, type
`C`, start 0, CR line ends (`        ORG #6000\rSTART   LD A,3\r...`).

`o` + `b:` + ENTER imports from drive B: a list of the files (choose with the cursor, ENTER), then `Upcase tokens
(Y/N)?` and `Split lines by ':' (Y/N)?`. A host folder in drive B is the easy source (see [README.md](README.md)):
a CR LF text file in it imports as is.

```text
media      {"action":"insert","slot":"B","path":"/abs/scratch/fold2","discard":true}
type_input {"action":"type","text":"ob:","delay_frames":6}
type_input {"action":"tap","key":"enter","frames":4}
type_input {"action":"tap","key":"enter","frames":4}          # the highlighted file
type_input {"action":"tap","key":"y","frames":4}              # upcase tokens
type_input {"action":"tap","key":"n","frames":4}              # don't split at ':'
```

After the import the current drive is B (`B>`); by the help `va` + ENTER (driVe A) switches back before saving to A (not tried).

### Older versions

The same commands. 4.42 - 4.5 report `Symbols:#0005-#FF6C-#C000`; 3.8c shows a help screen first (one key closes it:
send ENTER before the first command), has no `DB` (the line errs with code `#0D`: write `DEFB`) and says `System
data mismatch` when asked to assemble with no text.

### A host source into ALASM (every build, 3.8c to 5.09, 4.2 included)

At the `A>` prompt, `asm_source {"action":"sync_inject","path":"/abs/main.asm"}` writes a sjasmplus source (or any
dialect with `from`) as ALASM's current text; `a` + ENTER then assembles it. Refused in the editor and while a
command runs. 3.8c reads none of `( ) & | ! ~ < >`, MACRO or IF: the conversion writes such expressions as values,
expands macros and decides IFs itself (a comment line tells which part), so only the source's bytes match, not its
structure. Whether ALASM waits for a command shows on its stack: the return addresses of the line input and the
command loop follow each other above SP (5.09: `#91E3`, `#9707`; the other builds in
[research-sjasmplus-to-alasm.md](../../docs/inprogress/2026-10-05-unreal-asm/research-sjasmplus-to-alasm.md) §4). A
command, the editor included, has another address in that slot. Read with the 64-column loaders.

A `path` source brings the files it INCLUDEs (relative to the file that INCLUDEs each). Each goes into an ALASM text page
of its own, and its INCLUDE is written `INCLUDE "NAME",page`: ALASM then finds the text loaded and reads no disk. The
reply lists them under `includes`. 5.05-5.09 have pages 4 and 0 free, 4.5 pages 0 and 3, 4.2 pages 4 and 0, 5.00 / 4.42-4.46 / 3.8c page 4: a
project with more sources is refused (put it onto the disk with `zxasm inject main.asm disk.trd --assembler ...`).

`asm_source {"action":"sync_push_watch","path":"/abs/main.asm"}` does the same on every save of the file (or of a file
it INCLUDEs). In the editor the write waits until you leave it (Extend, `q`, ENTER); a text you changed in ALASM is a
conflict and stays unless `force`.
Details and the oracle script:
[asm-sources.md](../analysis/asm-sources.md#writing-a-host-source-into-the-assembler-part-b).

### A host source into an ALASM snapshot or disk, no emulator (5.09)

`zxasm` does it on files. The idle template is ALASM 5.09 at `A>` with no text
(`core/src/3rdparty/unreal-asm/testdata/templates/alasm-5.09.szx`, also `.sna` and `.z80`; made by
`tools/verification/unreal-asm/emulator/make-template.py alasm509 <ALASM509.trd> out.szx --also sna,z80`):

```bash
zxasm inject main.asm alasm-5.09.szx -o work.szx       # main.asm (sjasmplus) as ALASM's text; load work.szx, type a + ENTER
zxasm probe work.szx                                   # alasm-5.09  100  MAIN, 1242 bytes  idle
zxasm extract work.szx --to sjasmplus -o back.asm      # and back
zxasm inject main.asm disk.trd --assembler alasm-5.09  # MAIN.H on a disk (.scl .fdi .udi .td0 too): w + main + ENTER in ALASM loads it;
                                                       # the files main.asm INCLUDEs as files of their own beside it
```

The same on a surface: `asm_source {"action":"sync_inject","path":"/abs/main.asm","snapshot":"/abs/alasm-5.09.szx","output":"/abs/work.szx"}`
(the machine is not touched; `sync_extract` / `sync_probe` / `sync_status` take `snapshot` too).

## WebAPI

`tools/verification/unreal-asm/emulator/assemble-in-emulator.py alasm509 <ALASM509.trd> <source.$H> <address> <length> <out.bin>
--list-position C,R` runs a whole ALASM 5.09 assemble (it picks the file from `W`'s list by cursor). The calls are
those of [tasm.md](tasm.md#webapi) with `RUN "alasm64"` and the ALASM commands above.

## Pitfalls

- **A blank after the command letter** becomes part of the parameter (`WORK  ALTEST`: File not found).
- **Inverted keyboard**: file names in capitals are typed in lower case.
- **`#8000-#BFFF` in 5.09** is compiled into ALASM's system page, not where the CPU reads it: assemble lower or into
  a page.
- **ALASM 4.2 from the zxart `ALM.scl` falls to 48 BASIC**: its loader reads the raw sectors that follow it (48 of
  STS 6.2 into page 7, then 64 of `alasm48` into page 4), and that SCL holds `alasm48` first. Rebuild the disk in the
  order ALM, sts6.2, alasm48 (an SCL to TRD copy that writes the files in that order); then `ALM` starts it.
- **The `W` list draws for a moment**: a cursor key pressed right after ENTER is lost and the wrong file loads (seen
  with 5.0 / 5.05). Wait about 3 s, move with keys held 6 frames, and check the highlighted name before ENTER
  (`assemble-in-emulator.py alasm509` saves `<out>.selected.png`).
- **`IF` before 5.03 is `IF0`**: the same code `#D3`, renamed in 5.03; it takes the block when the expression is 0
  (ALASM 4.2's help, checked in ALASM 5.0). `IFN` takes it when not 0.
- **`UNTIL` (4.5 … 5.05) and `UNTIL0` (5.07 …)** are one keyword (`#D2`): `zxasm encode --version` picks the spelling.
- **`ADD B` is an error in 5.09** (code `#00`, nothing put down): write `ADD A,B` (also ADC, SBC; `ADD 5`, `ADD (HL)`, `ADD HX` too). Seen when the instruction oracle came out one
  byte short per `ADD x` / `ADD A,x` pair.
- **`"AB"` is `#4241`**: the first character low, the last high (`DW "AB"` = `41 42`, `LD HL,"AB"` = `21 41 42`,
  `DW "ABC"` = `41 43`), not the `#4142` other assemblers make of `'AB'`.
- **A template saved in the editor is refused**: the idle rule wants the `A>` prompt (`zxasm probe` says `busy`).
- **unreal-ng's `.z80` writer** can start a run right after a single `ED`, and the page then reads wrong in every
  reader (reported). Prefer `.szx` for templates; `zxasm probe` on each format shows whether they agree.
