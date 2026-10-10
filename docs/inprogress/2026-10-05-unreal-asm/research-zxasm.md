# unreal-asm: ZX-ASM / ZAsm source format research, every version (phase A4)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Codec** | `zxasm`, versions `2` (2.4-2.6), `3.0` (3.0, 3.01, 3.10), `lite` (Lite 1.07), `3.15` (3.15 … 4.20) (`core/src/3rdparty/unreal-asm/src/codecs/zxasm/`) |
| **Programs** | every release on [zxart](https://zxart.ee/prod/155635) and vtrd ([2.4](https://vtrd.in/system/ZXASM2_4.zip), [2.6](https://vtrd.in/system/ZASM2_6.zip), [3.10](https://zxart.ee/releasefile/id:249317/ZASM_310.ZIP), [3.15](https://zxart.ee/releasefile/id:249318/ZASM315.zip), [3.3 Final](https://vtrd.in/system/Z33_F9.zip), [4.20](https://vtrd.in/system/ZASM4_20.zip)), the 2.5 and 3.0 disks from the author's [zx-pk.ru thread](https://zx-pk.ru/threads/29356-zx-turbo-assembler.html); keyword tables read from each binary |
| **Format description** | `za_format.txt` in [ZAsm View 1.1](https://vtrd.in/pcutilz/ZASMVIEW.zip) (D. Mikhalchenkov, 2014) and the 3.3 ReadMe; checked against the files (za_format.txt states the keyword flags wrongly, §3) |
| **Corpus** | 372 distinct sources: the program disks of every version and 20 third-party releases found by scanning all zxart releases (Info Guide #10, Excess de Luxe Paint, Turbo Disk Utility, Telephone Number, The Story Writer …) |
| **Result** | all 372 decode and encode back **byte for byte**; the editor rules (§4) write 99.86 % of the lines exactly as stored (99.97 % of the `a` / `C` / `z` sources); the version from the catalog agrees with the bytes |

## 1. Example first

ZX-ASM 3.0 stores `Label  ld (hl),a:inc hl` as

```
4C 61 62 65 6C | 06 82 | 04 20 | 02 7D | 2C | 02 7E | 3A | 04 2F | 02 8C
"Label"          2 blanks "ld "   "(hl)"   ","  "a"     ":"  "inc "   "hl"
```

The label is text, `ld` is keyword `#20` with "one blank after" (`04`), `(hl)` and `a` are keywords too, the colon is
text. ZX-ASM 2.x stores the same line as text with the blank run only.

## 2. Versions

| Version | Who, year | Saved sources (TR-DOS) | Keywords in the file |
|---|---|---|---|
| 2.4, 2.5, 2.6 | Oleg Hohlov, Kharkiv, 1994-95 (2.6: Rubts0FF 2018) | type `C`, start #A135-#A1DF (2.4, 2.5), #2020 (2.6) | none: text with blank runs |
| 3.0, 3.01, 3.10 | Konstantin Afendikov, Vladimir Rubtsov, Donetsk, 1996-98 | 3.0: `C` at 35151; 3.01: type `z`, extension "as"; 3.10: type `a`, "sm" | 166 (`#20`-`#C5`) |
| Lite 1.07 | Rubts0FF | `a` "sm" | 170 (+ project public enda dbw) |
| 3.15 … 4.20 | Rubts0FF, 2017-2026 | `a` "sm" | 173 (+ repl loadobj chd) |

ZX ASM 1.x was not found anywhere.

## 3. The file

No header, no end marker: the editor's text buffer as is (catalog length = text length).

| Bytes | Meaning |
|---|---|
| `#0D` | end of a line (never `#0A`) |
| `#06 #80+n` | n blanks (n ≤ 127), every version |
| `p, #20+k` (3.0 and later) | keyword k; `p - 2`: bit 0 = shown in capitals, bit 1 = one blank after it (za_format.txt reads the bits off `p` itself, which is wrong for `#04` / `#05`) |
| `#10`, `#11`, `#18`-`#1F` | control codes inside strings (colours, arrows), kept as U+F700 + byte |
| other bytes | CP866 text |

ZAsm 3.10 and later save every text with a TR-DOS type letter and the next two characters of its extension in the
catalog's start field: `ovlib.asm` is type `a` with start "sm", `A315.lbl` type `l` with "bl", `About.txt` type `t`
with "xt". Its texts therefore come with any type; the detector takes a start made of two such characters as a ZAsm
start (phase A6).

2.6 and 3.2x and later write an optional first line `;*a,b,c,…` (the editor state); it is an ordinary line.

### 3.1 The settings file (type `s`)

ZAsm 3.15 and later keep their setup in a type `s` file at start `#7465` (`ZAsm3.15 s`, `z33.02 s`, `z33.51 s`, ...),
written by SaveSet in the setup overlay (source `samples/text/C33_05__setup.a.txt` of the collection, lines 514-528):

| Offset | Meaning |
|---|---|
| 0 | the version (`"3"` in 3.3.02) |
| 1-2 | the length of the block that follows (`FLAGS - SystDisk`, `#0078` in 3.3.02) |
| 3 | **SystDisk**: the system drive, ASCII `A`-`E` |
| 4 | **OverDisk**: the overlays drive |
| 5... | the rest of the RAM block from SystDisk to FLAGS (the default extension `asm`, the monitor `sts6.2 exe`, the font name ...), then W_Size and the windows |

In RAM (3.3.02 `ovldef.a`): SystDisk `#8457`, OverDisk `#8458`, CurDrive `#84EF`, DefDrive `#84F0`; 3.3.51
`#844B` / `#844C` / `#84E2` / `#84E3`. 3.15 has the drives at offset `#24` (an older layout). Shipped values: 3.15,
3.3.02, 3.3.51, x64 `DD`; ZXTA34X `AA`; 3.2x lite 1 `A`. The `D` default is the author's own set-up (Shalaev's DOS
emulator, overlays once on another disk or a RAM disk: his ReadMe files and zx-pk.ru thread 29356); its effect in the
emulator is in [zxasm.md](../../../.recipe/assemblers/zxasm.md#start-and-the-default-drive).

## 4. The editor's rules (canonical tokenizer)

Derived on the corpus; each rule raised the exact-line rate:

1. **Label field**: a name in column 0 (letters, Cyrillic included, digits, `_ . ? @ $`) is text.
2. **Blanks**: two or more become `#06` runs, one stays a blank, everywhere (strings and comments too). A keyword
   followed by blanks takes one of them as its "blank after" flag.
3. **Keywords** (3.0 and later): a word matching the version's table, in any case (shown in capitals when its first
   letter is a capital), first match in table order (`C` is the register `c`, not the condition). Not after a
   letter, digit, `_ . ? @ $`, `(` or `~`; not followed by a letter, digit or `_`. Keywords starting with `(`
   (`(hl)`, `(ix`, `(c)`) match at the parenthesis. Comments are tokenized like the rest.
4. **Strings** (`"…"` or `'…'`) are text; in 3.0 and later the blank right after the opening quote stays a blank.
   An apostrophe right after a keyword belongs to it (`AF'`, `BC'`).
5. Text that cannot be held (outside CP866) is an error naming the line and the column.

The remaining 0.14 % are mostly program documentation (`t` files) and lines produced by the conversion tools
(`gens4>za`, `tasm3>za`) or older editors, which tokenize comments differently; their bytes are kept exactly.

## 5. Version detection

The catalog narrows the versions (§2); within them, and without a catalog, the versions whose editor rules write the
most lines back unchanged, with no byte they give no meaning, are reported (`DecodeResult::subversions`) and the
newest is chosen. Keywords `#C6`-`#C9` mean Lite or 3.15+, `#CA`-`#CC` mean 3.15+, any keyword rules out 2.x, and
2.x's blank handling right after a quote differs from 3.x.

## 6. Test data (`testdata/zxasm/`)

| File | Version(s) | Why |
|---|---|---|
| `ZXASM2_4__a2.4_p.$C` | 2 | 2.4, text only |
| `ZASM2_6__a2.6_p.$C` | 2 | 2.6, the `;*` editor line |
| `IG_10D1__ACEpd55e.$C` | 3.0 | type `C` at 35151, [Info Guide #10](https://zxart.ee/releasefile/id:426650/INFERN10.ZIP) |
| `EPV_11__lx-800.$z` | 3.0 | 3.01, type `z`, [EPV 1.1](https://zxart.ee/releasefile/id:250322/EPV_11.ZIP) |
| `ZASM_310__prn_des.$a` | 3.0, lite, 3.15 | from the 3.10 disk; no newer keyword |
| `Z33_F9__AboutMe.$a` | 3.0, lite, 3.15 | 3.3 Final |
| `C33_F9__ddoc2_p.$a` | lite, 3.15 | `#C6`-`#C9` keywords |
| `ZASM315__fcnv1.$a` | 3.15 | `#CA`-`#CC` keywords |
| `Z4_20__ovlib.$a` | 3.15 | 4.20; one line the rules write differently |

### 6.1 Checked in the emulator

`fcnv1` with four lines added (`Test   ld (hl),a:inc hl:djnz Test`, `       LD   A,(IX+5) ;комментарий`,
`       db "  строка  ",13`, `       chd "b:test"`), written entirely by the codec's rules (no kept bytes) with
`zxasm encode --version 3.15` (type `a`, extension "sm"), opens in ZAsm 3.15 (File > Load) and shows the text line for
line: keyword case, blank runs, the blank after the opening quote, Russian text, the 3.15-only `CHD`. Own emulator
instance, TTD recorded; screenshots kept with the research materials.

## 7. Open items

| Item | Note |
|---|---|
| ZX ASM 1.x, the 3.01 program | not found |
| 3.0's own table | packed in its loader; the 3.0 files decode with the 3.10 indices, which the 3.0 ReadMe confirms |
