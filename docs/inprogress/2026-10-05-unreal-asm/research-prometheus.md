# unreal-asm: PROMETHEUS source format and conversion to sjasmplus

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Status** | Done: codec `src/codecs/prometheus/` (`prometheuscodec.{h,cpp}`, `prometheustable.{h,cpp}`), frontend `src/dialects/prometheus/`, tests `tests/prometheus_test.cpp`, test data `testdata/prometheus/`, `testdata/dialects/prometheus/` |
| **Assembler** | PROMETHEUS, assembler + editor + monitor by Proxima (Usti nad Labem, 1990-1993; the 48K tape edition shows "(C) 1990 UNIVERSUM"), [zxart](https://zxart.ee/prod/164989), [ZXDB 25427](https://spectrumcomputing.co.uk/entry/25427/ZX-Spectrum/Prometheus) |
| **Sources of the rules** | the annotated reconstruction of the program [oldcompcz/prometheus](https://github.com/oldcompcz/prometheus) (commit `4998e47`): `src/prometheus.asm`, `src/instructionTable.asm` and the book "The Liver of PROMETHEUS" (`book-en/`: chapters 12-14 source records, 22 expressions, 27 pseudo-instructions, 29-30 SAVE / LOAD); the English manual [prometheus_en.pdf](https://ci5.speccy.cz/files/prometheus_en.pdf); each rule below checked against PROMETHEUS 48K running in unreal-ng |
| **Corpus** | `d80/source-d80.tap` of the same repository: 55 PROMETHEUS saves of Proxima's routine library (graphics, fills, sprites, keys, tape, texts): 9 556 lines |
| **Result** | all 55 saves decode and encode back byte for byte; the canonical encoder rebuilds all 9 556 records from their text; the decoded text equals the independent decoder `prometheus-tap2asm` (same repository) line for line on the five test files; in the 32 saves whose table holds the values of an assembly, every label laid out from the conversion equals the stored value; a probe encoded from text loads into PROMETHEUS, shows line for line and assembles to the bytes its sjasmplus conversion gives |

## 1. Example first

```text
PROMETHEUS (as it shows the line)     record bytes                   sjasmplus (converted)
START    ld   (SPSTOR+1),sp            73 4A 80 01 80 02 2B 31 C6     START   LD (SPSTOR+1),SP
         ld   a,7*3+1/2                3E 09 37 2A 33 2B 31 2F 32 C9  LD A,(7*3+1&#FFFF)/(2&#FFFF)
         ld   (iy-3),7                 36 15 2D 33 1F 37 C4           LD (IY-3),7
         defw START,$                  09 37 80 01 2C 24 C4           DW START,$+2
         defs 3                        08 37 33 C1                    ORG $+3
```

A record keeps the opcode, an information byte, the label's and the names' ordinals and the operand's characters;
PROMETHEUS assembled `7*3+1/2` to 11 (left to right), `DEFW START,$` with the second word's own address, and left
`DEFS 3`'s bytes as they were.

## 2. The file

A save (`SAVE :name`, a tape CODE block; the disk adaptations write the same bytes as a file) is
`[records][2 bytes][symbol table]`:

| Part | Content |
|---|---|
| tape header | type 3, the name, the length of all, Param1 (not used by LOAD), **Param2 = the records' length** |
| records | one per line, below |
| 2 bytes | the checksum of the first part of the chained save (`#FF` XOR every record byte) and `#FF`, the next part's flag: SAVE writes the records and the table as one block in two ROM calls (all 32 checked saves) |
| symbol table | the count N; N vectors in ordinal order (bits 0-13 the offset of the name, bit 14 DEFINED, bit 15 LOCKED); then value (2 bytes) + name (high bit on the last character) records sorted by name; offsets count from two bytes into the records |

The values are those of the last assembly before the save (an oracle for the layout, §5). A codec reading a block
without its header finds Param2 itself: the first record boundary after which the checksum, `#FF` and a symbol
table fill the rest exactly (detection insists on the checksum: zeros read as NOPs with an empty table otherwise).

## 3. Records

| Byte | Meaning |
|---|---|
| +0 | the opcode, or a pseudo-opcode: 0 empty line, 1 comment, 2 `ENT`, 3 `EQU`, 4 `ORG`, 5 `PUT`, 6 `DEFB`, 7 `DEFM`, 8 `DEFS`, 9 `DEFW` (information byte DD+FD = `#3x`) |
| +1 | bits 7-4 CB / ED / DD / FD; bit 3 a label follows; bits 2-0 the operand class: 0 none, 1 byte, 2 word, 3 relative, 4 `(ix+d)`, 5 `(ix+d),n` (two parts split by `#1F`), 6 `rst` (opcode `#C7`, the operand kept as text), 7 pseudo |
| payload | the label's ordinal (`#80`+high, low) when bit 3 is set, then the operand's characters with every name as its ordinal; bytes `#00-#7F` literal, `#80-#BF` an ordinal's first byte |
| end | `#C0`+payload length (no end byte when there is no label and class 0) |

What the line looks like comes from PROMETHEUS' instruction table (686 records: opcode, prefix, class, mnemonic
and operand indices; `prometheustable.cpp` is read from it): `ld (N),hl`, `rlc (ix+d)`, `slia b`; FD records share
the DD ones with `iy`, `hy`, `ly`. The `+` of `(ix+d)` is the template's: a displacement written `-3` keeps its
sign, `+5` is stored as `5`. Hex digits are stored in capitals, names in capitals.

The text the codec gives is PROMETHEUS' own view: the label padded to 9 columns, the mnemonic to 5 when operands
follow, mnemonics and registers in lower case, comments as typed (`;` in column 0; PROMETHEUS has no comment after an
instruction).

## 4. Encoding

`EncodeLine` reverses it: the label, the mnemonic, the operands matched against the table's templates (a fixed word
before an indexed operand before an expression: `ld (ix+0),a` is not `ld (N),a` with `IX+0`), names numbered in order
of appearance (a new name joins the table). `Encode` keeps a line's record while it still reads as the line's text,
keeps the table while no name is added, and computes the two middle bytes. From text alone the table gets value 0 and
no flags (PROMETHEUS recomputes them when it assembles).

