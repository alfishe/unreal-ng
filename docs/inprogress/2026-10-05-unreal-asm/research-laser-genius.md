# unreal-asm: Laser Genius source format and conversion to sjasmplus

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Status** | Done: codec `src/codecs/lasergenius/`, frontend `src/dialects/lasergenius/`, tests `tests/lasergenius_test.cpp`, test data `testdata/lasergenius/`, `testdata/dialects/lasergenius/`; the tape container joins Laser Genius' blocks (`src/containers/tape.cpp`) |
| **Assembler** | Laser Genius, assembler + editor + monitor / analyser + the Phoenix compiler ("hash extensions"), Oasis Software 1986 (Spectrum and Amstrad), [ZXDB 8330](https://spectrumcomputing.co.uk/entry/8330/ZX-Spectrum/Laser_Genius); the Beta Disk version 1.04 (MOA, 1992) |
| **Sources of the rules** | the English manual [LaserGenius(EN).pdf](https://spectrumcomputing.co.uk/zxdb/sinclair/entries/0008330/LaserGenius(EN).pdf) (§1.3 statements, §2 commands, §4 the assembler, appendix D expressions); every token and every rule below checked in Laser Genius 1.04 running in unreal-ng |
| **Corpus** | the examples of the Oasis tape ([LaserGenius.tzx.zip](https://spectrumcomputing.co.uk/pub/sinclair/utils/l/LaserGenius.tzx.zip), tape 2 side B: SIEVE.ASM, ELLIPSEASM, and the Phoenix SIEVE.PHX, ELLIPSEPHX, MPAFNCSPHX) and Kamasoft's sources ([LaserGenius(LGZdrojaky)(Kamasoft).tzx.zip](https://spectrumcomputing.co.uk/pub/sinclair/utils/l/LaserGenius(LGZdrojaky)(Kamasoft).tzx.zip): LIB.MAKER, MEANED, COMP, EDITOR); the Beta Disk version from [LG_1_04.ZIP](https://vtrd.in/system/LG_1_04.ZIP) |
| **Result** | the nine files decode and encode back byte for byte, and from their text alone; 825 statement forms typed into Laser Genius encode to the tokens its editor stored; SIEVE.ASM, ELLIPSE.ASM, LIB.MAKER and an expression probe, converted to sjasmplus, assemble to the bytes Laser Genius built from them (5 157, 428, 977, 97 bytes) |

## 1. Example first

```text
Laser Genius (LIST)              tokens                                sjasmplus (converted)
   30 start: LD A,2              1E 00 F1 73 74 61 72 F4 B5 FD 02     start   LD A,2
      CALL #1601                 55 FE 01 16                                  CALL #1601
      ; BC is "count".           EF 20 42 43 ... F0                           ; BC is "count".
      LD HL,[1+2]*3              BE CB FD 01 D5 FD 02 D0 D7 FD 03 F7          LD HL,(1+2)*3
```

A paragraph holds a line number and statements; the editor stores tokens with no separators: `LD A,` is one token
(`#B5`), `2` is `#FD 02` (a decimal byte), and the next statement starts where a mnemonic or a label does.

## 2. The file

| Where | Layout |
|---|---|
| in memory | paragraphs from the address STATS calls `file` (`#7F2E` in 1.04 Beta), then `FF FF`; the end is in `(#A973)` |
| on disk (1.04 Beta, `SAVE`) | a CODE file: `#AF`, the text's length (2 bytes, without `FF FF`), the text's address (2 bytes), the first four characters of the name, the paragraphs |
| on tape (`SAVE`) | blocks of up to 2048 bytes, each after a 17-byte header block of its own: the block number (bit 7 on the last), `#AF`, the file's length, the block's length, a 10-character name and one byte; `zxasm files` shows the joined file as type `L` |

A paragraph is `number (2 bytes) statements… #F7`. Numbers increase (0-65534).

## 3. Tokens

| Token | Meaning |
|---|---|
| `#05-#1F` | registers and conditions: BC DE HL SP AF' AF A B C D E H L IX IY NZ Z NC M PO PE P I R F ON OFF (C the register and the condition) |
| `#20-#26` | (BC) (DE) (HL) (SP) (IX) (IY) (C) |
| `#27-#2B` | (IX+ (IX- (IY+ (IY- and ( : an expression follows, the closing parenthesis is not stored |
| `#2C-#7F` | mnemonics and pseudo-ops in the editor's table order: CCF … NEG, ELSE ENDC ENDM, DEC INC JP JR CALL ADC ADD SBC IN OUT EX LD ORG AND OR XOR SUB CP PUSH POP DJNZ RET BIT SET RES RLC RL RRC RR SLA SRA SRL IM RST, DB DEFB DEFM DW DEFW DL DEFL EQU DS DEFS PUT COND MACRO |
| `#81` name | a macro call `\name` |
| `#82-#C3` | a mnemonic with its first operand: JP cc, (`#82-#89`), JP (HL) (IX) (IY), JR cc, CALL cc, ADC/ADD/SBC A, and HL, ADD IX, IY, IN r, OUT (C), OUT (, EX DE, EX AF, EX (SP), LD (BC), … LD (IY+ LD (IY- LD (IX+ LD (IX- LD (, LD r, LD rr, LD I, LD R, LD SP, |
| `#C7-#CA` | unary * (contents of) ! ^ (complement) - |
| `#CB` `#D0` | [ ] |
| `#D5-#E8` | binary + - * / % <= >= ?= != << >> < > @< @> && \|\| & \| ^ |
| `#E9` `#EA` | $ (the statement's address), . (where its bytes go) |
| `#EB` c / `#EB #FD` n | a character constant "c" / "\n" |
| `#EC` name | a name (bit 7 on the last character; letters, digits, _ . $; case kept) |
| `#ED` … `#00` | a string; inside it `#FD n` is the escape `\n` |
| `#EE` … `#F0` | a comment after a statement |
| `#EF` … `#F0` | a sentence that is only a comment (`#EF #F0` = an empty one); after a statement it starts a new sentence |
| `#F1` name | a label (`name:`) |
| `#F2` name | a macro parameter `\name` |
| `#F5` n | a directive `*name`, n = `#82` LIST … `#95` PROMPTS |
| `#F7` | the end of the paragraph |
| `#F8`/`#F9` | binary word / byte; `#FA`/`#FB` octal; `#FC`/`#FD` decimal; `#FE`/`#FF` hex |

The width follows the value (a byte form below 256) unless more digits were written: `#0012` is a word; `0FFH`
(the H form) is stored as `#FF`. Commas between operands are not stored: an operand followed by another operand is
the next one (`DEFW start,$` is `77 EC … E9`).

Phoenix statements (the compiler's language) use tokens this codec does not read (`#00-#04`, `#C4-#C6`, `#CC`,
`#CD`, `#D1-#D4`, `#F6`): such a paragraph is decoded as `{hex bytes}`, kept, and reported.

## 4. The language, as Laser Genius 1.04 assembles it

| Rule | Checked | Conversion |
|---|---|---|
| 16-bit unsigned words: `-10/3` = 21842, `-10%3` = 0 (the manual says 2), `-1>>1` = `#7FFF`, `-1<1` = 0 | assembled probe (EXPR) | `expressionBits = 16`, unsigned |
| precedence (lowest first): `&& \|\|`; `& \| ^`; `?= !=`; `< > <= >=`; `<< >> @< @>`; `+ -`; `* / %`; unary `- ! ^ *` | `1\|2&4` = 0, `6&2\|1` = 3, `1<<2+1` = 8, `5>3?=1` = 1 | a tree; the backend adds sjasmplus' parentheses |
| comparisons, `&&`, `\|\|` and `!` give 1 | `2&&1` = 1 | `trueValue = 1` (sjasmplus gives -1: written `-(…)`) |
| `@<` `@>` rotate a 16-bit word | `#1234@<4` = `#2341` | rotates spelled out |
| `[ ]` group; `*e` is the word at e | — | `( )`; `{e}` |
| `$` the statement's address; `.` where its bytes go | `DW .` with PUT = ORG | `$`; `$$$` under a PUT |
| ORG sets the address only; PUT sets where the bytes go (no PUT: nothing is stored) | probes assembled with `ORG #6900` + `PUT #6900` | PUT: `ORG` there + `DISP` the address; ORG under a PUT: a new `DISP` |
| DS fills zeros | the memory was `#AA` | `DS` |
| DB truncates (warning): `DB 300` = `#2C`; `LD (IX+130)` = `#82` (warning) | | sjasmplus warns; `(IX+130)` is an error there |
| `"\13"` a character code; `\n` inside strings | `DB "\13"` = 13 | numbers beside the text: `DB 13,'OK',255` |
| EQU; DL / DEFL redefine | `v: DL v+1` | `EQU`; `v=…` |
| COND / ELSE / ENDC (true = not 0) | | `IF` / `ELSE` / `ENDIF` |
| `name: MACRO \p1,\p2` … `ENDM`; call `\name a,b` | the editor's tokens | `MACRO name p1,p2`; `name a,b` |
| `*WHILE e` … `*ENDW`; `*REPEAT` … `*UNTIL e` (until e is true) | | `WHILE`; the IR's repeat-until with `!e` |
| `*INCLUDE "f"`; `*PRINT "text",expr` (an expression printed in hex) | LIB.MAKER prints `#3D0` | `INCLUDE`; `DISPLAY /H,…` |
| `*LIST *LLIST *COUNT *SCREEN *PRINTER *MACLIST *FORM *REPORT *TITLE *PAUSE *PROMPTS *CODE *OPENOUT *CLOSEOUT` | | comments (`*CODE OFF` reported) |
| division by a word of `#8000` or more is wrong in Laser Genius: `#8000/#FFFF` = `#5555`; `7/0` gives a warning and 41 | assembled probe | not reproduced (sjasmplus computes it right / refuses) |

## 5. In the emulator (2026-10-08)

Laser Genius 1.04 Beta Disk (`L_G_ASS.` from `LG_1_04.SCL` on a Pentagon); the steps are in
`.recipe/assemblers/laser-genius.md`. What the work found:

| Finding | How it showed | What decides it |
|---|---|---|
| A Y/N question takes only the key pressed while it waits: the key routine clears LAST_K (`#5C08`) when it starts, and the question is printed slowly | Y pressed during the printing was lost; a key held for a second autorepeats into the next line | press once, after the question is fully shown |
| `LOAD ASCII` (the tool-kit) does not read from disk in 1.04 Beta, and asks "delete the name table?" again after Y when the file does not fit: Y retries the space check without deleting | the TTD trace of the question (`#FEDE`: the check at `#F80B`, the question `#4C` at `#BCE1`; the deletion call at `#F3F2` is patched to NOPs by the Beta adaptation) | type the text into the editor instead (each sentence with a paragraph number), or write a disk file with `zxasm encode` + the disk header |
| The editor's space is 10K (SET SPACE larger: "space too large" on a 128K Pentagon with the tool-kit) | | the probes were split; MEANED and COMP (9-10K) do not fit to be assembled there |
| Real-time tape loading failed in the emulator (fixed) | WebAPI play after `LOAD ""` restarted the header block with its edges generated twice | `Tape::generateBitstream` replaces the edges (research-prometheus.md §6) |

## 6. Open items

| Item | Note |
|---|---|
| Phoenix | the hash extensions' tokens are not mapped; their paragraphs are kept as bytes |
| EDITOR (Kamasoft) | the tape ends inside the file (6144 of more bytes): the last paragraph is dropped with a warning |
| The text form | `zxasm decode` writes one statement a line without the paragraph numbers (as for GENS / ZEUS); `zxasm encode` numbers each line as a paragraph (10, 20, …) |
