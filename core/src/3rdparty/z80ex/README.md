# z80ex (vendored)

Lightweight callback-driven Z80 CPU core, used as the coprocessor engine of the
General Sound (GS) sound card device (`SoundChip_GeneralSound`).

- **Source:** https://github.com/alfishe/z80ex (v0.16 lineage, GPL v2 — see
  `LICENSE` compatibility note below)
- **Upstream author:** Pigmaker57 aka boo_boo; the alfishe fork carries the
  build/fix changes the GS design reviewed against
  (`docs/inprogress/2026-09-19-general-sound/gs-tdd.md` §4.3)
- **Vendored files:** `z80ex.cpp` (single compilation unit — it `#include`s
  `ptables.c` and `opcodes/*.c`, none of which compile standalone),
  `macros.h`, `typedefs.h`, `z80ex.h`, `z80ex_common.h`, plus the disassembler
  pair `z80ex_dasm.c` / `opcodes_dasm.c` (not compiled today; kept for the
  future GS debugger window).

## Local patches

Applied on top of the upstream tree so the code builds under the core
project's `-Wall -Wextra -Werror` C++ compilation (the project enables CXX
only; C sources are renamed to `.cpp` — same precedent as `3rdparty/blip_buf`):

1. `z80ex.c` -> `z80ex.cpp`. Comment header added; the version macros
   (`Z80EX_API_REVISION`, `Z80EX_VERSION_*`, `Z80EX_RELEASE_TYPE`,
   `Z80EX_VERSION_STR`) that upstream injects via Makefile `-D` flags are
   defined in the file with `#ifndef` guards.
2. `macros.h`: `TSTATES()` loop counter changed from `int nn` to
   `unsigned nn_` (fixes `-Wsign-compare`; the counter is macro-local).

`typedefs.h` already exposes the full `_z80_cpu_context` layout (upstream
ships it that way), which the GS device uses for its TTD state serialization.

## License

GPL v2 (upstream). The emulator links z80ex into `libcore.a`; the project's
`THIRD_PARTY_NOTICES.md` carries the attribution entry.
