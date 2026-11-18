# unreal-asm: rasm data helpers (floats, INCBIN, WAV, headers, BUILDZX, strings)

The pure data part of the rasm engine: the functions of `rasm.c` v3.3.3 that turn values and files into the bytes
rasm writes, ported byte for byte into
[`rasmdata.h`](../../../core/src/3rdparty/unreal-asm/src/dialects/rasm/engine/rasmdata.h) /
[`rasmdata.cpp`](../../../core/src/3rdparty/unreal-asm/src/dialects/rasm/engine/rasmdata.cpp) (namespace
`unrealasm::rasm`). No engine state: the engine (labels, banks, passes, the output) calls them. The language
specification around them is [`research-rasm.md`](research-rasm.md); the owner table of the engine files is
[`rasm-engine.md`](rasm-engine.md).

Verification: [`rasmdata_test.cpp`](../../../core/src/3rdparty/unreal-asm/tests/rasmdata_test.cpp) runs the
fixtures of [`testdata/rasm/data`](../../../core/src/3rdparty/unreal-asm/testdata/rasm/data), which
[`rasmdatacheck.py`](../../../tools/verification/unreal-asm/checks/rasmdatacheck.py) writes from the rasm binary
(results in §8).

Line numbers below are `rasm.c` v3.3.3.

## 0. Conventions

- **Messages** are rasm's texts without the `[file:line] ` prefix, the `Warning: ` word and the newline. rasm goes on
  after an error and sometimes reports a second one for the same directive (`incbin 'f',0,70000`: "invalid size" and
  "size is greater than filesize"); the helpers stop at the first. An error fails the assembly, so the bytes after it
  never matter.
