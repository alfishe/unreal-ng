# ZX Spectrum Next: how it plugs into unreal-ng

**Date:** 2026-10-08 · part of [README.md](README.md) · decision by the owner (2026-10-08): the Z80N goes into its own
vendored library `unreal-next-z80`, forked from unreal-z80, holding everything that is not a standard Z80; in the long
run the CPU is a **switchable library instance** (`z80`, `z84c15`, `z80next`) behind one interface.

This article answers one question: *where does the Next's code go, and what stays out of the shared code?* It
replaces the earlier working name `z80n` in the other documents of this folder. Related: [design-cpu.md](design-cpu.md)
(the Z80N itself), [design.md](design.md) (the whole machine), the shipped precedent
[z84c15 library](../2026-10-01-z84c15-cpu-library/design.md).

## 1. The rule

1. **Everything that is not a plain Z80 lives in a vendored CPU library**, never in `core/src/emulator/cpu/z80.*`.
   For the Next that is the Z80N instruction set, the `NEXTREG` instructions, the stackless NMI mode and the
   T-state table of the extended opcodes. The Sprinter's Z84C15 already follows this rule.
2. **Everything that is not a CPU lives in the machine**: memory slots, ports, NextREG file, DMA, copper, video,
   interrupts. The library has no idea what a NextREG is; it calls the host.
3. **Shared code names no model.** The same isolation test that guards TS-Conf and Sprinter forbids `MM_NEXT`,
   `Next*` and `Z80n*` tokens in shared files. `PortDecoderNext` installs the engine; nothing else knows about it.
4. **The other machines pay nothing.** The engine path is chosen by the per-step work word that every step already
   loads (the z84c15 seam, performance guidelines section 5); the base Z80 and its hot path do not change, so the
   N1 gate is an A/B benchmark that shows zero.

## 2. The library `core/src/3rdparty/unreal-next-z80/`

| Item | Rule |
|:--|:--|
| Name | directory `unreal-next-z80` (like `unreal-z80`; directory names are exempt from the file-name rule), flat layout like the original |
| Origin | a fork of `core/src/3rdparty/unreal-z80/` at the same commit the z84c15 fork used (library 0.5.0, MIT). `LICENSE` kept, a row in `THIRD_PARTY_NOTICES.md`, version string `0.5.0-z80n.1` |
| C names | prefix `Z80n` (`Z80nCpu*`, type `Z80nCPU`, `Z80nRegs`), private namespace `Z80nLib`, bus namespace `Z80nCb`, macros `Z80N*`, files `z80n*`. The z84c15 README table of renames is the checklist; the renames are a script (`tools/`), run again on every upstream merge |
| Bus | the callback bus only: no flat or paged memory, no `AttachMemory`. The Next's memory is an 8-slot table of 8K slots that the host rebuilds on any mapping change; a CPU library that owned page tables could not follow it |
| Read callback | gets the access kind (`M1`, `Operand`, `Read`) as in z84c15, so the host and the contention logic tell fetches from data |
| Clock | T-states in CPU clocks; the library takes the clock back from the host after every callback (`AddWaitStates`), so contention and the 28 MHz SRAM wait state are added from the callback |
| Added | the extended `ED` entries ([research-z80n.md](research-z80n.md); `LDIRSCALE` as `LDIRX`, [research-fpga-vhdl.md](research-fpga-vhdl.md) section 6); `NEXTREG n,A` and `NEXTREG n,nn` through `Z80nCpuSetNextRegWrite` (no port cycle: the host is called, no `OUT` is seen); `Z80nCpuSetStacklessNmi(bool)` plus the two NMI-return bytes (below); `OUTINB`, `JP (C)`, `TEST`, `PUSH nnnn`, `MUL D,E`, the barrel shifts, `PIXELAD`, `PIXELDN`, `SETAE`, `SWAPNIB`, `MIRROR A`, the `ADD rr,A` / `ADD rr,nnnn` forms |
| Stackless NMI | with the mode on, the NMI acknowledge does not push PC: the two bytes go to NR `#C2` / `#C3` (the host stores them, the library calls `Z80nCpuSetNmiSink`), and the matching `RETN` pops them from there instead of the stack ([research-fpga-vhdl.md](research-fpga-vhdl.md) section 12). Off by default (reset value of NR `#C0` bit 3) |
| Behavior variant | the library keeps unreal-z80's core as the baseline and has a variant switch like the z84c15 fork (`OUT (C),0` value, `LD A,I` P/V quirk, undocumented flag bits). The hardware CPU is the T80 core in `Mode => 0` (`cpu/t80na.vhd`), not a Zilog die; which quirks it shows is found with the block-instruction, CCF/SCF and interrupt-skip tests of ZXSpectrumNextTests (Q12), and each one is recorded in the library's `README.md` |
| Not in the library | DMA (a bus master on the machine), copper, contention, the IM2 daisy chain, the NMI sources (Multiface, DivMMC button, expansion bus): the host |

