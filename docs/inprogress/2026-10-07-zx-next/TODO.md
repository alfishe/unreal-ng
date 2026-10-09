# Status: TODO

ZX Spectrum Next (`NEXT`): designed 2026-10-07; N1, N2 (skeleton), P1-P4 done 2026-10-08 on branch `zx-next`. Plan:
[phases.md](phases.md) (N0-N12, P1-P4). `NEXT` is creatable as the bare personality (128K board, user ROMs) and boots the
real firmware chain; no Next video, audio, DMA or DivMMC yet.

## Done
- [x] Design documents: requirements, architecture, core, CPU, peripherals, video timing, boot, media/snapshots, TTD, automation, tests, roms, research
- [x] Opcode, register and port tables read from MAME and ZXSpectrumNextTests

## N1 status (2026-10-08)
- [x] Library `core/src/3rdparty/unreal-next-z80/` (fork script, README, CMake glob, notices row); tests in `core/tests/3rdparty/unreal-next-z80/`
- [x] The V1 `Z80N` / `Z80Nc2` real-board programs on a bare host (acceptance) and the FUSE vectors, both in core-tests (`UNREAL_NEXT_TESTS`)
- [x] A/B benchmark of a non-Next machine (2026-10-08): A = `32b0dccd1`, B = `06bebbcbd` (the library, its CMake glob, the disassembler mode), `BM_HostFrame_*` (26 benchmarks: 48K, Pentagon, Scorpion, Profi, ATM, TSConf, Sprinter, NeoGS; fast and debug), rounds A B A B A B B A B A, CPU time. Host load 10-21 (above the guideline's 12 in most rounds). Mean of the 130 paired differences -0.08 %, 53 of 130 positive; per benchmark the mean is within +-1.3 % and the minimum of ten runs within +-1.2 %, with signs mixed in both orders: no change, as expected (the library is a separate target no machine calls)
- [x] A/B benchmark of non-Next machines at the N2 registration (2026-10-08): A = `3082b81e8`, B = N2 working tree (`Read/WriteDebugEffects` extracted from `MemoryReadDebug`/`MemoryWriteDebug`, `ModelMemoryInterface` in `SelectMemoryInterface`), `BM_HostFrame_*` (26 benchmarks), rounds A B A B A B B A B A, CPU time, host load 9-29 (above the guideline's 12 in most rounds). Mean over benchmarks of the per-benchmark mean ratio +0.20 %, range -1.9 % .. +1.6 %; the Debug variants lean positive (about +0.8 %), the Fast ones are centred on zero: within the noise of this load, but the Debug lean is worth a quiet-machine repeat if the extracted bookkeeping calls are not inlined
- [x] Debugger disassembler: opt-in `SetZ80nMode()`, 29 instructions with the table's sizes and T-states, `OF_BIGENDIAN` for `push nn`
- [x] `unreal-asm` Z80N mode (2026-10-08): sjasmplus frontend (`DEVICE ZXSPECTRUMNEXT` / `OPT --zxnext` / `zxasm convert --z80n`), sizes in the layout module, z80asm (`-mz80n`) and pasmo (bytes) output; the 31 programs of ZXSpectrumNextTests built through sjasmplus -> unreal-asm -> sjasmplus give the same files (`tools/verification/unreal-asm/checks/z80ncheck.py`); all 29 instructions give the same bytes in sjasmplus, z80asm and pasmo form
- [x] Assemblers that take the Next (2026-10-08): the matrix of 29 instructions across sjasmplus, z80asm, zasm, FantASM, the pasmo forks; frontends for pasmo, z80asm, zasm, FantASM, zmac, rasm, Specasm (.s), Odin (.odn) and Zeus on the Next (+3DOS): [research-modern-assemblers.md](../2026-10-05-unreal-asm/research-modern-assemblers.md)
- [ ] SPED (Next distribution editor / assembler) and the closed tools: no format documentation here
- [x] The engine adapter `Z80NEngine` (N2)

## N2 status (2026-10-08, committed `b1ee206c7`)
- [x] `NextMemory` (8x8K slot table, offsets), `Memory::ModelMemoryInterface`, `NextBoard` (NR #00-#04, #07, #50-#57), `PortDecoder_Next` (#7FFD/#1FFD/#DFFD, #243B/#253B), model plumbing, `data/configs/next`, `data/rom/next.rom`
- [ ] `next_regs` / `next_mmu` reports on all surfaces and recipe v0
- [ ] Contention for 48K timing (N3); quiet-machine repeat of the A/B if the Debug lean (+0.8 %) matters

## P2-P4 status (2026-10-08)
- [x] P2 memory-model micro-benchmark ([PoC 024](../../../tools/poc/024-next-memory-model/README.md)): D6 confirmed
- [x] P3 boot chain (`f63eb2684`): the real boot ROM -> `TBBLUE.FW` from a FAT16 card -> the golden NextREG sequence -> soft reset into the personality ROM; `NextFirmware_Test` needs `UNREAL_NEXT_FIRMWARE`
- [ ] P3 left: SPI flash (the firmware's core-version read passes only because NR #01/#0E answer 3.02.03), DivMMC automap on the Next, keyboard-driven menu
- [x] P4 `DivMmcPaging` + UnoDOS 3.141 on the 48K machine (`DivMmcUnoDos_Test`)
- [ ] P4 left: TTD blob for the device, the `divmmc` slot card, reset of the device with the machine, the "ROM 3 only" automap condition of the +2A/+3, the Next backend over the slot table (N9)

## N3 status (2026-10-08, uncommitted)
- [x] N3a: NR #07 CPU speed (`hw_turbo_ratio`, 3.5-28 MHz, applied at the frame boundary), NR #03 machine type + frame family (48K / 128K / +3 / Pentagon, applied at the frame end through `EmulatorState::ula_timing_class` and the screen's per-frame mode detection), paging ports by machine type (48K none, 128K/Pentagon no #1FFD), SPI byte time in base units at any speed
- [x] N3b (uncommitted): memory contention per frame family at 3.5 MHz only, from NR #03 timing, NR #07 and NR #08 bit 6: 8K-slot flags in `NextMemory` (banks 0-7; 48K bank 5, 128K odd banks, +3 banks 4-7, Pentagon none), contended slot interfaces, `Memory::ModelMemoryInterface(debug, contended)`, `RefreshSlotContention` virtual, `EmulatorState::hw_contention_disabled`; +3 uses the gate-array pattern. Tests in `NextSkeleton_Test`
- [ ] N3b left: the Next's "previous hc cycle" phase (3-14 instead of 4-15), port contention by the 8K slot of the address high byte (the 16K flags are the OR of the halves), floating bus, the +3 WAIT-line form
- [ ] N3c: 60 Hz (needs the 264-line raster, N6), NR table complete (reads/writes/reset values), line interrupt and `NextInterruptSource` pulse mode, NR #22/#23, timing tests of ZXSpectrumNextTests, A/B benchmark

## N3c / N5 / N9 start (2026-10-09, uncommitted)
- [x] `NextInterruptSource`: pulse mode (ULA + line, NR #22/#23), hardware IM2 mode (14 sources, NR #C0 vector/mode, #C4-#C6 enables, #C8-#CA status/clear, RETI, nesting)
- [x] `NextCtc` (4 channels, 28 MHz, /16 /256, chain, counter mode) and `NextI2c` + DS1307 (ports #103B/#113B)
- [x] NR #8E (paging ports in one register), NR #01/#0E core version, NR #B8-#BB reset values
- [x] `NextDivMmc`: #E3, automap by NR #B8-#BB and #0A bit 4 on the M1 hook, the memory view in `NextMemory` (DivMMC ROM = system page 4, RAM banks = pages 8-15)
- [ ] NR table from `registers.txt` (distribution docs, `documents/nextzxos-distribution-docs/extra-hw/io-port-system/`) as data: the rest of the registers, alt ROM NR #8C, mapping modes NR #8F
- [ ] The personality ROM with the full card (TBBLUE.FW + nextzxos + sys + dot): runs through palette, layer clip and esxDOS calls (`RST 8`), then loops: first diff against a reference emulator from the co-simulation tooling (agent task, tools/verification/next-cosim)
- [ ] UART skeleton (no peers), DMA, NMI button / stackless NMI, Multiface

## Remaining
- [x] Verification program: public suites collected and graded ([verification-program.md](verification-program.md)); esxDOS source availability checked ([esxdos-and-sd.md](esxdos-and-sd.md) section 1a)
- [x] N0 second pass (2026-10-08): ULA / Timex / ULA+ / ULAnext, LoRes, palettes and the layer compositor, audio (AY x 3, DAC, mixer), CTC, UART, SPI, DivMMC, keyboard, ZEsarUX comparison: [research-fpga-vhdl.md](research-fpga-vhdl.md) sections 16-22; [esxdos-and-sd.md](esxdos-and-sd.md); [design-integration.md](design-integration.md)
- [x] N0 first pass: FPGA VHDL (timing, contention, memory, ports, NextREG, tilemap, Layer 2, sprites, copper, DMA, IM2), `tbblue` firmware boot chain, ZXSpectrumNextTests inventory: [research-fpga-vhdl.md](research-fpga-vhdl.md). Closed: Q5, Q6, Q7
- [ ] N0 remainder: the unread parts listed in research-fpga-vhdl.md section 16 (Multiface internals, Pi / I2S, expansion bus, membrane scan, keymap RAM format, NextREG read-back table, `ym2149` volume table), ZEsarUX `tbblue.c` diff, the wiki pages and the manual; Q1-Q4, Q8-Q12 ([requirements.md](requirements.md))
- [ ] N1-N12 as in [phases.md](phases.md)
- [x] PLAN.md row #106

## Open questions added in design
Q12: which NMOS/CMOS behaviors the Next's T80-based CPU shows (design-cpu section 1). Q1-Q12: requirements.md section 6.
