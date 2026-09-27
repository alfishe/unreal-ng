# unreal-z80 (vendored)

Standalone Z80 CPU library, used as the coprocessor engine of the General
Sound (GS) sound card device (`SoundChip_GeneralSound`). It replaced the
vendored z80ex.

- **Source:** unreal-z80, commit `0001920` (library version 0.4.0,
  `Z80CpuVersion()`), MIT license - see `LICENSE`.
- **Origin:** extracted from this emulator's own Z80 core
  (`core/src/emulator/cpu`), with the verification the core lacks: the ZEX
  exerciser family and Patrik Rak's z80test pass, a lockstep differential
  against z80ex, byte-exact T-state traces, an interrupt/HALT/contention
  harness.
- **Vendored files:** the whole library, flat: `z80cpu.h` (public C API) and
  the sources from `z80lib/src` (`*.cpp` compile units, `*.inc` opcode
  bodies included by the three bus builds, private headers). Nothing
  outside this folder includes anything but `z80cpu.h`.

## Local patches

None. Update by copying `z80lib/include/z80cpu.h` and `z80lib/src/*` from a
newer unreal-z80 commit and noting the commit and version here.

## Build notes

The `.cpp` units are picked up by the core's source glob and compiled without
the `stdafx.h` precompiled header (`core/src/CMakeLists.txt`): the library is
self-contained C++17 and keeps its internal names in namespace `Z80Lib`, so it
links beside the emulator's own Z80 core (which has same-named C-style tables).

## License

MIT (compatible with the project's GPL v3); attribution in
`THIRD_PARTY_NOTICES.md`.
