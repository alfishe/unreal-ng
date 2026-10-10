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
- [x] P2 memory-model micro-benchmark ([PoC 024](../../../tools/poc/025-next-memory-model/README.md)): D6 confirmed
- [x] P3 boot chain (`f63eb2684`): the real boot ROM -> `TBBLUE.FW` from a FAT16 card -> the golden NextREG sequence -> soft reset into the personality ROM; `NextFirmware_Test` runs on `testdata/machines/zxnext/card`
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
- [ ] The personality ROM with the full card (TBBLUE.FW + nextzxos + sys + dot): runs through palette, layer clip and esxDOS calls (`RST 8`), then loops: first diff against a reference emulator from the co-simulation tooling (agent task, tools/machines/next/cosim)
- [ ] UART skeleton (no peers), DMA, NMI button / stackless NMI, Multiface

## A4 reached (2026-10-09): the NextZXOS main menu
- [x] The real chain (boot ROM -> TBBLUE.FW -> personality ROM -> NextZXOS) on `testdata/machines/zxnext/card` reaches the
  "Welcome to NextZXOS!" screen and, after SPACE, the main menu (Browser, Command Line, NextBASIC, Calculator, Guide, More...,
  1792K, the DS1307 date): `NextFirmware_Test.NextZxosMainMenuIsDrawn` (golden = the 6912 bytes of the ULA screen, checked
  by eye against the jnext screenshot). The ZX renderer already draws it (the OS uses the standard ULA screen);
  picture: [img/nextzxos-main-menu.png](img/nextzxos-main-menu.png). Border colour and the Next's own layers are N6/N7.