## 5. The language, as PROMETHEUS assembles it

| Rule | Checked | Conversion |
|---|---|---|
| Strictly left to right, `+ - * / ?` (`/` and `?` unsigned), 16-bit; a sign before any atom | probe: `7*3+1/2` = 11, `-1/2` = `#7FFF`, `100?7` = 2 | `expressionBits = 16`, unsigned: the backend masks for division |
| `#` hex, `%` binary, `"A"` one byte, `"AB"` two (`"A"` the high byte), `""""` the quote | probe: `"AB"` = `#4142`; `cp """"` in `-input` | numbers, character constants |
| `$` in a `DEFB` / `DEFW` item is that item's address | probe: `DEFW START,$` gave `#EA71` at `#EA6F` | `$+2`, `$+4` … per item |
| `DEFS n` reserves a hole: nothing written | probe: the three bytes kept `#AA` | `ORG $+n` (zeros under a `PUT`, with a note) |
| `ORG e` sets both the address and where the bytes go; `PUT e` only where they go | probe: `PUT 62000` put `LD HL,HERE` there with `HERE` = 60041 | `PUT`: `__PROMETHEUS_PUTn=$`, `ORG e`, `DISP __PROMETHEUS_PUTn`; `ORG` ends it (`ENT`) |
| `ENT e`: the address `RUN` jumps to | — | a comment |
| `EQU` evaluated in pass 1 (only earlier names or locked ones) | book ch. 27 | `EQU` |
| `HX LX HY LY`, `SLIA` | probe: `DD 26 05`, `FD 7D`, `CB 30` | `IXH IXL IYH IYL`, `SLI` |
| Without `ORG` the code goes after the source and the table | the saves' values (`+fill1`: `START` = 41892) | starts at 0 (the layout check uses the stored start) |

**Labels (32 saves):** laid out from the conversion, every label differs from its stored value by one constant per
save (the start); `EQU`s equal. `+fill1`, `+plots`, `+graphics` align an `ORG` to 256 / 512 from a label: with the
stored start all their labels are equal (test `LabelsEqualTheValuesPrometheusStored`).

## 6. In the emulator (2026-10-08)

PROMETHEUS 48K from `tap/prometheus-48.tap` (the repository's), 48K model, tape fast loading on. The steps are in
`.recipe/assemblers/prometheus.md`; what the work found:

| Finding | How it showed | What decides it |
|---|---|---|
| `LOAD` without a name loads only a file named like the last one (`prometheus` at first) | "Found:probe", then the loader waits for the next header (PC at ROM `#05ED` LD-EDGE, DE=`#0011`, IX the bottom screen line) | `acceptLoadedHeaderIfNameMatchesOrWildcard`: give `LOAD :name`, or a name starting with a blank (the wildcard) |
| A save whose two middle bytes are not the checksum | the same wait (an early probe had fixed bytes `#37 #FF`) | the checksum rule of §2; the encoder computes it |
| LOAD reads header and data with one ROM LD-BYTES call each, entered at `#0562` (not `#0556`) | the reconstructed source; works with the emulator's fast loading | only SAVE / VERIFY chain two parts (`#05C8` LD-MARKER) |
| The installer puts PROMETHEUS at 24000 with the monitor: up to about 42400 | the probe at 40000 would collide; it uses 60000 / 62000 | pick addresses above the source and table (status line: `I <end> <U-TOP>`) |
| Real-time tape loading fails in this emulator build | with `fast_tape` and `turbo_tape` off even a plain `LOAD "" CODE` of a two-block TAP stops with "R Tape loading error, 0:1", the tape `paused` at block 1 | an emulator issue (tape playback across the pause between blocks), not PROMETHEUS': noted for the tape code, keep fast loading on |

## 7. Open items

| Item | Note |
|---|---|
| TR-DOS adaptations | `trdos-prometheus48.trd` and the D40 / D80 builds may keep records and table in two files; the codec reads one save (the tape layout) |
| GENS import (`GENS` command) | PROMETHEUS reads GENS / MASM tape text; not needed for the conversion |
| Real-time tape loading | the emulator issue of §6 |
