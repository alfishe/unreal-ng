# TS-Conf audit - TODO

The audit itself is done (2026-10-05, [README.md](README.md)). The fixes go one at a time from master; the Status
column says which are in.
Each item names the area file and row with the evidence and the suggested test. A fix starts with that test, failing
first; tests that assert the current wrong value are named and change with the fix.

## Bugs, ranked

| # | Bug | Where | Effect | Tests asserting the wrong value | Status |
|--:|:--|:--|:--|:--|:--|
| 1 | Frame INT pulse keeps counting during vdos and is lost after 32 clocks; RTL and the fork freeze it (`zint.v:194` `!vdos`) | [interrupts.md](interrupts.md) row 7 | frame interrupts lost during long virtual-drive sessions | - | **fixed** with #2: `TsConfInterrupts::OnVdosEnter / OnVdosExit` freeze the pulse over `pre_vdos || vdos`; tests `TsConfVdosInt_Test` INT11a-e |
| 2 | vdos blocks INT only from the next M1; RTL blocks with `pre_vdos` during the trapped VG93 cycle | [interrupts.md](interrupts.md) row 8 | an INT right after a trapped access runs the IM1 handler from RAM page `#FF` | - | **fixed** with #1 (the gate is `pre_vdos || vdos`) |
| 3 | Line INT fires at tact 224n-1; RTL and the fork at 224n (tact 0 of the next line) | [interrupts.md](interrupts.md) row 11 | one tact early; raster code timed to the line INT drifts by a tact | `INT3_LineInterrupts` (asserts 223) | **fixed**: events at 224 n; INT_MASK / HS_INT / VS_INT writes first latch the events before them (`CatchUpTo`); tests INT3, INT3b, INT3c, INT5, two Vdac2Card tests |
| 4 | 5-bit VDAC levels: unreal-ng scales 31 to 255 and truncates; the VDAC1 CPLD (`vdac/vdac1/cpld/top.v`) is the VDAC2 table - `{level,3'b0}` with PAL_SEL, the rounded 0..24 table without | [video.md](video.md) row 44 | colors off by one level (white 255 instead of 248) on VDAC boards; fix: route `vdac == 3` through `Vdac2Level` | `VDAC_CurvesStatusAndRender`, `VDAC2_CardTable` | **fixed**: `vdac == 3` goes through the CPLD table (`Vdac2Level`); both tests check the 5-bit build against the card table |
| 5 | OUT `#FE` builds BORDER from the unlatched PAL_SEL; RTL uses the copy latched at line start (`video_ports.v:109`) | [video.md](video.md) row 16, [memory-ports.md](memory-ports.md) B2 | wrong border bank when PAL_SEL changes within a line before an OUT `#FE` | `BorderWriteUsesPalSel`, `VID4_Border` | **fixed**: `#FE` takes `latPalSel`; the tests write PAL_SEL and `#FE` in one line (old bank) and the next (new) |
| 6 | SPI / IDE DMA charged 10 DRAM cycles per word against the free budget; RTL: 34 fclk per word (two SPI exchanges), one DRAM cycle, independent of the video mode | [dma.md](dma.md) rows 30-31 | SD loads 15% slow without video, ~1.8x slow under 256C | - | **fixed**: 1 DRAM cycle per word + device time (SPI 34, IDE 12 fclk) accrued at 8 fclk per tact; tests TIM3, TIM3b, TIM3c |
| 7 | CPU RAM writes are not taken from the DRAM budget (DMA and TSU); RTL and the fork count them | [dma.md](dma.md) row 46, [tsu.md](tsu.md) row 40 | write-heavy code leaves DMA / TSU more slots than the hardware | - | **fixed**: the TS-Conf write overlay is always on and counts writes to writable RAM (`TsConfMemory::AfterWrite`); test TIM6; A/B `BM_HostFrame_TSConf_Fast/Debug` +0.7% / -0.7% (noise) |
| 8 | Video fetch cost per line 1-4 cycles low: `video_go` lasts w+4 dots (ZX 33, 16C 81, 256C 162, TXT 164); `TsConfArbiter::FetchOf` already has the right window | [dma.md](dma.md) row 42 | DMA / TSU budget slightly high | `ENG1_LineBudget` (asserted `w >> shift`) | **fixed**: `VideoCost` = ceil((w + 4) / len) x need; ENG1 checks all modes and a 360-wide window |
| 9 | Free DRAM cycles spread evenly over the line; RTL and the fork run DMA at full rate in the border | [dma.md](dma.md) row 43 | DMA words (and TIM-5 CRAM writes) land at the wrong point in the line | - | **fixed** for the video: its cost lies in the fetch window [h0, h1) (`TsConfArbiter::FetchOf`); test DMA12b. The TSU's cost stays spread over the line (where it takes its cycles is row 40's question) |
| 10 | `#BFF7` (CMOS) reads return AVR data inside vdos; RTL reads `#FF` (`zports.v:330` `!dos`) | [memory-ports.md](memory-ports.md) B1 | DOS code reading the clock inside vdos sees data the hardware hides | `VDOS2_CmosInsideVdos` | **fixed**: `DecodeF7In` gives `#FF` in DOS (the AVR still sees the read); VDOS2 checks the floating read and the write landing |
| 11 | IDE over-decode: `(low & 0x1E) == 0x10` takes `#31..#F1` as IDE registers; RTL only `#11` among the odd ports (the fork has the same bug) | [memory-ports.md](memory-ports.md) B3 | ports of other devices answered by the IDE | - | **fixed** in the shared `IdeAdapter::EvoIn / EvoOut` (TS-Conf and ATM3: the Base Configuration RTL decodes the same); test `IdeAdapter_Test.EvoOddPortsOtherThan11AreNotIde` |
| 12 | A cut-off sprite / tile at the end of the TSU line is dropped whole; RTL and the fork draw it up to the cut, 4 px per DRAM word | [tsu.md](tsu.md) row 39 | missing partial objects on overloaded lines | `TSU8_StarvedObjectsAreDropped` | **fixed**: `DrawSprites` / `DrawTiles` draw the fetched words of a cut object (bitmap order, X flip included); test TSU8b; TSU8 still right (no word left for the third sprite) |
| 13 | Tiles processed after `line_start` of L use the L-1 G_PAGE / X offsets / PAL_SEL; RTL uses L's latched set (the fork behaves like unreal-ng) | [tsu.md](tsu.md) row 49 | only on busy lines where these registers change | - | **fixed**: confirmed and measured with `tools/machines/tsconf/rtl-sim` (`tsulatch`: an object is late after `split` TSU DRAM cycles of the pass, 179-277 by mode); the engine draws the pass up to that position at `ts_start` and the rest at `line_start` with L's latch (`TsConfTsu::BeginLine` / `FinishLine`); the tile count per layer (row 30) and the prefetch's extra cycle (row 42) follow the RTL too; test `TsConfEngine_Test.TSU9_ObjectsAfterLineStartTakeTheNewLatch` |
| 14 | G_X_OFFS in ZX and TXT is a pixel scroll; RTL loads the fetch column with G_X_OFFS[8:2] (ZX: 16 px steps, bit 2 swaps pixel / attribute fetch; TXT: counted in 14 MHz pixels) | [video.md](video.md) rows 28-29 | wrong scroll in ZX / TXT with a nonzero X offset (rare) | `TSO2_RendererMatchesTheReference` used the same rule in its reference | **fixed** from the Verilog itself: `tools/machines/tsconf/rtl-sim` (Verilator) captured 174 lines; `ScreenTSConf::ZxSourceOf / TxtSourceOf` drive the renderer and the video mapper; test GX1 compares every captured line index for index; TSO2 reference and hash updated |
| 15 | INT vector chosen when INT is sampled, not at the acknowledge (~3 clocks later) | [interrupts.md](interrupts.md) row 20 | edge case | - | **fixed**: `AcknowledgeInterrupt` latches up to the IORQ (+3 clocks), drops a frame pulse that ended there, keeps `int_sel` (`TsConfState::intSel`) when nothing is left; tests INT12, INT12b |
| 16 | VGSYS IN `#FF` returns `#FF` for a virtual drive / inside vdos; RTL drives `{intrq, drq, 111111}` | [memory-ports.md](memory-ports.md) B4 | small | - | **fixed**: VGSYS reads `{INTRQ, DRQ, 111111}` for every drive select and inside vdos (the low 6 bits were the Beta interface's 0s too); test VDOS3 |
| 17 | `#xxF7` ownership reversed (A8 = 0 to the ZX-Bus outside DOS, A8 = 1 mainboard in DOS) | [memory-ports.md](memory-ports.md) B5 | only with a ZX-Bus card decoding `#xxF7` | - | **fixed**: `ClassifyPort` gives every `#xxF7` to the mainboard outside DOS, the ZX-Bus in DOS (the CMOS ports inside vdos excepted); the CMOS handler needs A8 = 1; test `GlukPortOwnershipFollowsDos` |
| 18 | Debug video mapper reports the full BORDER in TXT instead of `{PAL_SEL[3:0], BORDER[3:0]}` | [video.md](video.md) row 48 | debug view only | `MAP5_TextCellsAndBorder` | **fixed**: `BorderSources` gives `{PAL_SEL[3:0], BORDER[3:0]}` in TXT; MAP5 checks TXT and 256C |

## Gaps (unreal-ng follows the fork, not the RTL)

- ~~No 4-5 fclk stall on DOS entry / vdos exit - row 37.~~ **Fixed:** `PortDecoder_TSConf::DosStall` (4 fclk at every speed; a VG93 access that ends vdos at 14 MHz takes only this one, as `zclock.v` loads the DOS count); test TIM7.
- No /WAIT on `#BFF7` - row 39. **Deferred, with a plan:** the Gluk wait is the same AVR round trip as the COM
  port's (`zwait.v` `wait_status_glu` beside `wait_status_com`, one AVR main loop), whose timing model lives in
  `Uart16550::AccessCycles`. Doing it right moves that model (the main-loop phase, the service cycles, its TTD state)
  into the shared `EvoAvr` for both ports and both machines (TS-Conf, ATM3), then counts the Gluk service in the
  AVR listing - not a local patch.
- ~~IDE stall off by default (`IdeStall=0`) - row 38.~~ **Fixed:** on by default as the RTL (`top.v:557`), owner
  decision 2026-10-05 (was D2, off); `IdeStall=0` stays as the bypass. Test IDE4.
- ~~The NMI button works on TS-Conf, whose board never drives /NMI - row 41.~~ **Fixed:** `RequestBoardNmi` takes the press and does nothing (test `NmiButtonDoesNothing`); the debugger's direct NMI request stays.
- ~~A frame pulse running across a CPU clock switch is cut short - row 25.~~ **Fixed:** `TsConfInterrupts::BeforeClockSwitch / AfterClockSwitch` carry the counted clocks across (`intFrameAdjust` keeps the CPU-clock part, the blob size is unchanged); tests CLK3, CLK3b.
- The cache is cleared at reset and when disabled; the RTL never clears it (and the comment at
  `portdecoder_tsconf.cpp:220-222` says so, contradicting the code) - [memory-ports.md](memory-ports.md).
- W0_WE with ROM in window 0 should write the flash (`zmem.v:297`); both emulators drop the write.

## Known approximations (documented in unreal-ng, kept unless a program needs them)

- TSU registers and SFILE read at two points, `ts_start` and `line_start` (for the objects after it), not
  continuously ([tsu.md](tsu.md) rows 47-48).
- ~~Tile cost counts only tiles touching the window ([tsu.md](tsu.md) row 30).~~ **Fixed** with item 13: `width / 8 + 1`
  tiles per layer, as the RTL.
- Cache not filled while every window has it off ([interrupts.md](interrupts.md) row 31).
- DMA device 7 (wait port, AVR) not served ([dma.md](dma.md) row 19).
- GFXOVR and the 360-wide TSU window (T_CONFIG bit 0) on in every build, decision D1 (`hardware-spec.md` §0.1).

## Unclear (needs an RTL simulation or a hardware test)

- ~~14 MHz data-read wait (row 33).~~ **Settled by simulation** (`tools/machines/tsconf/rtl-sim`, `tsconf-cpu-sim`:
  the real zclock / zsignals / zmem / arbiter with a bus-cycle Z80): unreal-ng's +4..+7 fclk is right - the Z80 takes
  read data one half-clock after M1 data, from the cache data register; the `zmem.v` comment table is right for M1
  and 2 fclk short for reads. All 118 instruction loops in every video mode match fclk for fclk.
- ~~`stall357` (row 35).~~ **Settled:** it never fires at 3.5 / 7 MHz (384 simulated cases); no DRAM waits there,
  as unreal-ng has it.
- ~~**New (from the simulation):** a refused video cycle in 256C / TXT was charged by unreal-ng to the next DRAM
  access.~~ **Fixed 2026-10-05:** `TsConfArbiter` now simulates the refused cycles fclk by fclk as the RTL stops the
  Z80 clock (`stall14_cyc = memrd ? stall14_cycrd : !cpu_next`: stopped in every fclk without a memory read seen on
  the pins, the edge already on its way still comes) and charges the stopped fclk to the machine cycle whose clock
  edge they delay, settling on the following machine cycles (ROM / cache-hit reads and opcode fetches included).
  Test `TsConfArbiter_Test.ARB6_CpuWaitsMatchTheRtl` replays all 1170 14 MHz tests of `results/cpu-waits.txt`
  (copy in `testdata/machines/tsconf/rtl-sim/`) through the emulated CPU's bus cycles and matches every machine
  cycle's length; before the fix 48 differed (16 isolated 256C writes, 16 + 13 "three writes and a read" in
  256C / TXT, 3 `PUSH` loops in 256C, 21 of them in total time). Reference: pin delay 1; with a Z80 slower than
  41 ns (pin delay 2) the RTL moves the overlap with the next read by one fclk (20 tests), a race the hardware has
  too.
- 3 / 4-bit VDAC curves: no such board or firmware to compare ([video.md](video.md) row 46).
- Whether the fetch window 4 dots wider than the picture costs DRAM slots ([video.md](video.md) row 47).
- IDE DMA hangs on real VDAC firmware, works in unreal-ng with an IDE scheme ([dma.md](dma.md) row 32).

## Tests missing for behaviors that match

About 40 rows match in all three but have no unreal-ng test (TSU wraps and flips, frame constants, V_CONFIG
mid-line latching, FLASH rate, DMA counter stepping, masked / vdos-deferred DMA INT, register readback, ...). Each
area file's "Bugs and gaps" section names the test and its assertion.

## Spec corrections

`hardware-spec.md` §3.4, §4.2, §4.3, §4.4, §9, §12 - listed in [README.md](README.md) "Findings outside the code".
