# unreal-asm: prior art and comparison

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Local and public surveys done (2026-10-05); every link checked (HTTP 200 unless marked) |
| **Rule** | Nothing is vendored (decision D-2, [goals-and-requirements.md](goals-and-requirements.md) §3). Prior art is a reference for the formats: token tables, file layouts, test data. Every codec is a fresh implementation with its own tests. |

## 1. Local survey

Searched: the owner's project trees (`~/Projects`, the projects volume) and the local emulator source trees, by file
name and content (`tasm`, `detoken`, `alasm`, `xas`, `storm`, `gens`). Paths are given relative to the owner's
`ZX-Spectrum` projects folder (outside this repository).

### 1.1 What was found

| # | Project | Language, date | What it does | Formats |
|---|---|---|---|---|
| L1 | `zxtasm/zxtasm/src/ZConverter_TASM4.cpp` (+ `ZXIOTRD`, `DiskImage`) | C++, 2002-2012, the owner's own code | Detokenizes ZX **TASM** sources: opens a TRD, takes every file of type `A`, writes `NAME.a80` text | TASM tokenized source → text (decode only); TRD read |
| L2 | `zxtasm/tasm-to-text/` | C++17, 2025, CMake + GoogleTest | A cross-platform port of L1 with a CLI (`.a`, `.$A`, `.trd` input) and tests | the same as L1 |
| L3 | `zxtasm/doc/token_format.md`, `architecture.md` | Markdown, 2025 | A write-up of the TASM token format | — |
| L4 | `tasm-to-text/tasm_to_text.py` (+ `LIMITATIONS.md`) | Python 3, 2025 | A heuristic decoder for TASM **3.x** files: treats parts as Z80 machine code, guesses labels from ASCII runs | TASM 3.x → text (heuristic) |
| L5 | Test data: `TASM3_2.SCL.trd` (the TASM 3.2 disk), `000LOAD.$A`, `CALLLOAD.$A`, `LS4.$H`, reference outputs `*.a80`, `CALLLOAD.asm` | binary + text | Real tokenized files and the converters' outputs | — |
| L6 | `tap2bas`, `bas2tap` | C | ZX BASIC detokenize / tokenize | ZX BASIC (not symbols; the same technique) |
| L7 | Emulator trees: ZXMAK2, ZEsarUX, Fuse (+ utils), UnrealSpeccyP, Unreal Speccy (two copies), mctrd, the TS-Labs Unreal | C, C++, C# | No importer of ALASM / XAS / STORM / GENS / TASM labels. The Unreal copies here (0.38+ lineage) declare `find_xas` / `find_alasm` in `dbglabls.h` but have no bodies; the bodies exist in Unreal **0.37.1** (P7 in §2) | — |
| L8 | **This repository**: `docs/disasm/demo/thelink/extract_thelink.py` | Python, 2026-09 | Detokenizes **ALASM** sources (TR-DOS type `H`) of a demo disk to text, from the ALASM build on the same disk: 64-byte header (name, source length at `+#21`), lines `[length incl. itself][body]`, `#01-#1F` = space runs, first byte `>= #80` = mnemonic table (`#80` INCLUDE … `#E6` RUN), later ones = operand table (`(BC)` from `#9F`, `(C) (IX (IY AF'` from `#D0`, `BC..I` from `#E0`), CP866 text in comments / strings, `#FF` = editor line flag | ALASM source → text (decode only) |

### 1.2 The TASM format as L1 implements it (reference for the TASM research, S6)

L1 is the oldest and only first-hand implementation; L2 and L3 were derived from it later.

| Item | L1 behavior |
|---|---|
| File | a TR-DOS file of type `A` (`.$A` in hobeta naming) |
| Records | one per source line: `[length n][n bytes]`, and the next record starts `n + 2` bytes after this one: one more byte follows the data (its meaning is not used by L1; **to verify**: a repeated length for walking backwards, or a line terminator) |
| End | a record whose length byte is `#FF` |
| Bytes `#00-#7F` | ASCII as is |
| `#0A n` | n spaces (indentation) |
| Bytes `#80-#F0` | tokens: 113 entries, below |
| Bytes `#F1-#FF` | not tokens in L1 (printed as `0xNN` with an error) |

L1's token table (the trailing blank is part of the token: `"ld "` already carries the separator):

