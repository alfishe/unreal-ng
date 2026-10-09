# unreal-asm: ZEUS source format research (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Codec** | `zeus`, versions `1983` (Crystal ZEUS and the ports that keep its table: ZEUS 2.2, 2.2SI, ZEUS 128, ZEUS+ 3.1, the German and TS2068 releases), `gg` (ZEUS from GG), `pht` (ZEUS 1.1 beta of Professional Hackers Tools, ZEUS v7.E), `primus` (Primus Assembler 2.9, Trunov 1994) (`core/src/3rdparty/unreal-asm/src/codecs/zeus/`) |
| **Checks** | `unreal-asm-tests` (`ZeusCodec_Test`), testdata `zeus/` |
| **Result** | the ADS 2.0 sources, ZEUS v7.E's help, five ZXDB "Zeus Routines" and a probe typed into ZEUS 1983 in unreal-ng decode to ZEUS's listing and encode back byte for byte; the version's tokenizer reproduces all 4062 lines; none of 470 TR-DOS images of other software is taken for ZEUS |

ADS (Advanced Disk Service) is not an assembler: its sources, the reason "ads" was on the format list, are ZEUS files
written with PHT ZEUS.

## 1. Example first

Typed into the 1983 ZEUS (Crystal Computing):

```
40      LD (DE),A
```

is stored as

```
28 00 | 0A 05 | B3 | 28 94 29 2C 80 | 00
  40  | 5 blanks | "LD " | ( DE ) , A | end of line
```

and listed as `00040      LD (DE),A`. `#B3` is keyword 51 of the reserved word table (`LD ` with
its blank), `#94` is `DE`, `#80` is `A`. A comment is tokenized too: `;LD A,B HL` → `3B B3 80 2C 86 20 A5`.

## 2. The file

```
line number (2 bytes, little-endian, 0..65534)  line bytes  #00   ...   #FF #FF
```

No header. Lines are in increasing order; `#FF #FF` (line number 65535) ends the file. The 1983
manual saves a source from BASIC: `T` prints start and length, then `SAVE "name" CODE start,length`;
the length includes `#FF #FF` (typed probe: `Length = 00300` for 300 bytes ending `FF FF`). Disk
versions save the same block as a TR-DOS file (type `C`; ZEUS v7.E uses type `Z`); ZEUS 2.2SI's BASIC
appends files at `start+length-2` (over the old `#FF #FF`). On tape the source is the data block
of a CODE header (param 1 = 32768, the default `N` address).

### 2.1 Line bytes

| Bytes | Meaning |
|---|---|
| `#20`-`#7F` | the character (`#20` = one blank) |
| `#0A n` | n blanks (a run of two or more; n = 0 prints 256) |
| `#80`+k | keyword k of the table (§3); keywords that take operands include their trailing blank |
| `#80`+k past the table | printed as the raw byte (Spectrum token / UDG) |
| `#00` | end of line |

The line number is followed by **one** separating blank on screen; it is not stored. Blanks typed
after it are stored (`#0A 05` above). Trailing blanks are not stored.

## 3. Keyword tables

The table is a list of strings separated by `#0A` (keyword without blank) or `#20 #08` (keyword
with its blank), ended by `#0C`; token = `#80` + position.

1983 table (101 words, `#80`-`#E4`): `A ADC␠ ADD␠ AF' AF AND␠ B BC BIT␠ C CALL␠ CCF CP␠ CPD CPDR CPI
CPIR CPL D DAA DE DEC␠ DEFB␠ DEFM␠ DEFS␠ DEFW␠ DI DISP␠ DJNZ␠ E EI ENT EQU␠ EX␠ EXX H HALT HL I IM␠
IN␠ INC␠ IND INDR INI INIR IX IY JP␠ JR␠ L LD␠ LDD LDDR LDI LDIR M NC NEG NOP NV NZ OR␠ ORG␠ OTDR
OTIR OUT␠ OUTD OUTI P PE PO POP␠ PUSH␠ R RES␠ RET RETI RETN RL␠ RLA RLC␠ RLCA RLD RR␠ RRA RRC␠ RRCA
RRD RST␠ SBC␠ SCF SET␠ SLA␠ SP SRA␠ SRL␠ SUB␠ V XOR␠ Z` (␠ = the stored blank). The manual's
Appendix 3 lists the same words. Examples: `#96` DEFB, `#9F` ENT, `#A0` EQU, `#B3` LD, `#BF` ORG,
`#C9` PUSH, `#CC` RET, `#E4` Z.

