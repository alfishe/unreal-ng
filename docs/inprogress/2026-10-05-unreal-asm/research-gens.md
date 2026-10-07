# unreal-asm: GENS (HiSoft Devpac) source format research (phase A6)

| | |
|---|---|
| **Date** | 2026-10-06 |
| **Codec** | `gens`, versions `1` (GENS1, Devpac 1983) and `2` (GENS2, GENS3, GENS4 and their TR-DOS ports) (`core/src/3rdparty/unreal-asm/src/codecs/gens/`) |
| **Checks** | `unreal-asm-tests` (`GensCodec_Test`), testdata `gens/` |
| **Result** | five real sources and files typed or loaded and saved in GENS3 / GENS4 in unreal-ng decode to the editor's listing and encode back byte for byte; the editor's blank compression applied to the listed text reproduces every real and typed line; none of 470 TR-DOS images of other software is taken for GENS |

## 1. Example first

Typed into GENS3 (`I10,10`, then the line, ENTER):

```
LABEL  LD   A,(HL)   ; comment  two
```

is stored as

```
14 00 | 4C 41 42 45 4C 09 4C 44 09 41 2C 28 48 4C 29 20 20 20 3B ... 54 57 4F | 0D
 20   | L  A  B  E  L  TAB L  D  TAB A  ,  (  H  L  )  _  _  _  ;  ...  T W O | CR
```

and listed as `   20 LABEL  LD   A,(HL)   ; COM` (+ the rest of the line on the next screen row).
The first two runs of blanks became one TAB each; the third (before the comment) stayed as typed.