| Base | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
|---|---|---|---|---|---|---|---|---|
| `#80` | `a` | `adc ` | `add ` | `af'` | `af` | `and ` | `b` | `bc` |
| `#88` | `bit ` | `c` | `call ` | `ccf` | `cp ` | `cpd` | `cpdr` | `cpi` |
| `#90` | `cpir` | `cpl` | `d` | `daa` | `de` | `dec ` | `defb ` | `defm ` |
| `#98` | `defs ` | `defw ` | `di` | `phase ` | `djnz ` | `e` | `ei` | `unphase` |
| `#A0` | `equ ` | `ex ` | `exx` | `h` | `halt` | `hl` | `i` | `im ` |
| `#A8` | `in ` | `inc ` | `ind` | `indr` | `ini` | `inir` | `ix` | `iy` |
| `#B0` | `jp ` | `jr ` | `l` | `ld ` | `ldd` | `lddr` | `ldi` | `ldir` |
| `#B8` | `m` | `nc` | `neg` | `nop` | `nv` | `nz` | `or ` | `org ` |
| `#C0` | `otdr` | `otir` | `out ` | `outd` | `outi` | `p` | `pe` | `po` |
| `#C8` | `pop ` | `push ` | `r` | `res ` | `ret` | `reti` | `retn` | `rl ` |
| `#D0` | `rla` | `rlc ` | `rlca` | `rld` | `rr ` | `rra` | `rrc ` | `rrca` |
| `#D8` | `rrd` | `rst ` | `sbc ` | `scf` | `set ` | `sla ` | `sp` | `sra ` |
| `#E0` | `srl ` | `sub ` | `v` | `xor ` | `z` | `include ` | `incbin ` | `sli ` |
| `#E8` | `inf` | `lx` | `hx` | `ly` | `hy` | `db ` | `dm ` | `ds ` |
| `#F0` | `dw ` | | | | | | | |

Open points for the research: whether this table is TASM 4.x (L1's name says `TASM4`) and how TASM 3.x differs (L4
found tokens it could not place: `I`, `R`, `IM`, `IN` / `AND` by context; L1's table has `i` = `#A6`, `r` = `#CA`,
`im ` = `#A7`, `in ` = `#A8`, which suggests L4 decoded 3.x files with a 4.x-shaped guess, or 3.x numbers
differently); what the byte after each record is; how labels and local labels are written (plain ASCII at the line
start in the files seen); whether TASM keeps a label table in RAM after assembling (a live scanner).

**Settled** ([research-tasm.md](research-tasm.md)): L1's table is TASM 4.0 XL Design / 4.4 KVA (`#E7`-`#F0` exist
only there); TASM 3.x has the same table up to `#E6`; 4.12 is a separate Rst7 line with direct blank counts.

### 1.3 Comparison of the local implementations

| | L1 (owner, 2012) | L2 (port, 2025) | L3 (doc, 2025) | L4 (Python, 2025) |
|---|---|---|---|---|
| Source of the format | first-hand | copied from L1 | derived from L1 | guesses from samples |
| Token table | 113 entries `#80-#F0` | as L1 | **differs from L1** (e.g. `#97` = `d` instead of `defm `, `#98` = `equ ` instead of `defs `, duplicated entries) | own, partial, context-guessed |
| Record layout | `[len][data][1 byte]`, `#FF` end | as L1 | describes `[len][data]` only (misses the extra byte) | treats the file as mixed text and machine code |
| Format name | TASM4 (ZX) | "Telemark Assembler" in the README (wrong: Telemark TASM is a PC cross assembler) | the same mistake | TASM 3.x |
| Encode (text → tokens) | no | no | no | no |
| Labels / symbols | not separated (text output only) | no | no | ASCII-run heuristic |
| Tests | none | GoogleTest suite + TRD data | — | reference comparison (fails, see its `LIMITATIONS.md`) |
| Accuracy stated | — | — | — | 70-80 % of instructions |
| Use here | **reference for the token table and layout** | test ideas, test data | not used (errors) | not used; its failure list is a checklist for the 3.x research |

### 1.4 What this means for the design

- TASM becomes the **first tokenized codec** (phase A2, open question Q-1): L1 gives a table to verify instead of a blank page,
  and L5 gives real files.
- The codec must do what none of L1-L4 does: **encode** (text → tokens, decision D-1), separate **labels** into
  symbol records, tell **TASM 3.x from 4.x**, and keep the record trailer byte exactly.
- Golden files: the L5 files, plus files saved by TASM 3.2 and 4.x running in the emulator from one known source
  (R2-R4 of [source-formats.md](source-formats.md) §3). The emitted text is compared with what TASM itself shows on screen, not
  with L1-L4 outputs.

