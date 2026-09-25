# TODO — Z80 core upgrade

## Done
- `unreal-z80` vendored into `core/src/3rdparty/unreal-z80/` (0.2.0, MIT, no
  local patch), `Z80::Step()`'s opcode-execution seam delegates to it, public
  `Z80` API/struct layout unchanged.
- `testdata/z80/fuse/tests.expected` corrected for the four
  INIR/INDR/OTIR/OTDR repeat-MEMPTR cases (silicon-verified value); documented
  in `core/tests/emulator/cpu/fuse_phase_test.cpp`.
- Full verification gate green: zero build warnings, full `core-tests` suite
  passing (3296/3296, same 2 pre-existing skips as baseline).

## Remaining
- **Awaiting explicit "commit" instruction from the user** (per `AGENTS.md`/
  `CLAUDE.md` — no blanket commit permission). Nothing has been committed.
- Optional follow-up, not required for correctness: recover the ~1.3-1.4x
  CPU-bound slowdown from the per-instruction register-file copy (see
  "Performance" in [patches.md](patches.md)) — would need the engine to
  become the sole owner of the register file.
- Optional cleanup: the old interpreter files (`op_*.cpp`, `cpulogic`,
  `cputables`, `daa_tabs`) are still compiled but no longer called from
  `Z80Step()`. Deleting them is a separate, deliberate follow-up, not bundled
  into this change.

Once committed, flip this folder's marker to `DONE.md`.
