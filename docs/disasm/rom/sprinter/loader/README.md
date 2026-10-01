# ROM page `#C`: the PLD configuration loader

Listing: [bios304-pc-loader.asm](bios304-pc-loader.asm) · symbols:
[bios304-pc-loader.map](../../../../../data/symbols/sprinter/bios304-pc-loader.map) · overview:
[../README.md](../README.md)

At power-on the ACEX PLD that implements almost the whole Sprinter is empty. Only the small EPM7064
CPLD works: it shows the ROM (pages `#C-#F` at `#0000-#FFFF`) and turns every CPU memory write into
one configuration clock with data bit D0 (BIOS-TT 0271ac3 `src/altera/max/SP2_MAX.TDF`). The CPU runs
this loader from `#0000`.

## Layout

| Range | Name | Contents |
|---|---|---|
| `#0000-#003A` | `LoaderStart` | Z84C15 system registers (`#EE`/`#EF`: chip-select boundary `#FE`, so `#FE00-#FFFF` is fast RAM), SIO A, PIO |
| `#0038`, `#0066` | `IntRestart`, `NmiRestart` | `JP #0000` |
| `#003B-#006B` | `CheckReloadRequest`, `StreamFromRom` | `"ACEX_30K_LOADING"` at `#FEF0` → stream from RAM `#1000`, else from ROM `#0100` |
| `#006C-#0087` | `PickDestination` | DE = `#FE00` (`#FD00` if `#FEE0` holds `"IM"`); IY = `#0107`, IX = `#FFFD` for the BIOS |
| `#0088-#009B` | `StreamLoop` | 8 writes per byte, `RRCA` between them; no exit |
| `#009C` | `Halt` | `DI : HALT` (never reached) |
| `#009E-#00BD` | `ReloadString`, `PostCodeTable` | `"ACEX_30K_LOADING"`, the POST code table |
| `#0100-#E84E` | | the bitstream: 59 215 bytes, identical to BIOS-PP `ALTERA/SP2K_304.BIN` |

## How many writes (Q4)

Each byte is written 8 times with `LD (DE),A`, rotated right between the writes, so bit 0 goes first:

```text
byte #A5 = 1010 0101  ->  D0 of the 8 writes: 1, 0, 1, 0, 0, 1, 0, 1
```

There are no other memory writes before the stream (no `CALL`, no `PUSH`; the set-up is all `OUT`).
So the configuration is complete after

```text
59 215 bytes x 8 writes = 473 720 memory writes  (= 473 720 configuration bits)
```

The loop itself never stops: it keeps reading past `#E84E` (into the `#FF` filler) until the
configured PLD resets the CPU. Writes go to `#FE00-#FEFF` (only E is incremented), i.e. fast RAM.
The figure and how the end is detected are recorded in
[tdd-ports-memory.md §6](../../../../inprogress/2026-09-28-sprinter/tdd-ports-memory.md); a runtime
trace (MAME, or the emulator from S1 on) should confirm when CONF_DONE rises and how many extra
clocks the PLD takes before the reset.

Compared with BIOS-TT `bios/loader/loader.asm`: the same 8-write loop; the newer loader adds a check
for a packed / RLE bitstream header that 3.04 does not have, and does not set the Z84C15 SIO / PIO
registers that 3.04 does at `#001C-#0034`.
