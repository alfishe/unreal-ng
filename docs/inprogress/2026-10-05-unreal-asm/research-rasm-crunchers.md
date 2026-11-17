# rasm's crunchers in unreal-asm

rasm (3.3.3 "Monolith", build 09/11/2026, the copy in `scratch/wt-rasm/scratch/rasm/src`) crunches code sections
(`LZ48` ... `LZCLOSE`) and files it includes (`INCL48 'file'`). The rasm engine of unreal-asm needs the same bytes:
`include/unrealasm/compress.h` (`unrealasm::compress`) runs the compressor sources rasm links, with rasm's parameters,
and models what rasm does with the result. Sources in `core/src/3rdparty/unreal-asm/src/compress/`, tests in
`tests/compress_test.cpp`, fixtures in `testdata/compress/`, the rasm oracle script
`tools/verification/unreal-asm/checks/crunchcheck.py`.

## API

```cpp
Result Crunch(Algorithm, const uint8_t* data, size_t size, const Options& = {});  // what rasm emits
Result CrunchRaw(Algorithm, const uint8_t* data, size_t size, int lzsaMinMatch = 0);  // the cruncher's own bytes
Result Decrunch(Algorithm, const uint8_t* data, size_t size);                      // round trips
std::optional<Algorithm> AlgorithmFromRasmDirective(std::string_view);              // "LZX0B", "INCL48", ...
```

`Options::origin` is `Section` (LZxxx ... LZCLOSE) or `Incbin` (INCxxx): rasm calls the same crunchers in both places,
but sizes the ZX0 buffer differently and handles a crunch that fails or grows differently (below).
`Options::lzsaMinMatch` is the argument of a section opener (`LZSA1 4`). `Result` has `bytes`, `error` (rasm stops),
`warning` (rasm goes on) and `crunched` (false: the bytes are kept as they were). All functions are thread-safe.

## Directives

| Section | Include | rasm code | Algorithm |
|---|---|---|---|
| `LZ4` | `INCLZ4` | 4 | `Lz4` |
| `LZ48` | `INCL48` | 48 | `Lz48` |
| `LZ49` | `INCL49` | 49 | `Lz49` |
| `LZX7` | `INCZX7` | 7 | `Zx7` |
| `LZX0` | `INCZX0` | 70 | `Zx0` |
| `LZX0B` | `INCZX0B` | 71 | `Zx0Backward` |
| `LZEXO` | `INCEXO` | 8 | `Exomizer` |
| `LZAPU` | `INCAPU` | 17 | `ApUltra` |
| `LZSA1 [n]` | `INCLZSA1` | 18, version 1 | `Lzsa1` |
| `LZSA2 [n]` | `INCLZSA2` | 18 (include 19), version 2 | `Lzsa2` |

The include names are matched by the preprocessor (rasm.c, the `case 'I'` of the include scan: `INCAPU`, `INCEXO`,
`INCLZSA1`, `INCLZSA2`, `INCL48`, `INCL49`, `INCLZ4`, `INCZX0B`, `INCZX0`, `INCZX7`); `INCBIN` and `INCWAV` are code 10
(no crunch). The section openers are in the instruction table (`__LZ4`, `__LZX0B`, ...), closed by `LZCLOSE`.

## Per algorithm: source, call, quirks

