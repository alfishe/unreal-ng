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
| 3 | Line INT fires at tact 224n-1; RTL and the fork at 224n (tact 0 of the next line) | [interrupts.md](interrupts.md) row 11 | one tact early; raster code timed to the line INT drifts by a tact | `INT3_LineInterrupts` (asserts 223) | open |
| 4 | 5-bit VDAC levels: unreal-ng scales 31 to 255 and truncates; the VDAC1 CPLD (`vdac/vdac1/cpld/top.v`) is the VDAC2 table - `{level,3'b0}` with PAL_SEL, the rounded 0..24 table without | [video.md](video.md) row 44 | colors off by one level (white 255 instead of 248) on VDAC boards; fix: route `vdac == 3` through `Vdac2Level` | `VDAC_CurvesStatusAndRender`, `VDAC2_CardTable` | open |
| 5 | OUT `#FE` builds BORDER from the unlatched PAL_SEL; RTL uses the copy latched at line start (`video_ports.v:109`) | [video.md](video.md) row 16, [memory-ports.md](memory-ports.md) B2 | wrong border bank when PAL_SEL changes within a line before an OUT `#FE` | `BorderWriteUsesPalSel`, `VID4_Border` | open |
| 6 | SPI / IDE DMA charged 10 DRAM cycles per word against the free budget; RTL: 34 fclk per word (two SPI exchanges), one DRAM cycle, independent of the video mode | [dma.md](dma.md) rows 30-31 | SD loads 15% slow without video, ~1.8x slow under 256C | - | open |
| 7 | CPU RAM writes are not taken from the DRAM budget (DMA and TSU); RTL and the fork count them | [dma.md](dma.md) row 46, [tsu.md](tsu.md) row 40 | write-heavy code leaves DMA / TSU more slots than the hardware | - | open |
| 8 | Video fetch cost per line 1-4 cycles low: `video_go` lasts w+4 dots (ZX 33, 16C 81, 256C 162, TXT 164); `TsConfArbiter::FetchOf` already has the right window | [dma.md](dma.md) row 42 | DMA / TSU budget slightly high | - | open |
| 9 | Free DRAM cycles spread evenly over the line; RTL and the fork run DMA at full rate in the border | [dma.md](dma.md) row 43 | DMA words (and TIM-5 CRAM writes) land at the wrong point in the line | - | open |
| 10 | `#BFF7` (CMOS) reads return AVR data inside vdos; RTL reads `#FF` (`zports.v:330` `!dos`) | [memory-ports.md](memory-ports.md) B1 | DOS code reading the clock inside vdos sees data the hardware hides | `VDOS2_CmosInsideVdos` | open |
| 11 | IDE over-decode: `(low & 0x1E) == 0x10` takes `#31..#F1` as IDE registers; RTL only `#11` among the odd ports (the fork has the same bug) | [memory-ports.md](memory-ports.md) B3 | ports of other devices answered by the IDE | - | open |
| 12 | A cut-off sprite / tile at the end of the TSU line is dropped whole; RTL and the fork draw it up to the cut, 4 px per DRAM word | [tsu.md](tsu.md) row 39 | missing partial objects on overloaded lines | `TSU8_StarvedObjectsAreDropped` | open |
| 13 | Tiles processed after `line_start` of L use the L-1 G_PAGE / X offsets / PAL_SEL; RTL uses L's latched set (the fork behaves like unreal-ng) | [tsu.md](tsu.md) row 49 | only on busy lines where these registers change | - | open |
| 14 | G_X_OFFS in ZX and TXT is a pixel scroll; RTL loads the fetch column with G_X_OFFS[8:2] (ZX: 16 px steps, bit 2 swaps pixel / attribute fetch; TXT: counted in 14 MHz pixels) | [video.md](video.md) rows 28-29 | wrong scroll in ZX / TXT with a nonzero X offset (rare) | `TSO2_RendererMatchesTheReference` uses the same rule in its reference | open |
| 15 | INT vector chosen when INT is sampled, not at the acknowledge (~3 clocks later) | [interrupts.md](interrupts.md) row 20 | edge case | - | open |
| 16 | VGSYS IN `#FF` returns `#FF` for a virtual drive / inside vdos; RTL drives `{intrq, drq, 111111}` | [memory-ports.md](memory-ports.md) B4 | small | - | open |
| 17 | `#xxF7` ownership reversed (A8 = 0 to the ZX-Bus outside DOS, A8 = 1 mainboard in DOS) | [memory-ports.md](memory-ports.md) B5 | only with a ZX-Bus card decoding `#xxF7` | - | open |
| 18 | Debug video mapper reports the full BORDER in TXT instead of `{PAL_SEL[3:0], BORDER[3:0]}` | [video.md](video.md) row 48 | debug view only | `MAP5_TextCellsAndBorder` | open |

## Gaps (unreal-ng follows the fork, not the RTL)

- No 4-5 fclk stall on DOS entry / vdos exit (the ATM3 decoder models it) - [interrupts.md](interrupts.md) row 37.
- No /WAIT on `#BFF7` - row 39; IDE stall off by default (`IdeStall=0`) - row 38.
- The NMI button works on TS-Conf, whose board never drives /NMI - row 41.
- A frame pulse running across a CPU clock switch is cut short - row 25.
- The cache is cleared at reset and when disabled; the RTL never clears it (and the comment at
  `portdecoder_tsconf.cpp:220-222` says so, contradicting the code) - [memory-ports.md](memory-ports.md).
- W0_WE with ROM in window 0 should write the flash (`zmem.v:297`); both emulators drop the write.

## Known approximations (documented in unreal-ng, kept unless a program needs them)

- TSU registers and SFILE read as one snapshot at `ts_start` ([tsu.md](tsu.md) rows 47-48).
- Tile cost counts only tiles touching the window ([tsu.md](tsu.md) row 30).
- Cache not filled while every window has it off ([interrupts.md](interrupts.md) row 31).
- DMA device 7 (wait port, AVR) not served ([dma.md](dma.md) row 19).
- GFXOVR and the 360-wide TSU window (T_CONFIG bit 0) on in every build, decision D1 (`hardware-spec.md` §0.1).

## Unclear (needs an RTL simulation or a hardware test)

- 14 MHz data-read wait: unreal-ng +4..+7 fclk vs the `zmem.v` comment's +2..+5 ([interrupts.md](interrupts.md) row 33).
- `stall357` (3.5 / 7 MHz DRAM stall) never applied; analysis says it cannot trigger in the four modes (row 35).
- 3 / 4-bit VDAC curves: no such board or firmware to compare ([video.md](video.md) row 46).
- Whether the fetch window 4 dots wider than the picture costs DRAM slots ([video.md](video.md) row 47).
- IDE DMA hangs on real VDAC firmware, works in unreal-ng with an IDE scheme ([dma.md](dma.md) row 32).

## Tests missing for behaviors that match

About 40 rows match in all three but have no unreal-ng test (TSU wraps and flips, frame constants, V_CONFIG
mid-line latching, FLASH rate, DMA counter stepping, masked / vdos-deferred DMA INT, register readback, ...). Each
area file's "Bugs and gaps" section names the test and its assertion.

## Spec corrections

`hardware-spec.md` §3.4, §4.2, §4.3, §4.4, §9, §12 - listed in [README.md](README.md) "Findings outside the code".
