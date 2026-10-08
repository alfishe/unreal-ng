# unreal-asm: ZEUS → sjasmplus

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Code** | `src/dialects/zeus/zeusfrontend.cpp` (the `zeus` frontend); the sjasmplus backend (16-bit unsigned words, comparisons that give 1) |
| **Syntax source** | the ZEUS manual, section 5 ([Zeus.txt, ZXDB 9010](https://spectrumcomputing.co.uk/pub/sinclair/games-info/z/Zeus.txt)); the ZEUS v7.E help (a ZEUS source on the [ZEUS72ZK disk](https://vtrd.in/system/ZEUS72ZK.zip)); the GG ZEUS's `zeus.doc` on the [ZEUS_GG disk](https://vtrd.in/system/ZEUS_GG.zip); the format: [research-zeus.md](research-zeus.md) |
| **Oracles** | four ZEUS versions running in unreal-ng: ZEUS 1983 (tape), GG, ZEUS 1.1 beta (`PHT_ZEUS.LZH`, KLUG BBS archive [klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw)), ZEUS v7.E; probes for every rule below and the five Zeus Routines ([ZXDB 19058](https://spectrumcomputing.co.uk/entry/19058)) assemble through sjasmplus to the bytes ZEUS built |
| **Checks** | `unreal-asm-tests` (`ZeusFrontend_Test`; with `UNREAL_ASM_SJASMPLUS` the oracle programs too); `docs/inprogress/2026-10-05-unreal-asm/scripts/emulator/assemble-in-emulator.py zeus1983 / zeus11 / zeusgg / zeus7e`; `roundtrip.py --assemble`; the recipe `.recipe/assemblers/zeus.md` |

## 1. Example first

```text
ZEUS                               sjasmplus
START LD A,2+3&6                   START   LD A,+((2+3)&6)        ; no priorities: left to right
 NOP:L3 NOP:LD A,1                 NOP / L3 NOP / LD A,1           ; every statement has its own label
 LD C,"A+1                         LD C,+('A'+1)                   ; "c is a character
 JP V,START                        JP PE,START                     ; V / NV = PE / PO
 DEFM /AB;C:D/                     DB 'AB;C:D'                     ; any delimiter, ; and : are text
 DEFM /TAIL                        DB 'TAIL'                       ; no closing delimiter: to the end of the line
 LD HL,3000/7                      LD HL,+(((3000&#FFFF)/(7&#FFFF))+(-(((3000%7)&#FFFF)>(((7&#FFFF)/(2&#FFFF))&#FFFF))))
                                                                   ; v7.E: / rounds to the nearest (429)
 ORG 30000                         ORG 30000
 DISP 10000                        __UNREALASM_ZEUS0=$ / ORG (__UNREALASM_ZEUS0+10000) / DISP __UNREALASM_ZEUS0
 DISP 0                            __UNREALASM_ZEUS1=$ / ENT / ORG __UNREALASM_ZEUS1
```

## 2. ZEUS facts the conversion relies on

Each row was seen in the emulator (the probe files of `testdata/dialects/zeus/`), unless it says "manual".

| Fact | Conversion |
|---|---|
| A line holds statements separated by `:`, each "an optional label, an instruction, an optional comment" (manual 5.1); `;` starts the comment. Checked: `NOP:L3 NOP:LD A,1` gives `00 00 3E 01` with `L3` at the second `NOP` | one IR line per statement that has a label |
| A label is a first word that is no keyword. Keywords are the version's table (research-zeus.md §3), upper case only: `INF`, `SLL` (not in the table) and lower-case mnemonics are labels | the keyword test uses the table; `DB` is a label in a 1983 source, a keyword in GG / 1.1 / v7.E |
| Expressions: `+ - & !` (`!` is OR), "no operator priority is observed: expressions are evaluated strictly from left to right" (manual 5.3). Checked: `2+3&6` = 4, `10-2-3` = 5, `#FF00!#12` = `#FF12` | each operation grouped as built: `((2+3)&6)` |
| ZEUS v7.E adds `*`, `/` and `%binary`, still left to right (`2+3*4` = 20, `20-2/3` = 6) on 16-bit unsigned words (`65535*2` = `#FFFE`, `#8000/3` = `#2AAB`, `0-1/2` = `#7FFF`). ZEUS 1983 and 1.1 refuse them (error 0) | read in every version (a v7.E source's bytes are also valid 1983 bytes); `expressionBits = 16`, unsigned |
| v7.E's `/` rounds the quotient to the nearest, a remainder of exactly half down: `7/2` = 3, `11/4` = 3, `3000/7` = 429, `65535/2` = 32767, `23/6` = 4 (21 cases checked). A dividend smaller than the divisor (`1/3`, `65534/65535`) is error 4 | `a/b + (a%b > b/2)`; `Program::trueValue = 1` makes the backend write the comparison as `-(...)` |
| A leading minus (`LD DE,-1`) is error 0 in ZEUS 1983 | read as 0-x (no source needs it) |
| `"c` is the character code (`"A+1` = `#42`); `":` and `";` are characters, not separators | a character constant; the statement splitter skips the character after `"` |
| `DEFM` / `DM` text: the character after the blank is the delimiter (`/AB;C:D/`, `"XYZ"`); `:` and `;` inside are text; no closing delimiter: the text runs to the end of the line (the stored line has no trailing blanks). Checked in 1983 and 1.1 | `DB 'text'` |
| `DEFS n` (1983) and `DS n` (GG) leave memory as it was (the `#AA` fill stayed); ZEUS 1.1 and v7.E write zeros | `DS n`: sjasmplus writes zeros; the oracle comparison accepts zeros where 1983 / GG left the fill |
| `DISP d` is an offset from `ORG`: after `ORG 30000` / `DISP 10000` the code goes to 40000 and runs at 30000 (manual 5.5); checked: `DISP 1000` at `#9C6B` puts `LD HL,D1` at `#A053` with `D1` = `#9C6B`, a later `ORG 40100` puts code at `#A08C`, `DISP 0` puts it back at `$` | `tmp=$` / `ORG tmp+d` / `DISP tmp`; an `ORG a` while displaced: `tmp=a` / `ENT` / `ORG tmp+d` / `DISP tmp`; `DISP 0`: `tmp=$` / `ENT` / `ORG tmp` |
| `ENT` marks the entry point for the `X` command, no code (manual 5.5) | nothing (a label on it stays) |
| `V` / `NV` are `PE` / `PO` (manual 5.1.2); checked `JP V` = `EA`, `JP NV` = `E2` | conditions `pe` / `po` |
| GG: `INCBIN "name"` puts a file (checked: the 5 bytes of `dat` between the bytes around it; `INCBIN"name"` without a blank is error 0) | `INCBIN "name"` |
| v7.E: `INCLUDE name` assembles a type `Z` source of the disk (its labels are known), `PLACE name` puts a type `C` file (checked). ZEUS 1.1 does the same only when started from the PHT 3.6 shell (its file manager: File Functions, Call Subroutine at `#E000`), and there the INCLUDEd source is a type `C` file too (its help: "the extension is always *.C"); standalone it answers error A to both. `OPEN "name"` (an editor command) sends the code to a disk file instead of memory | `INCLUDE "name.asm"`, `INCBIN "name"` |

## 3. Oracle results

| Program | ZEUS | Bytes | Result |
|---|---|--:|---|
| `PROBE83` (every 1983 rule, `DISP` / `ORG` / `DISP 0`) | 1983 | 1103 | equal (3 `DEFS` bytes left as they were) |
| `PROBE11` (`DS`, an open `DM`) | 1.1 | 10 | equal |
| `PROBE7E` (`* / %`, the rounded division, 16-bit words) | v7.E | 132 | equal |
| `INCL7E` + `inc1` (type Z) + `dat` (type C) | v7.E | 11 | equal |
| `INCLGG` + `dat` | GG | 10 | equal (2 `DS` bytes) |
| `INCL11` + `inc1` (type C) + `dat`, in the PHT 3.6 shell | 1.1 | 11 | equal |
| ADS 2.0: `MAKE_ADS` + `CC0`-`CC2`, `PLACE $ads` / `FONT$`, `OPEN "adsobj"` in the PHT 3.6 shell | 1.1 | 20155 | equal |
| Zeus Routines: Glitter, Multiplot, Print, Scrolling, Select | 1983 | 239, 238, 623, 270, 295 | equal |

The ADS 2.0 sources (`MAKE_ADS` includes `CC0`-`CC2`, about 20K of code at `#6000`) do not fit beside the sources and
ZEUS in 48K memory, but ZEUS 1.1 compiles to a disk file with `OPEN` and reads the INCLUDEd parts from the disk: run
from the PHT shell it built all 20155 bytes, and the sjasmplus conversion of the same sources is equal to them byte for
byte (`testdata/dialects/zeus/ADS20.bin`). The released `ads_2^0i.b` of `ADS20SRC.LZH` is an "Improved version": a
BASIC loader reading its code from the disk to `#9C40`, not the program the sources build. Their conversion assembles with sjasmplus without errors
once the screen file is named as `PLACE` names it (`$ads`; the archive calls it `ads$`), and their 369 labels come
through `SymbolsFromProject` equal to what sjasmplus 1.24 wrote with `--sym` (`testdata/symbols/fromsource/zeus-ADS20.sym`).

`roundtrip.py --assemble` over the ADS sources and the Zeus Routines: 9 sources, 0 round-trip differences, every
main source assembles (ADS with its screen file under the `PLACE` name).

## 4. Open items

| Item | Note |
|---|---|
| `$ads` / `ads$` | ADS's `PLACE $ads` names a file the archive calls `ads$`; ZEUS 1.1 found the screen under `$ads` (the build above): the archive's name is a renaming by whoever packed it |
| Characters above `#7F` in `DEFM` | decoded in the ZX Spectrum code page; the sjasmplus text keeps CP866: none in the corpus |
