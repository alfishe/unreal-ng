# Co-simulation trace format

One text file per run (`trace.txt`), one line per event, five whitespace-separated fields, the same in every
emulator (jnext, ZEsarUX, MAME patches and our own `NextCosimTrace`):

```
<seq> <kind> <addr> <value> <pc>
```

| field | meaning |
|---|---|
| `seq` | decimal event number, from 0, increasing by 1 over the whole file (all kinds share it) |
| `kind` | see below |
| `addr` | NR number (2 hex digits), 16-bit port (4 hex digits), slot (1 digit), PC (4 hex digits) - per kind |
| `value` | 2 hex digits (the byte read / written / the 8K page) |
| `pc` | 4 hex digits: the address of the first byte of the instruction that caused the event (see "PC") |

Numbers are upper-case hex without a prefix, except `seq` and the `FRM` frame number (decimal).

## Kinds

| kind | addr | value | when |
|---|---|---|---|
| `NRW` | NR number | value written | a NextREG write by the CPU: `OUT (#253B)` after `OUT (#243B)`, or the `NEXTREG` instructions. Copper / DMA / internal writes are not reported |
| `NRR` | NR number | value the CPU read | `IN (#253B)`; the number is the register selected by the last `OUT (#243B)` |
| `POUT` | 16-bit port (the full BC / A:n address) | value written | an I/O write cycle, except `#243B` / `#253B` (those are NRW / NRR) |
| `PIN` | 16-bit port | value read | an I/O read cycle, except `#243B` / `#253B` |
| `MMU` | slot 0-7 (8K slot of the Z80 address space) | effective 8K RAM page `00`-`DF`, `FF` = ROM / not RAM | a slot's mapping changed (legacy `#7FFD` / `#1FFD` / `#DFFD` paging, NR `#50`-`#57`, config mode, boot ROM). The first 8 lines are the initial mapping. Reported when the next instruction starts (references) or after it ran (ours), with the pc of the instruction that changed it |
| `IRQ` | vector low byte, `00` when the emulator does not know it | `00` | a maskable interrupt was accepted. `pc` is the interrupted instruction's address |
| `PCS` | the sampled PC | `00` | a PC sample: every N instructions (`COSIM_PCS=N`) and / or every instruction of the frames in `COSIM_PCWIN`. MAME samples every M1 cycle, so a prefixed instruction gives one line per prefix byte |
| `FRM` | frame number (decimal, the first frame is 1) | `00` | a video frame ended. The `pc` is the PC at that moment |

`MMU` page numbering: the Next's 8K page of the 2 MB RAM (`00`-`DF` = 16K bank n is pages `2n`, `2n+1`; bank 5 =
`0A`/`0B`, bank 2 = `04`/`05`, bank 0 = `00`/`01`). Any ROM mapping (the boot ROM, the 48K / 128K / +3 ROMs, the
config-mode RAM-as-ROM) is `FF`: which ROM is not compared.

## PC

The PC is the start of the instruction (the address of its first opcode byte, the prefix for `CB` / `DD` / `ED` /
`FD` instructions). jnext, ZEsarUX and ours capture it at the start of the instruction; MAME reports its CPU's
`pcbase`. A DMA transfer's port accesses and an interrupt acknowledge carry the PC of the instruction that was
running. The diff tool does not compare PCs (it prints them as context): emulators differ in the PC attributed to
a write of a multi-cycle instruction.

## Run control and the dump

Every emulator is driven by the same environment variables (`run-ref.sh` and the patched emulators read them; our
test reads them too):

| variable | meaning |
|---|---|
| `COSIM_TRACE=<file>` | write the trace there; unset: tracing off (the patched emulators then cost one mask test per hook) |
| `COSIM_KINDS=NRW,NRR,...` | the kinds to write (default all but `PCS`) |
| `COSIM_PCS=N` | one `PCS` line every N instructions |
| `COSIM_PCWIN=a:n,a:n` | a `PCS` line for every instruction of frames `[a, a+n)` (frames count from 1) |
| `COSIM_PORT_SKIP=eb,e7` | low port bytes (hex) left out of `POUT` / `PIN` - the SPI data port `#EB` alone is ~90 % of the port events |
| `COSIM_FRAMES=N` | run N frames, then dump and exit |
| `COSIM_DUMP=<dir>` | where the dump goes |

After frame N the emulator writes, into `COSIM_DUMP`, the end-of-run dump:

* `state.txt`:
  ```
  emulator <name>
  frames <N>
  regs PC=.... SP=.... AF=.... BC=.... DE=.... HL=.... AF2=.... BC2=.... DE2=.... HL2=.... IX=.... IY=.... I=.. R=.. IM=n IFF1=n IFF2=n HALT=n
  mmu <slot> <page>          (8 lines)
  nr <reg> <value>           (256 lines, NR #00-#FF as the CPU would read them)
  ```
* `screen.png` (ZEsarUX writes `screen.bmp`, which `run-ref.sh` converts): the picture at that moment, to see whether
  the menu is up.

`run-ref.sh <emulator> <card> <frames> <out-dir>` puts `trace.txt`, `state.txt`, `screen.png` and `run.log` into
`<out-dir>`.

## Noise worth knowing

* The SD card is driven through `POUT #E7` (select) and `POUT/PIN #EB` (data): ~a million `PIN #00EB` events per
  1500 frames. `COSIM_PORT_SKIP=eb` (and `e7`) drops them; the diff tool also takes `--skip-ports eb,e7`.
* Keyboard scans (`PIN #7FFE`, `#BFFE`, ... one per half-row, every frame) are most of the rest.
* Polling loops (the same `NRR` / `PIN` over and over) differ in length between emulators: `diff-traces.py`
  collapses runs of an identical (addr, value) and ignores the count.
