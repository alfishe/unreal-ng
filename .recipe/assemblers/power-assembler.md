# Recipe: Power Assembler (PASM 3.0) in unreal-ng

Run Power Assembler 3.0 128K (Oleg Sergeyev, 1995), load a source, compile it, read the code, and move sources between
PASM and the host (`zxasm` reads its text files). Every step was run on 2026-10-08.

Related: [README.md](README.md) (own instance, `$PORT`), the language and conversion
`docs/inprogress/2026-10-05-unreal-asm/research-power-assembler.md`.

## Where PASM is

[PASM3_0.ZIP](https://vtrd.in/system/PASM3_0.ZIP) → `PASM3_0.SCL`: `PASM3.0` (the loader), `pasm3.0` (CODE 49152,
page 4), `PAHLP3.0` (the help), `GEN>PASM` / `GEN2PASM` (GENS text → PASM). Make a TRD with
`tools/verification/unreal-asm/lib/zxdisk.py scl2trd`.

## Start it

On a 128K model (`PENTAGON`): insert the disk, `RUN "PASM3.0"` from TR-DOS (`Emulator.run_trdos` in
`tools/verification/unreal-asm/lib/emulator.py`). The editor opens empty; the bottom line shows the free memory, the
line counters and `Work file: UNTITLED`.

The help: `RUN "PAHLP3.0"` (keys 5-8 page it). It is encrypted on the disk; while it runs its text (KOI-7: lower-case
Latin letters stand for Russian ones) is in memory from `#6578`.

## The menu

CAPS SHIFT + SYMBOL SHIFT opens the menu line `Cat Get Put Del put aS put obJ Zap Out compiLe RUn Bye`; a letter
picks: `G` load a text (merged into a text already there), `P` save, `S` save as, `J` save the object code, `Z` clear
(answer `Sure? (Y/N)` with Y), `L` compile, `U` compile and run. WebAPI: press `ss`, then `cs`, release both (a
combined tap sometimes types the letter into the text instead), then `/keyboard/tap` the letter.

## A source from the host

A PASM text is ASCII with CR LF line ends:

```text
        ORG #6000
LAB     LD HL,1+2*255
        DB 'HELLO',#00 DUP 3
        ENT
```

Write it to the TRD as a CODE file (`zxdisk.py add`, any start), insert the disk, menu `G`, delete the offered name
(`CS+0`), type the name in lower case (CAPS LOCK is on), ENTER.

## Compile and read the code

Fill the target with `#AA` if holes matter, then menu `L` ("Successful", `Executes:` with the run address). While
PASM runs the object code is in RAM page 4 from offset `#2000` (`#E000` there), not at the ORG address:

```text
GET /api/v1/emulator/{id}/memory/page/ram/4?offset=8192&length=64
```

`J` (PUT OBJ) writes it to the disk with the ORG address as its start (its help; not run here).

## Sources to and from the host

```bash
zxasm files pasm.trd                       # PASM texts show as codec "pasm" (by DUP, ITXT / IBIN, ENT, SLI)
zxasm decode pasm.trd --file PROBE.C -o probe.txt
zxasm convert pasm.trd --to sjasmplus -o out/     # ITXT / IBIN files of the disk come along
zxasm convert probe.txt --from pasm --to sjasmplus -o probe.asm   # a text without PASM's words
```

## Pitfalls

- **CAPS LOCK is on**: type lower case to get capitals (file names, text).
- **The menu tap may type its letter into the text**: check the first line after a menu action and delete a stray
  letter with `CS+0`.
- **GET merges**: clear with `Z` first and check `Work file: UNTITLED`.
- **ORG only once and first** ("Bad ORG"); without ORG the code goes to 24576.
- **`$` is ahead in instructions** (`JP $` jumps to the next byte, `DJNZ $` is `10 FF`): what PASM has put when it
  reads the operand; the first of two operands is read before the opcode.
- **A byte operand out of range stops the compilation** ("Value out of range").
