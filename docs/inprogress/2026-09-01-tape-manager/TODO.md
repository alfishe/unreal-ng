# TODO — Tape Manager (2026-09-01)

**Status:** mostly complete — P1/P1b/P2/P4/P5/P6 landed; only CSW support and
P7 polish remain (verified against code 2026-09-16).

## Progress
- **P1 + P1b** — unified tape block model, loader contract/registry with
  content probes, fast-load eligibility pre-analysis (design r4 records
  as-built deviations).
- **P2 TZX rewrite** — `loader_tzx.cpp` grew from a 231-line stub to 1568
  lines of block dispatch (+ `writer_tzx.*`); `.tzx` fixtures in
  `testdata/loaders/` since 2026-09-03.
- **P4 position & seek** — `Tape::GetPosition` / `SeekToBlock` /
  `RewindToStart` in `core/src/emulator/io/tape/tape.{h,cpp}`.
- **P5 control planes** — tape catalog/seek/rewind endpoints across
  WebAPI/CLI/MCP/Lua/Python (`tape_disk_api.cpp`).
- **P6 Qt Tape Manager window** — `unreal-qt/src/tape/tapemanagerwindow.*`
  (732 lines) + block table model; done 2026-09-03 (r6), zero warnings,
  suite green.
- Related shipped tracks: fast tape loading
  ([`../2026-08-30-fast-tape-loading/DONE.md`](../2026-08-30-fast-tape-loading/DONE.md)),
  turbo tape
  ([`../2026-09-04-turbo-tape-loading/DONE.md`](../2026-09-04-turbo-tape-loading/DONE.md)),
  tape audio bridge
  ([`../2026-09-04-tape-audio-bridge/DONE.md`](../2026-09-04-tape-audio-bridge/DONE.md)).

## Remaining
1. **P3 CSW v1/v2 loader** (+ optional PZX) — pulse decode, pseudo-block
   splitting, `LoaderCSW_Test`, zlib linkage spike (R7); `.csw` fixtures still
   pending under `testdata/loaders/`.
2. **P7 polish** — finish `tr()` coverage / translation file, move these docs
   out of `inprogress/` per the README lifecycle.
3. §8.3 visual-checklist leftovers from r6 (minor UI nits).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — tape manager leftovers (T3).
