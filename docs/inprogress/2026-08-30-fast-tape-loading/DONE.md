# DONE — Fast tape loading (ROM traps) (2026-08-30)

**Status:** complete (r4).

## What landed
- LD-BYTES trap-based instant loading per `design.md` r4: standard ROM loads complete
  near-instantly, decline matrix for everything else, custom-loader pause/resume
  lifecycle (insult.tap fix), feature `fasttape` with UI/CLI/WebAPI/MCP toggles
  (`fast_tape` io-acceleration setting).

## Evidence
- `core/src/emulator/io/tape/tapefastload.cpp`; `fasttape` feature in `FeatureManager`;
  `fast_tape` in `/settings` (cited by the 2026-09-14 gap analysis).

## Follow-ups
- Headerless blocks are served by turbo-tape ([2026-09-04-turbo-tape-loading](../2026-09-04-turbo-tape-loading/), done).
