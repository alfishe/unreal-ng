# Audit of the DMA, interrupts and timing against the RTL (2026-10-09)

Method: two read-only audits of our code against the official core 3.02.03 (`dma.vhd`, `zxnext.vhd`, `zxula_timing.vhd`, `zxula.vhd`,
`t80n_mcode.vhd`, `t80na.vhd`), jnext, ZEsarUX and MAME, then fixes with tests, then live checks (MCP, port trace, the board test
programs through the NextZXOS Browser). One emulator is never the only reference: where the RTL and an emulator disagree the RTL wins,
and where only arithmetic exists (no board numbers) the item stays marked "arithmetic".

## Fixed (with tests)

| Area | RTL | What we did before | Now | Test |
|:--|:--|:--|:--|:--|
| CTC interrupts, plain IM2 (pulse mode) | `im2_peripheral.vhd`: every enabled request pulses INT; the CTC enable is its control bit 7 | ignored outside hardware IM2, gated by NR #C5 | pulse-mode `Raise`; NR #C5 maps onto the channel control bits | `NextInterrupts_Test.CtcRequestPulsesTheIntLineInPulseMode` |
| DMA last byte | `dma.vhd` WRITE_4: the prescaler gate is checked before the block counter | the last period was skipped | the end-of-block flag, the stop and the auto-restart come after the last period | `TheLastBytesPrescalerPeriodIsWaitedBeforeTheBlockEnds`, `AutoRestartKeepsTheByteSpacingAcrossTheSeam` |
| End-of-block flag on auto-restart | only LOAD, CONTINUE, `0x8B`, reset clear it | cleared by the reload | stays set | `AutoRestartLeavesTheEndOfBlockFlagSet` |
| Bus in the prescaler wait | only burst (WR4 mode 10) releases it | any mode released it | continuous keeps the CPU stopped | `OnlyBurstModeReleasesTheBusInThePrescalerWait` |
| Reset | reset block of `dma.vhd`, hard and soft | only the machine reset touched the DMA | both reset it; addresses, length, direction, port types stay | `ResetKeepsTheAddressesAndClearsTheRest` |
| DMA cycles on #6B/#0B | `port_dma_rd/wr ... and not dma_holds_bus` | the DMA could program itself | those cycles are ignored | (integration) |
| zxn / Z80 mode latch | set by every read and write of either port | writes only | `ReadAs()` | `AReadThroughEitherPortSetsTheModeLatch` |
| Status bit 0 | `status_atleastone` | never set | set while a byte was moved and the DMA is not idle | `StatusBitZeroIsSetBetweenTheBytesOfARunningBlock` |
| NR #CC / #CE reads | masked (`#83`, `#77`) | raw value | masked | `DmaInterruptEnableRegistersReadBackTheirMaskedBits` |
| DMA delay (NR #CC-#CE) | `im2_dma_delay`: a chosen source requested or in service (until its RETI), or an NMI with #CC bit 7, holds the DMA | stored only | `NextInterruptSource::DmaDelay`; hardware IM2 mode only | `ChosenInterruptsDelayTheDmaUntilTheirReti` |
| DMA to / from SPI #EB | `dma_wait_n` includes `spi_wait_n` (16 clocks) | bytes dropped | the DMA waits | (integration) |
| Pentagon INT pulse | `pulse_count_end`: 36 for 128K and Pentagon, 32 for 48K and +3 | 32 | 36 | `NextTiming_Test.IntPulseLengthPerTiming` |
| Line interrupt | `int_line` at `hc_ula = 255` of the row before the target | at the row start | 128 T into that row | `LineInterruptPulsesAtItsLine` |
| `NEXTREG` bus shape | `t80n_mcode.vhd` X"91" / X"92": the trailing cycles are MREQ/RD reads (`NoRead = 0`); `Z80N_dout_o` rises with the first one | idle cycles, write 3 T late | contended reads, write at +14 T (`n,v`) / +11 T (`n,A`) | `Z80nOpcodes_Test.NextregCallsTheHostWithoutAPortCycle`, `NextSkeleton_Test.NextRegStreamInContendedBank5TakesTheSixCycleWaits` |

| 28 MHz SRAM read wait | `zxnext.vhd` 3171-3181: +1 clock per CPU read that reaches the SRAM or bank 5; not bank 7 BRAM (page #0E), unmapped slots, boot ROM; writes / I/O / refresh never | none | `NextMemory::SetSramWait28`, rides on the contended interface | `At28MHzSramReadsWaitOneClockAndBank7DoesNot` |
| Video bank 7 = 8K BRAM | `zxnext.vhd` "ULA BANK 7 (8k only ...)": video address bits 12:0, page #0E | tilemap / LoRes read 16K of bank 7 | wraps at 8K | `NextTilemap_Test.Bank7MapOffsetWrapsInTheEightKilobyteBram` |

Mapscroll 3 (`demos/tech-demo`, seedy1812) is the case for the last row: it writes its map through MMU page 14 at offset 0 and points NR #6E at #A0
(bank 7, offset #2000), so only the 8K wrap shows the map. After the fix one of our frames equals the author's README screenshot pixel for
pixel (the ULA showing bank 5's tile data right of and below the clipped tilemap window is the demo's own look: `nextreg $68` ULA-off is commented out
in its source).

## Changing8kBank (the contention-ON timing test)

Port-traced on the emulator (`OUT #FE` start / end of the green border), through the Browser, contention ON / OFF:

| | interval | lines |
|:--|--:|--:|
| ON (`Chg8kBan.snx`) | 56 817 T | 249.2 |
| OFF (`Chg8kB_2.snx`) | 42 671 T | 187.2 |
| difference | 14 146 T | 62.0 |

RTL arithmetic (+3 timing, six contended cycles per `NEXTREG n,v`, 48 T per instruction in the window): +14 001 T = 61.4 lines; the old
four-cycle model gave 47.7. The OFF interval agrees with the program: 2048 x 20 T + 128 DJNZ = 42 619 T plus 52 T of timing calls. MAME
has **no** contention (`specnext.cpp`: "TODO: contention", `SPECTRUM_ULA_UNCONTENDED`), so its two photographs are identical and prove
nothing. There are no real-board numbers; this item stays "arithmetic" until a board run exists.

## Open

| Item | RTL | Note |
|:--|:--|:--|
| INT pulse in CPU clocks | counted on the CPU clock | ours is in base T scaled by the speed, 8x too long at 28 MHz |
| CPU speed change | latched within the instruction | ours at the next frame start |
| 60 Hz timing | `zxula_timing.vhd` 216-308 | NR #05 bit 2 ignored |
| NR #03 timing decode | bit 3, user lock, codes 101-111 map to +3 | ours ignores them |
| NR #08 contention latch | at `hc(8)` | ours at once |
| Port contention details | `#BF3B`, `#FF3B`, per-8K-page decision | ours ORs two halves |
| Other contended internal cycles (48K / 128K) | `o_cpu_contend` also for no-MREQ cycles | moderate confidence |
| DMA bus contention / handover latency | DMA cycles go through the same waits | not modelled |
| DMA power-on values, register read from #6B by the DMA | no initialisers in the VHDL | not determined |
| `Other Z80N opcodes` MREQ shapes | only `NEXTREG` was checked | PUSH nn, ADD rr,nn, LDIRX ... |

## Demonstration of the DMA sample demo

`demos/tech-demo/DMA Sample Engine Demo` (CTC build): IM2, CTC channel 0 (control `#87`, time constant `#DB`, prescaler 16 ~ 8 kHz), the
ISR writes a sample to `OUT (#FFDF)` (the DAC). Before the CTC fix it ran one OUT per frame (silent); now 14 310 `OUT #FFDF` in 1.76 s.
Recipe: [.recipe/machines/next.md](../../../.recipe/machines/next.md) (section 6).
