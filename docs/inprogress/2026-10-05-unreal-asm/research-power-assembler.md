# unreal-asm: Power Assembler (PASM) and conversion to sjasmplus

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Status** | Done: codec `src/codecs/pasm/` (text), frontend `src/dialects/pasm/`, tests `tests/pasm_test.cpp`, test data `testdata/pasm/`, `testdata/dialects/pasm/` |
| **Assembler** | Power Assembler 3.0 128K beta release (PASM, Oleg Sergeyev, Minsk 1995; distributed by SANALEX), [PASM3_0.ZIP](https://vtrd.in/system/PASM3_0.ZIP): `PASM3.0` (BASIC loader), `pasm3.0` (code at 49152, page 4), `PAHLP3.0` (the help), `GEN>PASM` / `GEN2PASM` (GENS text → PASM) |
| **Sources of the rules** | the help `PAHLP3.0` (protected: read decrypted from memory while it runs, KOI-7 text) and PASM 3.0 compiling probes in unreal-ng |
| **Result** | three probes (expressions, data, `$` in every operand position, ITXT / IBIN) converted to sjasmplus assemble to the bytes PASM 3.0 built (61, 39, 9 bytes) |

## 1. Example first

```text
PASM 3.0                          sjasmplus (converted)
        ORG #6000                         ORG #6000
LAB     LD HL,1+2*255                     LAB LD HL,+((1+2)*255)
        DW #A0ED DUP 2                    DUP 2 / DB (#A0ED)&#FF,((#A0ED)&#FFFF)>>8 / EDUP
        DW #1234,LAB,$                    DW #1234,LAB,($+4)
        DJNZ $                            DJNZ +($+1)
```

## 2. The file

A source is plain text with CR LF line ends ("the old TASM format", the help), tabs allowed; PASM saves it as a TR-DOS
CODE file (`PUT`; the checked save had start 31151). The `GEN>PASM` converter turns a GENS file into that form
(each CR's line number becomes LF). The codec `pasm` is the text codec with the `pasm` dialect, detected by what only
PASM writes: `DB` / `DW` with `DUP`, `ITXT` / `IBIN`, `ENT` without an operand, `SLI` (a plain text is left to the
text codec). A disk's PASM files are taken into a project (`zxasm convert image.trd`) although they are text.

## 3. The language, as PASM 3.0 compiles it

| Rule | Checked | Conversion |
|---|---|---|
| A label in column 0 (up to 14 characters), then the mnemonic and operands; `;` comment | the help | labels; sjasmplus renames what it cannot take |
| `+ - * /` strictly left to right, 16-bit, the division truncating: `7*3+1/2` = 11, `1+2*255` = 765, `100/7` = 14 | probe | grouped as built; unsigned 16-bit |
| `#` hex, `%` binary, `'c'` characters (no `"`); a leading minus refused by the help ("LD A,#FF, not LD A,-1"), `0-8` fine | probe | numbers, character constants |
| `DB` items: numbers, `'text'`, an open `'text` to the line's end; `DB` / `DW` `value DUP count` | `DB #AA DUP 3`, `DW #A0ED DUP 2` | runs as `DB` / `DW`, a fill as `DS n,v` or `DUP n` / `DB lo,hi` / `EDUP` |
| `$` in a `DB` / `DW` item is the item's address | `DW #1234,LAB,$` | `$+offset` per item |
| `$` in an instruction is what PASM has put when it reads the operand: the first of two operands is read before the opcode (`LD ($),A`, `LD ($),HL`, `LD ($),DE`: the instruction's address), the others after it (`JP $`, `JR $`, `DJNZ $`, `CALL $`, `LD HL,$`, `LD HL,($)`: +1; `LD BC,($)` (ED) and `LD IX,$` (DD): +2). The help's examples rely on it (`JR Z,$+3` skips a two-byte instruction) | probe (CURRENT) | `$+1` / `$+2` where PASM's `$` is ahead |
| a byte operand out of range is an error ("Value out of range"), the compilation stops | `LD (IX+1),$` | sjasmplus refuses it too |
| `ORG` once, at the start ("Bad ORG" after other lines); without `ORG` the code goes to 24576 | the help, probe | `IF $==0` / `ORG 24576` / `ENDIF` in a text without `ORG` (an ITXT text goes on from its includer) |
| `ENT` (no operand): the run address; `EQU` | the help | a comment; `EQU` |
| `ITXT name` / `IBIN name`: a text / code file of the current drive read while compiling, the name the first 8 characters after the blank | `ITXT LIB1` + `IBIN DAT` | `INCLUDE "name.asm"` / `INCBIN "name"` |
| `SLI` (undocumented, the "SLA with 1") | `SLI B` = `CB 30` | `SLI` |
| `(IY)` without an offset refused (the help) | | — |

## 4. In the emulator (2026-10-08)

PASM 3.0 on a Pentagon 128 (`RUN "PASM3.0"` from the TR-DOS disk made from `PASM3_0.SCL`); the steps are in
`.recipe/assemblers/power-assembler.md`.

| Finding | How it showed | What decides it |
|---|---|---|
| The help is protected ("PROTECTED BY PETE ABRAMOVICH… DON'T EVEN THINK ABOUT IT!") | the BASIC file is encrypted | run it: the decrypted text (KOI-7, 39K) sits in memory from `#6578` |
| The object code is in page 4 at `#E000` (offset `#2000`), not at the ORG address, while PASM runs | the memory at `#6000` stayed `#AA` | read RAM page 4 (`/memory/page/ram/4`) |
| CAPS LOCK is on: lower-case letters arrive as capitals | `probe` loaded the file `PROBE` | type file names in lower case |
| The menu (CAPS SHIFT + SYMBOL SHIFT) does not always open with a combined tap; then the letter goes into the text | an `S` in the first line | press SYMBOL SHIFT, then CAPS SHIFT, release both |
| `ZAP` asks "Sure? (Y/N)"; GET into a text merges | the next probe merged, "Bad ORG" | answer Y (held a little), check that the work file is UNTITLED |

## 5. Open items

| Item | Note |
|---|---|
| `& |` and other operators | not in the help; not tried |
| `$` in `LD (IX+d),n` | the shift for `n` not checked (the probe's `$` did not fit a byte) |
| PASM 2.5 (48K) | not found |