## N6a (2026-10-09): the Next picture
- [x] `NextVideoRenderer` (ULA standard / Timex hi-colour / hi-res / ULANext, LoRes, Layer 2 at 256x192 / 320x256 / 640x256 with scroll, clip and palette offset, 9-bit palettes, global transparency, layer order SLU...ULS, Layer 2 priority colours, fallback colour) and `ScreenNext` (mode `M_NEXT`, a 640x512 frame = the 320x256 grid at two sub-pixels with every line twice, drawn line by line; the beam is the ZX family of NR #03); `[NEXT] SdCard` (folder or image) and `[ROM] NEXTBOOT` make the machine boot the real chain in the app; Qt machine menu entry
- [ ] N6b: sprites, tilemap, copper, blend modes (N7); border stripes inside a line; ULA+; Radastan; the Layer 2 CPU mapping of port #123B (read / write windows, shadow bank); ULA half-pixel scroll
- [ ] The Next border colour of the welcome screen (grey = the palette's paper entry of the border) is drawn; the bright-border and flash details are not compared with a reference yet

## NEX loader (2026-10-09)
- [x] `loaders/nex/LoaderNex` (V1.0-V1.3 sizes: palette / screen blocks skipped by flags, banks in file order 5,2,0,1,3,4,6.., entry bank in slot 3, border, SP, PC); 3 synthetic tests + `UNREAL_NEX=<file>` bring-up run (frame.rgba)
- [ ] First check: `tilemap/tm.nex` on jnext shows the tilemap; ours shows the ULA only - the tilemap is N7. Reference screenshots: `jnext --headless --silent --sdcard cosim-cards/full.img --sdcard-readonly --delayed-screenshot f.png --delayed-screenshot-frames N file.nex`

## N7a (2026-10-09): tilemap
- [x] `NextVideoRenderer::TilemapLine`: 40 / 80 columns, 8 / 16-bit map entries (NR #6B bit 5), text mode, 512 mode, x / y mirror, rotate, palette offset, per-tile "ULA over", NR #6B bit 0 (ULA on top), index transparency NR #4C, scroll NR #2F-#31, clip window 3, bank 5 / 7 bases NR #6E / #6F; the ULA and the tilemap merge into one layer before Layer 2 (the stencil mode NR #68 bit 0 is not done)
- [x] `tilemap/tm.nex` on jnext vs ours: identical but the ULA normal-colour level (jnext 6/7, ours 5/7 = the firmware's `DefaultPalette` 0xB6 etc., nexload.asm:728) - jnext deviates, ours follows the firmware
- Bring-up lesson: a turbo frame is not drawn (the picture comes from the beam passing), so frame comparisons run without turbo

## N7b (2026-10-09): sprites, copper, NEX loader screens
- [x] `NextSprites` (128 x 5 attribute bytes, 16K patterns, ports #303B / #57 / #5B, NR #34-#39 / #75-#79 with the tie bit, 8 / 4-bit patterns, mirror / rotate / scale, composite and unified relative sprites, collision and too-many flags from a 1792-cycle line budget (estimate), zero-on-top, over-border + clip) and the three-layer order SLU..ULS with the border exception
- [x] `NextCopper` (NR #60-#64, 28 MHz, paper-relative raster, mode 11 restart, vertical offset); `ScreenNext` runs it to each line's paper start, so MOVEs show from the line they are in
- [x] NEX loader: nexload's register reset (palettes, clips, transparency, priorities), loading screens (Layer 2 banks 9-11, ULA, LoRes, HiRes, HiColour, V1.3 big Layer 2) with their display modes
- [x] `tools/machines/next/cosim/nexcmp.sh <file.nex> [frames]` runs a NEX on jnext and on us and counts pixels differing by more than 40 per channel; `nexside.sh` makes a side-by-side. Over the 125 NEX files of the collection: 37 identical, ~25 within 1000 px; the rest = blend modes, stencil, tilemap/Layer 2 CPU windows, port #123B mapping, timing-dependent demos, V1.3 files (jnext needs --experimental-nex-v1.3)
- Known divergence: jnext shows the global-transparency colour in places where the real nexload leaves a black ULA entry (index 24 = 0x00 after the register reset); ours follows nexload.asm

## N8 (2026-10-09): DMA
- [x] `NextDma`: WR0-WR6 sequencer, ports #6B (zxnDMA) and #0B (Z80-DMA compatible), memory / I/O ports with increment / decrement / fixed, continuous and burst modes, prescaler (875 kHz steps in 28 MHz clocks), auto restart, read mask and read sequence; `PortDecoder_Next::StepDma` runs between instructions and holds the CPU (two clocks per byte) until the block is done or a prescaler wait releases the bus
- [ ] Not yet: the exact per-byte timing (2-4 clocks by the port timing bytes, contention on the read), DMA-on-interrupt (`dma_delay`), NR #82 / #85 port gates, the pulse of the DMA interrupt, bus arbitration with the expansion bus
- jnext comparison: DMAFill, zxnext_dma_sample, test03sprite identical; DMACopy / LDIRCopy 6932 px (a timing screen)

## N7c (2026-10-09): ULA clip, stencil, blend modes
- [x] ULA clip window (NR #1A, with the border beside it), NR #68 bit 0 stencil, NR #15 modes 110 / 111 (additive / subtractive Layer 2 + ULA / tilemap by the blend bits NR #68 6:5); test04tilemap, test10tilemapper, stencil_test now identical to jnext
- Open in the NEX hunt (jnext differs from the VHDL or the case is a moving picture): Layer 2 320x256 default clip (VHDL 191, jnext shows 255), animated demos (frame phase), uninitialised-RAM noise screens (tmHiRes, tmHiCol), timing screens (DMACopy, LDIRCopy, contention_test, floating_bus_test), V1.3 files

## Cross-check against ZEsarUX (2026-10-09, owner: jnext alone is not trustworthy)
`REF=zesarux tools/machines/next/cosim/nexcmp.sh file.nex 250` runs the same NEX on the patched ZEsarUX (`nexzesarux.sh`; its 704x608 window cropped to our 640x512 grid at (32,48), calibrated on tm.nex = 0 px). Over the 125 NEX files of the collection, ours is pixel-identical (<= 2000 px) to ZEsarUX on 42 and to jnext on 46; 28 agree with both. The points where jnext and the VHDL part ways:
- ULA normal colour level: ZEsarUX (0,182,182) = ours = the firmware `DefaultPalette` (0xB6); jnext 219 is the outlier
- Layer 2 320x256 default clip y2 = 191: ZEsarUX clips at 192 rows with the ULA below, like ours; jnext shows 256 rows
- show512: ZEsarUX black background = ours; jnext magenta
- DMACopy, LDIRCopy, contention_test, bbcbasic, palette_demo, tilemap_demo, tmNoStart, hott, empty, preserveNextRegs: ZEsarUX = ours exactly, jnext differs
- plotit-lite and beast need the real system under them: a NEX is loaded by NextZXOS, so DivMMC / esxDOS API, the 48K ROM and system variables are there. `UNREAL_NEX_BOOT=<card>` boots the chain to the main menu first, then loads (the loader also leaves HALT); beast then matches ZEsarUX and jnext, plotit-lite runs its UI
- Open where ZEsarUX differs from ours and jnext agrees with ours (loading screens of V1.1 files, a few demos): beanbros, TX-1696, lom, tmLoRes, bloaters, revivalsurvival - ZEsarUX's own NEX loader, not a video difference to chase

## N4 (2026-10-09): audio, a Next-specific device
- [x] `NextAudio` (core/src/emulator/sound/nextaudio.{h,cpp}): three AY / YM chips with chip select and pan through #FFFD (bit 7 + bits 4:2 = 111, chip bits 1:0, pan 6:5), ACB / mono per chip (NR #08 bit 5, NR #09 7:5), AY / YM (NR #06), turbosound enable (NR #08 bit 1); the four-channel DAC on every port of the VHDL (#1F #0F #4F #5F, #F1 #F3 #F9 #FB, #3F, #DF, #B3; gated by NR #84) and the NR #2C / #2D / #2E mirrors; generators on the AY clock (a tick per 16 base T-states, any CPU speed), levels per tick resampled to the frame; a model audio source in the mixer (one row "Next audio"); `ay-socket = none` in the Next config so the 128K TurboSound is not fitted
- [x] Calibrated against the VHDL and jnext (`tools/machines/next/cosim/nexaudio.sh file.nex frames outdir` -> ours.wav / jnext.wav + spectra; test programs from `mknexsound.py`: ay3, pan, dac, mix, ear, mic): the per-chip stereo law of turbosound.vhd (ABC left = A+B, right = B+C; ACB left = A+C; mono = A+B+C on both; pan bits), 13-bit PCM x4, DAC (v-128) x 16 per channel, a 10 Hz high-pass for the DC; AC rms of ay3 873/873, pan 712/712, dac 1168/1171 and 292/293 against jnext. The beeper runs at the Next's levels (EAR 2048, MIC 512 of 32768, `Beeper::setDacLevels`) and leaves the mix with NR #06 bit 6 + NR #08 bit 4. `AudioStateFields` gives the AY registers, pans and DAC values (/state/audio/covox report)
- [ ] Not yet: I2S, the AY chain / voicing of the standard sound stack (the device mixes plainly, one mixer row), TTD blob, PT3 players through NextZXOS (need the OS programs), the demos' music phase against jnext (night-knight 99 vs 218 AC rms, santaspressie 351 vs 454, ScrollNutter 379 vs 442: same order, timing of the music differs), `zxnext_sfx` / DMA sample play need keys

## Layer 2 CPU mapping (port #123B), 2026-10-09
- [x] `NextMemory::Layer2View`: bits 2:0 / 3 / 7:6 and the offset write (bit 4), the 16K banks of Layer 2 replace #0000-#3FFF (or #0000-#BFFF for segment 3) for reads and / or writes below the DivMMC and above the MMU; NR #12 / #13 refresh it

## unreal-qt opens NEX files (2026-10-09)
- [x] `.nex` is a program of the Next like `.spg` is of TS-Conf: `Emulator::LoadSnapshot` (LoaderNex), `SnapshotLauncher::NeedOf` (model NEXT, 2048K, program only), media classification, the Qt file dialog / drag and drop / command line (`unreal-qt file.nex` starts the Next and runs it), `FileManager` category. Shown in the running app: `tm.nex` (img/unreal-qt-next-tilemap.png) and `scratch/soundnex/ay3.nex` - the master mix of the app measured through the automation (`audio_capture`): left rms 0.0266 = the 873 / 32768 of the test run, right silent (A channels are left in ABC), dominant 874 Hz; `/state/audio/covox` shows the three AYs' registers. The bare machine has no NextZXOS: programs that call the esxDOS API need `[NEXT] SdCard` and the boot to the menu first
- Known: the generic video report of the app says 512x384 for the Next

## Real-board suite V1 on the whole machine (2026-10-09)
The 35 programs of `ZXSpectrumNextTests/release` (48K snapshots, results checked on real boards, photos in the repo) run on the NEXT machine with the state NextZXOS leaves (`#7FFD` = #30, core id 0, ULA palette of the 16 defaults); the same files on ZEsarUX are the second reference (`scratch/v1run.sh`-style loop: bring-up `UNREAL_NEX=<file.snx>`, `UNREAL_NEX_KEYS`). `NextRealBoard_Test` (UNREAL_NEXT_TESTS) pins what is decided:
- [x] NextReg_defaults: no red cell (was 6): NR #08 bit 7 reads the inverse of the #7FFD lock (and writing 1 unlocks), NR #09 bit 3 reads 0, NR #6E / #6F bit 6 reads 0 and reset to #2C / #0C, ULA palette entries 32-255 repeat the 16 defaults after the firmware; core id 0 after the boot
- [x] NextReg #69: 10 of 10 (was 7): bit 6 is bit 3 of #7FFD (shadow screen) both ways, the video state follows the copper per line
- [x] Z80N: all rows OK at 28 MHz (also `!Z80Nc2`)
- [x] LayersMixingHiRes: Timex hi-res attribute = bright | paper << 3 | ink through ULANext, border = paper colour
- [x] Copper at pixel resolution inside a line (Copper.snx flags as on the board photo): the pixels before a MOVE are drawn with the state before it; CPU video register writes flush the picture first
- [x] The line counter of NR #1E / #1F and the line interrupt is the copper's cvc: 0 at the paper's first line and at the ULA pixel counter zero, plus NR #64 (was counted from the frame's vc 0): linesIRQ 2724 -> 300 px against ZEsarUX, LmixLoRs and LmxHiRes identical, L2Colour identical
- [x] LoRes keeps the ULA border (a transparent-white border is the pink fallback, as the board shows); ULANext attributes do not flash (LmxHiCol now equals its expected miniatures and the board photo)
- [x] UlaScrol (after the animation: green border, clip, as the board photo and MAME), Ula_Pal (border stripes and both palettes as MAME), SprDelay (structure as the board photo; the single-line sprite is shorter than on the board), DMA: zilogDMA's hex read-back lines equal the board's (`3A3A1A03042A...`, status bit 0 never set on the core), per-byte timing from the port timing bytes (borders flash at 4T per byte)
- [ ] Open: Chg8kBan / Chg8kB_2 (green border ends ~31 lines after MAME's: contention of the NEXTREG fetches), dma / zilogDMA border blocks (heights not measured), SprDelay (the sprite renderer's delay), int_skip (ours OK, ZEsarUX ERR: ours follows the readme)
- [x] z80bltst: the red INIR / INDR rows were **our Next not having the Kempston port**: the program reads port #1F for its INxR case 1 and our #1F answered the floating bus (FF / 55 / 1F) instead of 0; with `#1F` = 0 (the joystick built into the board, zxnext.vhd port_1f_lsb) every row is green like the board. (First written up here as "a defect of the common Z80 core" - wrong: the classic models have no Kempston, the program is not meant to run there; ZEXALL / the other core tests do not exercise interrupted block instructions, so they could not have shown it either way)
- A NEX / SNA is loaded by NextZXOS with the 48K snapshot's `#7FFD` locked: nothing in the machine model is a 48K type lock (the VHDL: lock = bit 5 of #7FFD)

## Found while running NextZXOS in unreal-qt (2026-10-09)
- [x] The status bar's 28 MHz in the NextZXOS menu, Browser and editor is **right**: the NextZXOS guide (`guides/NextZXOS.gde`, "NMI Menu") says the OS runs at 28 MHz when it can; the "3.5MHz >" on the menu's first line is the speed it gives the user's programs. NR #07 reads `#33` there and the WebAPI shows it: `GET /state/next` (`machine.cpu_clock_hz`, `speed_ratio`), `/state/next/regs`
- [x] NMI = the DRIVE button: NR #06 bit 4 + the DivMMC button latch at the `#0066` fetch; on NextZXOS it opens the Browser, as the board's Drive button does
- [x] **Multiface** (`NextMultiface`: NMI by NR #02 bit 3 / the M1 button, ROM + RAM at #0000-#3FFF from the #0066 fetch to the RETN, the enable / disable ports by NR #0A bits 7:6, invisible / mode rules of multiface.vhd) - found because **NextZXOS starts every snapshot with it** (`SPECTRUM`, `.sna` / `.z80` / `.snx` from the Browser): they showed a black screen. Five gaps stood behind it, each found with the NextREG journal and the Browser driver: RETN did not leave the DivMMC automap; NR #02 always read "power on" (NextZXOS restarts with a soft reset and reads it); NR #02 bit 2 / 3 did not generate the NMIs; **the stackless NMI (NR #C0 bit 3) was not wired** (the library had it, the engine did not install it: NR #C2 / #C3 never got the return address); the DivMMC automap stayed active while the Multiface memory was in (the board's `sram_pre_override` is "000" there). The snapshot start: Multiface ROM sets NR #C2/#C3 = #2313, a `RET` of the 48K ROM, and `RETN` pops the snapshot's PC from its stack
- [ ] The host's NMI action is the DRIVE button; an M1-button action (the NMI menu of the guide) needs a Qt / API entry: `GenerateMultifaceNmi` is there, the NR #02 route works
- [x] **NEXTREG instructions and copper writes were invisible to the port trace, port breakpoints and the TTD I/O journal**: the NextREG write journal ([design-nextreg-journal.md](design-nextreg-journal.md)) is on every plane (WebAPI + OpenAPI, MCP `inspect_state next_reg_journal`, CLI `state next journal`, Lua, Python; recipe `.recipe/machines/next.md`). TTD integration of NEXTREG writes: open question 2 of the design
- [ ] **NEXTREG instructions are invisible to the port trace, the port breakpoints and the TTD IO journal**: `Z80NEngine` calls `NextBoard::WriteNextReg` directly, only `OUT (#243B / #253B)` is traced. NextZXOS writes its registers with NEXTREG, so "who set NR #07" cannot be answered with the tools. Give NEXTREG writes an event of their own (device `NEXTREG`, register + value + pc) in the port trace and the TTD journal, and a read of the register file in the WebAPI

## Automation-plane coverage for the debugger (2026-10-10, [design-automation-coverage.md](design-automation-coverage.md))
- [x] **Phase A** - `next_dma`, `next_video`, `next_palette`, `next_ports`, `next_nextreg` (read) and `POST next/nextreg` (write) on every plane (WebAPI + OpenAPI manifest `openapi_next.inc`, MCP `inspect_state` aspects with `nr_` parameters, CLI `state next dma|video|palette|ports|nextreg` and `next nextreg`, Lua, Python); no side effects (R3: const accessors on `NextDma`; palette / copper indexes untouched); tests `NextReports_Test` (+ golden texts `core/tests/automation/golden/next/`), `CliNextReports_Test`, MCP aspect tests, the OpenAPI route test; recipe `.recipe/machines/next.md` section 3b
- [x] **Phase B** - `next_copper` (disassembly, control mode, pc, write address, `around_pc`, `raw`) and `next_sprites` (128 attributes decoded, relative sprites resolved against their anchor, flags that a report does not clear, pattern memory summary) on every plane; tests `NextReportsB_Test`, CLI / MCP / OpenAPI additions; goldens `copper.txt`, `sprites.txt`, `cli-copper.txt`, `cli-sprites.txt`
- [ ] **Per-layer capture** (`capture/screen?layer=`) is not done: `NextVideoRenderer` composes the layers in one function and takes the register state of the line being drawn (copper effects), so an on-demand re-render from the registers of the moment would differ from what was drawn - it needs the renderer to expose its layer buffers and a capture entry on every plane; left for its own change
- [ ] `ports.enforced`: only the DAC ports (NR #84) and the Multiface (NR #83 bit 1) are gated by the internal port enable word NR #82-#85; the report says which. Gating the rest (and the expansion-bus word NR #86-#89, `#8A` propagate) is a decoder change with its own tests
- [ ] The state reports are read from the HTTP thread without a coherent moment (like every `state/*` report); only the write control runs at one. A report that must be exact on a running machine could use `Emulator::RunAtCoherentMoment` the way the debugger snapshot does
- [ ] Phase C of the design: Qt panels, breakpoints on NextREG writes, DeZog / GDB wiring, TTD of NEXTREG writes

## Running the board's programs the way a person does (2026-10-09)
- [x] `tools/machines/next/browser-drive/suite.py`: every program of `tests/<area>/<test>/` on the card, one fresh boot each, through NextZXOS's Browser (the path the snapshot loader tests do not cover); `!Copper.snx` and `L2Colour.snx` show their pictures. Result of the whole set: see the report in the commit message / `scratch/browser-suite/report.md`
- [x] The four differences of the first Browser run, closed 2026-10-09: **L2Port** (its IM1-in-Layer-2 test mapped the DivMMC at #0038 - on the board the ROM-3 automap entries need "not Layer 2 reads" (`sram_divmmc_automap_rom3_en`), so the DivMMC stayed mapped over slot 0 for the rest of the program; fixed, all green, green border); **TFalBUla** (all three phases by "n" match the readme); **UlaScrol** (R skips the animation, the green-border interactive state matches); **SprDelay** (identical to MAME 0.282's picture of core 3.02.1, the same family as ours; the board photos are core 3.0.5 with the older double-buffered sprite engine, so they differ by design)
- [x] Execution breakpoints need the `debugmode` feature, and an address alone matches in every ROM page (`page: rom2`): both in the recipe

## ZXSpectrumNextTests through NextZXOS's Browser (2026-10-09): 35 programs, 34 as expected
Run by `tools/machines/next/browser-drive/suite.py`; "ok" = the picture / the program's own verdict equals the readme and the board photo (or MAME 0.282 of the same core family where the photos are of older cores).

| Area | Program | Result |
|:--|:--|:--|
| base | `!Copper` `!dma` `!NextReg` `!Z80N` `!Z80Nc2` | ok (Z80N / Z80Nc2 with keys 2, 5: every row OK) |
| Graphics | `L2Colour` `L2Scroll` `LmxHiCol` `LmxHiRes` `LmixLoRs` `Lmix_LxU` | ok |
| Graphics | `L2Port` | ok after the DivMMC / Layer 2 automap fix (all green, green border) |
| Graphics | `NReg0x69` | ok (10/10) |
| Interrupts | `DIHalt` | ok (green border, white paper: the CPU stays in DI+HALT) |
| Misc | `zilogDMA` `dmaDebug` | ok (the read-back lines equal the board's; dmaDebug is interactive) |
| Sprites | `SpritBig` `SprBig4b` `SpritRel` `SpritTra` | ok |
| Sprites | `SprDelay` | ok = MAME 0.282's picture of core 3.02.1; the board photos are core 3.0.5 (older sprite engine), differ by design |
| Timing | `linesIRQ` `Chg8kB_2` (contention OFF) | ok |
| Timing | **`Chg8kBan` (contention ON)** | differs from MAME: ours is ~31 lines longer. The test swaps an 8K bank 2048 times with NEXTREG from `#6000` (contended bank 5): 4 reads a NEXTREG, ~128 of 224 T of a line contended, so ~+5 T an instruction in the paper area - what ours gives; MAME adds ~0.5 T. No board photo: open |
| ULA | `TFalBUla` (3 phases by `n`) `CPalTran` `CPalTrV2` `CPalTrV3` `Ula_Pal` `DefTrans` `UlaScrol` (R skips the animation) | ok |
| ZX48_ZX128 | `z80bltst` | ok (all green) |
| ZX48_ZX128 | `ccffrm` `int_skip` `ULAvsSJS` | run; ccffrm "no error", int_skip's report as the readme; ULAvsSJS is interactive (keys) |

## What counts as a reference (owner, 2026-10-09)
No single emulator is the reference (MAME included): a difference is a defect only when the board (photo / the program's own verdict on the board) or at least two independent implementations that follow the VHDL agree against us. Everything else is *undecided* and listed as such:
- Undecided, no board evidence: Chg8kBan / Chg8kB_2 (MAME alone ends the green border ~31 lines before us; ZEsarUX has no per-line border), SprDelay (sprite renderer delay: board photo shows a longer single-line sprite than ours and ZEsarUX), int_skip (ours follows the readme, ZEsarUX differs), the heights of the dma / zilogDMA border blocks
- For these the way forward is **targeted tests run on a real board** (the Z80N / NextReg programs already show the method): a program per question that prints a number and checks it, so a photo or a log from a board gives the answer. Proposed: (1) NEXTREG instruction cost under contention: a loop of N `NEXTREG n,n` at 3.5 MHz with contention on, border toggled at the loop edges, count lines in the picture (the number of lines of the loop at several code addresses - #6000 contended, #8000 not); (2) sprite renderer delay: one sprite line switched visible by the copper on line L, read back which line shows it; (3) DMA block height: 2918 bytes to port #FE at each timing code, count border rows
- Consensus found so far (board photo + ZEsarUX or VHDL): NextReg defaults and read-back masks, NR #69 and its ports, Z80N, hi-res attribute / border, ULANext without flashing, LoRes keeps the ULA border, copper at pixel resolution, the line counter origin, ULA clip, DMA read-back bytes

## CSpect release notes as a second checklist (2026-10-09)
Sources: the CSpect itch.io page and devlog (3.0.15.2, 2.19.5.x), Mike Dailly's blog and the forum thread (2.15.1 - 2.19.x), the SpecNext wiki "CSpect: known bugs". CSpect is **not** a reference (owner's rule above): each line is a question to ask our implementation, and an item is closed only with board evidence or a second implementation. Every fix in the notes is a hint at a place where the hardware surprised an emulator author; the "known bugs" page is the reverse: where CSpect is *wrong*, so the board does the opposite.

Hardware behaviours the notes mention (check ours):
- [x] NR #6E / #6F power-up #2C / #0C (2.12.30, 2.16.6) - done
- [x] Modes 6 / 7 ULA-tile order, blend B+L and B+L-5, ULA+tile stencil (2.18.0, 2.19.0.0) - done (N7)
- [x] Layer 2 palette offset in 256 / 320 / 640 modes (2.12.30) - NR #70 bits 3:0
- [x] Layer 2 banks in the full 2 MB, NR #12 / #13 (2.15.2); DMA ports #0B and #6B (2.17.0); copper at pixel resolution (2.19.2.0 "copper writes were not executing all CPU T-states")
- [x] OTIR / OTDR / INIR / INDR flags when interrupted (3.4 "Z80 hardware bugs"): z80bltst is all green
- [ ] Layer 2 clip with left > right / top > bottom: no crash, and **top > bottom renders nothing** (2.19.1.0) - add a unit test
- [ ] Layer 2 640 mode clips properly in the lower screen; Layer 2 pixels in the border area (2.16.6, 2.19.3.0)
- [ ] Timex hi-res and hi-colour ink / paper orders with and without ULANext, smooth scrolling (2.17.0, 2.19.0.3); ULA Y-scroll calculation, ULA last line (2.19.0.0 / .1)
- [ ] Hi-res tilemap scroll speed (2.16.6)
- [ ] CTC: timers always at 28 MHz whatever the CPU speed, cascading timers, interrupts disabled at the start of IM2 (2.16.3, 2.17.0) - we have `NextCtc`, compare each
- [ ] Contended memory does not affect 7 / 14 / 28 MHz (2.17.0) - ours: check the contention path with NR #07 > 0
- [ ] DivMMC direct paging NR #B8-#BB, NR #0A bit 2 disables the automap, a NEX load sets #B8-#BB to #82 #00 #00 #F0 and the automap off for NEX (2.15.2, 2.16.0) - our table has them; the NEX loader value and the NR #0A bit are not checked
- [ ] AY reset through NR #06 bits 1:0 (2.19.4.2); AY stereo ABC / ACB / mono (2.19.2.0) - NextAudio pan
- [ ] Multiface paged out on RETN (2.18.0) - only if we model Multiface on the Next
- [ ] Sprites cleared on hard reset, memory cleared / ROMs reloaded; "ULA line always cleared if enabled" (2.19.5.2)
- [ ] NR #02 hard reset (2.16.6) and the soft reset (the wiki: CSpect's differs from the board)
- [ ] 3.0.x: **ULA overlay** - 24 KB more RAM through banks 10, 11 and 14, **overscroll** modes 320x256 / 640x256 / 256x192, "layer 2 banking conflicts" fixes: this is core 3.02.xx behaviour we do not have documented; find it in the VHDL (the `zxnext` ULA bank / overscan registers) before doing anything

Where the wiki says CSpect is wrong, so the board does the opposite (our tests should assert the board side):
- [ ] sprite collision bit exists on the board (ours sets it); sprites are one scanline delayed (the SprDelay question above); sprites in the border versus tiles in modes 3-5 are drawn "weirdly" by the hardware; sprite wrap-around of the 512x512 space
- [ ] DMA: **setting the WR3 enable bit starts a transfer** on the board (CSpect does not); read-back registers (we match the zilogDMA board lines); Z80-DMA (#0B) mode
- [ ] NR #09, #34, #41, #8E read-back (ours: #09 done, check the others against the VHDL)
- [ ] Z80: `di : halt` must not loop waiting for an NMI (the DIHalt test: ours matches the photo); block instructions and prefixes must inhibit a masked interrupt correctly
- [ ] Debugger step-over runs the interrupt handler often (CSpect's own, not ours)

Not relevant to us (CSpect UI / plugin API / esxDOS emulation / assembler / DeZog / Boriel / printer plugin / NextZXOS streaming API).

## Remaining
- [x] Verification program: public suites collected and graded ([verification-program.md](verification-program.md)); esxDOS source availability checked ([esxdos-and-sd.md](esxdos-and-sd.md) section 1a)
- [x] N0 second pass (2026-10-08): ULA / Timex / ULA+ / ULAnext, LoRes, palettes and the layer compositor, audio (AY x 3, DAC, mixer), CTC, UART, SPI, DivMMC, keyboard, ZEsarUX comparison: [research-fpga-vhdl.md](research-fpga-vhdl.md) sections 16-22; [esxdos-and-sd.md](esxdos-and-sd.md); [design-integration.md](design-integration.md)
- [x] N0 first pass: FPGA VHDL (timing, contention, memory, ports, NextREG, tilemap, Layer 2, sprites, copper, DMA, IM2), `tbblue` firmware boot chain, ZXSpectrumNextTests inventory: [research-fpga-vhdl.md](research-fpga-vhdl.md). Closed: Q5, Q6, Q7
- [ ] N0 remainder: the unread parts listed in research-fpga-vhdl.md section 16 (Multiface internals, Pi / I2S, expansion bus, membrane scan, keymap RAM format, NextREG read-back table, `ym2149` volume table), ZEsarUX `tbblue.c` diff, the wiki pages and the manual; Q1-Q4, Q8-Q12 ([requirements.md](requirements.md))
- [ ] N1-N12 as in [phases.md](phases.md)
- [x] PLAN.md row #106

## Open questions added in design
Q12: which NMOS/CMOS behaviors the Next's T80-based CPU shows (design-cpu section 1). Q1-Q12: requirements.md section 6.

## DMA / timing audit against the RTL (2026-10-09)

Done: DMA last-byte prescaler wait, auto-restart keeps the end-of-block flag, continuous mode keeps the bus in the wait, reset
(hard and soft) per the dma.vhd reset block, DMA cycles skip #6B/#0B, mode latch on reads, status bit 0, NR #CC/#CE read masks, DMA delay
(NR #CC-#CE), SPI waits for DMA to #EB, CTC interrupts in the pulse mode, Pentagon INT pulse 36, line interrupt 128 T into the row.

Done later the same day: NEXTREG n,v / n,A have 2 / 2 trailing MREQ reads in the RTL (t80n_mcode.vhd 1668-1712, NoRead = 0), so they are
contended memory cycles; the write is driven at the start of the first (+14 T, +11 T). Changing8kBank on the emulator, port-traced (OUT #FE
start/end of the green): contention ON 56817 T, OFF 42671 T, difference 14146 T = 62.0 lines (RTL arithmetic 61.4, the old 4-cycle model
47.7). The program's own logic agrees: 2048 NEXTREG x 20 T + 128 DJNZ = 42.6 kT uncontended (OFF), and the ON frame still fits the 311-line
frame. There are no real-board numbers; MAME has no contention (its two photos are identical).

Open (arithmetic only, no board numbers): 28 MHz SRAM read wait (+1 clock per read, zxnext.vhd 3171-3181); INT pulse in CPU clocks (not base T) at
turbo; CPU speed change applied at the next frame; NEXTREG write lands 3 T late; 60 Hz timing; NR03 timing decode (bit 3, lock, 101-111);
NR08 latch at hc(8); port-contention details (#BF3B, #FF3B); DMA bus contention.
