# Modern assemblers and the ZX Spectrum Next (phase A9)

The assemblers people write Spectrum and Next code in today, one text dialect each, and the Next's own tools. What reads what,
how each frontend was checked, what it does not read. Written 2026-10-08; every claim below was run against the assembler's
sources or binary (cloned under `emulators/github`, `emulators/gitlab`), none is from memory.

## Who assembles the Z80N instructions

`tools/verification/unreal-asm/checks/z80nmatrix.py` assembles each of the 29 instructions alone with each assembler and
compares the bytes with sjasmplus' (`--zxnext`, the reference the ZXSpectrumNextTests suite is built with):

| Assembler | Equal of 29 | Switch | Notes |
|---|---|---|---|
| sjasmplus 1.23 | 29 (reference) | `--zxnext[=cspect]`, `DEVICE ZXSPECTRUMNEXT`, `OPT --zxnext` | |
| z88dk z80asm 2.3 | 29 | `-mz80n` | `swapnib`, `mirror a`, `mul d,e`, `pixeldn` without operands |
| Megatokio zasm 4 | 29 | `--z80n`, `.z80n`, `#!` options | |
| FantASM 1.1.10 | 22 | `-N` | no barrel shifts, `mirror a`, `jp (c)` |
| pasmo forks (2018, spec-chum, Ckirby101) | 18 | built in | old opcodes: `mirror de` = ED 26, `ldix` = ED A5, `push` / `nextreg` wrong |
| zmac, rasm, asm80-core | none | | no Z80N in their sources |
| Zeus (Next distribution) | by `DEFB NEXTX,MUL` | `nextras.god` | the 1983 assembler, extended with names for the ED bytes |
| specasm, Odin | built in | | Next-native, run on the Next |

## Frontends added (phase A9)

All text dialects share `src/dialects/common/textfrontend.{h,cpp}` (the line engine: label, command, operands, the block stack of
MACRO / REPT / IF / PROC, the mapping of a dialect's commands to IR directives) and `textexpr.{h,cpp}` (a table-driven expression
parser: per-dialect operator priorities, number spellings, quoting). A dialect is a `TextDialect` table of about 100 lines.

| Dialect | Codec id | Checked against | Result |
|---|---|---|---|
| pasmo 0.5.5 | `pasmo` | pasmo on the 35 sources of its own distribution | 29 of 33 equal through sjasmplus, 27 of 33 back through pasmo, 23 of 33 through z80asm |
| z88dk z80asm | `z80asm` | z80asm and its test suite's 72 cases | 54 of 68 equal through sjasmplus, 46 of 68 through z80asm |
| zasm 4 | `zasm` | zasm on its Test and Examples folders | 19 of 36 flat-binary sources equal through sjasmplus, 15 of 36 through z80asm |
| FantASM | `fantasm` | FantASM on its tests | 10 of 13 through sjasmplus, 8 of 13 through z80asm |
| zmac | `zmac` | zmac on sources written for the purpose (`testdata/zmac`) | 6 of 6 |
| rasm | `rasm` | rasm on the decrunch routines of its repository | 11 of 13 |
| Specasm (.s text) | `specasm` | saimport + salink on the tests and examples of its repository | 12 of 14 |
| Odin (.odn) | `odin` | the three documents of its repository | byte-exact; the tokenizer reproduces the stored bytes of 105 of 156 lines (the rest were saved by another Odin version) |
| Zeus on the Next (.god) | `zeus` | `ECHO.god` and the others; `ECHO.bin` | equal; +3DOS container read |

What each does not read (reported by the backend as "not converted", never dropped silently):

- **pasmo**: IRP, EXITM, `.SHIFT`, `##`, NUL, `?:`, PUBLIC, `.ERROR` / `.WARNING`.
- **z80asm**: EXITM, REPTI / REPTC, FLOAT, `#define` with parameters, SECTION / PUBLIC / EXTERN and the linker directives; DEFL
  sequences that rely on z80asm's single-pass evaluation.
- **zasm**: segments with a fill byte or size (`#data`, `#target rom`'s `DS` filled with FF), `#assert`, `.test` / `.expect` blocks, the
  8080 and Z180 modes, `--flatops`.
- **FantASM**: STRUCT / ENUM, `!opt`, UTF-8 string tables, `break` (the CSpect byte pair differs between sjasmplus and FantASM).
- **zmac**: `--mras` and `--zmac` modes, relocatable output directives, `++` / `+=`, `exitm`.
- **rasm**: floating point arithmetic, EQU as a text alias, SAVE / BANK / BUILDSNA / LIMIT, crunched sections, `ORG a,b` with two
  addresses, WHILE / WEND, SWITCH.
- **Specasm**: the `.x` object files (not text), the `test` and `map` blocks of the linker, several files linked together.
- **Odin**: the macro and structure directives of its newer versions; SAVE, OPT, ENT*.
- **SPED** (the Next distribution's editor / assembler, tape images) and the closed tools (Sol, Astrum, SNasm): no format
  documentation or tool to run here.

## Backends

The z88dk backend writes Z80N mnemonics in z80asm's spellings (`SWAPNIB`, `MIRROR A`, `MUL D,E`, `PIXELDN`); the pasmo backend
writes the bytes (`DB 237,35`), since upstream pasmo has none. Both turn sjasmplus-style local labels (`.loop`) and qualified names
into unique plain names. The sjasmplus backend keeps a sjasmplus source's own label names (a STRUCT member `a` stays `a`), writes
strings holding control characters as numbers, and emits `AND n` where another dialect had `AND A,n`.

## Also fixed on the way (found by the corpora)

- the sjasmplus frontend read `\A`, `\D`, `\?` and capital-letter escapes wrongly (the bytes of the DMA tests changed);
- a quoted string or character constant with a control character was written raw inside quotes;
- sjasmplus labels named like registers were renamed (and their qualified uses were not);
- a macro-local label with a leading `@` was written as `.@name`.