Upstream fixes keep applying by diff: the opcode bodies differ from unreal-z80 only by the renames and the added
entries, and the added entries live in their own `opcodes-z80n.inc` that `opcodes-ed.inc` includes.

## 3. One interface, three instances

Today the shared seam is `ICpuEngine` (`core/src/emulator/cpu/z80.h`): `ExecuteStep`, `AcknowledgeInterrupt`,
`AcknowledgeNmi`, installed with `Z80::SetEngine`. The engine runs on the machine's `Z80Registers` (zero copy, the
layout is pinned with `static_assert` in the adapter) and the machine's time. That is already "switchable instances":
the native interpreter is the default engine, `Z84C15Engine` the second, `Z80NEngine` the third.

Proposal, in three steps; only step 1 is part of the Next work:

1. **Same C API shape in all libraries (now).** The three libraries differ in prefix only: `Z80Cpu*`, `Z84Cpu*`,
   `Z80nCpu*`. The functions the adapters use are the same set (`...Init`, `...AttachRegisterFile`,
   `...Reset`, `...Step`, `...Interrupt`, `...Nmi`, `...SetCallbacks`, `...SetBoundary`), so an adapter for one
   differs from another only by the extras above. That is guaranteed by building the Next fork with the same
   rename script as z84c15 and by a test that compiles one trait-driven adapter against each library.
2. **One adapter template (with the Next).** `Z84C15Engine` and `Z80NEngine` share ~80 percent of their body: register
   attachment, time mapping around the callbacks, boundary sync, M1 handling, port routing through `Z80::in` / `out`.
   That body moves into a small header-only `LibEngine<Lib>` parameterized by a traits struct (a handful of function
   pointers and the extra-callback hook); the chip-specific parts (the Z84C15's daisy chain, the Next's NextREG and
   stackless NMI) stay in the two derived classes. No virtual call is added on the per-step path: the engine is
   already a virtual call per instruction, the template only removes the copy-paste.
3. **Retire the native interpreter (later, separate decision).** The `master-z80lib` experiment ran every machine on
   unreal-z80 through the same seam. If it is revived, the "native" core becomes just the `z80` instance and every
   model picks its instance from a field of the model table: `cpu: z80 | z84c15 | z80n`. The Next work does not
   depend on it and does not make it harder: nothing in the Next engine reads the native interpreter's state.

Switching at run time is by model change only (the engine is installed in `PortDecoder` initialization and removed
in its destructor, as for the Sprinter). The register file is the same struct for all three, so a machine-state
transfer between a Spectrum and the Next carries the registers unchanged; the CPU variant is not part of the saved
state, the model is.

## 4. The adapter `Z80NEngine`

`core/src/emulator/io/z80n/z80nengine.{h,cpp}`, derived from the shared template of step 2 above.

