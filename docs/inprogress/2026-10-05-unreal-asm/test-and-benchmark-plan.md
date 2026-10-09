# unreal-asm: test and benchmark plan

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Design** | [tdd.md](tdd.md); the symbol module's plan: [symbols/test-and-benchmark-plan.md](symbols/test-and-benchmark-plan.md) |

## 1. Oracles

| Oracle | Proves | How |
|---|---|---|
| **Byte-exact round trip** | a codec loses nothing | `Encode(Decode(f)) == f` for every corpus file the real assembler saved |
| **The assembler's screen** | decoding gives the exact text | the assembler, running in the emulator, lists the source; the screen is captured (OCR / text capture) and compared line by line with `Decode` |
| **The assembler loads our file** | encoding is valid | a file encoded from text (or converted from another sub-version) is loaded by the real assembler in the emulator; its listing equals the input text |
| **Binary equality** | dialect conversion keeps the meaning | the original assembler builds binary A from the original; the target tool builds binary B from the converted text; A == B |

The emulator-driven oracles run as scripted sessions (TTD recorded, automation surfaces) that **produce** golden
files; the unit tests then compare against those files without an emulator.

## 2. Corpus

The corpus is part of the library (decision D-14): `core/src/3rdparty/unreal-asm/testdata/`.

```text
core/src/3rdparty/unreal-asm/testdata/
├── README.md                 # provenance of every file: assembler + version, how it was made, source disk
├── probe/                    # the probe sources (every construct of each dialect), as text
├── tasm/                     # files saved by TASM in the emulator + real-world files (the TRD test data found locally)
│   └── <name>.$A  <name>.expected.txt  <name>.bin (assembled)
├── alasm/                    # real files of every ALASM era (3.8 … 5.09)
├── storm/ zxasm/
├── text/                     # every code page and line end
└── convert/                  # pairs: original, converted per backend, expected report, both binaries
```

## 3. Unit tests (`unreal-asm-tests`, also run from `test-parallel`)

| Area | Tests |
|---|---|
| core | code page tables (every byte of CP866 / KOI8-R / CP1251 round trips), line ends, attribute bags |
| text lexer | every number notation, strings with both quotes and escapes, comments, columns |
| each codec | decode golden → expected text; byte-exact round trip; canonical encode of edited text; unknown-token escapes; truncated and corrupted files give diagnostics, never crashes; detection scores on the whole corpus (the right codec ≥ 60 and ≥ 15 above the next) |
| format conversion | every planned pair of [source-formats.md](source-formats.md) §4; lines the target cannot hold are reported (DT-3) |
| IR | builders, the debug printer, each transform (label renaming collision-free, macro expansion, number spelling, current address) |
| each frontend | probe source → expected IR (a stable text dump); `Raw` fallback with a diagnostic |
| each backend | IR → expected text; capabilities honored; DT-2 for every unsupported kind |
| dialect pairs | corpus conversions → expected text and report; binary equality where the target tool is available in CI (sjasmplus) |
| fuzzing (optional build) | one libFuzzer target per codec and per frontend; the corpus is the seed |

## 4. Emulator-side tests (`core-tests`)

`DiskFileSource` / `DiskFileSink` on TRD and FAT images (read, write, write-protected refusal, modified flag);
`CatalogHintsFromTrdos`; the surfaces' shared functions; MCP `asm_source` routing; Qt disk browser actions
(`unreal-qt-tests`); live checks over WebAPI and CLI on an own instance.

## 5. Benchmarks

| Benchmark | Target |
|---|---|
| `BM_Asm_Decode/{codec}` (a 640 KB disk of sources) | ≥ 50 MB/s |
| `BM_Asm_Encode/{codec}` | ≥ 50 MB/s |
| `BM_Asm_Parse/{dialect}` (100 000 lines) | ≥ 200 000 lines/s |
| `BM_Asm_Convert/{pair}` | ≥ 200 000 lines/s |

## 6. Results table (filled in A9)