So **GENS sources are not tokenized**. "Compressed" (the manuals' word, GENS2 on) means only this
blank-to-TAB step. GENS1 (the first Devpac, 1983) keeps the blanks as typed.

## 2. The file

| | GENS1 (Devpac 1983) | GENS2 / GENS3 / GENS4 (all builds) |
|---|---|---|
| Line record | line number (2 bytes, little-endian, 1..32767), text, `#0D` | the same |
| File header | none | none |
| End marker | `#00 #00` after the last line (written by the P command, GENS1 `#6766`) | none: the container length ends the text |
| Blanks | as typed | compressed (§3); a TAB is the byte `#09` |

There is no other byte in a line: no tokens, no length byte, no flags. Text is whatever the keyboard
gave (upper / lower case kept); GENS prints bytes with `RST #10`, so a byte `#80`-`#FF` shows as a
block graphic, UDG or BASIC keyword (seen with crafted input only; no real source has one).

### 2.1 Containers

| Where | How the text is framed | Seen in |
|---|---|---|
| Tape, `P` command (GENS2-4) | standard header type 3 (CODE), name (10 chars, blank padded), length = text length, param 1 = the text's address in memory (depends on where GENS was loaded), param 2 = left over (0 in GENS3, #03F9 in a GENS4 session); data block = the lines | captured from GENS3 and GENS4 |
| Tape, `T` command ("include", for `*F`) | header **type 4**, name, length field = **number of lines + 1**, params 0; then data blocks of the include-buffer size (256 or the `C` command's value; 1089 bytes in a GENS3 session) each holding whole lines only. The reader (GENS3 `#78F3`, GENS4 `#7E16`) counts lines and takes a `#00 #00` word as "next block". GENS3 zeroes the buffer after each block, GENS4 only its first 34 bytes, so GENS4 blocks may end in stale bytes | typed and crafted files saved by GENS4 / GENS3 |
| Tape, GENS1 | built as a BASIC `SAVE name CODE` line run through the ROM (GENS1 `#617E`); text + `#00 #00` | code reading only: the save could not be captured in the emulator |
| TR-DOS (GENS4B, GENS4++ and other disk ports) | a file of type `C` (one corpus file is `A`, renamed), start = the text's address in memory (37066, 36066, 56066, 37072 in the corpus), length = text length; no marker | 5 real files |
| Microdrive / +3 / Opus / +D | the editor's same text block, saved through the system's own file calls (not examined) | — |

The text's memory address (the TR-DOS start / tape param 1) varies with the GENS build and where it
was loaded, so it does not identify the format; the line walk does.

## 3. Blank compression (GENS2-4)

The same routine in every GENS2, GENS3, GENS3M, GENS3M1, GENS3M21, GENS4, GENS4-51, GENS4 microdrive,
Devpac +3 (GENP3, GENP351, GENS4, GENS451), Opus Devpac, the TR-DOS GENS4B / GENS4++ / gens4edi and
VK Devpac 7.8 (byte pattern `7E FE 3B 28 16 FE 2A 28 12 CD`; GENS3 relocated to 26000: `#6ECF`).
On ENTER the line buffer is rewritten in place:

1. A line starting with `;` or `*` is kept as typed.
2. Otherwise find the first blank (from the first character on). If the line ends first: done.
3. Skip the run of blanks. If the line ends after it, the run is removed (trailing blanks dropped);
   else the run becomes one TAB.
4. From the character after that TAB, repeat 2-3 **once more**. The rest of the line is kept as typed.

So at most two TABs are made: label/mnemonic and mnemonic/operand, or, for a line typed with a
leading blank, the empty label and mnemonic/operand. Blanks before a comment in the third field
stay blanks (`A,(HL)   ; x`); `LAB ;COMMENT X` becomes `LAB<TAB>;COMMENT<TAB>X` (the rule does
not know about comments); `A1 B1 C1 D1` becomes `A1<TAB>B1<TAB>C1 D1`. CAPS SHIFT 8 while typing puts
blanks up to the next tab stop into the buffer, which the same rule then compresses.

GENS1's `Q` command (the manual's "convert GENS1 text") applies the same rule to a loaded file.

## 4. Display (what the editor lists)

`L` prints each line as the number right-aligned in 5 columns, a blank, then the text in a
26-column field; a longer line continues on the next screen rows, indented by 6 (an empty row
follows when the text ends exactly at a row end). A TAB moves to the next stop **7, 12, 21, 25**
of the current 26-column row; after 25 it goes to column 7 of the next row. (Measured on GENS3 and
GENS4, 32 columns. GENS4-51 and the TR-DOS GENS4B use a 51-column font with other stops; their
listing was not measured.)

Example, a line of seven TAB-separated letters: `    1 A      B    C        D   E` /
`             F    G        H`.

The codec's line text is this listing without the number and without the row wrap (`GensCodec::Expand`); the
number is kept in the line's attributes (`GensCodec::LineNumber`).

## 5. Versions found

| Program | File (where) | Size | Notes |
|---|---|--:|---|
| GENS (Devpac 1983, "GENS1") | `HiSoftDevpac.tzx.zip`, block `GENS` | 7170 | no compression, `#00 #00` end |
| GENS2 | `HiSoftDevpacV2.0.tzx.zip`, `HiSoftDevpacV2.tap.zip` | 8065 | compression at block offset `#093A` |
| GENS3 | `HiSoftDevpacV3.0.tzx.zip` (8354), `HiSoftDevpacV3.tap.zip` (8355), Dr.MG tape | 8354 | `#0944` |
| GENS3M / 3M1 | `HiSoftDevpacV3M.tzx.zip`, `HiSoftDevpacV3M1.tzx.zip` | 9046 / 9047 | `#0A7F` |
| GENS3M21 | `HiSoftDevpacV3M21*.zip`, `HiSoftDevpacV3M2Gens.tap.zip` | 10034 / 10035 | `#0B21` |
| GENS4 (V4.0 1987) / GENS4-51 | `HiSoftDevpacV4.tap.zip` | 10880 / 11392 | `#0AC6` / `#0C9E` |
| GENS4 V4.1 / 4-51 | `HiSoftDevpacV4.1GENS.tzx.zip`, `V4.1M`, vtrd `GENS4.ZIP` (SCLs) | 10880 / 11392 | `#0AD2` / `#0CAA` |
| GENS4 V4.1b microdrive | `HiSoftDevpacV4.1bGensMicrodrive.tap.zip` | 11010 | `#0AD2` |
| Devpac +3 (GENP3, GENP351, GENS4, GENS451) | `HiSoftDevpac.dsk.zip` | — | four copies of the routine |
| Opus Devpac | `HiSoftDevpac.opd.zip` | — | two copies |
| GENS4B (TR-DOS, V4.1) | `MONSGENS.LZH` → `GENS4B.$C` (start 30000) | 12174 | body offset `#0CAA` |
| GENS4++ (TR-DOS) | vtrd `GENS4TF.ZIP` | 12407 | `#0F87` |
| gens4edi (Edit-Gens System 1996) | vtrd `GENS+EDT.ZIP`, zxdb `Edit-GensSystemV1.0.trd.zip` | 12174 | `#0CAA` |
| VK Devpac 7.8 (Racunari 1985) | `VKDevpac7.8.tap.zip` | 20192 | `#360B` |

The archives come from vtrd.in and spectrumcomputing.co.uk (ZXDB). Manuals: `HiSoftDevpacV3.txt` / `.pdf`, `HiSoftDevpacV4.0.pdf`,
`HiSoftDevpacV4.1.txt` / `.pdf`; Russian ones in `ZX_GENS3.LZH`, `ZX_GENS4.LZH` and
`MONSGENS.LZH` (`GENS4.DOC`). The GENS3 manual's section 3.1 is the source of the words
"compressed text format".

Sources: [HiSoftDevpacV3.txt](https://spectrumcomputing.co.uk/pub/sinclair/games-info/h/HiSoftDevpacV3.txt),
[HiSoftDevpacV4.1.txt](https://spectrumcomputing.co.uk/pub/sinclair/games-info/h/HiSoftDevpacV4.1.txt),
[ZXDB entry 8091](https://spectrumcomputing.co.uk/entry/8091), [vtrd.in system](https://vtrd.in/system.php).

## 6. Detecting GENS bytes

1. Walk records from offset 0: number (1..32767, strictly increasing), bytes `#09` or `#20`-`#FF`,
   `#0D`. A TR-DOS / tape length must be filled exactly (a `#00 #00` right after the last line =
   GENS1). This walk alone separated the 5 real sources from everything else in about 4300 disk images
   and archives scanned.
2. GENS1 vs GENS2-4: the `#00 #00` end marker; TABs only in GENS2-4. A GENS2-4 file with no TAB at
   all (only `;` lines) cannot be told apart, and does not need to be: the text is the same.
3. Not ZEUS: ZEUS lines end in `#00`, the file in `#FF #FF`, and ZEUS uses bytes `#80`+ and `#0A n`.

## 7. Corpus and results

Testdata `gens/` (`*.txt` = the expected listing, number and text):

| File | Origin | Lines |
|---|---|--:|
| `WINDOW__WINDOW.$C` | window library, Vlad Staroselsky 1993 (Gens-4D), `WINDOW.ZIP` of the KLUG BBS archive | 1004 |
| `HISOFT-C__64-A95.$C` | 64-column print driver for HiSoft C, `HISOFT-C.ZIP` of the KLUG BBS archive | 368 |
| `ISC11VRG__ISCOP.C.$C` | iS-DOS tool source, [ISC11VRG.zip](https://vtrd.in/system/ISC11VRG.zip) | 165 |
| `ZX_NET__ZX_NET1.$A` | ZX-NET, `ZX_NET.RAR` of the KLUG BBS archive (saved as type A) | 116 |
| `PF212__BOOT.A.$C` | boot of DomenOS PinkFloyd 2.12, `PF212.ARJ` of the KLUG BBS archive | 42 |
| `typed-gens3-P-PROBE.bin`, `typed-gens4-P-PROBE4.bin` | **typed in the emulator**, saved with P (the data block, captured at the ROM save entry) | 9 + 9 |
| `crafted-gens3-P-PROBE1.bin` | a crafted file (many TABs, high bytes) loaded with G and saved again by GENS3 | 8 |

The KLUG BBS archive: [klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw). The T-command include files (one typed in
GENS4, one saved by GENS3) were checked with the reference decoder; the codec reads one text block (a TR-DOS file or
a tape data block) and leaves T's multi-block files to a tape container (TODO).

All 8 files byte-exact; the GENS2-4 rule (§3) applied to the listed text reproduces every real and typed line (1713);
the crafted lines with more than two TABs or `#80`+ bytes keep their stored bytes in the line attributes.

Every line of the 5 real sources was loaded into GENS4 (tape, 32 columns) with G and listed; the screen (OCR) was
compared row by row with the listing wrapped at 26 columns: see §8 for the counts.

## 8. Checks in the emulator

Own unreal-ng instances (48K model; GENS loaded with `LOAD "" CODE 26000`-equivalent memory writes
and `RANDOMIZE USR 26000`). Tape saves were captured by putting `JR $` at the ROM's SA-BYTES
(`#04C2`) and reading A / IX / DE and the bytes there, then returning to GENS.

| Check | Result |
|---|---|
| Probe lines typed into GENS3 and GENS4 (`typed-*-P-*.bin`) | stored exactly as §3 says (two TABs at most, `;` / `*` lines untouched, trailing blanks dropped) |
| P save, GENS3 and GENS4 | header type 3, length = text, param 1 = text address; data = the lines, no end marker |
| T save | header type 4, length = lines + 1; blocks of the include-buffer size with whole lines |
| Crafted file with many TABs and `#7F`-`#FF` bytes, loaded with G | listed with stops 7 / 12 / 21 / 25 per 26-column row; `#80`+ printed as Spectrum graphics / keywords |
| The 5 real sources loaded into GENS4 with G, listed in chunks of 7 lines, OCR vs the wrapped listing | **1809 screen rows, 0 differences** (WINDOW 1061, 64-A95 409, ISCOP 180, ZX_NET1 116, BOOT 43; chunks taller than one screen were skipped) |

GENS4B (TR-DOS, 51 columns) was started on a Pentagon with TR-DOS but its disk `G` crashed in
this session; its stored format is the same code (§3), and the five TR-DOS corpus files decode and
list correctly in the tape GENS4.

## 9. Open questions

| Item | Note |
|---|---|
| GENS1 tape save | not captured (P returned without saving in the emulator); layout from the code |
| 51-column listings (GENS4-51, GENS4B) | tab stops not measured; the stored bytes are the same |
| GENS4 T blocks | stale bytes after the lines of a block (no zeroing) would make the `#00 #00` reader misread; a tape container would keep them as each block's tail. Whether real GENS4 includes ever worked from such blocks was not tested |
| More real sources | only 5 found; GENS-format files on other tapes (e.g. magazine cover tapes) would widen the corpus |
| Not GENS | `AssemblerSources.tap.zip` (ZXDB 7925, German sources, `#0D #1F` lines) is another assembler's format |
