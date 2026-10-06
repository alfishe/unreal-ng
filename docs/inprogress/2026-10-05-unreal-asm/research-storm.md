# unreal-asm: STORM source format research (phase A4)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Codec** | `storm`, versions `1.0` (1.0beta) and `1.3` (1.2, 1.3, 1.3+, 1.3i) (`core/src/3rdparty/unreal-asm/src/codecs/storm/`) |
| **Authors** | LD (Dmitry Lomov) and Dark (Vitaliy Vidmirov) of X-Trade, 1997; 1.3+ by Cyberex/RS (1999), 1.3i by Pastor/Triumph (2001) |
| **Programs** | [STORM 1.3](https://zxart.ee/releasefile/id:155558/StormAssemblerV1.3.trd.zip) ([vtrd](https://vtrd.in/system/STORM_13.ZIP)), [1.3+](https://vtrd.in/system/STRM13CR.ZIP), [1.3i](https://vtrd.in/system/STORM13I.ZIP); 1.0beta and STORM 1.3's **own source** (`STORM1_3.ZIP`) from the KLUG BBS archive ([klug_bbs.7z](https://yadi.sk/d/N_p56RIHWU15Gw)); the binaries are packed, their tables were read after running the unpacker in a Z80 emulation |
| **References** | the decoder in [ZX-M8XXX asm-detok.js](https://github.com/Bedazzle/ZX-M8XXX/blob/main/core/asm-detok.js) (a port of D. Kozlov's FAR plugin) and xLook ([XFAR.zip](https://vtrd.in/pcutilz/XFAR.zip)); the keyword table `TBTK` and operator table `TBSIGN` in STORM's own source |
| **Corpus** | 42 real sources: STORM's own (ASM, DPC, ED, MAIN, XED), a Dark/X-Trade demo source on the ZX Format #7 disk, Deja Vu #4, Wallpaper #7, Global Commander GS, Adventurer #15, the Fighter Ace demo (25 files), an emulator test |
| **Result** | all 42 decode and encode back **byte for byte**; STORM's rules (§4) write 23 492 of 23 497 lines exactly as stored (the other five differ only in a separator byte, §5); the text matches the reference decoder except where it is better (a leading empty line, Russian strings in CP866) |

## 1. Example first

`DLNA    CALL DLN:RET NZ` is stored as twelve bytes, the last one being the line's length:

```
C3 57 19 8C | 51 | C3 17 99 | 2A | 52 | 78 | 0B
"DLNA" def    CALL "DLN"      ":"  RET  NZ   length 11
```

A label is packed: the first byte is `#C0+` (`#C3` = `D`), then one 6-bit code per character, the second byte marks a
definition (bit 6), the last ends the label (bit 7). `LD HL,#5FFF` is just `3E 8D FF 5F`: the byte of `HL` implies
`LD`, `#8D` says "a hex word, the last element", then the word.

## 2. The file

- No header and no end marker: the text area from the catalog's start (#C00B; 1.0beta has no name field, #C003) to the
  end of the text. In memory (STORM's source `MAIN`): #C000 the end pointer, #C002 the name, #C00A an #FF sentinel.
- **Lines are `[body][length]`** and are walked **backwards** from the end of the file; every file reaches its first
  byte exactly. An empty line is a single `00` (a file may start with one).
- Lines are at most 40 characters on screen; a body is at most 63 bytes.

## 3. The body

| Bytes | Meaning |
|---|---|
| `#2F` text `00` | a comment (`;`); `#01`-`#1F` in it = that many blanks (a single blank too), CP866 |
| `#C0`-`#DB` with bit 6 on the next byte | a label definition at the line start |
| `#06` | the command starts in column 0 (shown as `_`, used by 40-character lines) |
| `#07 n` | `.n` in the label field: the line is assembled n times (0 = 256; STORM 1.3's help) |
| `#09`-`#2E`, `#41`-`#43`, `#4A`-`#6E` | commands (`ORG` … `RETN`, `DEFB DEFW DEFS`, `LD` … `DS`); `#49 n` = `INCL` … `ENDM` |
| `#2A`-`#2E` after operands | `:` with five spacings (`:`, ` :`, ` : `, ` :  `, `  : `) |
| `#30`-`#3F`, `#44`-`#48`, `#6F`-`#7F` | registers and conditions |
| `#80`-`#BF` | an expression: descriptor `#80` + brackets `#20` + last `#08` + the number's form (bits 0-2: 1 hex byte, 2 decimal byte, 3 label / `$` / `=n`, 4 character, 5 hex word, 6 decimal word, 7 binary, 0 a sub-expression); bit 4 = a leading minus, or with brackets `(IX` / `(IY` (bit 3) with a short offset form |
| operator bytes | `(operator << 4)` + the next number's form + last `#08`; postfix operators `#F0`+n |
| `#C0`-`#DB` | a label reference |
| `#DC` text `00` | a string |
| `#DD`-`#E6` | a single digit 0-9 |
| `#E7`-`#FF` | `$-12` … `$+12` |

**Implied commands**: when the first operand says which command it is, the command byte is left out:

| First operand | Command |
|---|---|
| a register `#30`-`#3F`, or `IX IY (BC) (DE) I R` | `LD` |
| a bracketed expression | `LD` |
| `AF`, `(SP)` | `EX` |
| `(C)` | `OUT` |
| `NZ Z NC`, a label, an unbracketed expression, a digit, `$±n` | `JR` |
| `PO PE P M` | `JP` (STORM 1.3's binary has `JP` in the 15 places its own source has these, and its editor shows `JP`; the reference decoders show `CALL`) |

The corpus shows STORM always uses the implied form when one exists, with one quirk: `C` is written as the register
`#31` everywhere except as `JR`'s first operand, where it is the condition `#7B`, and `JR C,…` is still written with
an explicit `JR` byte (the implied range is only `NZ Z NC`).

## 4. STORM's rules (canonical encoder)

The text has STORM's fixed layout (a label then the command in column 8, one blank after the command, operands
separated by commas, `LD HL,1,DE,2` for several loads), so an edited line is parsed back into the structure above:
numbers keep the form they are written in (`#05` a hex byte, `#0005` a hex word, `5` a digit as a whole operand or a
decimal byte inside an expression, `256` a decimal word, `%…` binary, `"ab"` a character constant, a longer quoted text
a string), whole operands in brackets are bracketed expressions, other parentheses sub-expressions (the inner stream
begins with an operator when the byte that opens it has bit 4 set: `(+C2-KEYTXT)` after a minus).

## 5. What the corpus shows

| | Lines | STORM's rules exact |
|---|---|---|
| 42 files | 23 497 | 23 492 |

The five others are in STORM's own `DPC`: their `:` separator byte has bit 6 set (`#6A`, `#6C`); the decoder shows the
same text, so only the kept bytes reproduce them.

## 6. Test data (`testdata/storm/`)

| File | Why |
|---|---|
| `EMULTEST__EMUL` | starts with an empty line (KLUG BBS archive) |
| `ZX-FOR72__PLASM` | Dark / X-Trade demo source, comments with blank runs ([ZX Format #7](https://zxart.ee/releasefile/id:429289/ZX-FORM7.ZIP)) |
| `DEJAVU4__LDISCROL` | [Deja Vu #4](https://zxart.ee/releasefile/id:425744/DEJAVU4.ZIP) |
| `GC131IGS__HMEM` | [Global Commander 1.31 GS](https://zxart.ee/releasefile/id:587330/GC131IGS.zip) |
| `STORM1_3__MAIN` | STORM's own source: sub-expressions, postfix operators |
| `STORM1_3__DPC` | STORM's own source: the keyword table `TBTK`; the five separator bytes of §5 |

### 6.1 Checked in the emulator

`EMUL` with five lines added (`TEST    LD A,(IX+13),B,5,DE,-33`, `JR NZ,$-3:JR C,$+2:CP C`, `TBUF    DS (TEST+1)*2`, a
Russian comment, `DB "ПРИВЕТ",#0D,0`), written entirely by the codec's rules with `zxasm encode` (type C, start
#C00B), loads in STORM 1.3 (BREAK, L, the name) and shows the text line for line, the leading empty line included
(STORM shows `EX AF,AF` for the token pair `AF AF`, as the codec does). Own emulator instance, TTD recorded;
screenshots kept with the research materials.

## 7. Open items

| Item | Note |
|---|---|
| A file saved by 1.0beta (start #C003) | none found; the codec reads it as version `1.0` from the catalog |
| Exported plain-text files (Spectrum Expert) | text with CR lines: the `text` codec reads them |
