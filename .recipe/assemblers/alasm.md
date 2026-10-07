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
| 5.07 | `ALASMV5.07(AloneCoder).trd` in `zxart/ALASMV5.07(AloneCoder).trd.zip` ([zxart](https://zxart.ee/releasefile/id:155270/ALASMV5.07(AloneCoder).trd.zip)) | `alasm64` | load, assemble |
| 4.5 | `zxart/alasm45.scl` ([zxart](https://zxart.ee/releasefile/id:589336/alasm45.scl)) | `ALASM4.5` | load, assemble |
| 4.44 (Stall edition) | `ALASM444.SCL` in `zxart/ALASM444.ZIP` ([zxart](https://zxart.ee/releasefile/id:249270/ALASM444.ZIP)) | `al64_444` | load, assemble |
| 4.42 | `zxart/Alasm442.SCL` ([zxart](https://zxart.ee/releasefile/id:408359/Alasm442.SCL)) | `alasm442` | load, assemble |
| 3.8c (Alem, 1997) | `al38c_.scl` in `zxart/ALASM38c.zip` ([zxart](https://zxart.ee/releasefile/id:406950/ALASM38c.zip)) | `ALASM` | load, assemble; no `DB` (use `DEFB`) |
| 4.2 | `zxart/ALM.scl` ([zxart](https://zxart.ee/releasefile/id:574325/ALM.scl)), `klug-bbs/ALASM42.ZIP` | `ALM`, `ALASM48` | **did not start** on `PENTAGON`: `ALM` crashed, `ALASM48` ended at once with `0 OK` |

Each version saves sources in its own variant of the format (type `H`): give `zxasm encode --codec alasm --version`
`3.8`, `4.2`, `4.42`, `4.44`, `4.5` or `5.07` (5.07-5.09). ALASM 5.09 compiles `#8000-#BFFF` into its system page:
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

## WebAPI

`tools/unreal-asm/assemble-in-emulator.py alasm509 <ALASM509.trd> <source.$H> <address> <length> <out.bin>
--list-position C,R` runs a whole ALASM 5.09 assemble (it picks the file from `W`'s list by cursor). The calls are
those of [tasm.md](tasm.md#webapi) with `RUN "alasm64"` and the ALASM commands above.

## Pitfalls

- **A blank after the command letter** becomes part of the parameter (`WORK  ALTEST`: File not found).
- **Inverted keyboard**: file names in capitals are typed in lower case.
- **`#8000-#BFFF` in 5.09** is compiled into ALASM's system page, not where the CPU reads it: assemble lower or into
  a page.
- **ALASM 4.2** did not start in unreal-ng here; use 4.42 or later.