| Program (file) | Table at | Words | Differences from 1983 |
|---|---|--:|---|
| ZEUS 1983 (ZXDB `Zeus.tap`, `Zeus(ZeusAssembler)(SinclairResearchLtd).tzx`), German and TS2068 releases, ZEUS 2.2 (`ZEUS2_2.SCL` `z.C`), ZEUS 2.2SI (`ZEUS1.C`), ZEUS 128 (`zeus.exe.C`), ZEUS+ 3.1 (`zeus +.C`), the GNS>ZEUS converter | `#EE57` (code at `#E000`; ZEUS.TAP block offset `#0E57`) | 101 | — |
| "ZEUS" from GG (`ZEUS_GG.SCL` `ZEUS.C`) | `#EE57` | 102 | `#96`-`#99` = `DB DM DS DW`; `#E5` = `INCBIN` |
| ZEUS 1.1 beta, YRIC 1993 (`PHT_ZEUS.LZH` of the KLUG BBS archive, `ZEUS.$C`), ZEUS v7.E ([ZEUS72ZK.zip](https://vtrd.in/system/ZEUS72ZK.zip) `ZEUSv7.E.B`, file offset `#1505`) | `#EEC5` | 103 | `#96`-`#99` = `DB DM DS DW`; `#E5` = `INCLUDE`, `#E6` = `PLACE` |
| Zeus Plus 2.16 Max (D.J.Stepanenko 1991, vtrd `ZSPL216M.zip`, `zeus.c.C`), Zeus Pro 2.01 (RIP Group 1995, vtrd `ZPRO.zip`, `zp.C`) | `zeus.c` file offset `#1357`; `zp` `#0E57` | 101 | — (Zeus Plus' disk holds 13 sources `name;Z`, type C at 28500, 18-1369 lines: all byte-exact and canonical as 1983) |
| Primus Assembler 2.9, Pavel Trunov 1994 ([PRIMUS29.ZIP](https://vtrd.in/system/PRIMUS29.ZIP) `PRI.ASS`, code at 25000) | `#6AB9` (file offset `#0911`) | 101 | `#9B` = `DISK` (include a text file) for `DISP`; Russian letters at `#EB`-`#FF` (below) |

**Primus 2.9** ("written after ZEUS, 99% compatible", its manual `PRI.DOC`, itself a Primus text): the line records,
the blank count `#0A n` and the 1983 table are ZEUS's. Its tokenizer also keeps `? . @ _ $` inside words (`L.X` is
one word; the 1983 tokenizer would store the register `L`), and it stores the Russian letters that do not look like
Latin ones at `#EB`-`#FF` in KOI-8 order: Ю Б Ц Д Ф Г И Й Л П Я У Ж Ь Ы З Ш Э Щ Ч Ъ (its keyboard table at 26774 (`#6896`)
gives the A-Z keys' codes; the others are typed as the Latin twins: Н is `H`, Р is `P`). Its text files are type `C`
with start 33364, its text buffer (`PRI.DOC.C`: 33364, 9392). The codec decodes the letters as Cyrillic capitals,
picks `primus` for a CODE file at 33364 and for bytes only Primus' table covers.

The converters `CONVASM.LZH` (ZEUS→TASM, ZEUS→ASCII) carry a copy of the 1983 table (`#00`-separated).

## 4. The tokenizer (canonical encoder)

From the code (1983: `#E246`; PHT: `#E269`); the editor tokenizes the screen line, which is padded
with blanks to the row end, after skipping the one blank after the line number:

1. Blanks are counted; before the next non-blank character one blank is written as `#20`, two or
   more as `#0A n`; blanks at the end of the line are dropped.
2. At each character the table is searched in order and **the first keyword that matches wins**: a
   keyword with a trailing blank must be followed by a blank in the line (consumed); one without
   must be followed by a non-word character. (1983 stops the search early at a table word whose
   first letter is greater; the table is sorted so the result is the same.)
3. No keyword: a word character starts a word that is copied whole (so `HELD`, `LDX`, `A1`, `Ld`
   stay text); `"` or `#` is copied with the word after it (`"A` and `#DE` stay text); any other
   character is copied and the search runs again at the next one.
4. Word characters: 1983 and GG: `0-9 A-Z a-z`; PHT / v7.E: `0-9` and `#3C`-`#7E` (letters plus
   `< = > ? @ [ \ ] ^ _ \` { | } ~`). So `X_A_Y` stores the `A` as a token in 1983 ZEUS and as text
   in PHT ZEUS.

Consequences seen on screen: `PUSH` typed alone is stored `#C9` and listed `PUSH ` (the screen pad
supplied the blank); lower-case mnemonics are text (and fail to assemble); keywords in comments and
strings are tokens (`DEFM /LD A,B/` → `97 2F B3 80 2C 86 2F`).

## 5. Display

`L` prints the number as 5 digits with leading zeros, a blank, then the line, wrapping at 32
columns with no indent. The codec's line text is this line without the number and its blank (the number is kept in the line's
attributes, `ZeusCodec::LineNumber`).

## 6. Detecting ZEUS bytes and the version

1. Record walk from offset 0: increasing numbers, `#00` line ends, `#FF #FF` at the end (with a
   container length: right at the end). Bytes `#80`+ and `#0A n` are typical; GENS uses `#0D`.
2. A token byte past a table rules that table out: `#E5`/`#E6` → not 1983. `#E6` → PHT.
   `#E5` alone: PHT (INCLUDE) or GG (INCBIN) — the bytes cannot tell; only the source's meaning can.
3. Otherwise the tokenizer that reproduces the stored lines from their text: a word with `_` (or
   another `#3C`-`#7E` symbol) next to a one-letter keyword separates 1983 from PHT
   (ADS sources: 1983 tokenizer 830 of 852 lines, PHT 852). Files with no such word are valid in
   both; the codec then says 1983 for a tape and the display choice (`DEFB` / `DB`) is the only
   difference.

## 7. Corpus and results

Testdata `zeus/` (`*.txt` = the expected listing, number and text):

| File | Origin | Lines | Version |
|---|---|--:|---|
| `ADS20SRC__CC0.bin`, `CC1`, `CC2`, `MAKE_ADS` | Advanced Disk Service 2.0 sources (MIPh&T Hacker's Club; PHT ZEUS help names "ADS 2.00"), `ADS20SRC.LZH` of the KLUG BBS archive ([klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw)); whole sectors, so bytes follow `#FF #FF` | 852, 854, 812, 4 | PHT |
| `ZEUS72ZK__ZEUShelp.$Z` | ZEUS v7.E help kept as a ZEUS source ([ZEUS72ZK.zip](https://vtrd.in/system/ZEUS72ZK.zip), type `Z`) | 491 | PHT |
| `ZeusRoutines__*.bin` (Glitter, Multiplot, Print, Scrolling, Select) | Theo Develegas, [ZXDB 19058](https://spectrumcomputing.co.uk/entry/19058) "Zeus Routines" ([tape](https://spectrumcomputing.co.uk/pub/sinclair/utils/z/ZeusRoutines.tzx.zip)), the data blocks | 140, 169, 398, 135, 179 | 1983 |
| `typed-zeus1983-PROBE.bin` | **typed in the emulator** in ZEUS 1983 (every rule of §4), the block `SAVE CODE 32768,300` writes | 28 | 1983 |

All 11 files byte-exact; the version's own tokenizer applied to the listed text reproduces **4062 of 4062** lines.
A scan of about 4300 disk images and archives with the record walk found the ADS set as the only ZEUS source; the rest
came from ZXDB and the ZEUS v7.E disk.

## 8. Checks in the emulator

Own unreal-ng instance (48K model). Each corpus file was written to 32768, made current with
`O 32768`, and listed eight lines at a time with `L first last`; the screen was read by OCR and
compared row by row with the listing wrapped at 32 columns:

| ZEUS | Files | Screen rows compared | Differences |
|---|---|--:|--:|
| ZEUS 1983 (tape, run at 57344) | typed probe, five Zeus Routines | 1050 | 0 |
| ZEUS 1.1 beta (PHT `ZEUS.$C`, run at 57344 without the PHT shell) | ADS CC0, CC1, CC2, MAKE_ADS, ZEUS v7.E help | 3013 | 0 |

The PHT run showed the renamed keywords (`DB 0` where the 1983 table says `DEFB 0`); the first
comparison, made with the 1983 table, failed on exactly those lines, which is how the renamed
`#96`-`#99` were found.

The tokenizer rules of §4 were found by typing probe lines and reading memory at 32768 (`typed-zeus1983-PROBE.bin`
holds the final 28-line probe) and confirmed in the disassembly.

## 9. Open questions

| Item | Note |
|---|---|
| `#E5` in a file | INCLUDE (PHT, v7.E) or INCBIN (GG): undecidable from bytes |
| Disk save commands of each TR-DOS port | not run; their files are the same block (start..`#FF #FF`) |
| ZEUS 128 | runs as several overlays (`zeus.res`, `zeus.dis` …) with the 1983 table in `zeus.exe`; the 128K source paging was not examined |
| Primus' `#E6`-`#EA` | not produced by its keyboard table; kept as bytes |
| Other ZEUS ports | vtrd.in lists no more besides Primus; ZEUSD1_0 (a decompiler writing ZEUS sources, its `tab.C` holds tokenized templates) and ZEUSTOT2 (ZEUS→TASM) are tools, not sources |

Sources: [Zeus manual (ZXDB)](https://spectrumcomputing.co.uk/pub/sinclair/games-info/z/Zeus.txt),
[ZXDB entry 9010](https://spectrumcomputing.co.uk/entry/9010), [ZXDB entry 19058](https://spectrumcomputing.co.uk/entry/19058),
[vtrd.in system](https://vtrd.in/system.php).
