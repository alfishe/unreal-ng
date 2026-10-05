# Symbol exchange — TODO

**Status:** design written 2026-10-05 (branch `symbols-design`); proposals P-1…P-7 wait for the owner. No code.
PLAN.md row **#97**.

## Owner decisions

- [ ] P-1 module in `core/src/debugger/symbols/`, std-only core + emulator adapters
- [ ] P-2 native file `*.usym.json`
- [ ] P-3 `LabelManager` stays the facade
- [ ] P-4 default merge policy `both` + conflict report
- [ ] P-5 tokenized formats researched first, one document per assembler
- [ ] P-6 live scans on request only
- [ ] P-7 ROM bundles by SHA-256 through `data/symbols/manifest.json`

## Remaining

- [ ] Review round 1: requirements, architecture, formats, TDD, test plan
- [ ] S1 model, index, store, merge, native JSON, std-only build check
- [ ] S2 existing five formats + `unreal-l` into the registry; `LabelManager` facade (existing tests unchanged)
- [ ] S3 tokenizer; sjasmplus `.sym` / `.sld` / `.lst`; pasmo (golden files from the real tools)
- [ ] S4 export side, name rules; IDA, Ghidra, MAME, CSpect; `tools/symbols/symconv`
- [ ] S5 surfaces (WebAPI + OpenAPI, CLI, MCP, Lua, Python, Qt), bundles + manifest, recipe `.recipe/analysis/symbols-import-export.md`
- [ ] S6 ALASM: research document, corpus, file importer, live scanner (closes E7 together with S7)
- [ ] S7 XAS: research, importer, scanner
- [ ] S8 STORM, GENS, MASM, ZX ASM, STS (research each first)
- [ ] S9 benchmarks and results table; user docs `docs/features/symbols.md`
- [ ] Verify every **verify** row of [formats.md](formats.md) against the real tool before its phase