- **Bit exactness.** rasm computes in IEEE doubles. The helpers do the same operations in the same order, call no libm
  transcendental (powers of two are `std::ldexp`, exact), and `rasmdata.cpp` is compiled with `-ffp-contract=off`
  (`/fp:precise` on MSVC, set in the library's `CMakeLists.txt`), so the compiler fuses nothing by itself.
- **Fused multiply-add in the reference build.** The rasm binary the fixtures come from (arm64 macOS, clang -O2)
  contracts some `a*b+c` into `fmadd` (one rounding instead of two). Disassembly of the WAV code shows it on:
  - the sample scaling `floor(acc/n*normalize+0.5)`;
  - the 32-bit float sample `floor((f+1.0)*127.5+0.5)`;
  - the DMA band-pass filter, which compiles to
    `fma(oos, 0.960513, fma(os, -0.039487, fma(e, -0.980257, ooe*0.980257)))`.

  The port writes exactly those as `std::fma`, which is correctly rounded on every platform, so the results equal the
  reference binary everywhere. A rasm built for x86-64 without FMA would round twice and can differ by one level on a
  sample that lands on a rounding edge. The fixtures follow the reference binary.
- **Build-dependent conversions.** `double -> unsigned long` and `double -> int` out of range are undefined in C. The
  reference build saturates: NaN and negatives give 0 unsigned, values from 2^64 up give all ones. The port does the
  same explicitly (`ToU64`, `ToIntSaturated`). Plain `char` is signed on the reference build. That matters for the
  16/24-bit WAV samples and for `%X` in the quote error (`FFFFFFC3`).
- **Reads outside rasm's buffers** (rasm reads garbage) become 0 bytes or an error, as listed in each section.

## 1. Floats: DEFR / DR, DEFF / DF, DF24

| Directive | Function | Format |
|-----------|----------|--------|
| `DEFR`, `DR` | `MakeAmsdosReal` (`__internal_MakeAmsdosREAL`) | Locomotive BASIC: 4 mantissa bytes little endian, bit 7 of byte 3 = sign (replaces the implied 1), byte 4 = exponent + 128 |
| `DEFF`, `DF` | `MakeMicrosoftReal` (`__internal_MakeRosoftREAL`) | the same 5 bytes in reverse order: exponent first, mantissa most significant byte first |
| `DF24` | `MakeFloat24` | z80float f24: `(float)v`, then byte 0 = `(exponent field - 64) | sign`, byte 1 = mantissa bits 7..14, byte 2 = bits 15..22 |

The struct variants (`DEFR` in a `STRUCT`) use the same encoders. No option changes the formats. `/` in an expression
is a float division (`defr 1/3` = `AB AA AA 2A 7F`).

The algorithm, shared by both 40-bit formats:

```
neg = v < 0; v = |v|
deci = (u64)v                                  saturating
if deci != 0:
    exp = number of bits of (deci & 0xFFFFFFFF) loop never ends when the low 32 bits are 0
    m = (u64)(ldexp(v, 32 - exp) + 0.5)
elif v == 0: exp = -128, m = 0
else:
    m = (u64)(v * 2^32 + 0.5)                  0 -> rasm loops forever
    exp = -(leading zero bits of m in 32)
    m = (u64)(ldexp(v, 32 - exp) + 0.5)
if m & 0xFF00000000: m = 0xFFFFFFFF            the rounding carried out
exp += 128; outside 0..255 -> error "Exponent overflow", exp = 128
```

Quirks the port keeps:

| Input | Effect |
|-------|--------|
| integer part with the low 32 bits 0 (`4294967296`, `pow(2,40)`) | rasm hangs: `RealStatus::Hangs`, the engine reports `kRealHangsMessage` |
| `0 < v < ~2^-33`, NaN | rasm hangs (`1/pow(2,34)`) |
| `4294967297` | only the low 32 bits count: `00 00 00 00 81` (= 1.0) |
| `pow(2,200)` | the u64 saturates: `FF FF FF 7F A0` |
| `pow(2,-33)` | top mantissa bit 0: `00 00 00 40 61` |
| DF24 below the float range (`-1/pow(2,140)`, a float denormal) | sign lost: `C0 04 00` |
| DF24 above the float range (`pow(2,200)` = inf) | `BF 00 00` |

The exponent overflow cannot be reached: a value large enough hangs or saturates first.

## 2. INCBIN

### 2.1 Directive and carrier

`INCBIN 'file'[,offset[,size[,offsetHigh]]][,options]`. Aliases with a cruncher after the transform: `INCL48`,
`INCL49`, `INCLZ4`, `INCZX7`, `INCEXO`, `INCAPU`, `INCZX0`, `INCZX0B`, `INCLZSA1`, `INCLZSA2`. `INCWAV` is `INCBIN`.
The preprocessor rewrites them all into an internal `HEXBIN #index` word, which is the only form of `HEXBIN`. There is
no import of hexadecimal text files: `HEXBIN` in a source is that internal carrier, and the port has no use for it.

rasm reloads the file on every execution (`datalen = -1`, delayed load), so the in-place transforms (REVERT, the
ISLICE line swap) do not carry from one execution to the next. `IncbinTransform` copies the file.

### 2.2 Parameter rules (`__HEXBIN`)

In order:

1. Integers: offset, size, then `offset += 65536 * third`. A fourth gives "INCBIN has unexpected integer parameter
   [%s]" (an engine parse error).
2. `REMAP` below 1: "INCBIN REMAP need a proper number of columns (>1)".
3. size > 65536: "INCBIN invalid size, must be lower than 65536".
4. More than one of ITILES, GTILES, VTILES (non 0), REMAP, REPACK: "INCBIN options ITLES, GTILES, VTILES, REPACK and
   REMAP are mutually exclusives" (sic). SLICE / ISLICE are not counted.
5. `SKIPHEADER`: applies when the file has an AMSDOS header (`HasAmsdosHeader`):
   - the file is longer than 128 bytes;
   - the checksum of bytes 0..66 is not 0 and equals `d[67] + 256*d[68]`;
   - `d[0] < 16` and `d[0x12] <= 2`;
   - `d[0x18..0x19] == d[0x40..0x41]`;
   - that length lies in `[len-128-512, len-128]`.

   Then offset += 128, or the warning "SKIPHEADER is useless with negative offset" when the offset is negative.
6. Empty file: size != 0 is the error "INCBIN cannot read %d byte%s as the file is empty", otherwise the warning
   "INCBIN of an empty file" and nothing more. The offset alone does not count (`incbin 'empty',5` only warns).
7. size < 0: `size += filesize`; below 1 the error "INCBIN negative size is greater or equal to filesize".
8. offset < 0: `offset += filesize`.
9. size == 0: `size = filesize - offset`.
10. size > filesize: "INCBIN size is greater than filesize (%d>%d)". size + offset > filesize: "INCBIN size+offset is
    greater than filesize (%d+%d>%d)".
11. `REVERT`: reverses `data[offset .. offset+size-1]` in place.

### 2.3 Transforms (first match wins)

| Option | Output |
|--------|--------|
| `SLICE,w,h` / `ISLICE,w,h` (+`MISALIGNED`) | the whole file, ignoring offset and size, as `w x h` tiles. Each 256-byte row holds `256/w` tiles side by side, and a group of `h` rows holds one row of tiles. The output is zero-filled to `(groups*h-1)*256 + w*tilesInLastGroup` bytes. ISLICE first swaps the lines of each 8-line group of the file in place, so the new lines 0..7 are the old 0,1,3,2,6,7,5,4. Without MISALIGNED the engine pads to the next 256-byte boundary and, if it padded, warns "INCBIN ISLICE was automatically aligned, use MISALIGNED to force in place" (`IncbinResult::alignTo256`) |
| `ITILES,w` / `GTILES,w` | 8-line tiles of width w taken from the selection, lines in gray order 0,1,3,2,6,7,5,4. ITILES also mirrors every other output line (positions 1, 3, 5, 7: zigzag) |
| `REPACK` | `out = d[i] | d[i] << 4` for every even i below size, from the start of the file. The offset is ignored and each byte is ORed with itself, not with its neighbour (rasm's code). An odd size warns "INCBIN REPACK is using an odd length" |
| `REMAP,c` | the selection as c columns, read column by column: `out = d[it + tx*c + offset]` |
| `VTILES,n` | the selection as lines of `size/n` bytes, read column by column |
| none | the selection |

Errors of the transforms:

- "INCBIN SLICE need a tile width in the range [1-256] and a proper height"
- "INCBIN SLICE need input data to be a multiple of tiles size"
- "INCBIN ISLICE need tile height to be 8 factor"
- "INCBIN SLICE outputdata will be too big (>64K)"
- "INCBIN ITILES/GTILES need a tile value in the range [1-256]"
- "INCBIN ITILES/GTILES cannot reorder tiles %d bytewidth with file of size %d"
- "INCBIN REMAP need a remap (column number) value in the range [1-32768]"
- "INCBIN REMAP cannot reorder %d columns%s with file of size %d" (rasm prints "columnss")
- "INCBIN VTILES need a value in the range [1-32768]"
- "INCBIN VTILES cannot reorder %d line%s with file of size %d"

A final size of 0 warns "wont try to crunch no data at all" and writes nothing (`incbin 'f',256` on a 256-byte
file). For the crunching aliases the engine reports "Cannot crunch [%s]" when the crunched length is 0 or at least
`selectedSize`.

### 2.4 Where rasm reads outside the file

rasm's copy loop for the plain case runs from offset to offset+size. When offset+size is still negative after rule 8
(`incbin 'f',-300,10` on a 256-byte file), rasm copies bytes from before its buffer. The same happens for a negative
offset with REVERT, ITILES / GTILES, REMAP or VTILES. The port returns the error "INCBIN offset before the start of
the file (rasm reads outside its buffer)" in those cases.

## 3. WAV import (`_AudioLoadSample`)

`INCBIN 'f',SMP[,amplification]`, `SM2[,a]`, `SM4[,a]`, `DMA[,options]`. Options of DMA:

- `DMA_CHANNEL_A`, `DMA_CHANNEL_B`, `DMA_CHANNEL_C`: PSG register 8, 9, 10 (C is the default);
- `DMA_INT`;
- `DMA_REPEAT,n`, with n in 1..4095, else "DMA_REPEAT value out of bounds (1-4095)";
- anything else: "Unrecognized DMA option [%s]".

The amplification is stored as a float and only SMP / SM2 / SM4 use it; DMA uses 1.0.

### 3.1 Header

- **Raw mode.** Without a 44-byte header beginning with `RIFF` / `RIFX`, the file is raw: mono 8-bit unsigned,
  every byte a sample.
- **Byte order.** `RIFX` makes the samples big endian; the header itself is still read little endian.
- **Checks, in order:**
  - "WAVE" at 8, else "WAV import - unsupported audio sample type (format must be 'WAVE')".
  - fmt size 16, else "WAV import - invalid wav chunk size (subchunk1 control)".
  - "fmt" at 12, else "... (subchunk1id must be 'fmt')".
- **The data chunk.** The chunks from offset 36 are walked to "data"; "WAV import - data subchunk not found" when a
  chunk's size reaches the file size.
- **Channels and format.** Channels below 1: "WAV import - invalid number of audio channel". wFormatTag not 1 or 3:
  "WAV import - invalid or unsupported wFormatTag (%04X)".
- **DMA frequency.** A rate more than 20 % away from 15600 Hz warns "WAV sample frequency (%dHz) is very different
  from 15KHz DMA frequency".
- **Bits per sample:**
  - 8: `byte - 128`;
  - 16 and 24: only the most significant byte, as a signed char;
  - 32: requires format 3 (float), else "WAV import - unsupported 32bits PCM". The float sample `f` becomes
    `floor((f+1)*127.5+0.5)`, which is not centred on 0: silence reads as +128, so the downmix saturates high.
  - Other widths: "WAV import - unsupported bits per sample (%d)".
- **Length.** samples = dataSize / channels / bytes. A data size + 44 above the file size is the error "WAV import -
  cannot read %d byte%s of audio whereas the file is %d bytes big!".
- **Quirk.** The samples are read from the `data` chunk id itself, not after its 8-byte header. The first 8 bytes'
  worth of samples are the letters "data" and the size, and the last 8 bytes of the audio are never read. The port
  keeps this, and the fixtures confirm it.

### 3.2 Conversion

Each sample is the sum over the channels (a double), then:

```
level = clamp(floor(fma(sum / channels, normalize, 0.5)) + 128, 0, 255)
```

- **SMP.** One byte per sample: `psgfine[level]`.
- **SM2.** Two samples per byte, high nibble first: `v = (v << 4) + psgfine[level]`. The loop runs
  `i < samples + 1` in steps of 2, so an odd count reads one sample past the end.
- **SM4.** Four samples per byte. Each adds `(v << 2) + ((psgtab[level]+1) >> 2) - 1`, or just `v << 2` when that
  level is 0; the loop runs `i < samples + 3` in steps of 4.
- **Reads past the data.** SM2 and SM4 read up to 3 samples past the data; rasm takes whatever follows. Where that is
  past the end of the file, the port reads 0. The generated WAVs keep a zero chunk after the data, so the fixtures
  stay defined.
- **DMA.**
  - The first sample gives `prev = psgtab[level]` and writes nothing.
  - Every next sample goes through the band-pass filter (static state in rasm: it carries over from one import to the
    next in the same assembly; `AudioFilterState` is the caller's):

    ```
    s = (float) fma(oldOldS, 0.960513, fma(oldS, -0.039487, fma(e, -0.980257, oldOldE * 0.980257)))
    level = clamp(floor((double)(s * normalizeF) + 0.5) + 128, 0, 255)      float product, then double
    value = dmatab[level]
    ```

  - Runs of an equal value are counted and written as `(prev, reg)` followed by pause words
    `(min(n,4095) & 255, 0x10 | (n >> 8 & 15))`, n -= 4096.
  - At the end, the value once more if no run is open.
  - The framing:
    - with DMA_REPEAT first `(n & 255, 0x20 | (n >> 8 & 15))`;
    - the samples;
    - with DMA_REPEAT `(1, 0x40)` (LOOP);
    - then `(0, reg)` for volume 0;
    - with DMA_INT `(0x10, 0x40)`;
    - and `(0x20, 0x40)` (STOP).

Tables (rasm's start-up code): `psgtab` = 64 x each of {0, 10, 14, 15}; `dmatab` = 51 x 0, 51 x 10, 52 x 12, 51 x 14,
51 x 15; `psgfine` levels 0..15 with run lengths 1, 1, 1, 2, 3, 4, 5, 6, 9, 18, 20, 24, 30, 34, 48, 50.

## 4. File headers, SAVE, pokers, CPC colours

### 4.1 AMSDOS name and header

`MakeAmsdosName` (`MakeAMSDOS_name`):

- A `u:` prefix sets the user (`atoi`).
- Everything up to the last `/` and then `\` is dropped.
- Up to 8 characters before the first dot are uppercased and padded with spaces, then up to 3 characters after the
  first dot (`x.y.z` gives `X       Y.Z`).
- A name longer than 12 characters, or a dot after position 8, warns "filename [%s] too long for AMSDOS, will be
  truncated".

`MakeAmsdosHeader`, 128 bytes:

| Offset | Content |
|--------|---------|
| 0 | user |
| 1..11 | name |
| 18 | type 2 |
| 19..20 | length |
| 21..22 | load address |
| 24..25 | length |
| 26..27 | run address |
| 64..65 | length |
| 67..68 | checksum of 0..66 |

It also has a "shadow sector" tail, which is part of the bytes:

- 0x47..0x4E: the name;
- 0x4F..0x51: `$`;
- 0x52 and 0x54: `FF`;
- 0x57: 2;
- 0x5A..0x5E: copies of the address and length;
- then `sprintf(+0x58, " created by %-9.9s ", "RASM v3.3")`, which overwrites 0x58..0x6D and puts a NUL at 0x6E.

### 4.2 HOBETA header

`MakeHobetaHeader`, 17 bytes:

| Offset | Content |
|--------|---------|
| 0..7 | the first 8 characters of the AMSDOS name (uppercased, space padded) |
| 8 | `C` |
| 9..10 | the length. This is where the start address belongs; rasm's bug, kept |
| 11..12 | the length |
| 13 | sectors, `(len+255) >> 8` |
| 14 | 0 |
| 15..16 | the checksum `sum(h[i]*257 + i)`, i < 15 |

### 4.3 SAVE

`SAVE 'f',offset,size[,AMSDOS|HOBETA|DSK,...|TAPE,...]`. `MakeSaveFile` covers the plain, AMSDOS and HOBETA forms
(`PopAllSave`):

- The limit is 65536 for AMSDOS, HOBETA, DSK and TAPE, otherwise the bank size.
- The errors:
  - "cannot save [%s] as the size is invalid!" (size < 1 or above the limit);
  - "cannot save [%s] as the offset is invalid!";
  - "cannot save [%s] as the offset+size will be out of bounds!".
- The run address is the last `RUN`, else the offset.
- **The `:` quirk.** For AMSDOS rasm calls `MakeAMSDOS_name` twice, and the first call cuts the filename at its `:`
  in place. So `SAVE '3:prog.bin',...,AMSDOS` writes a file named `3` with the name `3` and user 0. The port
  reproduces this.

The `AMSDOS` directive (a header on the main output) is `MakeAmsdosHeader` with run = load address and the first 11
raw bytes of the output path as name.

### 4.4 Pokers: SUMMEM, SUM16, XORMEM, CIPHERMEM

They run at the end of assembly, in source order, over `[start, end)` of the bank the directive is in. SUMMEM, SUM16
and XORMEM write their result (1, 2 little endian, 1 byte) at their own address. rasm warns when one poker's result
lies inside another poker's range (engine).

| Method | Effect |
|--------|--------|
| 1 (default, also 0 and any unknown value) | `x = m[start++]`, then `m[j] = x = m[j] ^ x` |
| 2 | the same chain starting from `x = start & 255` |
| 3 | `m[j] ^= j & 255` |
| 4,'key' | repeating key XOR. A key of one character gives "CIPHER needs a string of one char or more to run properly" |

### 4.5 CPC colours

- `SOFT2HARD_INK` / `S2H_INK` maps soft colours 0..26 to `0x40 +` the firmware hardware number. Other values give
  "SOFT2HARD_INK needs 0-26 color index" (`kSoft2HardError`) and 0.
- `HARD2SOFT_INK` / `H2S_INK` maps `h & 31` through the inverse table.
- `GETR`, `GETV` / `GETG` and `GETB` return the nibbles 4..7, 8..11 and 0..3 of `(int)v`.
- `SETR`, `SETV` and `SETB` return `clamp((int)v, 0, 15)` shifted by 4, 8 or 0.
- `(int)` truncates toward 0: `getb(-2.5)` = 14.

## 5. ZX outputs

### 5.1 BUILDZX snapshot

`BUILDZX` makes the main output a 48K `.sna` (49179 bytes):

- **Header.** All zero except IFF2 (0x13) = 0, SP (0x17..0x18) and IM 1 (0x19). The border byte 26 lies past rasm's
  26-byte header array and is 0 in the reference build.
- **RUN pc,sp** pushes pc at `bank0[sp]`, `bank0[sp+1]`. For sp = #FFFF the high byte would land at 65536, outside the
  bank: dropped.
- **Pages.**
  - With `BANKSET 0`, bank 0 from #4000 to #FFFF.
  - Otherwise the first 16K of BANK 5, BANK 2 and BANK 0. An ORG inside a bank is a raw offset into that bank, so data
    at `bank 0` / `org #C000` is not saved, and pages 1, 3, 4, 6 and 7 are ignored.
  - pc and sp live in bank 0's buffer whatever the page mapping: with sp = #7FF0 and no BANKSET the pushed pc is
    outside the saved 16K.
- **Mode rules.** BUILDZX excludes the snapshot, tape, cartridge and ROM modes. BANK must be 0..7. RUN needs pc and sp
  (engine checks).

### 5.2 ZX tape

rasm has no ZX Spectrum tape output. `SAVE ...,TAPE` and `BUILDTAPE` always write a CPC CDT (a TZX container with
`ZXTape!` and turbo blocks, id 0x11, CPC timings). Nothing to port. A ZX `.tap` for converted sources is the backend's
choice (sjasmplus `SAVETAP`), not a rasm behaviour.

### 5.3 CPC-only outputs (not ported)

| Trigger | Output |
|---------|--------|
| `BUILDTAPE`, `SAVE 'f',o,s,TAPE,'tape'` | CPC CDT |
| `BUILDSNA [V2]`, `SNASET`, `-v2`, `-ss`, `-sb`, `ROMBANK`, `BANKSET` outside BUILDZX | CPC SNA v2 / v3 |
| `BUILDCPR`, or `BANK` with no build mode (rasm defaults to a cartridge) | CPR / XPR cartridge |
| `SAVE ...,DSK,'image'[,side][,PROTECT|HIDDEN]`, `EDSK` | EDSK disk image |
| `HFE` | HFE floppy image |
| `BUILDROM` | ROM set |
| `AMSDOS` directive | AMSDOS header on the main binary (portable: `MakeAmsdosHeader`) |

The engine reports these as unsupported.

## 6. Strings

### 6.1 Escapes per context (`DecodeQuotedString`)

| Context | Escapes | `\\` | other `\c` | plain bytes |
|---------|---------|------|------------|-------------|
| `DEFB` / `DB` (`Defb`) | `\e`=1B `\a`=07 `\b \v \f \0 \r \n \t` `\xHH` | `charset['\\']` | `c` raw | `charset[c]`; above 126 with UTF8REMAP entries: the table |
| a STRUCT field's `DEFB` default (`DefbStruct`) | `\b \v \f \0 \r \n \t` only | `charset['\\']` | `c` raw (`\x41` = `x`, then `41` through the charset) | as DEFB |
| `-ass` (`DefbAs80`) | none: the `\` is dropped, the next character goes through the charset | | | `charset[c]`, no UTF8REMAP |
| `STR` (`Str`) | `\b \v \f \0 \r \n \t \xHH` | `\` raw | `c` raw | `charset[c]`; bit 7 set on the last character when the string is the last argument or another string follows; no UTF8REMAP |
| a STRUCT instance value (`StructField`) | as DEFB | | | as DEFB, at most the field size; overrun warning "struct field number %d overrun! (%d byte%s max)!" |
| expression `'c'` | `'\c'`: `EscapedCharValue` (b v f 0 r n t, others raw), no charset | | | `charset[c]` |

- A bad `\x` gives "DEFB invalid escaped hexadecimal value!", in STR too.
- With a UTF8REMAP table and a byte above 126, the entries are tried in order. A match writes its value and skips the
  sequence. No match warns "UTF8 mapping was defined but no code were found for this one! [#%02X]" for that byte and
  writes nothing, so every byte of an unmapped sequence warns.
- In a STRUCT field each character counts against the field size, unmatched UTF-8 bytes included. The engine pads the
  rest with the field's default.
- `PRINT` decodes no escapes. `DEFSTR` is compiled out (`#if 0`).

### 6.2 Quote checks and -utf8 (preprocessor)

Without `-fq` / `FREEQUOTES` and without `-utf8`, every byte of a quoted word that is not 32..126 or a tab gives the
error "Invalid char for quoted string [%s(%X)] use -fq option to allow UTF8" (`CheckQuotedChars`).

- The context is up to 15 printable characters from 15 before, and it stops at the first bad character. The second
  bad byte of `'aéb'` therefore shows the first one again.
- `%X` prints the signed char: `FFFFFFC3`.

`-utf8` (`ApplyUtf8Option`) rewrites quoted words with a fixed table:

| UTF-8 | Byte |
|-------|------|
| `¿` | 0xAE |
| `¡` | 0xAF |
| `ù` | 124 |
| `é` | 123 |
| `è` | 125 |
| `à` | 64 |
| `Ñ` | 0xA1 |
| `ñ` | 0xAB |
| `ç` | `\\` (two backslashes: one `charset['\\']` in DEFB) |

Any other byte above 126 gives "Unsupported UTF8 char for quoted string". After each one the next byte is removed
unless it is a backslash, so a 3-byte sequence gives two errors.

### 6.3 UTF8REMAP and CHARSET

- `UTF8REMAP 'seq',v` adds an entry (`ParseUtf8Remap`: up to 15 bytes after the quote, the closing quote dropped,
  value truncated to a byte). With no arguments it clears the table.
- `CHARSET` (`DefaultCharset` resets it). The forms:

| Form | Function | Error |
|------|----------|-------|
| `CHARSET` | identity | |
| `CHARSET 'abc','xyz'` | `CharsetMapStrings` (escapes skipped in both) | "CHARSET <string>,<string> must use strings of the same size!" |
| `CHARSET 'abc',v` | `CharsetMapString`: v, v+1, ... | "CHARSET string,value has invalid quote!" |
| `CHARSET code,v` | `CharsetMapByte` | "CHARSET byte value must be 0-255" |
| `CHARSET s,e,v` | `CharsetMapRange`: s..e to v, v+1, ... | "CHARSET Winape directive wrong interval value" |

## 7. Fixtures and the generator

[`rasmdatacheck.py`](../../../tools/verification/unreal-asm/checks/rasmdatacheck.py) `--rasm <rasm>` (and `--check`
to compare without writing):

- **Inputs.** It generates the input files deterministically into `testdata/rasm/data/inputs/`:
  - binaries;
  - tile sheets;
  - an AMSDOS file written by rasm, and a copy with a broken checksum;
  - WAVs: 8/16/24/32-float bit, mono and stereo, RIFF and RIFX, raw, broken headers, a LIST chunk before the data.
- **Runs.** It assembles 217 small rasm sources and stores bytes, errors and warnings in `cases.json`, one case per
  line. Outputs above 512 bytes are stored as length + FNV-1a 64.
- **Size.** The fixtures take about 120 KB.

| Kind | Cases |
|------|-------|
| reals | 3 formats x 370 values: p/q, powers of two, random, mantissa rounding edges; plus 8 hang cases (the generator checks rasm times out) |
| incbin | 81: every option, offset/size rule, error, SKIPHEADER, SLICE at org 0 and 1 |
| wav | 40: every mode and DMA option, every header error, filter state across imports |
| save | 13: plain, AMSDOS, HOBETA, names, RUN, errors |
| poker | 17: SUMMEM, SUM16, XORMEM, CIPHERMEM 0-4 and 7 |
| colour | 157: every ink, GET / SET on edge values, the SOFT2HARD error |
| zx | 5: BANKSET 0, banks 0/2/5 with ignored pages, pc / sp placement |
| string | 39: DEFB / STR / STRUCT escapes, CHARSET forms and errors, UTF8REMAP, `-fq`, `-utf8`, `-ass` |

## 8. Results

`unreal-asm-tests --gtest_filter='RasmData*'` (2026-11-17, macOS arm64 clang): 11 tests pass, about 20 ms in total.
Every fixture case matches rasm v3.3.3 in bytes, first error and warnings:

| Kind | Coverage |
|------|----------|
| reals | 1110 values (370 per format), plus the 8 hang cases |
| incbin | 80 cases; `-300,10` refused as documented in §2.4 |
| wav | 40 |
| save | 13 |
| poker | 17 |
| colour | 157, of which 2 are the SOFT2HARD error |
| zx | 5 |
| string | 39 |

The whole `unreal-asm-tests` suite still passes (463 tests). `rasmdatacheck.py --check` reproduces the stored fixtures
exactly; the one case where rasm reads outside its buffer gives different bytes on every run, so it is stored as
`"outside": true`.

## 9. Open points

- The port follows the reference binary where rasm's result is build dependent: FMA contraction, the saturating
  conversions, signed char. Another rasm build may differ on WAV levels at rounding edges. Floats, headers and strings
  do not depend on the build.
- Reads outside rasm's buffers return 0 (WAV) or an error (INCBIN, §2.4). rasm's own result there is undefined.
- rasm hangs on some DEFR / DEFF values (§1); the engine must report `kRealHangsMessage` instead.
