# unreal-asm: label tables in RAM (ALASM, XAS, STS)

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Scope** | symbols/formats.md §4.2, steps R1-R6: where an assembler running in the machine keeps its labels and how one entry is laid out, found from RAM dumps, not from guesses |
| **Method** | own unreal-ng instance (`PENTAGON`, 128K); each assembler started from its disk, a source with known labels assembled, the RAM pages read before and after (`GET /memory/page/ram/{n}?format=binary&offset=0&length=16384`; without `length` the endpoint gives a 128-byte window) and compared; every layout confirmed on a second source |
| **Result** | ALASM 5.09, 4.5, 4.44, 4.2 and 3.8c (5.09 also over two pages of a Pentagon 512; the 4.x builds keep one), XAS 9.10, 9.07m, 7.447 (also on a Pentagon 512), 5.05 and 4.18: layouts below, golden pages in `core/src/3rdparty/unreal-asm/testdata/symbols/live/`, read by `symbols/live.h` (`symconv live`). STS keeps no table of its own (§3) |

The prior art (Unreal Speccy 0.37.1's `dbglabls.cpp`, [prior-art.md](prior-art.md) P7) described both layouts; the dumps
confirm them and add the end offsets, the flag values, the version differences and XAS's split of the names.

## 1. ALASM

**Where.** ALASM's `INFO` names the page: 5.09 prints `Symbols pg - #43,#C3` (RAM page 3 with its memory driver's
bits), 4.44 `Symbols list page - #03`. The table grows down from the top of the page and ends at a zero byte:
`#3DFF` in 5.09, `#3F7F` in 4.44 (ALASM 4.46's own source, `AL446SRC`: `labTAB=#FE00;#FF80` and `labEND=#FDFE`, the
two placements). `ASSEMBLE` reports the lowest byte: `Symbols:#FD89` for the first source (the table from `#3D8A`).

**Entry** (one record, lowest address first; the newest label is the lowest record):

| Offset | Bytes | Meaning |
|---|---|---|
| +0 | 1 | bits 0-5: the record's size (5 + the name's length); bits 6-7: `0` defined, `1` a macro name (value = its address in the macro page, `#C000` up), `2` used but not defined (`NoDef`), `3` its line had an error (`Wrong`) |
| +1 | 2 | the value, low byte first |
| +3 | 2 | zero in a small table (also for labels after `ORG #C000,4`: the page is not kept); in a large one (1800 names) a CPU address in the table for many records: ALASM's hash chain link, not needed to read the table |
| +5 | size-5 | the name, last character first; case kept (`alpha`), full length (19 characters seen) |

ALASM's `SYMBOL` command walks the same records (`al2_44` in `AL446SRC`, label `symbols`): `AND 63` for the size,
`#40` Macro, `#80` NoDef, `#C0` Wrong, the name compared backwards from the record's end.

**Versions.** 3.8c and 4.2 keep the table in page 3, 4.5 in page 6 (`INFO`: `Symbols list page - #0E`), all with
the 4.4x end at `#3F7F`; 5.0x ends at `#3DFF`.

**One page in 4.x** (2026-10-18, run): 4.42, 4.44 and 4.5 on a Pentagon 512 report `Memory test: 128kB mask#07`
and stop 1800 names with `Symbols overflow!` after `#0610` of them; the table stays in its one page (1600 records
read). ALASM 4.46's source sets `MSK_PG EQU #07` and one symbol page in the build everybody got; the author's own
configuration (`ver2pipl`) has `MSK_PG #C7` and a second page (`SYM1 #C3`), and no such build is in the collection.

### 1.1 A table over two pages (ALASM 5.09)

`INFO`'s `Symbols pg - #43,#C3` names two pages: the memory driver's page numbers, whose bits 0-2 go to `#7FFD`
and bits 6 / 7 select the extended pages (a Pentagon 512: `#43` = RAM 11, `#C3` = RAM 27). The table grows down
from `#FDFF` of the first page to `#C0FB` (`#C000-#C0FA` hold other data) and goes on from the top of the second,
which ALASM addresses at `#8000-#BFFF`: 1800 names `L0000`-`L1799` filled RAM 11 with 1562 records and put 238 at
the top of RAM 27 (`ASSEMBLE`: `Symbols:#B6B3`). On 128K both numbers mean page 3: the second part overwrites the
first (the oldest 187 names were gone, and no terminator was left). The scanner joins a 5.0x chain that starts near
the bottom of a page with the top of any other page given (`LiveCandidate::lowerPage`, `split`).

