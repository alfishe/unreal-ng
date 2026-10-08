# Status: TODO

ZX Spectrum Next (`NEXT`): design only, written 2026-10-07. Plan: [phases.md](phases.md) (N0-N12). Nothing implemented;
`MM_NEXT` is still not creatable.

## Done
- [x] Design documents: requirements, architecture, core, CPU, peripherals, video timing, boot, media/snapshots, TTD, automation, tests, roms, research
- [x] Opcode, register and port tables read from MAME and ZXSpectrumNextTests

## N1 status (2026-10-08)
- [x] Library `core/src/3rdparty/unreal-next-z80/` (fork script, README, CMake glob, notices row); tests in `core/tests/3rdparty/unreal-next-z80/`
- [x] The V1 `Z80N` / `Z80Nc2` real-board programs on a bare host (acceptance) and the FUSE vectors, both in core-tests (`UNREAL_NEXT_TESTS`)
- [ ] A/B benchmark of a non-Next machine (must show zero)
- [x] Debugger disassembler: opt-in `SetZ80nMode()`, 29 instructions with the table's sizes and T-states, `OF_BIGENDIAN` for `push nn`
- [x] `unreal-asm` Z80N mode (2026-10-08): sjasmplus frontend (`DEVICE ZXSPECTRUMNEXT` / `OPT --zxnext` / `zxasm convert --z80n`), sizes in the layout module, z80asm (`-mz80n`) and pasmo (bytes) output; the 31 programs of ZXSpectrumNextTests built through sjasmplus -> unreal-asm -> sjasmplus give the same files (`tools/verification/unreal-asm/checks/z80ncheck.py`); all 29 instructions give the same bytes in sjasmplus, z80asm and pasmo form
- [x] Assemblers that take the Next (2026-10-08): the matrix of 29 instructions across sjasmplus, z80asm, zasm, FantASM, the pasmo forks; frontends for pasmo, z80asm, zasm, FantASM, zmac, rasm, Specasm (.s), Odin (.odn) and Zeus on the Next (+3DOS): [research-modern-assemblers.md](../2026-10-05-unreal-asm/research-modern-assemblers.md)
- [ ] SPED (Next distribution editor / assembler) and the closed tools: no format documentation here
- [ ] The engine adapter `Z80NEngine` (N2)

## Remaining
- [x] Verification program: public suites collected and graded ([verification-program.md](verification-program.md)); esxDOS source availability checked ([esxdos-and-sd.md](esxdos-and-sd.md) section 1a)
- [x] N0 second pass (2026-10-08): ULA / Timex / ULA+ / ULAnext, LoRes, palettes and the layer compositor, audio (AY x 3, DAC, mixer), CTC, UART, SPI, DivMMC, keyboard, ZEsarUX comparison: [research-fpga-vhdl.md](research-fpga-vhdl.md) sections 16-22; [esxdos-and-sd.md](esxdos-and-sd.md); [design-integration.md](design-integration.md)
- [x] N0 first pass: FPGA VHDL (timing, contention, memory, ports, NextREG, tilemap, Layer 2, sprites, copper, DMA, IM2), `tbblue` firmware boot chain, ZXSpectrumNextTests inventory: [research-fpga-vhdl.md](research-fpga-vhdl.md). Closed: Q5, Q6, Q7
- [ ] N0 remainder: the unread parts listed in research-fpga-vhdl.md section 16 (Multiface internals, Pi / I2S, expansion bus, membrane scan, keymap RAM format, NextREG read-back table, `ym2149` volume table), ZEsarUX `tbblue.c` diff, the wiki pages and the manual; Q1-Q4, Q8-Q12 ([requirements.md](requirements.md))
- [ ] N1-N12 as in [phases.md](phases.md)
- [x] PLAN.md row #106

## Open questions added in design
Q12: which NMOS/CMOS behaviors the Next's T80-based CPU shows (design-cpu section 1). Q1-Q12: requirements.md section 6.
