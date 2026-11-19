# 051 — p2666-hdl-timing-oracle

Feature: [docs/inprogress/2026-11-11-pentagon-2666-le](../../../docs/inprogress/2026-11-11-pentagon-2666-le/README.md)
(Pentagon 2.666 LE, model `MM_P2666`).

## Goals

1. What does the Pentagon 2.666 LE CPU clock really do, cycle by cycle, at 3.5 / 14 / 28 MHz: the length of every
   instruction, the cost of an I/O machine cycle at turbo (the board drops to the 3.5 MHz grid for it), the moment a
   clock switch takes effect, the INT pulse length and the moment an interrupt is accepted?
2. Does the emulator's `MM_P2666` match it exactly (every opcode fetch at the same master clock)?
3. When does the board read each ZX screen byte from the SRAM, relative to the INT strobe, and does the emulator's
   renderer latch the cell in the same T?

## Requirements

- The oracle is the board's own VHDL at the pinned upstream revision
  [`e808402`](https://github.com/koe1234/pentagon_2_666_le/tree/e808402ff193491b94cfd3733f3e1f045c2aca6c): the
  koe-modified `T80a` CPU core (`common/T80/*`) unchanged, the CPU clock process (`PaE.vhd:1581-1609`) and the INT
  process (`PaE.vhd:1543-1566`) copied line for line, the raster counters and the INT strobe of `zx_main.vhd`
  (`:429-440`, `:462-468`) copied line for line.
- Memory: the board's SRAM has no wait line (`cpu_wait` is only the external ZX-BUS `WAIT`, `PaE.vhd:1636`); the
  CPU and the video take alternate phases of the 4-phase `mainclk` cycle (`PaE.vhd:1069-1130`). The testbench's memory
  answers at once with no wait, which is what the board's arbitration amounts to for the CPU.
- Ports: the two clock bits as the board latches them (`xx77` bit 3, `EFF7` bit 4); every other port reads `0xFF`.
- Programs cover every speed, every I/O instruction kind (`IN A,(n)`, `OUT (n),A`, `IN r,(C)`, `OUT (C),r`, `INI`,
  `OUTI`, `INIR`, `OTIR`), switches in every direction, `HALT` + IM 1 / IM 2 interrupts at every speed.
- Output: a text trace, one line per opcode fetch (`M1 <mainclk> <address>`), interrupt acknowledge, port cycle and
  INT edge, in `mainclk` (114 MHz) counts from power-on.

## Constraints

- Only `ghdl` (installed, `/opt/homebrew/bin/ghdl`); no Quartus, no Altera primitives: the PLL is replaced by the
  `mainclk` it produces, the video RAM and the rest of `PaE.vhd` are not instantiated.
- Upstream sources are fetched by `fetch.sh` into the scratch folder, never copied into the repository.
- Raw traces go to `scratch/2026-11-11-poc-051/`; the golden traces the emulator tests compare against are committed
  under `testdata/machines/p2666/timing/`, with the program bytes that produced them.

## Acceptance criteria

- The oracle reproduces the HDL arithmetic of hardware-reference §1: a NOP at 3.5 MHz = 128 `mainclk`, at 14 MHz = 32,
  at 28 MHz = 16; the frame = 2293760 `mainclk`; INT = 32 CPU clocks.
- `go` for the emulator when every `M1` of every program lands on the same `mainclk` as in the oracle (zero
  difference over all programs), and every interrupt is accepted at the same fetch.

## Files

| File | What |
|---|---|
| `fetch.sh` | clones the upstream repository at `e808402` into `scratch/2026-11-11-poc-051/src` |
| `tb/p2666_timing_tb.vhd` | the testbench |
| `programs.py` | the test programs (Z80 bytes, no assembler needed) and the golden-file writer |
| `run.sh` | analyse, elaborate and run every program, write traces and the golden files |
| `tb/p2666_video_tb.vhd` | video fetch oracle: `zx_main.vhd` unchanged with its line buffers, the SRAM read path of `PaE.vhd:1071-1083, 1135-1141` copied |
| `video.sh` | one frame of the video oracle in the ZX mode with three border writes (about 2 minutes), then `video_fetch.py` |
| `video_fetch.py` | maps every screen read and the border writes of the trace to the emulator frame (line, T) through `kFrameOriginShift` |

## Video fetch result

`zx_main` puts a cell's pixel and attribute addresses on the SRAM bus in turn for the cell's whole 4 T, so each byte
is read several times; the read it keeps is the last before its capture (`zx_main.vhd:549-555`). With the decoder's
`kFrameOriginShift` = 114176 edges, every one of the 6144 pixel reads it keeps lands on line 80 + Y, T 30.375 + 4c,
every attribute read on T 30.25 + 4c of the same line, and the INT strobe on T 71640 (line 319, T 184): the
emulator's renderer latches the cell at T 30 + 4c, the same T.

Border (`video.sh` writes `FE` at line 100 T 10, line 100 T 180, line 60 T 100): each write shows on the line it was
made in, 3.25 T (6.5 ZX pixels) right of where the paper mapping puts its T (`border_buf` read-out). The `FE` latch
is the emulator's port write T (timing oracle `IOW`: `OUT (C),r` + 9.0 T, `OUT (n),A` + 8.0 T), so the renderer
holds the old colour 2 T after the write (`P2666Geometry::kBorderHoldT`), the plain Pentagon 1 T.
