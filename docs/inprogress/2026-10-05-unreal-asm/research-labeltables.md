# unreal-asm: label tables in RAM (ALASM, XAS, STS)

| | |
|---|---|
| **Date** | 2026-10-07 |
| **Scope** | symbols/formats.md §4.2, steps R1-R6: where an assembler running in the machine keeps its labels and how one entry is laid out, found from RAM dumps, not from guesses |
| **Method** | own unreal-ng instance (`PENTAGON`, 128K); each assembler started from its disk, a source with known labels assembled, the RAM pages read before and after (`GET /memory/page/ram/{n}?format=binary&offset=0&length=16384`; without `length` the endpoint gives a 128-byte window) and compared; every layout confirmed on a second source |
| **Result** | ALASM 5.09 and 4.44, XAS 7.447 and 4.18: layouts below, golden pages in `core/src/3rdparty/unreal-asm/testdata/symbols/live/`, read by `symbols/live.h` (`symconv live`). STS keeps no table of its own (§3) |

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
| +3 | 2 | zero in every table seen (also for labels after `ORG #C000,4`: the page is not kept) |
| +5 | size-5 | the name, last character first; case kept (`alpha`), full length (19 characters seen) |

ALASM's `SYMBOL` command walks the same records (`al2_44` in `AL446SRC`, label `symbols`): `AND 63` for the size,
`#40` Macro, `#80` NoDef, `#C0` Wrong, the name compared backwards from the record's end.

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

**4.18** (`XASo`, "XAS by Max Petrov (HPM) 3.091"): one list in RAM page 6 **going up from `#0B16`**, in definition
order, ended by a zero byte; no markers. Seen: `cons418` (7 labels) and the same probe (27 names).

## 3. STS

STS 7.0 / 7.1's own source (`STS7!!!!.RAR`, `STS70`, `71DASM`) has no symbol table: its label lookup (`RES_WL`)
walks **ALASM's** table in the page ALASM passes it (`AL_STS_BANK`; ALASM 4.46 sets `STSlabAD` / `STSlabPG`), and
its disassembler's label buffer (`71DASM`) holds 3-byte entries (a flag and an address) whose names STS makes up. So
STS's labels are ALASM's, read by the ALASM scanner. STS 5.x / 6.x (with XAS) were not examined: their help is
packed, and Unreal 0.37's note that STS keeps labels in bank 7 is not confirmed.

## 4. The scanners (`symbols/live.h`, `src/symbols/live/`)

- `alasm-table`: in every page given, every start whose records chain (sizes 6-63, valid name characters, a name not
  starting with a digit) to a zero byte; the longest chain per terminator. Score: the entries, +50 when the chain
  ends at `#3DFF` (5.0x) or `#3F7F` (4.4x), +20 in page 3. Defined labels only; macro names, undefined and wrong
  entries are counted in the diagnostics.
- `xas-table`: a page with both markers is a 7.x table (the two lists, A-L then M-Z); otherwise runs of at least two
  entries going up to a zero byte are 4.x tables (+50 at `#0B16`). Names fold case.
- Values are CPU addresses: neither table says which page a value was assembled for.
- `symconv live 3:ram3.bin 6:ram6.bin --to unreal-map` lists the tables found (stderr) and writes the best;
  `--pick N` another.

## 5. Open

- Label files on disk: neither ALASM nor XAS was seen saving its table to a file (none found in the collection).
- ALASM tables larger than one page (`Symbols pg - #43,#C3` names a second page) and above 128K; XAS 7.x on a
  Pentagon above 128K (bank `#46` by Unreal's note); XAS 5.05 / 9.x; ALASM 3.8c / 4.2 / 4.5.
- STS 5.x / 6.x.
- The emulator surface (copy the pages of a running machine, offer the candidates): with S5.
