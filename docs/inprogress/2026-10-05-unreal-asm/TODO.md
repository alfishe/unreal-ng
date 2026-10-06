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
- [x] A4 `storm`, `zxasm` (2026-10-05, branch `unreal-asm`)
  - [x] `zxasm`, every version 2.4 … 4.20 (2026-10-05, branch `unreal-asm`): editor rules derived on 372 real sources (byte-exact all, canonical 99.86 %), version detection, testdata `zxasm/` ([research-zxasm.md](research-zxasm.md))
  - [x] `storm`, versions 1.0beta / 1.2-1.3i: decoder and STORM's encoding rules (implied commands, number forms, packed labels, IX / IY offsets, sub-expressions) derived on 42 real sources (byte-exact all, rules exact 23 492 / 23 497), testdata `storm/` ([research-storm.md](research-storm.md))
  - [x] emulator oracle for ZX-ASM (ZAsm 3.15) and STORM (1.3): files written by the codecs' rules load and show the decoded text (2026-10-05; research-zxasm.md / research-storm.md §6.1)
  - [ ] a STORM file saved by 1.0beta (start #C003)
- [x] A5 IR, sjasmplus frontend + backend, alasm frontend (ALASM → sjasmplus) (2026-10-05, branch `unreal-asm`): IR v1 (`ir.h`), `Convert` / `ConvertProject`, `zxasm convert` (file, `--from`, whole disk with `INCBIN` files); The Link 18 / 19 objects byte-equal with ALASM's, a constructs sample byte-equal with ALASM 5.09 in the emulator, 508 / 513 sources unchanged through the sjasmplus round trip; example `convert-dialect`; testdata `dialects/` ([research-alasm-to-sjasmplus.md](research-alasm-to-sjasmplus.md))
  - [x] A5b `tasm` frontend, versions 3 / 4.0 / 4.12 (TASM → sjasmplus, D-9) (2026-10-05, branch `unreal-asm-next`): the GS 1.04 ROM sources assemble to the ROM byte for byte, TASM 4.12's SINUS equal to what TASM 4.12 built in the emulator, 99 sources unchanged through the round trip; macro expansion shared (`common/macros`); check scripts in `tools/unreal-asm/` ([research-tasm-to-sjasmplus.md](research-tasm-to-sjasmplus.md))
  - [ ] TASM 4.12 `.PAGE` / `.RUN`; its compressed HyperText manual (in `tasm.ovl`)
  - [ ] the `tasm` detector takes some code files (type C, start #8000) for TASM text: weigh the catalog type and start
  - [x] TASM 5.x codec versions `5.0` / `5.5` (structural lines, both beta builds' tables, the editor's layout), the 5.0 assembler checked in the emulator (2026-10-06, [research-tasm-to-sjasmplus.md](research-tasm-to-sjasmplus.md) §5)
  - [ ] TASM 5.5 directives (IF / MACRO / REPT ...): meaning unknown, its beta assembler refuses them
  - [ ] a frontend for the M80-style text dialect of PC cross assemblers (`*D-`, `*Z80` option lines, `NAME$` local labels)
  - [x] `storm` frontend, STORM 1.3 (STORM → sjasmplus) (2026-10-06, branch `unreal-asm-a6`): STORM 1.3's own source rebuilt equal to the released program, two test programs equal to what STORM 1.3 built in the emulator; codec fix: `PO PE P M` imply `JP` ([research-storm-to-sjasmplus.md](research-storm-to-sjasmplus.md))
  - [x] `zxasm` frontend, ZX-ASM 2.x / 3.x / ZAsm (ZX-ASM → sjasmplus) (2026-10-06, branch `unreal-asm-a6`): four programs equal to what ZAsm 3.15 built in the emulator; project-wide macros (`ParseInProject`), ZX-ASM's `IFUSED`; detector fixes in tasm / zxasm ([research-zxasm-to-sjasmplus.md](research-zxasm-to-sjasmplus.md))
  - [ ] ZX-ASM `MAKE`, `~text~` with `LOADTAB`, `ENDA`
  - [ ] ALASM: a label `-` in column 0 (sources use it several times per file: an anonymous label?); research and convert
  - [ ] ALASM `DISPLAY` output differs from sjasmplus' (research-alasm-to-sjasmplus.md §7)
- [ ] A6 more frontends / backends; research codecs xas, masm, gens3, zeus, ads
  - [x] `pasmo` backend (2026-10-06, branch `unreal-asm-a6`): the oracle programs and the GS ROM equal through pasmo; `crosscheck.py` 0 differences against sjasmplus on the collection's disks ([research-pasmo-backend.md](research-pasmo-backend.md))
  - [x] `z88dk` (z80asm) backend over the writer shared with pasmo (2026-10-06): the oracle programs equal; `crosscheck.py --targets z88dk` 329 equal on the collection, z80asm's limits in [research-z88dk-backend.md](research-z88dk-backend.md) §3
  - [x] research of the XAS, MASM, GENS / Devpac and ZEUS formats (2026-10-06): every corpus file byte-exact with reference codecs, editors checked in the emulator; ADS is a disk utility whose sources are ZEUS beta 1.1 files; materials in the collection
  - [x] codec `masm`, versions 1.0 demo / 1.1 / 2.0 / 3.0 (2026-10-06): MASM 1.1's own source and typed files of every version byte-exact, canonical 100 %; testdata `masm/` ([research-masm.md](research-masm.md))
  - [x] codec `gens`, versions GENS1 / GENS2-4 (2026-10-06): five real sources and GENS3 / GENS4 saves byte-exact, the editor's compression reproduces every real and typed line; testdata `gens/` ([research-gens.md](research-gens.md))
  - [ ] GENS tape files: P blocks and T's multi-block include files need a tape (TAP / TZX) container
  - [x] codec `zeus`, versions 1983 / GG / PHT (2026-10-06): ADS 2.0 sources (the "ads" format), ZEUS v7.E help, ZXDB Zeus Routines and a typed probe byte-exact, tokenizer 4062 / 4062 lines; testdata `zeus/` ([research-zeus.md](research-zeus.md))
  - [ ] codec `xas` (4.18 … 9.10)
- [ ] A7 emulator adapters and surfaces, Qt disk browser, recipe
- [ ] A8 symbols on the library (symbols S1-S5)
- [ ] A9 benchmarks, user docs
- [ ] P2: memory bridge B0-B6 ([memory-bridge.md](memory-bridge.md)), design only for now
