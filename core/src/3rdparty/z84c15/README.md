# z84c15 (vendored fork)

The Zilog Z84C15's CPU for the Peters Plus Sprinter: a CMOS Z84C00 core with
the chip's on-chip block (wait-state generator, chip selects, MCR, watchdog,
CTC / SIO / PIO with their interrupt daisy chain, the fixed ports). Only the
Sprinter uses it; every other machine runs on the native interpreter in
`core/src/emulator/cpu/`. Design:
`docs/inprogress/2026-10-01-z84c15-cpu-library/design.md`; the chip facts:
`docs/inprogress/2026-09-28-sprinter/research-cpu-z84c15.md`.

- **Origin:** forked from unreal-z80 commit `a0433ec` (library version 0.5.0),
  the same version vendored at `core/src/3rdparty/unreal-z80/` for the General
  Sound card. MIT license - see `LICENSE`. `Z84CpuVersion()` reports
  `0.5.0-z84c15.2`.
- **Public headers:** `z84cpu.h` (the core, C API, prefix `Z84Cpu`) and
  `z84c15.h` (the on-chip block, C++ class `Z84Lib::Z84C15`). Nothing outside
  this folder includes anything else.

## Local changes against unreal-z80 0.5.0

| Change | Files | Why |
|:--|:--|:--|
| C API prefix `Z80Cpu` -> `Z84Cpu`, type `Z80CPU` -> `Z84CPU`, `Z80Regs` -> `Z84Regs`, private namespace `Z80Lib` -> `Z84Lib`, bus namespace `Z80Cb` -> `Z84Cb`, macros `Z80*` -> `Z84*`, files `z80*` -> `z84*` | all | links beside unreal-z80 and the native core without a clashing symbol or type |
| Only the callback bus: `opcodes-flat.cpp`, `opcodes-paged.cpp`, `Z80CpuAttachMemory`, `Z80CpuAttachPageTables`, `Z80CpuSyncPageTables` and the page macros removed | `z84cpu.h`, `z84cpu.cpp`, `z84cpu-internal.h`, `z84cpu-dispatch.h` | the host owns memory (paging, the board's /WAIT); the wait generator is written once |
| The memory read callback gets `Z84CpuAccessKind` (`M1`, `Operand`, `Read`) instead of a 0/1 flag | `z84cpu.h`, `opcodes-callback.cpp`, `z84cpu.cpp` | the host and the wait generator tell an opcode fetch from an operand fetch |
| The clock is taken back from `cpu->t` after every memory / port callback; `Z84CpuAddWaitStates` | `opcodes-callback.cpp`, `z84cpu.cpp` | the board's external /WAIT, added from the callback |
| A halted CPU reads the byte after the HALT as an M1 every quantum (PC stays on the HALT) | `opcodes-callback.cpp`, `z84step.inc` | the board sees (and stretches) those M1 cycles |
| The INT / NMI acknowledge's pushes and IM2 vector reads go through the memory callbacks (no raw access) | `z84cpu.cpp` | the board's /WAIT logic sees them; only the callback bus exists |
| CMOS core: `OUT (C),0` writes `#FF` by default; no LD A,I / LD A,R P/V quirk in `Z84CpuInt` | `z84cpu.cpp`, `z84cpu.h` | the Z84C00 (research section 3) |
| The wait generator (`Z84WaitGen` in `Z84CPU`, `z84waits.cpp`) in the memory, M1, I/O, INTA, NMI and halted cycles | `z84cpu-internal.h`, `opcodes-callback.cpp`, `z84cpu.cpp`, `z84waits.cpp` | the chip's programmed waits (research section 4.1) |
| The chip's RETI watcher before the host's RETI callback | `opcodes-ed.inc`, `z84cpu-internal.h` | the on-chip daisy chain |
| `Z84C15::SaveState` / `LoadState` (`kStateSize` bytes, fixed little-endian layout), `Z84Ctc::SetVector` | `z84c15.h`, `z84c15.cpp` | time travel and snapshots: the chip's state beside the register file (Sprinter S7) |
| The CTC's counter mode and CLK/TRG inputs (`Z84Ctc::SetTrigger`: none, a fixed-frequency clock in real time, a lower channel's ZC/TO), timer trigger start (control bit 3), a time constant written while counting loads at the next zero, a software reset keeps the count; the clock is a time base of the owner's choice with the CPU clock's length in it (`SetSystemClockPeriod`, folded at a speed change; the watchdog too); lazy: counts, zero counts and the next interrupt are derived when read or polled (`Poll` is one clock read and a compare until a zero is due) | `z84c15.h`, `z84ctc.cpp`, `z84c15.cpp` | the Sprinter's playback tick: TRG2 875 kHz, ZC/TO2 -> TRG3, channel 3 interrupting at 48.83 Hz (Bad Apple, dontBlink); ZC/TO0 as SIO B's baud clock |
| A halted CPU's idle cycles in one go: `Z84CpuIdleM1Repeats` (the wait generator is unchanged by another idle M1), `Z84Ctc::NextDue`, `Z84C15::Clock` / `NextEventClock` (the earliest clock the chip acts on its own) | `z84cpu.h`, `z84cpu.cpp`, `z84c15.h`, `z84c15.cpp` | the host's HALT fast-forward (unreal-ng `Z84C15Engine::RunIdleCycles`): a halted Sprinter demo spends 82 % of its steps in idle M1 cycles |
| New: `z84c15.h`, `z84c15.cpp`, `z84ctc.cpp`, `z84sio.cpp`, `z84pio.cpp` | | the on-chip block; the CTC / SIO / PIO models moved here from unreal-ng's `core/src/emulator/io/z84c15/` (Sprinter S1) and gained their interrupts |

The opcode bodies (`opcodes-*.inc`) differ from unreal-z80 only by the renames
and the RETI hook, so upstream fixes still apply by diff.

## Build notes

The `.cpp` units are picked up by the core's source glob and compiled without
the `stdafx.h` precompiled header (`core/src/CMakeLists.txt`): the library is
self-contained C++17. Tests: `core/tests/3rdparty/z84c15/`.

## License

MIT (compatible with the project's GPL v3); attribution in
`THIRD_PARTY_NOTICES.md`.
