# DONE — Z80 hidden flags: MEMPTR/WZ research & implementation (2026-01-18)

**Status:** complete.

## What landed
- The `z80-memptr.txt` research and `z80-xcf-proposal.md` were implemented: MEMPTR (WZ)
  is a first-class CPU register, set per the documented rules, observable through the
  BIT n,(HL) flag behavior, and serialized in snapshots/TTD checkpoints.

## Evidence
- `core/src/emulator/cpu/z80.h` (MEMPTR documentation block + member),
  `core/src/emulator/cpu/z80.cpp` (per-instruction updates incl. interrupt paths),
  `core/src/debugger/ttd/ttdcheckpoint.h` (memptr serialization with static_asserts),
  XCF-flavor validation material kept in this folder.

## Follow-ups
- Block-interrupt proposal folded into TTD interrupt handling; reference status tracked
  in [2026-01-18-z80-tests](../2026-01-18-z80-tests/) (done).
