# unreal-asm: ALASM source format research, every version (phase A3)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Codec** | `alasm`, versions `3.8`, `4.2`, `4.42`, `4.5`, `4.44`, `5.07` (= 5.07-5.09) (`core/src/3rdparty/unreal-asm/src/codecs/alasm/`) |
| **Primary source** | ALASM 5.09's own sources, `alTOKENS.H` (`cnv2str` = text → line, `str2txt` = line → text, `mnemtkn` / `regstkn` tables), on the disk of [ALASM509_STS75.rar](http://alonecoder.nedopc.com/zx/ALASM509_STS75.rar) from [Alone Coder's page](http://alonecoder.nedopc.com/zx/) (prior-art P9) |
| **Other versions** | the keyword tables read from each release's binary: [ALASM 3.8c](https://zxart.ee/releasefile/id:406950/ALASM38c.zip), [4.2](https://zxart.ee/releasefile/id:574325/ALM.scl), [4.42](https://zxart.ee/releasefile/id:408359/Alasm442.SCL), [4.44](https://zxart.ee/releasefile/id:249270/ALASM444.ZIP), [4.5](https://zxart.ee/releasefile/id:589336/alasm45.scl), [5.07](https://zxart.ee/releasefile/id:155270/ALASMV5.07(AloneCoder).trd.zip), [5.08](http://alonecoder.nedopc.com/zx/ALASM508.rar), [5.09](https://zxart.ee/releasefile/id:249271/ALASM509.zip), [5.09 ZX Evolution](https://zxart.ee/releasefile/id:424011/ALSM509E.zip) (release list: [zxart](https://zxart.ee/prod/155267)) |
| **Corpus** | every type-`H` file on those disks, plus [PT372SRC.rar](http://alonecoder.nedopc.com/zx/PT372SRC.rar), [ACE102SRC.rar](http://alonecoder.nedopc.com/zx/ACE102SRC.rar) and the repository's `testdata/machines/pentagon1024sl/TheLink.trd`: **429 files, 205 959 lines** |
| **Result** | all 429 files decode and encode back **byte for byte**; the canonical tokenizer alone writes 99.6 % of the lines exactly as the editor stored them (§5) |

## 1. Example first

ALASM shows `        LD A,1` (the mnemonic in column 8) and stores it as five bytes:

```
05 | D4 | F5 | 2C 31
len  LD   A    ",1"
```

No blanks are stored before `LD`: the editor pads every keyword that would land left of column 8 to column 8, unless
blanks or a `#FF` byte come right before it. After the first keyword (the mnemonic) it adds one blank. `A` is `#F5`
from the operand table, which is used once a keyword has been seen on the line. Codes are looked up in two tables by
position: `#9F` is `ELSE` as the first keyword and `(BC)` as an operand.

## 2. File

| Offset | Bytes | Content |
|---|---|---|
| `+#00` | 8 | file name, blank padded |
| `+#08` | 1 | `H` |
| `+#09` | 24 | zero |
| `+#21` | 2 | source length (bytes after the header) |
| `+#23` | 5 | editor state (current line pointer `#C0xx`, cursor); `40 C0 00 00 00` = at the first line |
| `+#28` | 8 | signature `F3 76 C7 DD FD ED B0 D9` |
| `+#30` | 16 | zero |
| `+#40` | length | lines |

The TR-DOS catalog says start 0, length = 64 + source length. The layout is the same in every version from 3.8 to
5.09 (checked on all 429 files); there is **no version field**.

## 3. Lines and keywords

A line is `[n][n-1 bytes]`, `n` counting itself (`01` = an empty line). In a line (`str2txt`):

| Byte | Meaning |
|---|---|
| `#01`-`#0F` | that many blanks (a longer gap is several runs) |
| `#10` | the rest of the line is text (written before the first Russian letter outside a string or comment) |
| `"` | text up to the closing `"` |
| `;` | the rest of the line is a comment |
| `#20`-`#7F` | ASCII |
| `#80`-`#FA` | a keyword: the mnemonic table while no keyword was seen on the line, the operand table after |
| `#FF` | no text: before a keyword, "do not pad to column 8"; at the end, padding (below) |
| text bytes `≥ #80` | CP866 (Russian) |

`cnv2str` (text → line) matches keywords **case-sensitively** (only upper case is a keyword), in table order, at the
start of a word. A mnemonic must be followed by the end, `;` or a blank (which it absorbs); an operand keyword by
`;` or a character below `0`. `DD` (`DEFM` in 3.8-4.5) stops tokenizing its operand so hex digits like `BC` are not
split into registers. Blank runs: every blank gap outside strings and comments becomes runs of at most 15.

**Backward walk padding.** The editor moves to the previous line by walking back from a line's end to the first byte
whose value equals its distance from the end. When a byte of the line would stop that walk early (a trailing `#01`
run, for instance), `cend` appends `#FF` until the walk reaches the length byte. Example: `LABEL ` (one trailing blank)
is `08 'L' 'A' 'B' 'E' 'L' 01 FF`.

### Keyword tables by version

The operand table is the same in every version. The mnemonic codes are shared; versions differ in which codes exist
and how a few are spelled:

| Code | 3.8 | 4.2 | 4.42 | 4.5 | 4.44 | 5.07-5.09 |
|---|---|---|---|---|---|---|
| `#83` | ERASE | LOCAL | LOCAL | LOCAL | LOCAL | LOCAL |
| `#96` | DEFM (string) | DEFM | DEFM | DEFM | DD (hex) | DD (hex) |
| `#9D` | STOP | ENDL | ENDL | ENDL | ENDL | ENDL |
| `#9F` | — | ELSE | ELSE | ELSE | ELSE | ELSE |
| `#A0` | — | — | DISPLAY | DISPLAY | DISPLAY | DISPLAY |
| `#A1` | — | — | — | — | EXA | EXA |
| `#A2`-`#A4` | — | DB DW DS | DB DW DS | DB DW DS | DB DW DS | DB DW DS |
| `#D0` | — | IFN | IFN | IFN | IFN | IFN |
| `#D1` | — | — | — | REPEAT | REPEAT | REPEAT |
| `#D2` | — | — | — | UNTIL | UNTIL | UNTIL0 |
| `#D3` | — | IF | IF | IF | IF | IF0 |
| `#E0` | — | ENDIF | ENDIF | ENDIF | ENDIF | ENDIF |
| `#E1`-`#E6` | — | — | — | — | — | EXD JNZ JZ JNC JC RUN |

3.8 stores its table grouped by word length (`alasm4x8.C`), the others as `DC` strings with gap bytes; the codes come
out the same. ALASM 2.8 (only its help survives) and 5.00-5.06 (no release found) are not covered.

The same code can mean different things: 3.8's `DEFM "text"` is a string, 5.x's `DD 1D4F` hex bytes (help of each
version). The codec therefore never maps one spelling to another: converting to another version re-tokenizes the text
with the target's table, a keyword the target does not have stays text, and the encoder warns
(`'DEFM' is not a keyword of ALASM 5.07: written as text`). Rewriting the meaning is the dialect plugins' job (A5).

## 4. Version detection

The file has no version field, so the version comes from evidence:

1. **Re-tokenizing.** For each version, decode every line and encode it again with that version's `cnv2str`. A line
   comes back unchanged only if every keyword byte exists in that version **and** no plain-text word is a keyword the
   version would have tokenized (`DD` left as text means "typed in a version without `DD`").
2. **The `#96` operand.** A string after `#96` counts for the `DEFM` versions, hex digits for the `DD` versions.

The versions with the most evidence are all reported (`DecodeResult::subversions`); the newest of them is chosen.
When those versions would spell the file differently, an Info diagnostic names them and one line that differs.

Corpus: no file on a 5.x disk excludes 5.07; every file on a 4.x disk includes its own version except `AL444nfo` on
the 4.44 disk, which keeps `DD` as text (typed in 4.5 or earlier). Files without version-specific keywords (drivers,
small examples) are consistent with every version from 3.8 on, which is the honest answer.

| Consistent with | Files (of 429) |
|---|---|
| 5.07 only (uses `EXA`, `JNZ`, `UNTIL0` …) | 174 |
| every version, 3.8 … 5.07 (nothing version-specific) | 101 |
| 4.42, 4.5, 4.44, 5.07 | 79 |
| 4.2 … 5.07 | 51 |
| 4.44, 5.07 | 17 |
| 4.5, 4.44, 5.07 | 5 |
| 4.2, 4.42, 4.5 (`DEFM "string"`) | 1 |
| 4.5 only (`DD` as text) | 1 |

## 5. Lines the canonical tokenizer writes differently (0.4 %)

Every one is still written back exactly from the kept bytes; these are what an edit of such a line would change:

| Case | Example | Why |
|---|---|---|
| Blanks before a column-8 keyword kept | `DIS_GO`, run 2, `EXX` | written by an editor older than 5.09's `c2s7` rule, or imported |
| Trailing blank runs | `FNT     EQU     #8800` + 15 blanks | text imports (`alImpExp`) do not trim |
| Switch letter tokenized | `DISPLAY /L,"..."` with `L` = `#F4` | an older editor tokenized after `/`; 5.09 does not |
| `DD` as text | `DD #...` in `AL444nfo` | typed in a version without `DD` |

## 6. Test data (`testdata/alasm/`)

Ten files, each chosen for a version range or a case of §5: `ZADACHA` (3.8 disk), `SCR4MAKE` (text import),
`128KDRV`, `fibo`, `SNAKE`, `2Kolonki`, `RECPIC`, `BUILD+` (`DISPLAY /D`), `AL442nfo` (`DEFM "string"`), `AL444nfo`
(`DD` as text). Expected text = the codec's output, read against ALASM's own display rules above.

## 6.1 Checked in the emulator

`AL442nfo` (ALASM 4.x, `DEFM`) converted by the codec to 5.07 and added to the ALASM 5.09 disk opens in ALASM 5.09
(`W`, the file from the list, `E`): the editor shows the decoded text line for line (CP866 Russian, keyword
spellings, indents; the pseudographics header is drawn by ALASM's font). Own emulator instance, TTD recorded; the
screenshot is kept with the research materials. ALASM, like TASM, starts with the keyboard in inverted case: a file
name typed in capitals arrives in lower case.

## 7. Open items

| Item | Note |
|---|---|
| ALASM 2.x, 5.00-5.06 | no binaries found; add when found |
| `#10` placement | the canonical encoder writes `#10` right before the first Russian letter; files with `#10` earlier keep their bytes |
