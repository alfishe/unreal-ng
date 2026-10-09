# ZX Spectrum Next: the Z80N CPU

**Date:** 2026-10-07 · part of [README.md](README.md) · opcodes in [research-z80n.md](research-z80n.md)

The pattern is the Z84C15's ([library design](../2026-10-01-z84c15-cpu-library/design.md), shipped as
`core/src/3rdparty/z84c15/` and `core/src/emulator/io/z84c15/z84c15engine.*`). The Next copies it and changes as
little as possible. **The base Z80 and the native interpreter are not touched.**

## 1. The library `core/src/3rdparty/unreal-next-z80/`

| Item | Rule |
|:--|:--|
| Origin | a fork of the **z84c15 core** (`core/src/3rdparty/z84c15/`, itself unreal-z80 0.5.0, MIT) without the chip, so the interface is the one the Sprinter adapter already uses; flat like the originals; its own `README.md` listing every local change and a row in `THIRD_PARTY_NOTICES.md`. **Implemented 2026-10-08** (first pass: all 29 instructions of the table, NEXTREG callback, stackless NMI; 29 + the core tests) |
| Renames | C API prefix `Z80n` (`Z80nCpu*`, type `Z80nCPU`), private namespace `Z80nLib`, files `z80n*`, so it links beside unreal-z80 (GS card), z84c15 and the native core with no symbol clash (the z84c15 README table is the checklist) |
| Bus | callback bus only: the host owns memory (slot table, overlays, DMA, contention) |
| New | the `ED`-page extension in `opcodes-ed.inc`: the entries of research-z80n.md; callbacks `Z80nCpuSetNextRegWrite(reg, value)` and `Z80nCpuSetNextRegRead(reg)` for `NEXTREG` (no port cycle: the hook is called, the host does not see an `OUT`), and `Z80nCpuSetStacklessNmi(bool)` + a `RETN`-seen callback for NR `#C0` bit 3 (MAME has the same two) |
| Timing | the library reports T-states in CPU clocks; the adapter turns them into the machine time base (section 3) |
| Variant | the T80-based hardware CPU is not an NMOS Z80: the library keeps unreal-z80's NMOS core first and records each behavior where the real core differs (undocumented flags, `LD A,I` P/V quirk, `OUT (C),0` value) in `README.md`. Which differences the Next shows is open (Q12), found by the FUSE/z80test style suites run against the library and by ZXSpectrumNextTests |
| Tests | the library has its own `core/tests/` target-independent tests: every Z80N opcode (table-driven), flags, T-states from the wiki, the `LDIX` family flag rule (jnext's citation of `t80n.vhd`: S, Z, C preserved; H = N = 0; P/V = BC != 0; X/Y from the sum), `NEXTREG` without a port cycle, and the base-Z80 suites (ZEXALL style) to show nothing regressed from the fork |
| Upstream fixes | because opcode bodies differ from unreal-z80 only by the renames and the additions, upstream fixes apply by diff (as for z84c15) |

Why not extend `core/src/emulator/cpu/z80.cpp`: a Z80N opcode in the native interpreter would add a decode case,
a flag rule and a hook in code every machine runs at 100+ million steps per second; the engine seam makes the
Next pay and nobody else.

## 2. The adapter `Z80NEngine`

`core/src/emulator/io/z80n/z80nengine.{h,cpp}`: the same job table as `Z84C15Engine` (registers attached zero-copy
via `Z80nCpuAttachRegisterFile`; time `tt` taken from and returned to the library around every callback; M1 /
memory / port callbacks through the machine's existing paths; boundary state synchronized around every step and
acknowledge; INT through the machine's `IInterruptSource`; RETI/RETN observed), plus:

| Job | How |
|:--|:--|
| `NEXTREG` | the library calls `NextRegs::Write(reg, value, fromCpu)` directly; the register write may rebuild the slot table or change the CPU speed in the middle of the instruction, so the adapter re-reads `tt` and the speed after the callback |
| Memory | `NextMemory` interface read/write; the bus overlays (DivMMC automap, Multiface, Layer 2 mapping) are folded into the slot table, so the callbacks see one pointer lookup |
| DMA hold | before each instruction `machineStep` runs the DMA if it owns the bus (section 5); the CPU simply does not step while the DMA transfers |
| Opcode fetch hook | `PortDecoderNext::OnM1(addr)` for DivMMC automap and the NMI-return detection, as `OnMachineM1` is used elsewhere |

Selection: `PortDecoderNext` installs the engine at init with `Z80::SetEngine` and removes it on destruction. The
isolation test (shared code names no model) is extended to `MM_NEXT`.

## 3. Speed and time

| Item | Rule |
|:--|:--|
| Speeds | NR `#07` bits 1:0: 0 = 3.5, 1 = 7, 2 = 14, 3 = 28 MHz (nextreg.txt). MAME scales the CPU, DMA and IM2 devices by `1 << speed` from a 28 MHz / 8 base |
| Mechanism | `EmulatorState::hw_turbo_ratio` = 1, 2, 4, 8 and `Z80::ApplyHardwareTurboNow()` on the write, exactly as Scorpion's turbo; the frame length in CPU T-states scales and the raster/INT instant is preserved |
| Machine time | the video, copper, CTC and audio use the 3.5 MHz-equivalent frame position `framePos = T / ratio` (the video clock does not speed up). The T-state counters the debugger shows (`t_state`) stay in CPU clocks |
| Hotkeys | F8 cycles the speed when NR `#06` bit 7 is set; F3 toggles 50/60 Hz when bit 5 is set (nextreg.txt). The host speed control (1x, 2x, ...) is separate and unchanged |
| Contention | at 3.5 MHz the ULA contends (48K / 128K / +3 patterns by machine timing); NR `#08` bit 6 disables it; at 7, 14 and 28 MHz there is **no contention** (VHDL, [research-fpga-vhdl.md](research-fpga-vhdl.md) section 2). At 28 MHz SRAM reads add one wait state (`sram_wait_n`, [research-fpga-vhdl.md](research-fpga-vhdl.md) section 7) |
| Wait states other than contention | the DMA burst, SPI and expansion bus have none in the sources read |

## 4. Interrupts and NMI

| Item | Rule |
|:--|:--|
| Source | `NextInterruptSource : IInterruptSource` ([design-peripherals.md](design-peripherals.md) section 2): 14 sources in hardware IM2 mode with priority, or the classic pulse mode (a 32-cycle-ish pulse; the width is N-read from the VHDL, jnext's comments mention a 32/36-cycle pulse window) |
| Modes | NR `#C0` bit 0 = pulse (0) or hardware IM2 (1); vector bits 7:5; the CPU's own `IM` mode still applies |
| RETI | the engine's RETI callback goes to the IM2 controller to clear the highest source under service (the daisy-chain behavior of MAME's `specnext_im2`) |
| NMI | two button sources: the Multiface M1 button (NR `#06` bit 3 enables) and the DivMMC DRIVE button (bit 4); both set the corresponding hardware (Multiface page-in, DivMMC `NMI` mapping) and request an NMI. Stackless mode (NR `#C0` bit 3) saves the return address in NR `#C2`/`#C3` instead of the stack and `RETN` jumps there |
| TTD | the NMI request and the buttons are journaled inputs ([design-ttd.md](design-ttd.md)) |

## 5. DMA as a bus agent

The zxnDMA takes the bus (BUSREQ) between instructions. The adapter's `machineStep` hook asks the DMA whether it
owns the bus; if yes it runs its transfer in 28 MHz-tick units, advancing the machine time exactly as the CPU
would have, and returns when the DMA gives the bus back (end of burst or byte, or interleaved prescaler waits in
burst mode). The Sprinter's accelerator is the in-tree precedent for "something else owns the bus"; this adds no
instruction to shared code.

## 6. Debugger, disassembler, assembler

| Surface | Work |
|:--|:--|
| Disassembler | the Z80N entries added to the shared disassembler table behind a "Z80N" flag set for `MM_NEXT` (zero cost for others: a different table pointer, selected at model init) |
| `unreal-asm` | a `ZXN` instruction-set option exists in sjasmplus-class assemblers; whether `unreal-asm` has it is checked in N12 (Q10) |
| Breakpoints, step, call trace | unchanged: the engine seam keeps `RunInstructionStartHooks` |
| Opcode profiler | the profiler counts `ED xx` in its own table; extra entries for Z80N in N1 |
| DeZog | `dzrptypes.h` already has `ZXNEXT = 4` and bank/sprite commands ([design-automation.md](design-automation.md)) |
| Magic breakpoints | CSpect `DD 01` and ZEsarUX `ED FF` as jnext offers: an option `[NEXT] MagicBreakpoint=` (off by default); in N12 |

## 7. Performance

The gate is `BM_HostFrame_*` on Pentagon and 48K before and after the registration of the engine selection (the
only shared-file change), per the performance guidelines. A Next-specific benchmark (`BM_HostFrame_Next_3_5MHz`,
`_28MHz`) is added in N2 and N5 to catch regressions in the Next's own path; no absolute target is set before the
first measurement ("naive first, measure later").
