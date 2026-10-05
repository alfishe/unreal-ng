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

## Workflow (owner, 2026-10-05)

Design on master; development in a fresh worktree from master (branch `unreal-asm`); a commit after each green
phase; master only after the owner's review.
- [ ] symbol module proposals P-2 … P-7 ([symbols/goals-and-requirements.md](symbols/goals-and-requirements.md) §3.2)

## Phases ([tdd.md](tdd.md) §8)

- [x] Prior-art survey, local and public ([prior-art.md](prior-art.md))
- [ ] A0 review round 1 of every document
- [ ] A1 skeleton, document model, encoding detectors and code page tables (D-13), `text` codec, sjasmplus text codec (D-10), registry, detection
- [ ] A2 `tasm3`, `tasm4` (research first), sub-version conversion
- [ ] A3 `alasm4`, `alasm5`
- [ ] A4 `storm`, `zxasm`
- [ ] A5 IR, transforms, sjasmplus frontend + backend, alasm frontend (ALASM → sjasmplus)
- [ ] A6 more frontends / backends; research codecs xas, masm, gens3, zeus, ads
- [ ] A7 emulator adapters and surfaces, Qt disk browser, recipe
- [ ] A8 symbols on the library (symbols S1-S5)
- [ ] A9 benchmarks, user docs
