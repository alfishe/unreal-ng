# unreal-asm — TODO

**Status:** design 2026-10-05 (branch `symbols-design`). Decided: D-1 one codec per format, decode and encode;
D-2 nothing vendored; D-3 the library `unreal-asm` in `core/src/3rdparty/unreal-asm`, symbols are one consumer;
D-4 codecs and format conversion first, dialect conversion plugins after. No code. PLAN.md row **#97**.

## Open questions (one at a time)

- [ ] Q-1 the first codecs (proposed: TASM 3 + 4)
- [ ] Q-2 plugin form (proposed: compiled-in modules, one registry)
- [ ] Q-3 the dialect the IR is closest to (proposed: sjasmplus)
- [ ] Q-4 the first dialect pair (proposed: ALASM → sjasmplus)
- [ ] Q-5 host encoding of decoded text (proposed: UTF-8, original code page recorded)
- [ ] Q-6 CLI name (proposed: `zxasm`)
- [ ] symbol module proposals P-2 … P-7 ([symbols/goals-and-requirements.md](symbols/goals-and-requirements.md) §3.2)

## Phases ([tdd.md](tdd.md) §8)

- [x] Prior-art survey, local and public ([prior-art.md](prior-art.md))
- [ ] A0 review round 1 of every document
- [ ] A1 skeleton, document model, code pages, `text` codec, registry, detection
- [ ] A2 `tasm3`, `tasm4` (research first), sub-version conversion
- [ ] A3 `alasm4`, `alasm5`
- [ ] A4 `storm`, `zxasm`
- [ ] A5 IR, transforms, sjasmplus frontend + backend, alasm frontend (ALASM → sjasmplus)
- [ ] A6 more frontends / backends; research codecs xas, masm, gens3, zeus, ads
- [ ] A7 emulator adapters and surfaces, Qt disk browser, recipe
- [ ] A8 symbols on the library (symbols S1-S5)
- [ ] A9 benchmarks, user docs
