# GENS4B 4.1 (MOA B-Disk version) — how it starts, how it talks to TR-DOS, where its text is

> **The point.** GENS4B is HiSoft's GENS4 assembler with its tape calls replaced by TR-DOS calls (MOA, 1990).
> It ships as one CODE file and **no loader**, and it only works when TR-DOS has already made its system
> variables. This dossier says why, gives the start sequence that works and the ones that do not, lists every
> TR-DOS call in the binary, and gives the memory layout of the source text (what the asm-synchronizer reads).
> Every claim was checked in unreal-ng on 2026-10-10 (port trace, TTD write journal and coverage, memory reads).

## What's in this folder

| artifact | what it is |
|---|---|
| [`gens4b_org7530.bin`](gens4b_org7530.bin) | the code, 12174 bytes, org 30000 (`#7530`): the data of `GENS4B.$C` |
| [`gens4b-z80dasm.asm`](gens4b-z80dasm.asm) | machine-generated z80dasm listing (`z80dasm -a -l -g 30000 -t`), the reference for the addresses below |

Source: `GENS4B.$C` in `MONSGENS.LZH` (ZX collection, `software/programming/gens-mons/`; KLUG BBS), hobeta,
name `GENS4/B`, type `C`, start 30000, length 12174. Its manual is `GENS4.DOC` (Russian, cp866) in the same archive.
The TR-DOS facts below come from P. Fedin's full description and disassembly of the TR-DOS 5.04T ROM
(`os/tr-dos/trdosdis.rar` in the collection, 1996) and `docs/inprogress/2026-01-21-trdos-analyzer/trdos-rom-forensics.md`.

## Starting it

The manual's rule (GENS4.DOC 1.1): load the code, then `RANDOMIZE USR <address>` from BASIC; restart with the same.
GENS4B adds a condition the manual does not state: every disk command goes through the TR-DOS service entry
`#3D13` (15635), and those services use TR-DOS's system variables (the area from 23746 / `#5CC2`, the file
descriptor at 23773 / `#5CDD`). On a machine where TR-DOS never ran they are not there.

**Works** (from the Pentagon menu's *48 BASIC*):

```text
RANDOMIZE USR 15616            enter TR-DOS: it makes its variables, the A> prompt
LOAD "gens4b" CODE 30000       a TR-DOS command at the prompt (the file is named gens4b, type C)
RETURN                         back to the BASIC TR-DOS was entered from: 48K BASIC
RANDOMIZE USR 30000            GENS4B: "MOA B-Disk version.", the > prompt
```

Port trace of that run: Restore `#08`, Seek `#18`, Read Sector `#80`, Read Address `#C0` (the catalog and the
file's sectors); `#7530` then holds `21 A0 28 09 5E 23 …`.

**Does not work, and why:**

| try | what happened | why |
|---|---|---|
| `RANDOMIZE USR 30000` typed at the TR-DOS `A>` prompt | `trdos_rejected` | `RANDOMIZE USR` is a BASIC statement; the prompt takes TR-DOS commands |
| Pentagon menu *TR-DOS*, code written, `RETURN`, `RANDOMIZE USR 30000` | the typed line did not go in | `RETURN` goes back to the BASIC TR-DOS came from: from the menu's TR-DOS item that is 128 BASIC, not 48K BASIC |
| a PC write to 30000 at the `A>` prompt | the machine ended on a broken screen | entered from inside TR-DOS's command loop (DOS ROM paged, its stack), not from BASIC |
| `RANDOMIZE USR 15619: REM : LOAD "gens4b" CODE 30000`, typed or as program line 10 | returned at once, nothing loaded | TTD: TR-DOS was entered (`#3D00`-`#3D10` executed) for one frame and returned with `ERR_NR` = 255 and TR-DOS error 0; the port trace shows only Force Interrupt `#D0`, no disk access. Explained: the typer of that day stored a blank (`#20`) after `REM :`, TR-DOS read it as the command token and found no command. Fixed in the tokenizer (F1); re-checked 2026-10-16: the line without the blank loads, the same line with `#20` before `LOAD` does not |
| the same lines written with the WebAPI `basic/inject` | `RUN` ran line 20 first | the injected line 10 lists as `247?`: the line number was stored wrongly (a Spectrum line number is high byte first); the run then jumped to empty memory at 30000 |

## TR-DOS calls in the binary

All disk work is `LD C,n` + `CALL #3D13` (15635: "call TR-DOS routines from machine code", function in C). The
sites, with the function's meaning from the TR-DOS 5.04T description:

| address | C | TR-DOS function | in GENS4B |
|---|---|---|---|
| `#7E8B`, `#7EBF` | `#0B` | write file: name at 23773, start HL, length DE | `P` (save text) and the object save |
| `#8004` | `#18` | check the disk, set the system up for its type | before a file operation |
| `#800C` | `#13` | copy 16 bytes from HL to 23773 (the descriptor) | the file name into the descriptor |
| `#8026` | `#0A` | find file (the part at 23773, its length in 23814) | `G` / `P` look the name up (type set to `C` just before: `LD (HL),#43`) |
| `#80D9` | `#12` | delete files (name at 23773) | `P` replaces an old file of the same name |
| `#8136` | `#0E` | load file (23801 = 0) | `G` loads the text file |
| `#93CE` | `#07` | catalogue to stream A (here 2) | `X` |
| `#9B9C` | `#08` | read descriptor A into 23773 | include file (`*F n:NAME`): its catalog entry |
| `#9BB2`, `#9C43`, `#9D8A` | `#14` / `#13` | descriptor to / from the program's copy | include file and object file bookkeeping |
| `#9C08`, `#9D07` | `#05` (B = 1, set before) | read sectors: HL address, D track, E sector | include file read sector by sector (track / sector from the descriptor, `IX+#15` / `IX+#16`) |
| `#9C70`, `#9D56` | `#05` | read 1 sector, track 0 sector 8 (`DE` = 8) | the disk's system sector (free space, file count) |
| `#9D93` | `#09` | write descriptor A from 23773 | a new file's catalog entry |
| `#9D9E` | `#06` | write 1 sector, track 0 sector 8 | the system sector updated |
| `#9DC0`, `#9DC4` | `#01` | select drive A (0-3) | the `n:` of a name |

A file name is `n:NAME` with `n` = 1-4 for drives A-D; the type is always `C` (`#43` written before the search).

## The source text in memory

GENS4B keeps the text after its code, as every GENS does:

| | |
|---|---|
| text start | `#A06A`, the word at `#7928` |
| text end (exclusive) | the word at `#7566`, an operand the code rewrites: load address + `#36`, as in the tape GENS4 and GENS3M21 |
| records | `[number lo] [number hi] text #0D`; the file `P` writes is these records |
| while a line is inserted (`I`) | the line being typed stays out of the text; the line it goes before gets bit 7 of its number's high byte |
| identification | `MOA B-Disk version.` at `#9A70`; `HiSoft GEN Assembler ZX Spectrum` at `#9A48` |

The asm-synchronizer reads it live as descriptor `gens-4b` (golden dumps `gens4b-*`, made by
`tools/verification/unreal-asm/emulator/sync-dumps.py gens4b` with the start sequence above). Recipe:
[`.recipe/assemblers/gens.md`](../../../../.recipe/assemblers/gens.md).
