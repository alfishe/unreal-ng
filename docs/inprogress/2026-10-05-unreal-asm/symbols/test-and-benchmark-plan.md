# Symbol exchange: test and benchmark plan

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Draft for review |
| **Design** | [tdd.md](tdd.md) |

## 1. Principles

- **Golden files come from the real tools.** Each format's corpus is produced by running the tool (sjasmplus,
  z88dk, pasmo, IDA / Ghidra scripts, the ZX assemblers in the emulator) on a known source, never written by hand
  from the documentation. The expected result is a normalized native JSON beside each input.
- **Test first per phase**: the corpus and the failing tests land before the importer.
- **Round trips are tests**: native → native is an identity; cross round trips lose exactly what the lossiness
  matrix ([formats.md](formats.md) §2) says, and the test asserts the loss list.
- **No emulator in the format tests**: model, tokenizer and formats are tested without `EmulatorContext`
  (they compile in the `symbols-std-only` library); only adapter and facade tests create an emulator.
- Every test under 50 ms (AGENTS.md); the big-file cases live in benchmarks, not in tests.

## 2. Corpus

```text
testdata/symbols/
├── README.md                       # how each file was produced: tool, version, command line, source
├── sources/                        # the known sources every tool assembles (globals, locals, EQU, long names,
│                                   #   every allowed character, pages via ORG / DISP / DEVICE)
├── sjasmplus/  game.sym  game.sld  game.lst  game.exp  + *.expected.json
├── z88dk/      game.map  + expected
├── pasmo/      ...
├── unreal-map/ 48k_rom.map (copy of data/symbols)  + expected (today's parser output)
├── simple-sym/ sos.l  + expected
├── vice/  sjasm/  ida/  ghidra/  mame/  cspect/
├── native/     every expected.json above, plus edge cases (unknown fields, aliases, locals, devices, gs CPU)
├── alasm/      disk files (.trd) + RAM dumps per version + expected      (S6, after research)
└── xas/  storm/  gens/  masm/  sts/                                      (S7-S8)
```

## 3. Unit tests (core-tests)

| File | Covers |
|---|---|
| `symbolstore_test.cpp` | add / update / remove; sets enable / priority; resolved view; `labels_changed` once per change |
| `symbolindex_test.cpp` | exact, nearest-below (`PLAYMUS+12`), ranges; page beats CPU view; disabled sets invisible; 16-byte entries |
| `mergepolicy_test.cpp` | every branch of DT-2 with each policy; conflict records; moved names |
| `nameinterner_test.cpp` | identity, UTF-8 names, case fold sets |
| `addressspace_test.cpp` | parse / format every spelling (`cpu:main`, `rom2`, `ram3`, `cache0`, `vram`, `const`, `port`, `gs.rom0`); bad spellings |
| `tokenizer_test.cpp` | every number notation and width; identifiers per charset; strings; comments; columns; over-long lines; NUL and invalid UTF-8 bytes |
| `codecregistry_test.cpp` | detection matrix: every corpus file is recognized as its format with ≥ 60 and ≥ 15 above the next; forced format; ambiguous and unknown reports |
| `namerules_test.cpp` | DT-3 per target: invalid characters, leading digit, reserved words, length, collisions → `_2` |
| `<format>_test.cpp` (one per format) | import golden → expected JSON; export expected → golden bytes; diagnostics with line numbers on broken copies |
| `roundtrip_test.cpp` | native identity on the whole corpus; sjasmplus ↔ native ↔ IDA ↔ VICE with the asserted loss lists |
| `normalizer_test.cpp` | DT-1 for every combination; base offset; out-of-range offsets skipped with a diagnostic |
| `bundlemanifest_test.cpp` | manifest parse; SHA-256 match; model match; unknown ROM → nothing |
| `labelmanager_facade_test.cpp` | every existing `LabelManager_Test` expectation through the facade; old API answers unchanged |
| `diskfilesource_test.cpp` (adapter) | `disk:A/NAME.EXT` from a TRD and from a FAT image; missing file; bad spec |
| `livescan_alasm_test.cpp` (S6) | the scanner finds the table in the recorded RAM dumps, at the documented place and by scan; several candidates reported |
| `mcp-tools-test.cpp` additions | `manage_symbols` `import`, `export`, `formats`, `sets` route and text |
| `unreal-qt-tests` | `LabelEditor` import / export dialogs (offscreen): format list, report shown, rename list shown |

Fuzzing (optional build, `-DFUZZ=ON`): the tokenizer and every text importer behind one libFuzzer target; the
corpus is the seed. A crash or a hang over 1 s is a failure.

## 4. Live checks (per surface)

WebAPI with curl on an own instance (own ports, own PID): import a corpus file, a `disk:A/...` file, a live ALASM
session (S6); export to each format; the reports' fields. CLI over its port. Lua and Python where their harness
exists, else compile checks (as today).

## 5. Benchmarks (`core-benchmarks`)

| Benchmark | Measures | Target |
|---|---|---|
| `BM_Symbols_ImportSym/{1k,10k,100k,1M}` | text import end to end (detect, tokenize, normalize, merge, index) | 100k < 300 ms |
| `BM_Symbols_Tokenize/{lines}` | tokenizer alone | ≥ 1 M lines / s |
| `BM_Symbols_IndexBuild/{n}` | index build | `O(n log n)`; 100k < 30 ms |
| `BM_Symbols_LookupAt/{n}` | exact lookup | ≤ 100 ns at 100k |
| `BM_Symbols_LookupNearest/{n}` | nearest-below | ≤ 150 ns at 100k |
| `BM_Symbols_DisasmLine` | one disassembly line with labels (the hot consumer) A/B against today's `LabelManager` | not slower |
| `BM_Symbols_Export/{format}/{n}` | export | 100k < 200 ms |
| `BM_Symbols_LiveScanAlasm` (S6) | scan of 128K / 1M / 4M RAM | 4M < 100 ms |

Runs follow the A/B procedure of [performance-guidelines.md](../../../guidelines/performance-guidelines.md) §4
(quiet machine, interleaved rounds, both orders).

## 6. Results table (filled during S9)

| Benchmark | n | Result | Target | OK |
|---|---|---|---|---|
| | | | | |
