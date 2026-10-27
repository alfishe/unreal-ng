# ZX Spectrum Next: hardware probe programs (an autonomous proof of concept)

**Status:** ready to run, **never run yet** (no board at hand when written, 2026-10-08). Nobody needs to know the
unreal-ng project to use this folder: it is four small programs, instructions and a list of what each result means.
Run them when a real ZX Spectrum Next is within reach; there is no deadline.

## Why

The emulator we are designing ([the Next design](../2026-10-07-zx-next/README.md)) follows the FPGA's VHDL source. That
source does not give numbers for a few things that decide how exact the emulator has to be, and no public test measures
them on a real board. This POC does.

| Program | Question answered | Feeds |
|:--|:--|:--|
| `h1timing` | how many instructions per frame the CPU really runs at 3.5 / 7 / 14 / 28 MHz; what a memory read costs at 28 MHz (the SRAM wait state); what contention costs at 3.5 MHz; lines per frame | N2-N3 timing tables, the contention model, the 28 MHz wait rule |
| `h2dma` | how long the zxnDMA takes for 4096 bytes at each CPU speed, with port timing 4 / 3 / 2 cycles and in burst mode | N8 (DMA bus agent), the DMA cycle model |
| `h3sprites` | at which number of sprites on a line the hardware sets "too many sprites on a line", for 8-bit and 4-bit patterns, scaled sprites and sprites spread over lines (a COUNT limit or a TIME budget?) | N7 (sprite line budget instead of a fixed count) |
| `h4z80n` | what `LDIRSCALE` (`ED B6`), `LDIRX` and `LDPIRX` really do | the Z80N library (N1) |

## What you need

- a ZX Spectrum Next (any board) with NextZXOS on its SD card;
- the four files in [`out/`](out/) (`h1timing.nex`, `h2dma.nex`, `h3sprites.nex`, `h4z80n.nex`) copied to the card
  (any folder);
- a phone or camera to photograph the screen. (Or read the numbers aloud; they are hexadecimal digits.)

The programs do **not** read or write the SD card, change no setting that survives a reset, and need no Wi-Fi. After each
run press the reset button (or F4 soft reset) to return to NextZXOS.

## How to run

1. In the NextZXOS browser select a `.nex` file and press Enter.
2. The screen shows the program id in the top left, then the core version, board and machine type, then the results
   fill in. The programs are done when `D0DE` appears in the bottom row.
3. Photograph the screen after `D0DE` appears. (`h1timing` takes a few seconds, `h2dma` a few seconds,
   `h3sprites` about one to two minutes, `h4z80n` a second.)
4. Name the photo `<program>-<core version>-<yyyymmdd>.jpg`, e.g. `h1timing-3.02.04-20261101.jpg`.
5. Send the photos (or the numbers) back, together with the core version shown on the machine's boot screen and the
   video mode (50 Hz / 60 Hz, set in the NextZXOS settings) if you know it.

What each screen shows and what we expect is in [experiments.md](experiments.md).

## Building from source

`tools/build.sh` assembles the four programs with z88dk (`z88dk-z80asm -mz80n`) and wraps them into `.nex` files
(`tools/mknex.py`, NEX V1.2, one 16K bank at `#8000`). `Z88DK_BIN` names the z88dk `bin` folder when it is not on the PATH.
The sources in [`src/`](src/) are written from scratch for this purpose; they use no ROM routine and no operating-system call.

## Layout

| Path | What |
|:--|:--|
| [experiments.md](experiments.md) | each experiment: method, screen layout, how to read the numbers, what the VHDL predicts |
| [TODO.md](TODO.md) | open items and the order to run things |
| [results/README.md](results/README.md) | the form for returning results |
| `src/` | `common.inc` (screen, hex digits, raster, NextREG access), `h1timing.asm`, `h2dma.asm`, `h3sprites.asm`, `h4z80n.asm` |
| `tools/` | `build.sh`, `mknex.py` |
| `out/` | the built `.nex` files |
