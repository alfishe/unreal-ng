# 036 — analysis

How the RTL's numbers (`measurements-2026-10-26-rtl-traces.md`) come out of the core 3.02.03 sources, and why this emulator differed.

## The time origin: ours is the RTL less 2 T

The T80 registers INT on the rising edge that starts an instruction's last T and acts on it a clock later; this emulator checks the
line at the instruction's end. Both run the same prologue (EI / three HALTs), and the HALT's M1 against the INT only agrees in all
four phases with an offset of 2 T: a body that starts `n` T after its INT on the RTL starts `n - 2` here. With that origin the
48K / 128K contention lead of 5 T (the wait pattern 6,5,4,3,2,1,0,0 starting at the line's T 14335 on the 48K) is right; a lead of
4 seen before was the offset of 1 T in the harness.

## Where a fetch's wait goes

The 48K / 128K hold the CPU clock (`o_cpu_contend`, zxula.vhd 583-592) before M1 falls; the +3's WAIT (`o_cpu_wait_n`) is sampled
from T2, after M1 falls. This emulator inserts a fetch's wait after its M1 hook. So the instruction starts are compared at the opcode
fetch: the RTL's M1 end less 12 clocks, ours the first read at the PC less its modal offset from the M1 hook.

## 48K / 128K

`o_cpu_contend = wait_s and ((mem_contend and mreq23_n and ioreqtw3_n) or (port_contend and not iorq_n and ioreqtw3_n))`, with
`mreq23_n` / `ioreqtw3_n` the strobes registered on the CPU clock's rising edge.

- `mem_contend` (zxnext.vhd 4508) is decoded from the address on the bus alone through the MMU: any cycle whose address is in a
  contended page waits at its first T, whatever the strobe. That includes the T80's internal T-states (IR or the last address on the
  bus) and a port cycle whose high byte maps to contended memory (the classic C:1 pattern). The Z80N's own internal T-states
  (the extra T of `NEXTREG`, `MUL`, `PIXELAD`...) were not contended here: fixed with a `Contend` hook in the Z80N engine.
- `port_contend` (zxnext.vhd 4515: A0 = 0, #7FFD, the ULA+ pair) holds the clock in T2, once IORQ is low on the rising edge.
  Ours had no high-byte contention on the Next (an early return): fixed.

## +3

`o_cpu_wait_n` is low only for MREQ cycles to contended memory (the port term is commented out), gated by `wait_s`. There is no I/O
contention and no contention of internal T-states. The T80 samples WAIT from T2, so the waits of an access are the count of grid
cells `y` from `x + 1` with `0 <= y < 128` and `y mod 8 != 1`: the pattern 1,0,7..2 starts 3 T before the ULA's, its last cell is
1 T shorter, and the +3's line 0 starts 1 T later after INT than the 128K's (`c_int_h` 126 against 128). Ours used the 48K's table
shifted: 372 instruction durations of the `mix` program were off; with `UlaContention::SetWindowedWait` none is.

## The DMA

- Hand-over (dma.vhd FSM): the enable is written in the enabling instruction's last cycle, so BUSREQ is seen at the next
  instruction's M1 and the bus goes over after it; the first read starts 1 T after BUSAK; the CPU runs again 4 T after the last write
  (FINISH_DMA, then IDLE); in the burst mode the prescaler wait gives the bus back for 3 T. Each byte with the default timing is
  3 + 3 T. Ours started the transfer one instruction early and had no hand-over cost: the NOP after the enabling OTIR was 1551 T,
  the RTL's 1545.
- Contention: the DMA runs on the CPU's clock (`clk_i => i_CLK_CPU`), and while it holds the bus `cpu_a` / `cpu_mreq_n` /
  `cpu_iorq_n` are its own, so its cycles meet the same logic. Its strobes are shaped on the falling edge (dma.vhd 150-186): the
  address goes out on the rising edge that starts the cycle, MREQ / IORQ fall half a T later. So `mem_contend` holds its first T as a
  CPU's T1, but the port term never does: by the next rising edge `ioreqtw3_n` is already low. A DMA write to port #FE in the paper
  area is not contended (the `dmapaper` block 1: 8 T a byte, the 2 waits of the bank-5 read only); ours contended it as the CPU's
  I/O and took 2367 T for the RTL's 1809.
- The T-states without a strobe: while the DMA holds the bus its address stays on it (the source from WAITING_ACK on and again
  after each write, dma.vhd 287 / 414), and `mem_contend` has no strobe term, so on the 48K / 128K the hand-over T-states wait too
  when the source is in contended memory (block 1 ends with #4100 on the bus: 8 T on the 48K and the 128K). Ours now runs the
  hand-over T by T through the same contention as a cycle without MREQ (`NextDma::HoldBus`).
- The +3: only MREQ cycles wait (`dma_wait_n` carries the video logic's WAIT), but the DMA registers WAIT on the clock's falling
  edge (`wait_n_s`) and reads it in READ_4 / WRITE_4 on the rising one: half a T before the T80 samples the line. On the grid of
  `WindowedGateArrayWait` its free T of each cell is one earlier (y mod 8 = 0 instead of 1), the window the same; the rule was fitted
  to all 2 x 255 byte spacings of `dmapaper-p3` (reads and writes) and is the only family of the search that fits both blocks with
  the CPU's window (`UlaContention::DmaDelayAt`).

## NR #08 bit 6

`i_contention_en` takes the effective switch (`eff_nr_08_contention_disable`, zxnext.vhd 5822), which follows the register at
`hc(8) = 1`, the end of the line's contention window. Ours switches there (`UlaContention::LineWindowEndFrom`) and matched on all
three timings, through `NEXTREG` and through the ports.