| Algorithm | Upstream (as rasm carries it) | rasm's call |
|---|---|---|
| LZ4 | LZ4 1.7.x (Yann Collet), lz4.c + lz4hc.c merged into `lz4.h` by rasm | `LZ4_compress_HC(data, dest, len, 65536, 9)`: HC level 9, a raw block (no frame, no header), destination 64K |
| LZ48 | rasm.c `LZ48_encode_legacy` (v005, Roudoudou) | own code; ported to C++ (`rasmcrunchers.cpp`) |
| LZ49 | rasm.c `LZ49_encode_legacy` (v002) | own code; ported |
| ZX7 | ZX7 (Einar Saukas) optimize.c + compress.c in `zx7.h` | `ZX7_compress(zx7_optimize(data, len), data, len, &out)`; ported (rasm's copy keeps the output state in globals and leaks the optimiser tables) |
| ZX0 | salvador 1.4.2 (Emmanuel Marty) | `salvador_compress(in, out, len, max, FLG_IS_INVERTED, 32640, 0, NULL, NULL)`: ZX0 v2 (inverted Elias gamma), window 32640 |
| ZX0 backward | salvador 1.4.2 | `zx0_reverse(in)`, `salvador_compress(in, out, len, len, FLG_IS_INVERTED \| FLG_IS_BACKWARD, 32640, ...)`, `zx0_reverse(out)` |
| Exomizer | Exomizer 2 (Magnus Lind), merged and modified into `exomizer.h` | `Exomizer_crunch(data, len, &out)`: default options (max offset 65535, max length 65535, 65535 passes, literal sequences on), forward `exocrunch`, raw output (no header, no load address) |
| aPLib | apultra 1.3.6 (Emmanuel Marty) | `apultra_compress(in, out, len, apultra_get_max_compressed_size(len), 0, 65536, 0, NULL, NULL)`: flags 0, window 64K |
| LZSA1 | lzsa 1.3.5 (Emmanuel Marty) | `lzsa_compress_inmem(in, out, len, max, LZSA_FLAG_RAW_BLOCK, minmatch, 1)`: a raw block (no frame), forward, not "favor ratio"; minmatch 5 unless the section gives 2..5 |
| LZSA2 | lzsa 1.3.5 | the same with version 2; minmatch 2 unless the section gives 2..5 |

Vendored unmodified: `salvador/`, `apultra/`, `lzsa/` (the files rasm's makefile builds plus the headers `lib.h`
pulls in), `libdivsufsort/` (one copy: rasm's makefile builds apultra's and salvador's against lzsa's, the two copies
are identical), `lz4/lz4.h`, `exomizer/exomizer.h`. `lz4unit.c` / `exomizerunit.c` compile the single-file headers
with the C headers rasm.c has included before them, and rename every external symbol (`unrealasm_` /
`unrealasm_exo_` prefix: exomizer exports names such as `get`, `membuf_init`, `read_file`), as CMake renames
libdivsufsort's (zstd links another libdivsufsort). The vendored C files are compiled with their warnings off
(`-w`, MSVC `/W0`) and `-ffp-contract=off` (exomizer scores paths in float), only those files.

Quirks found (each modelled by `Crunch`):

- **Empty input.** A section: "crunched section is empty" and "LZ section is bigger than original" warnings, nothing
  emitted. An include of an empty file: "INCBIN of an empty file", nothing emitted. rasm never calls a cruncher with
  no data (LZ48 / LZ49 / ZX7 would read `data[0]` and `optimal[-1]`).
- **A crunch that does not help.** Section: `input_size<lzlen || lzlen<=0` keeps the section as assembled, with the
  warning "LZ section is bigger than original, it wont be crunched!" (equal size is crunched). Include:
  `!outputidx || outputidx>=size` is the error "Cannot crunch [file]" (equal size is an error). So a 1-byte input is
  never crunched: kept in a section, an error as an include (LZ48 of `A` is `41 00 FF`, `CrunchRaw` returns it).
- **ZX0 buffer sizes.** A section passes `max = input size` to salvador (forward and backward), `INCZX0` the worst
  case (`salvador_get_max_compressed_size`), `INCZX0B` the input size. salvador only uses the limit to fail, so the
  bytes are the same where both succeed; only the failure path differs.
- **`LZX0B` section that does not crunch: the section stays reversed.** rasm reverses the section's memory in place
  before crunching and reverses only the output back; when salvador fails (`(size_t)-1` -> `lzlen=-1`) the section is
  kept "uncrunched" but byte-reversed. `Crunch` emits the reversed bytes (outcome `reversed` in the fixtures,
  `random.bin`).
- **`INCZX0B` of data that does not crunch: rasm crashes** (segfault): `outputidx=-1` passes the `Cannot crunch`
  check and the copy loop runs over it. `Crunch` returns an error (outcome `crash` in the fixtures: `one.bin`,
  `random.bin`).
- **LZSA over 64K.** `LZSA_crunch` fails when the output is over 65536 bytes or the raw block cannot be built, with
  "LZSA cannot crunch more than 64K in RAW MODE" when the input is over 64K. It is an error in both places (the
  section path adds the raw-kept warning, a section is never over 64K anyway).
- **LZ4 over 64K.** The destination is 64K: an output that does not fit is a failure (raw-kept section / include
  error). Inputs over 64K that crunch below it work (the 100K generated input: 429 bytes).
- **Includes over 64K.** rasm crunches a whole file of any size; the crunched bytes must fit memory. ZX0, ZX0B,
  LZ4, LZ48, LZ49, apultra do it (fixtures `@big`); LZSA refuses; ZX7 and Exomizer did not finish in 2 minutes on the
  100K input in rasm (left out). `Crunch` refuses Exomizer inputs over 64K (its tables are 64K long).
- **Section vs include.** For every input that crunches, the two give identical bytes for every algorithm
  (the fixtures store one file for both; crunchcheck.py would store two if they differed).
- **LZSA minmatch.** The section argument is `atoi`'d; outside 2..5 it becomes the default. The includes always use
  the default (INCLZSA1 sets 5, INCLZSA2 sets 2).
- **Thread safety.** Exomizer keeps static search buffers and option tables: its calls are serialised by a mutex.
  rasm's ZX7 keeps its output in globals: the port has none. LZ4 HC allocates its state on the heap; salvador,
  apultra and lzsa allocate per call. `Crunch.ThreadSafe` runs six algorithms on twelve threads.
- **Exomizer_crunch** allocates an argv it never frees; `exomizerunit.c` has the same body (default options, the
  same `exocrunch`) without it.

## Decrunch

`Decrunch` decodes what `CrunchRaw` / `Crunch` produce, up to 16 MB out: LZ4 (`LZ4_decompress_safe`), ZX0 / ZX0B
(`salvador_decompress`, the backward one reversed around it), LZSA1/2 (`lzsa_decompressor_expand_block`, raw), and
own C++ decoders for LZ48, LZ49, ZX7, aPLib and Exomizer 2 raw. Damaged data gives an error, never a crash or a hang
(`Crunch.DamagedDataIsAnErrorNotACrash`: every cut of a crunched block and a changed byte every 7). Upstream decoder
traps met on the way:

- **apultra** (`apultra_get_max_decompressed_size`, `apultra_decompress`): `nResult` is `unsigned`, so
  `if (nResult < 0) return -1` never fires; at the end of the data the read bit -1 counts as 1, and the size probe
  loops for ever on a cut block. Both also read the offset byte and the `110` command byte without a bound check.
  `ApUltraDecode` (`rasmcrunchers.cpp`) is the same format with every read checked.
- **lzsa v1 raw end marker**: the LZSA1 end is token `0x0F`, offset byte `00` (offset 256), length escape `EE 00 00`
  (0). `lzsa_decompressor_expand_block_v1` checks the match source against the start of the output before it reads
  the length, so it refuses the end of its own block when less than 256 bytes came out (17 bytes of `ABC...`:
  `3B 41 42 43 FD 0F 00 EE 00 00`). The facade decodes after 256 bytes of zeros, as if they were a dictionary.
- **lzsa v2** reads the low offset byte of a 13-bit offset after a nibble fetch without a bound check, salvador and
  lzsa can read one byte past damaged data: their input copy has 16 zero bytes past the size they get.
- **exomizer's `exodecrunch`** calls `exit()` on truncated data, hence the own decoder.
- **LZ49 offset byte**: rasm stores `(unsigned char)offset - 1`, after taking 256 off and setting the token's bit 7
  for offsets over 255; offset 256 is therefore `0xFF` with bit 7 (0 - 1 in 8 bits), not 512. A decoder adds 1 in 8
  bits before the 256 (`Crunch.Lz49OffsetsAroundTheHighBit`).

## Tests

Fixtures (`testdata/compress`, 95 KB): `inputs/` = empty, 1 byte, 17 bytes (`ABCABC...`), 3000 bytes of text
(rasm's README), 1 KB of noise (LCG), 4K of zeros, rasm's `decrunch/lzDataTest.bin` (29158 bytes), the first 20K of
`data/rom/128.rom`, plus a generated 100K input; `manifest.tsv` = per input, algorithm and origin what rasm emitted
(crunched / raw / reversed / empty / error / crash) and the expected file (`expected/`; the outputs of the ROM slice
and of lzDataTest.bin as CRC-32 and size). LZSA sections with minmatch 3, 4 (LZSA1) and 3, 5 (LZSA2) on the text.
190 cases: 108 crunched, 20 raw, 1 reversed, 20 empty, 21 error, 2 crash. By default `unreal-asm-tests` runs the
cases of empty, one, seventeen and random.bin (they reach every outcome); text.bin, zeros4k.bin, the ROM slice,
lzDataTest.bin and the 100K input are the long tier (`UNREAL_LONG_TESTS=1`): salvador, apultra, exomizer and ZX7 take
up to a second on 4K of zeros or the text.

`crunchcheck.py` regenerates the fixtures from rasm (`--update`), compares a binary against them (`--tests`) and
fuzzes (`--fuzz N --seed S`: pseudo-random text, Z80-like bytes and runs of 1 to 8000 bytes; rasm's answers are
written as a fixture set that `UNREAL_ASM_CRUNCH_FIXTURES` hands to the tests).

## Results (2026-11-17, macOS arm64, clang, rasm 3.3.3 built from its sources)

Every algorithm is byte-exact with rasm on every case, section and include, including the failure outcomes:

| Run | Result |
|---|---|
| `unreal-asm-tests --gtest_filter='Crunch*'` (short tier) | 25 / 25, 2.5 s (Exomizer's fixed setup cost dominates: 0.1-0.9 s per test) |
| the same with `UNREAL_LONG_TESTS=1` (all 190 cases, ROM slice, lzDataTest.bin, the 100K input) | 25 / 25, 98 s |
| `crunchcheck.py --tests` (rasm's output now against the fixtures, then CrunchFixtures) | no difference, 10 / 10 |
| `crunchcheck.py --fuzz 40 --seed 7 --tests` (800 cases: 400 crunched, 182 raw, 18 reversed, 180 error, 20 crash) | 10 / 10 |
| the whole `unreal-asm-tests` | 477 passed |

Exomizer is byte-exact with the vendored C compiled with `-ffp-contract=off`; its path costs are float sums, so a
compiler that fuses them (FMA, the arm64 default of some compilers) could pick other paths. Not checked yet: gcc,
MSVC and MinGW builds of the same fixtures (the CI run of `unreal-asm-tests` covers the short tier there).