Seen: `LTA` (11 entries: code and EQU labels, a 19-character name, a LOCAL block's label, a macro, an undefined name)
in 5.09 and 4.44, `LTB` (7 entries, labels after `ORG #C000,4` / `ORG #C100,6`) in 5.09. Names with `?` or `$`
are refused by ALASM 5.09 (error `#0D`), so the reader accepts letters, digits and `_ @ . !`.

## 2. XAS

Both versions store an entry as **9 bytes: the name in 7 characters** (capitals, blank-padded, cut to 7 - XAS compares
the first 7) **and the 16-bit value**; no page, no kind.

**7.447** (`XAS7.447`): RAM page 6 holds two lists, each marked by the byte `5` at `+#1FFF` / `+#3FFF` and going
down from just below the marker: names starting **A-L** under `#3FFF`, **M-Z** under `#1FFF`, each list **sorted**
(`EQ1` between `EEEEE` and `FFFFFF`). An entry whose first byte is `#80` ends a list (the unused entries below are
`#80` and zeros). Seen: `constrct` (the XAS frontend's oracle: 8 labels, values equal to sjasmplus' `--sym` of its
conversion) and a probe of 26 names over the alphabet.

**9.10 / 9.07m** keep the 7.x layout in page 6 (the same probe: the same 27 entries); on a Pentagon 512, 7.447 puts
it in **RAM 14** (`#46`: page 6 with bit 6, as Unreal's note says), and on a Pentagon 1024 too (2026-10-18: the
scan found the 4 labels of a probe there, RAM 14, the 7.x layout).

**5.05** (5.05SE): **one list** going down from just below `#3FFF` of page 6 **in definition order**, no markers
(`#56` at `#3FFF`), ended by an entry starting with `#80`.

**4.18** (`XASo`, "XAS by Max Petrov (HPM) 3.091"): one list in RAM page 6 **going up from `#0B16`**, in definition
order, ended by a zero byte; no markers. Seen: `cons418` (7 labels) and the same probe (27 names).

## 3. STS

STS 7.0 / 7.1's own source (`STS7!!!!.RAR`, `STS70`, `71DASM`) has no symbol table: its label lookup (`RES_WL`)
walks **ALASM's** table in the page ALASM passes it (`AL_STS_BANK`; ALASM 4.46 sets `STSlabAD` / `STSlabPG`), and
its disassembler's label buffer (`71DASM`) holds 3-byte entries (a flag and an address) whose names STS makes up. So
STS's labels are ALASM's, read by the ALASM scanner. STS 5.1 / 5.4 hold no string about labels. STS 6.2 has
"[SS+5] - Disasm/Label_Disasm Mod". Run on 2026-10-18: the copy that ALASM 4.2's ALM loader puts in page 7
(`#D000`), entered with ALASM's `Debug`, shows 4.2's labels after SS+5 (`start` in the address column at `#6000`,
`LD DE,(table)`). The copy on the XAS 9.07m disk (332 bytes differ), entered with XAS's `sTs`, drops the byte column
on SS+5 but shows no name while XAS 9.07m's table is in page 6: the XAS file names in its body (`MTABAS  XAS`, ...,
`]XAS9.06+B`) are leftover catalog sectors after the code, not a reader of XAS's table. Unreal 0.37's note that STS
keeps labels in bank 7 is not confirmed for any version.

**STS 5.1** ("Monitor-Debugger 48-512K, STS Main Bank 23, Test version, Piter '97", `sts5.1a` / `sts5.1b` on the
XAS 7.447 disk) reads **XAS 7.4x's** table. Run on 2026-10-20 (Pentagon): XAS 7.447 assembled a probe (`START`,
`ROUTIN`, `TABLE` at `#6000`), EXT then CS+T entered STS with PC at the code; SS+5 put the names in the address column
and the operands (`LD HL,TABLE`, `CALL ROUTIN`, `JP START`, `LD A,(TABLE)`), capitals as XAS folds them. The
`xas-table` scanner finds that table in page 6 (7.x, the same 3 names and values). So STS 5.1's labels, like STS 6.2's
and 7's, are the assembler's own table; the string search found nothing because 5.1 has no message about labels.

## 4. The scanners (`symbols/live.h`, `src/symbols/live/`)

- `alasm-table`: in every page given, every start whose records chain (sizes 6-63, valid name characters, a name not
  starting with a digit) to a zero byte; the longest chain per terminator. Score: the entries, +50 when the chain
  ends at `#3DFF` (5.0x) or `#3F7F` (4.4x), +20 in page 3. Defined labels only; macro names, undefined and wrong
  entries are counted in the diagnostics.
- `xas-table`: a page with both markers is a 7.x table (the two lists, A-L then M-Z; +20 in page 6 or 14); a list going
  down from `#3FFF` without markers is 5.x; otherwise runs of at least two entries going up to a zero byte are 4.x
  tables (+50 at `#0B16`). Names fold case.
- Several tables may be in memory at once: a reset does not clear RAM, so an assembler run before leaves its table
  (an ALASM table from an earlier run outranked a live XAS one on the Pentagon 512). The caller picks (`--pick N`).
- Values are CPU addresses: neither table says which page a value was assembled for.
- `symconv live 3:ram3.bin 6:ram6.bin --to unreal-map` lists the tables found (stderr) and writes the best;
  `--pick N` another.

## 5. Open

- Label files on disk: neither ALASM nor XAS was seen saving its table to a file (none found in the collection).
- An ALASM 4.x build configured for more memory (none in the collection; the shipped ones keep one page).
- The emulator surface (copy the pages of a running machine, offer the candidates): with S5.
