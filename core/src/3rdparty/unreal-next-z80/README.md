# unreal-next-z80 (vendored fork)

The ZX Spectrum Next's CPU: a Z80 core with the **Z80N** extended instructions, as a standalone library that is meant to be
extracted into its own repository and re-vendored later. Design and the reasoning:
`docs/inprogress/2026-10-07-zx-next/design-integration.md` and `design-cpu.md`. The machine that uses it (memory slots,
NextREG file, DMA, contention, interrupts) is not here: the library calls the host.

- **Origin:** a fork of the `z84c15` core (`core/src/3rdparty/z84c15/`, itself a fork of unreal-z80 0.5.0 at commit
  `a0433ec`), **without** the Z84C15's on-chip block. It keeps that fork's interface: callback bus only, the read callback gets
  the bus-cycle kind, the clock is taken back after every callback, a halted CPU reads the byte after the HALT, INT / NMI
  acknowledge through the callbacks. A host adapter written for `z84c15` is written for this library with the prefix renamed
  (`Z84Cpu*` -> `Z80nCpu*`). MIT license - see `LICENSE`. `Z80nCpuVersion()` reports `0.5.0-z80n.1`.
- **Public header:** `z80ncpu.h` (C API, prefix `Z80nCpu`). Nothing outside this folder includes anything else.
- **Re-deriving:** `tools/machines/next/z80n-fork/forkz80n.py` copies the core files of the z84c15 fork under the new names (the renames
  below); the hand edits of the next table are then redone. Upstream fixes to unreal-z80 reach this library through the z84c15
  fork.

## Renames against the z84c15 core

C API prefix `Z84Cpu` -> `Z80nCpu`, type `Z84CPU` -> `Z80nCPU`, `Z84Regs` -> `Z80nRegs`, namespaces `Z84Lib` / `Z84Cb` ->
`Z80nLib` / `Z80nCb`, macros `Z84*` -> `Z80N*`, files `z84*` -> `z80n*`. The library links beside unreal-z80, z84c15 and the native
core without a clashing symbol.

## Local changes against the z84c15 core

| Change | Files | Why |
|:--|:--|:--|
| The chip is gone: no `Z80nC15` class, no CTC / SIO / PIO, no wait generator (`z80nwaits.cpp`, `Z80nWaitGen`), no daisy-chain RETI watcher | `z80ncpu.cpp`, `z80ncpu-internal.h`, `opcodes-callback.cpp`, `opcodes-ed.inc` | the Next has none of it; waits (contention, the 28 MHz SRAM wait) come from the host through `Z80nCpuAddWaitStates` and the contention hook |
| NMOS baseline instead of CMOS: `OUT (C),0` writes 0, the `LD A,I` / `LD A,R` + INT quirk is on; `Z80nCpuSetLdAirQuirk(cpu, 0)` and `Z80nCpuSetOutC0Value` switch them | `z80ncpu.cpp`, `z80ncpu.h`, `z80ncpu-internal.h` | the Next's core is a T80, not a Zilog die; which behaviors it shows is found with the Next's test programs (design `requirements.md` Q12) |
| The Z80N instructions: `opcodes-z80n.inc` (new) and 30 entries in the ED table | `opcodes-z80n.inc`, `opcodes-ed.inc`, `opcodes-callback.cpp` | see the table below |
| `NEXTREG` through `Z80nCpuSetNextRegFn` (no port cycle) | `z80ncpu.h`, `z80ncpu.cpp`, `opcodes-callback.cpp` | the host owns the NextREG file |
| Stackless NMI: `Z80nCpuSetStacklessNmi` (the NMI acknowledge moves SP but writes no memory, the address goes to the host; the next RETN reads it back) | `z80ncpu.h`, `z80ncpu.cpp`, `opcodes-ed.inc` | NextREG `#C0` bit 3, `#C2` / `#C3` |
| ED `5D`, `6D`, `7D` are RETN (as `45`, `55`, `65`, `75`); only `4D` is RETI | `opcodes-ed.inc` | the FPGA's microcode (`t80n_mcode.vhd`) |

## The Z80N instructions

Sizes and T-states: https://table.specnext.dev/ (the table is also stored in the zx-next collection). Bus shapes and flags: the
FPGA core's T80 microcode (`t80n_mcode.vhd`, `t80n.vhd`). Where the two disagree the code follows the VHDL and a test pins it.

| Opcode | Instruction | T | Notes |
|:--|:--|--:|:--|
| `ED 23` | `SWAPNIB` | 8 | |
| `ED 24` | `MIRROR A` | 8 | |
| `ED 27 n` | `TEST n` | 11 | flags as AND, A unchanged |
| `ED 28-2C` | `BSLA` / `BSRA` / `BSRL` / `BSRF` / `BRLC DE,B` | 8 | count B bits 4:0 (BRLC: 3:0); no flags |
| `ED 30` | `MUL D,E` | 8 | DE = D * E; no flags |
| `ED 31-33` | `ADD HL/DE/BC,A` | 8 | **carry cleared** (the table: "carry is not preserved"); VHDL: the carry is assigned from a bit that is never set |
| `ED 34-36 nn` | `ADD HL/DE/BC,nn` | 16 | each operand cycle carries an idle T; no flags |
| `ED 8A hh ll` | `PUSH nn` | 23 | operand big-endian; 3 idle T at SP, then the writes |
| `ED 90` | `OUTINB` | 16 | OUT (C),(HL); HL++; B and flags unchanged. The VHDL sets a repeat bit that depends on Z; the real-board test treats it as a single step, so does the library |
| `ED 91 n v` / `ED 92 n` | `NEXTREG n,v` / `NEXTREG n,A` | 20 / 17 | no port cycle; the host's callback at the cycle where the core writes |
| `ED 93` / `ED 94` / `ED 95` | `PIXELDN` / `PIXELAD` / `SETAE` | 8 | |
| `ED 98` | `JP (C)` | 13 | PC = (PC & #C000) + (IN (C) << 6), PC already past the instruction |
| `ED A4` / `ED AC` | `LDIX` / `LDDX` | 16 | copy unless the byte equals A; flags as LDI (the table lists none; the VHDL sets them) |
| `ED B4` / `ED BC` | `LDIRX` / `LDDRX` | 21 repeat, 16 last | PC rewinds, interrupts can come between elements; X / Y from the sum, not from PC |
| `ED B7` | `LDPIRX` | 21 / 16 | source = (HL & #FFF8) + (DE & 7); HL does not move |
| `ED A5` | `LDWS` | 14 | (DE) = (HL); L++; D++; flags as INC D |
| `ED B6` | `LDIRSCALE` | = LDIRX | decoded by the FPGA, the scaling commented out in the VHDL |

## Tests

`core/tests/3rdparty/unreal-next-z80/`: `z80ncpu_test.cpp` (the FUSE vectors with NMOS expectations, the two variant switches, the
bus-cycle kinds, external wait, halted M1, attached register file, version) and `z80nopcodes_test.cpp` (the whole instruction table's
sizes and T-states, every instruction's semantics and flags, NEXTREG, the stackless NMI, the RETN aliases). A table-driven
sizes-and-T-states test is the gate for the instruction table.

## Build notes

The `.cpp` units are picked up by the core's source glob and compiled without the `stdafx.h` precompiled header
(`core/src/CMakeLists.txt`): the library is self-contained C++17.

## License

MIT (compatible with the project's GPL v3); attribution in `THIRD_PARTY_NOTICES.md`.