| Job | How |
|:--|:--|
| Registers, time, boundary | exactly the Z84C15 adapter's (zero copy, `tt` taken from and returned to the library around every callback, `Z80State::boundary` synchronized around every step and acknowledge) |
| Memory | the callbacks do one pointer lookup in the host's slot table (`NextMemory`); a write to a read-only slot or to a slot without RAM is dropped there. Contention and the 28 MHz SRAM wait are added by `AddWaitStates` from the callback, only when the machine's speed, timing and bank rules ask ([research-fpga-vhdl.md](research-fpga-vhdl.md) sections 2 and 7) |
| Ports | through `Z80::in` / `Z80::out` (TTD journal, interceptor, port trace) into `PortDecoderNext`, which owns the port table of [research-fpga-vhdl.md](research-fpga-vhdl.md) section 4 |
| `NEXTREG` | `NextRegs::Write(reg, value, source = cpu)`; the write may rebuild the slot table or change the CPU speed in the middle of the instruction, so the adapter re-reads the clock and speed after the callback |
| Interrupts | the CPU's `IInterruptSource` is the IM2 chain object; `RETI` / `RETN` reach it through the library's observer hook (section 12 of the research note) |
| NMI | the host's NMI state machine (three sources, first come first served) pulses the library's NMI line; the stackless mode is set by NR `#C0` bit 3 |
| DMA | a separate bus master in the machine step: while it holds the bus the engine is not stepped; it gets the bus back between M-cycles of the host model's choosing (first version: between instructions, refined when the DMA tests of ZXSpectrumNextTests require it) |
| M1 hook | `PortDecoderNext::OnM1` for the DivMMC automap and Multiface entry points, as `OnMachineM1` is used elsewhere; the automap decision uses the **previous** M1 with the instant / delayed rules of [esxdos-and-sd.md](esxdos-and-sd.md) |

## 5. What lives where

| Piece | Place | Why |
|:--|:--|:--|
| Z80N opcodes, `NEXTREG` hook, stackless NMI, T tables | `core/src/3rdparty/unreal-next-z80/` | CPU facts; reusable by a different host (the same library could drive a standalone Z80N test tool) |
| Engine adapter | `core/src/emulator/io/z80n/` | host glue, as `z84c15engine` |
| Port decoder, slot memory, NextREG file | `core/src/emulator/ports/models/next/`, `core/src/emulator/memory/` addition (own interface, no change to `Memory`'s four windows) | machine |
| Video (layers, sprites, copper), audio mix, DMA, CTC, UARTs, SPI, DivMMC, Multiface | `core/src/emulator/io/next/` (one subfolder per device) | machine devices; each with a TTD blob and a report |
| Z80N in the disassembler, assembler (`unreal-asm` ZXN mode), DZRP | the existing tools get a CPU-kind parameter | R47 of [requirements.md](requirements.md) |
| Model entry, creation, supported-models lists | the model table row stays; one factory case in the port decoder factory | `list_models` shows `creatable: true` only when N2 lands |

## 6. Tests and gates for the integration

- Library: every added opcode table-driven against the wiki table and the FPGA microcode; base Z80 suites
  (ZEXALL / FUSE style) on the fork; the ZXSpectrumNextTests `Z80N` and `Z80Nc2` programs as acceptance.
- Adapter: one test per job in section 4 against a fake host; the shared template is compiled against both the
  z84c15 and the z80n libraries in the same test target, so an interface drift in either fails the build.
- Isolation: the shared-file token test extended with `MM_NEXT`, `Next`, `Z80n`.
- Performance: A/B benchmark of a non-Next machine before and after N1 (interleaved, quiet machine) must show zero
  difference; the Next's own engine speed is measured and reported, not gated.
- Linux and MinGW: the fork is plain C++17 like the z84c15 fork; the cross-compiler checks of AGENTS.md apply.

## 7. Risks

| Risk | Mitigation |
|:--|:--|
| Two forks drift from upstream unreal-z80 | rename script + the `README.md` change table + upstream merges done in one step for both forks |
| The template hides a difference between the chips | the chip-specific parts stay in the derived classes; the template has no `#ifdef` and no chip name |
| NextREG writes in the middle of an instruction change the mapping under the engine | the host rebuilds the slot table before returning from the callback; the engine never caches a pointer across a callback |
| DMA granularity (instruction vs M-cycle) | start with the instruction; the DMA tests decide; the interface does not change |