## 2. Public survey

Searched: GitHub (code and repository search), vtrd.in (PC utilities), zxart.ee, spectrumcomputing.co.uk, the
sjasmplus and DeZog documentation, zx-pk.ru (answers 403 to scripts: browser only). Links checked 2026-10-05.

### 2.1 Tokenized sources

| # | Name | Link | Language / licence | Formats | What it reveals | Maturity |
|---|---|---|---|---|---|---|
| P1 | **ZX-M8XXX** `core/asm-detok.js`, `core/asm-convert.js` (Bedazzle) | [repo](https://github.com/Bedazzle/ZX-M8XXX), [asm-detok.js](https://github.com/Bedazzle/ZX-M8XXX/blob/main/core/asm-detok.js), [docs/assembler.md](https://github.com/Bedazzle/ZX-M8XXX/blob/main/docs/assembler.md) | JavaScript, ~800 lines / **no licence stated** | decode: ALASM, TASM 3.x, TASM 4.x, ADS (TASM-family editor), STORM → sjasmplus text; recognizes MASM, ZXASM, XAS without decoding; GENS, Zeus, Pasmo only as text | its header: a port of the xLook v0.2b FAR plugin sources (ALASM.CPP, TASM.CPP, STORM.CPP; D. Kozlov "HalfElf", A. Medvedev). ALASM signature `F3 76 C7 DD FD ED B0 D9` (checked in the file); full token tables; framing; TR-DOS catalog hints per assembler (2.3) | active (last push 2026-10-02), documented |
| P2 | **H2ASM** (A. Shabarshin "Shaos", 2002) | [H2ASM.zip](https://vtrd.in/pcutilz/H2ASM.zip) | C++ (`h2asm.cpp`, ~8 KB) / none stated | decode: ALASM (hobeta) → text | a full switch over the `#80+` tokens; **conflicts with P1**: `#96` = `DEFM` here, `DD` in P1; `#9F` depends on the column (`(BC)` vs `ELSE`) - likely ALASM version differences, to settle on real files | small, old |
| P3 | **ZAsm View 1.1** (Hard / D. Mikhalchenkov, 2014) | [ZASMVIEW.zip](https://vtrd.in/pcutilz/ZASMVIEW.zip) | binary + `za_format.txt` (CP1251) | ZX-ASM 2.5 / 3.x, ZAsm 3.01 / 3.10 | a **written format spec**: text with `#0D` line ends; `#06 n` = n & `#7F` spaces; `#02-#05 t` = token t (`#20-#C8`), prefix bit 0 = upper case, bit 2 = a space follows; the full token list; extensions `C`, `zas`, `asm` | 2014 |
| P4 | **xLook 0.2b** (HalfElf, A. Medvedev) | inside [XFAR.zip](https://vtrd.in/pcutilz/XFAR.zip) | binary (FAR2 / FAR3 x64 `.DTR` detectors); sources private | ALASM, MASM, STORM, TASM, XAS, ZXASM | the original P1 ports; MASM, XAS, ZXASM decoding exists only here | stable |
| P5 | AlasmView 0.2 (Vitamin / CAIG, 2001), inAlasm 3.01 (PushPC, 2014) | [ALVIEW.ZIP](https://vtrd.in/pcutilz/ALVIEW.ZIP), [INALASM.zip](https://vtrd.in/pcutilz/INALASM.zip) | binary | ALASM (inAlasm: up to 5.09) | inAlasm ships a sample `.H` file (a test fixture) | old / 2014 |
| P6 | ALASM → XAS converter (1997, runs on the Spectrum) | [zxart](https://zxart.ee/eng/software/prikladnoe-po/word-processor/utility-dlja-teksta/alasm-to-x-assembler-converter) | Z80 binary | ALASM → XAS | both formats as seen by a native tool (reverse engineering material) | — |
| — | GENS / Devpac | [HiSoft Devpac manual](https://spectrumcomputing.co.uk/pub/sinclair/games-info/h/HiSoftDevpacV3.pdf) | PDF | GENS1 text → GENS3 "compressed" (the `Q` command: tab compression) | no byte layout | — |

Not found anywhere public: open decoders or written layouts for XAS 5 / 7, MASM / MASM80 (Sprinter), Zeus
tokenized, GENS3 compressed, AS80, STS / MONS label tables.

### 2.2 Label tables in the running machine

| # | Name | Link | Language / licence | What it reveals |
|---|---|---|---|---|
| P7 | **Unreal Speccy 0.37.1** `src/dbglabls.cpp` (a copy of the 0.37.1 sources kept in a game repository) | [dbglabls.cpp](https://github.com/ZXSpectrumVault/MightyFinalFight/blob/master/emul/US0371/src/dbglabls.cpp) | C++ / Unreal's licence | the bodies of `find_alasm` / `import_alasm` / `find_xas` / `import_xas`, removed in later Unreal versions (they keep only the declarations). Checked in the file: **ALASM** - every RAM page is scanned for record chains: byte `s1` (low 6 bits = record size, high 2 bits = flags; flagged records skipped), `+1` = 16-bit value, the name **reversed** from `+5` to the record end (characters `0-9 A-Z a-z @ $ _`, not ending in a digit), size ≥ 6, the chain ends at a zero byte or at offset `#3E00`, at least 2 records. **XAS** - bank 6 (or `#46` on a Pentagon above 128K) marked by `5` at `+#1FFF` and `+#3FFF`; two lists going down from `#1FFD` and `#3FFD` with a 9-byte stride: a 7-character blank-padded name just below a 16-bit value; a list stops when `ptr[2] < 5` or has bit 7 set. Both bind a value to RAM page 5 / 2 / 0 by its top bits (`#4000` / `#8000` / `#C000`), ignoring the real paging and dropping values below `#4000` |
| P9 | **ALASM's own sources** (Alone Coder): ALASM 5.09 + STS 7.5 and 5.08 with sources; `alTOKENS.H` holds the tokenizer (`cnv2str`), the detokenizer (`str2txt`) and the keyword tables | [Alone Coder's page](http://alonecoder.nedopc.com/zx/): [ALASM509_STS75.rar](http://alonecoder.nedopc.com/zx/ALASM509_STS75.rar), [ALASM508.rar](http://alonecoder.nedopc.com/zx/ALASM508.rar), [ALASMENG.rar](http://alonecoder.nedopc.com/zx/ALASMENG.rar) (English help); every release on [zxart](https://zxart.ee/prod/155267) | ALASM sources (TR-DOS images) / none stated | the format **as ALASM implements it**: settles the P1 / P2 conflicts (`#96` = `DD` in 5.x, `DEFM` in 4.x; `#9F` = `ELSE` or `(BC)` by position); the page also has many ALASM project sources (PT, ACE, games) used as the corpus | 2007-2011 |
| P8 | Xpeccy `src/xcore/labels.cpp` | [labels.cpp](https://github.com/samstyle/Xpeccy/blob/master/src/xcore/labels.cpp) | C++ / MIT | reads the sjasmplus / Unreal `PP:AAAA name` form (a line starting with `:` is a CPU address); no ALASM / XAS memory import |

### 2.3 TR-DOS catalog hints (from P1, to verify on real disks)

| Assembler | Catalog entry |
|---|---|
| TASM 3 | type `A`, start 39221 or 40872 |
| TASM 4 | type `A`, start ≤ 4096 |
| STORM | type `C`, start `#C00B` or `#C003`; or type `R`, start `#C00B` |
| MASM | type `a`, start 38667 or 38821 |
| ZXASM | type `a` with the start bytes spelling `sm` or two blanks; type `z` spelling `as`; type `C` start 35151; type `a` start 28001 |
| XAS | type `X` or `x`, start bytes spelling `AS` |
| ALASM | type `H` (L8, P2); signature `F3 76 C7 DD FD ED B0 D9` (P1) |

P1 also documents: **TASM** lines framed `len, content, len` (this explains L1's `n + 2`: the trailing byte repeats
the length), space runs `#0A n` in TASM 3 and `#01 n` in TASM 4, TASM 4 reassigning some tokens (`DEFMAC`,
`DISPLAY`, `ENDMAC`); **STORM** lines stored back to front, each ending with a byte whose low 6 bits are its length
(the file is walked from the end), tokens `#09-#8B`, fully tokenized expressions (8 number forms, nested
sub-expressions, labels packed as 6-bit characters with the first byte ≥ `#C0`); **ALASM** lines start 24 bytes
after the signature, `[length][length − 1 bytes]` (0 ends), `#FF` skipped, bytes `< #10` = space runs, `#10` = CP866
text to the line end.

### 2.4 Symbol files of current tools

| Tool | Link | Licence | Facts (from its documentation or code) |
|---|---|---|---|
| sjasmplus | [documentation](https://z00m128.github.io/sjasmplus/documentation.html) | BSD-3 | `--sym`: `name: equ 0x0000XXXX`; `--exp`: EXPORT output; `LABELSLIST` / `--labels`: the Unreal form `NN:AAAA name` (page + `0000-3FFF`, or `0000-FFFF` for virtual labels); `CSPECTMAP`: `ADDR16 LONGADDR TYPE name`, types `00` label, `01` EQU, `02` DEFL, `03` ROM, `04` STRUCT; **SLD v1**: first line `\|SLD.data.version\|1`, then 8 fields `srcfile\|line[:col[:end]]\|deffile\|defline\|page\|value\|type\|data`, types `T L Z K` (`F`, `D` deprecated aliases of `L`); for `L`, data = `module,main,local,traits...` |
| DeZog | [repo](https://github.com/maziac/DeZog) | MIT | `src/labels/`: parsers for sjasmplus SLD, Savannah z80asm, z88dk (v1 / v2 `.lis`), a reverse-engineering list; z88dk `.map` line pattern `^(\w*)\b\s*=\s*\$([0-9a-f]+)` |
| RetroGhidra | [repo](https://github.com/hippietrail/RetroGhidra) | none stated | ZX loaders for Ghidra (no symbol import seen) |
| ZEsarUX | — | GPL | its changelog mentions "Symbol Table loading"; the format was not found in the sources: **unconfirmed** |
| Fuse, MAME, VICE, CSpect (beyond its map), pasmo, ZXMAK2, EmuZWin, SpecEmu, Es.pectrum | — | — | **not checked**; the catalog rows that rely on them stay **verify** |

## 3. Comparison

### 3.1 Which reference serves which codec

| Codec | Best reference | Second | Gaps to close by research |
|---|---|---|---|
| `tasm` | P1 (3.x and 4.x tables, framing, catalog hints) | L1 (the owner's 4.x table), L5 test data | 3.x vs 4.x token differences confirmed on L5 and on files TASM saves in the emulator; encode (none of them does) |
| `alasm` (source) | P1 | L8 (our script), P2 (conflicting `#96` / `#9F`) | version differences 4.2 … 5.09 (P5's sample, the collection's ALASM releases); encode |
| `alasm` (live table) | P7 (layout) | — | real paging instead of P7's fixed pages; tables above 128K; the meaning of the two flag bits |
| `xas` (live table) | P7 | — | the source file format (P4 only, binary): research in the emulator |
| `xas` (source) | — (P4 binary, P6) | — | full research |
| `storm` | P1 | — | encode; version differences |
| `zxasm` | P3 (written spec) | P1 (catalog hints) | verify the spec on real files |
| `masm`, `gens`, `zeus`, `sts` | — | — | full research (R1-R5 of [source-formats.md](source-formats.md) §3) |
| symbol codecs `sjasmplus-*`, `cspect-map`, `unreal-l` | sjasmplus documentation | DeZog, P8 | golden files from sjasmplus itself |
| symbol codec `z88dk-map` | DeZog's pattern | — | golden files from z88dk |

### 3.2 The implementations side by side

| | L1 TASM (owner) | L8 ALASM (this repo) | P1 ZX-M8XXX | P2 H2ASM | P7 Unreal 0.37.1 |
|---|---|---|---|---|---|
| Formats | TASM 4 source | ALASM source | ALASM, TASM 3 / 4, ADS, STORM sources | ALASM source | ALASM, XAS label tables in RAM |
| Decode / encode | decode | decode | decode | decode | decode |
| Labels as symbols | no (text) | no (text) | no (text) | no (text) | **yes** (name + value) |
| Pages | — | — | — | — | fixed 5 / 2 / 0 by address bits |
| Version detection | no | no | TASM 3 / 4 by catalog | no | XAS bank by marker bytes |
| Tests | none | used on one demo disk | yes | none | none |
| Licence | owner | this repo | none stated | none stated | Unreal |

**Conclusion.** No existing tool does what the design needs: none **encodes** (D-1), none returns **symbols** from a
source (only text), and the one that reads label tables binds them to fixed pages. The references settle most of the
TASM, ALASM, STORM and ZX-ASM layouts and the ALASM / XAS live tables; XAS, MASM, GENS, Zeus and STS sources still
need research in the emulator. Nothing is copied (D-2): every codec is written from its research document and
tested against files the real assembler produced.
