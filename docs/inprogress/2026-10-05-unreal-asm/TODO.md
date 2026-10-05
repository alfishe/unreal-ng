# unreal-asm — TODO

**Status:** design 2026-10-05 (branch `symbols-design`). Decided: D-1 one codec per format, decode and encode;
D-2 nothing vendored; D-3 the library `unreal-asm` in `core/src/3rdparty/unreal-asm`, symbols are one consumer;
D-4 codecs and format conversion first, dialect conversion plugins after. No code. PLAN.md row **#97**.

## Questions (asked one at a time)

- [x] Q-1 the first codecs: TASM 3 + TASM 4 (D-5, 2026-10-05)
- [x] Q-2 plugin form: compiled-in modules, one registry (D-6, 2026-10-05)
- [x] Q-3 the IR is neutral, designed from all dialects (D-7, 2026-10-05)
- [x] Every catalog codec stays queued; TASM first, the rest at lower priority (D-8, 2026-10-05)
- [x] Q-4 the first dialect pair: TASM → sjasmplus (D-9, 2026-10-05)
- [x] sjasmplus is the first output target implemented (D-10, 2026-10-05)
- [x] Q-5 decoded text is UTF-8, original code page recorded (D-11, 2026-10-05)
- [x] Q-6 CLI name: `zxasm` (D-12, 2026-10-05)
- [x] Encoding detectors are separate reusable classes of the library (D-13, 2026-10-05)
- [x] Examples and test data are mandatory parts of the library; a phase is done only with them (D-14, 2026-10-05)
- [x] Every codec detects and supports every version of its format (D-15, 2026-10-05)

## Workflow (owner, 2026-10-05)

Design on master; development in a fresh worktree from master (branch `unreal-asm`); a commit after each green
phase; master only after the owner's review.
- [ ] symbol module proposals P-2 … P-7 ([symbols/goals-and-requirements.md](symbols/goals-and-requirements.md) §3.2)

## Phases ([tdd.md](tdd.md) §8)

- [x] Prior-art survey, local and public ([prior-art.md](prior-art.md))
- [ ] A0 review round 1 of every document
- [x] A1 skeleton, document model, encoding detectors and code page tables (D-13), `text` codec, sjasmplus text codec (D-10), registry, detection (2026-10-05, branch `unreal-asm`): `core/src/3rdparty/unreal-asm` with the `zxasm` CLI, two examples and the text / sjasmplus corpus (D-14); 22 tests in `unreal-asm-tests` (run by `test-parallel`); byte-exact round trip of every corpus file (CP866 / KOI8-R / CP1251 / UTF-8 × LF / CRLF / CR, BOM, no final break, mixed ends, invalid bytes); detector: KOI8-R 75 vs 13, CP1251 72 vs 16, CP866 98
- [x] A2 `tasm3`, `tasm4` (research first: [research-tasm.md](research-tasm.md)), sub-version conversion (2026-10-05, branch `unreal-asm`): hobeta / TR-DOS containers, `zxasm` reads `.$X` / `.trd --file`, writes `.$X`; example `read-hobeta`; testdata `tasm3/` (two real TASM 3.2 files + expected text); 34 tests; byte-exact round trip, the canonical tokenizer alone reproduces TASM 3.2's bytes, TASM 3 → 4 → 3 exact
  - [x] TASM every version (2026-10-05, branch `unreal-asm`): `3` (3.0-3.5), `4.0` (4.0 XLD / 4.4 KVA), `4.12` (direct blank counts); tables from each binary; 127 real sources (24 / 91 / 12) byte-exact, canonical 99.78 %; tokenizer: keywords everywhere, `"x"` constants literal ([research-tasm.md](research-tasm.md))
  - [x] emulator oracle (2026-10-05): TASM 3.0, 4.0, 4.12 show the decoded text exactly; a 4.0 → 4.12 conversion opens in 4.12; keywords display in capitals ([research-tasm.md](research-tasm.md) §6.1)
  - [x] TASM 2.0 as version `2.0` of the `tasm` codec (2026-10-05): plain text with editor TABs, learned from a file made in the emulator ([research-tasm.md](research-tasm.md) §2.1)
- [x] A3 `alasm`, every version 3.8 / 4.2 / 4.42 / 4.5 / 4.44 / 5.07-5.09 ([research-alasm.md](research-alasm.md)), and D-15 for all codecs (2026-10-05, branch `unreal-asm`): `tasm3` + `tasm4` merged into `tasm` (versions 3, 4); `DecodeOptions::subversion` / `catalog`, `EncodeOptions::subversion`, `DecodeResult::subversions`; version detection by re-tokenizing; conversion warnings for keywords the target lacks; `zxasm check` / `--version`; example `convert-version`; testdata `alasm/` (10 files); 429 real files byte-exact, canonical 99.6 %
  - [x] emulator oracle for ALASM (2026-10-05): a 4.5 → 5.07 conversion opens in ALASM 5.09 and shows the decoded text ([research-alasm.md](research-alasm.md) §6.1)
  - [ ] ALASM 2.x and 5.00-5.06 binaries (not found)
- [ ] A4 `storm`, `zxasm`
  - [x] `zxasm`, every version 2.4 … 4.20 (2026-10-05, branch `unreal-asm`): editor rules derived on 372 real sources (byte-exact all, canonical 99.86 %), version detection, testdata `zxasm/` ([research-zxasm.md](research-zxasm.md))
  - [ ] `storm` (STORM 1.0beta … 1.3i: machine-code-like lines, packed labels; corpus of 42 files collected)
- [ ] A5 IR, transforms, sjasmplus frontend + backend, alasm frontend (ALASM → sjasmplus)
- [ ] A6 more frontends / backends; research codecs xas, masm, gens3, zeus, ads
- [ ] A7 emulator adapters and surfaces, Qt disk browser, recipe
- [ ] A8 symbols on the library (symbols S1-S5)
- [ ] A9 benchmarks, user docs
- [ ] P2: memory bridge B0-B6 ([memory-bridge.md](memory-bridge.md)), design only for now