`unreal-asm-benchmarks` (`-DBENCHMARKS=ON`, target `unreal-asm-benchmarks`), 2026-10-07, Apple M-series, Release,
the testdata corpus of each codec; load average about 7 (other builds running): figures are a floor. Decode is
measured twice: with the version detected (every version of the format tried on every line, decision D-15) and with
the version given (`DecodeOptions::subversion`, what a caller that knows it pays).

| Benchmark | Result | Target | OK |
|---|---|---|---|
| Decode, version given: tasm / alasm / zxasm / storm / masm / gens | 135 / 114 / 115 / 63 / 78 / 67 MB/s | ≥ 50 MB/s | yes |
| Decode, version given: zeus / xas | 39 / 33 MB/s | ≥ 50 MB/s | no (each line is encoded again to check it is canonical) |
| Decode, version detected: tasm / storm / masm / gens | 112 / 63 / 65 / 65 MB/s | ≥ 50 MB/s | yes |
| Decode, version detected: alasm / zxasm / zeus / xas | 6.0 / 9.2 / 20 / 34 MB/s | ≥ 50 MB/s | no (6 / 4 / 3 versions tried; a 640 KB disk of ALASM in about 0.1 s) |
| Encode: tasm / alasm / zxasm / storm / masm / gens / zeus / xas | 123 / 125 / 118 / 81 / 81 / 111 / 71 / 51 MB/s | ≥ 50 MB/s | yes |
| Parse: every dialect | 208 k (alasm) to 1.95 M (masm) lines/s | ≥ 200 000 lines/s | yes |
| Convert to sjasmplus: every dialect but alasm | 720 k (zxasm) to 1.28 M (masm) lines/s | ≥ 200 000 lines/s | yes |
| Convert to sjasmplus: alasm | 187 k lines/s | ≥ 200 000 lines/s | no (7 % short) |

Speed-ups the profiles led to (2026-10-07): keyword tables of XAS, ALASM and ZEUS built once per version and
searched by first character; XAS's font read backwards once (it scanned 240 glyphs per character); an ASCII fast path
in `CodePointToByte`. XAS decode went from 1.5 to 33 MB/s, encode from 8 to 51; ZEUS encode from 20 to 71.

Version detection and conversion (2026-10-09). The same benchmarks were built before and after and run in interleaved
rounds. The load average was 16-40, so the absolute figures are a floor and the ratios are what counts:

| Benchmark | Before | After | Change |
|---|---|---|---|
| Decode, version detected: alasm | 4.3 MB/s | 45.9 MB/s | x10.7 |
| Decode, version detected: zxasm | 8.8 MB/s | 52.8 MB/s | x6.0 |
| Decode, version detected: zeus | 12.2 MB/s | 16.5 MB/s | x1.35 |
| Decode, version given: zeus | 25.8 MB/s | 36.1 MB/s | x1.4 |
| Convert to sjasmplus: alasm | 170 k lines/s | 328 k lines/s | x1.9 (target met) |
| Decode: xas | 30 MB/s | unchanged | the packer's own work; buffer reuse gained under 5 %, not kept |

How:
- **ALASM.** The versions differ in a few mnemonic codes only. A line whose mnemonic byte is none of them, and whose bytes
  hold none of their spellings as a word, counts alike for every version and cannot change the choice, so it is not
  decoded at all. The other lines decode once per spelling of their mnemonic byte.
- **ALASM to sjasmplus.** Macro arguments were tried as operands and the failures thrown. The expression parser now has
  a soft mode that sets a flag instead.
- **ZX-ASM.** 2.x (no tokens) is counted over the plain text lines. 3.0, Lite and 3.15 are counted over the lines that
  tell them apart, where a late keyword appears as a token or in the bytes. The lines they read alike are checked only
  while 2.x could still tie.
- **ZEUS.** The word-character tables and the keyword index are built once, and EncodeBody reuses its buffers.
  `FromUtf8` has an ASCII fast path.

Every decode was compared before and after, for 1578 sources from 673 disk images of the collection and the test data,
and so was every conversion (235 images). All of them are byte-identical. ZEUS (re-encoding each line for the canonical
check) and XAS (the packer) stay below 50 MB/s.
