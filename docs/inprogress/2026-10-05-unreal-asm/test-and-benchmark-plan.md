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
├── tasm3/ tasm4/             # files saved by TASM in the emulator + real-world files (the TRD test data found locally)
│   └── <name>.$A  <name>.expected.txt  <name>.bin (assembled)
├── alasm4/ alasm5/           # the same (+ the inAlasm sample)
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

| Benchmark | Result | Target | OK |
|---|---|---|---|
| | | | |
