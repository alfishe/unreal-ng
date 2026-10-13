# ZX Next findings 2026-10-12: UART / ESP, port gating, copper picture latency (POC 028), D2

One place for everything found and measured in this stretch of work. Details live where linked; this is the index with the facts
that change how the emulator behaves or how the RTL reads. The RTL (`cores/zxnext/src`, core 3.02.03) is the only reference.

## 1. UART and ESP (D1)

- `NextUart` (serial/uart.vhd): ports #133B Tx / status, #143B Rx / prescaler low, #153B select + prescaler high, #163B frame; decode
  A15:A11 = 00010, A10 xor (A9 and A8) = 1, A7:A0 = #3B. FIFOs: Tx 64, Rx 512 (9-bit entries with an error flag), near full at 384,
  RTS drops at 510 with hardware flow control. The prescaler is 17 bits in 28 MHz clocks (243 ~ 115200); a peer is told the nearest
  standard baud.
- Interrupts are rising edges of the levels (RX: near full, or data available and not NR #C6 bit 1; TX: FIFO empty). NR #C6 bits 2:0
  UART 0, 6:4 UART 1; NR #CA status: UART 0 rx bits 1:0 and tx bit 2, UART 1 rx bits 5:4 and tx bit 6.
- Peers: UART 0 is the ESP (`ComPort=`, default AT, ESP-12; ESP8266 NonOS AT 1.7.4 unless `EspChip`), UART 1 the Pi (`ComPort2=`).
  NR #A8 / #A9 drive ESP GPIO 0: low at restart is the download mode (`EspModule::RestartIntoDownloadIfStrapped`).
- State: TTD image version 6 saved the UARTs (the peers' state is the machine serial peer's blob; the UART 1 peer is not saved);
  version 7 adds the ULA+ select; version 8 (cluster D) the receiver's break state. UART 1's peer is blob `MachineSerialPeer2`
  (id 64) and the joystick connector's device `JoystickSerialPeer` (id 65): the ids that needed the wide peripheral mask (file schema 3,
  controller facts 2). Reports: `next_uart` on WebAPI, MCP, CLI, Lua, Python.
- Joystick-port UART (zxnext.vhd 3340-3350, 3516-3539): NR #0B bit 7 and bit 5 select the UART mode, bit 0 the UART (0 replaces the ESP,
  1 the Pi), bit 4 the connector; Rx is the pad's C line (not JOY(5)), CTS_n its B line, Tx is pin 7 (`joy_iomode_pin7`). The cut-off
  module sees Tx forced 1 and RTR forced not ready; the connector has no RTR, so a device there is never held off. Modelled as an
  `ISerialPeer` (`[NETWORK] ComPortJoy=`); the pad's own button bits are not read as lines.
- Break (uart_rx.vhd): the line low through start + data + parity + stop sample (half a bit + (bits + parity + 1) bit times) is a
  framing error (the byte is dropped, status bit 6 sticky); in S_ERROR the line is shifted into a register set to ones, so eight bit times
  later `o_err_break` holds (status bit 7, not sticky) until the line rises; a FIFO reset puts the receiver in S_PAUSE until the line is
  high. `uart.vhd` status bit 4 (Tx empty) is FIFO empty and not `o_busy`, and `o_busy` holds for frame bit 6 (break) and 7.
- Open: a GPIO peer for the Pi, the ESP flasher's SLIP protocol (design note
  [design-esp-flasher-2026-10-12.md](design-esp-flasher-2026-10-12.md)), NR #02 bit 7 (`o_RESET_PERIPHERAL`) as the module's reset.

## 2. Port gating (ports.enforced)

- `PortEnabled(bit)` = NR #82-#85 bit, and NR #86-#89 as well while the expansion bus is on (NR #80 bit 7). A write to a port whose
  bit is off is ignored; a read is the idle bus #FF. Every port the Next decodes is gated now: #FF, #7FFD / #DFFD / #1FFD, #6B / #0B
  DMA, joystick, I2C, sprites, #123B, AY, CTC, the UARTs (12), the mouse (13), ULA+ (24), the DACs, Multiface, #E3, #E7 / #EB.
  Not decoded at all: #EFF7 and the +3 floating bus.
- The base class's partial Kempston-mouse decode must not answer when the exact Next decode is off: the Next returns the idle bus.

## 3. Copper picture latency (POC 028)

Full table, method and calibration: [copper-picture-latency-2026-10-12.md](copper-picture-latency-2026-10-12.md); raw numbers in
`tools/poc/028-next-video-pipeline-alignment/measurements-2026-10-12-register-sweep.md`. Corrections to earlier counts:

- The layer order (NR #15 bits 4:2) is 22 clocks (counted 24). NR #43 bits 3:1 are the active palettes, bits 6:4 the write select
  (the emulator had used 6:4 for the active ones).
- Layer 2 and the ULA are aligned: pixel 0 of both leaves at line position 64; the "5 pixels behind" was a counting error.
- Palette values are not 16 for all: sprites 16, tilemap 17, ULA and Layer 2 18. LoRes scroll and sprite clip 22 per pixel, NR #6A 23,
  Layer 2 resolution 27, tilemap control bits and scroll 53 at the cell, 80 columns 24, NR #4C 20.
- NR #4B is compared with the raw pattern byte before the palette offset (4-bit patterns: the nibble).

### Sprite engine facts

- It fills paper row R during row R - 1 from about raw hc -21, in index order: 1 clock a sprite, 17 more for a drawn one. Pattern,
  attribute and NR #4B writes therefore show a row or two later; modelled by `ScreenNext` snapshots (one instant per row).
- Its vertical counter is ahead of the picture's: with the test grid sprites 64-127 (Y = 96..159) show on raw lines 64-127. A sprite's
  8-bit X wraps at 256 unless attribute 2 bit 0 is set.
- NR #34 sets the pattern pointer only when the sprite index is tied to the port (NR #09 bit 4); otherwise only port #303B does.
- Enable and clip are output-stage (22 clocks), not fill-stage.

### Bench notes (for the next run)

- GHDL 6.0 VHDL-2008: no conditional expressions in port maps or parenthesised in concurrent statements; unisim and coregen RAMs need
  models (`patch_rtl.py`); registers are forced at the end of the NextREG process.
- A NextREG write injected for NR #6B showed no change in the bench while forcing the register did; NR #70 and #6A injections worked.
- A pattern that is the same for every neighbouring pixel hides a register (LoRes scroll, palette values): the variants `lores` and
  `dense` in `patch_rtl.py` exist for that. Only the minimum over the 32 clock offsets is the latency; sparse patterns raise it.

## 4. D2: Kempston mouse, ULA+, Radastan

- Mouse: #FADF buttons + wheel, #FBDF X, #FFDF Y; A7:A0 = #DF, A11:A8 = A / B / F; NR #83 bit 5. The buttons byte is
  `{wheel[3:0], 1, not middle, not left, not right}`: D1 is the left button, D0 the right (the reverse of the ZX Evo's order); NR #0A
  bit 3 swaps them; without a mouse the inputs idle (counts 0, #FADF = #0F). NR #0A bits 1:0 scale the motion (`ps2_mouse.v`): 00 x2,
  01 x1, 10 half, 11 a quarter.
- ULA+: #BF3B selects (bits 7:6 mode, 5:0 index), #FF3B reads / writes the CLUT entry GGGRRRBB in mode 0 (entries 192-255 of the ULA
  palette, the second one when NR #43 bit 6 is set) and the enable bit in mode 1; the enable is the flop of NR #68 bit 3. Index
  `11 flash bright half colour`, no flashing, the border is the paper half, ULANext wins. The read wait state: none up to 14 MHz,
  at most one T at 28 MHz (not modelled).
- Radastan (NR #6A bit 5, LoRes on): 128 x 96 at 4 bits, 64-byte rows, display file = port #FF bit 0 xor NR #6A bit 4, high nibble
  first, index high nibble NR #6A 3:0 (`11` + bits 1:0 with ULA+). The LoRes / Radastan row runs on past 192 (y(7:6) + 1), 14-bit
  address wrap, instead of wrapping at 192.

## 4b. The Pi (Tier A of research-pi-accelerator.md)

- `ComPort2=NEXTPI` (or `PI`): `NextPiPeer`, the NextPi supervisor at the level the Next's tools use: prompt `SUP> `, tty echo, ^C / ^D,
  `nextpi-admin_version` (1.93D), `nextpi-file_receive -nbn` and `nextpi-file_transmit -nbn` over an in-memory card (NBN: 16 KB blocks,
  8-bit sum, `!` / `?`), `bash: x: command not found` for the rest. Real NextPi under QEMU is reachable with `ComPort2=TCP:...`.
- UART 1's pins reach the header only with NR #A0 bit 4 (`pi_uart_en`) and bit 5 (`pi_uart_rxtx`, the Pi orientation): with either clear
  the peer's bytes are lost and the transmitter reaches nobody (`NextUart::SetLineConnected`, synced from NR #A0 at each UART access).
- NR #90-#93: bits 1:0 of #90 and 7:4 of #93 read 0; NR #98-#9B read the PIN state (enabled output: its latch, otherwise the input the
  header gives, low for now; `NextBoard::SetGpioInput` is the hook for a peer); reset values #98 = #FF, #99 = #01, any reset.

## 4c. Power-on palettes (measured 2026-10-13)

- The RTL's palette RAMs (`dpram2`, init `none.bin.txt`) power on at zero. The real boot ROM + TBBLUE.FW run in the emulator from zeroed
  palettes write: ULA palettes (0, 4) the 16 ULA colours repeated through all 256 entries; Layer 2 and sprite palettes (1, 2, 5, 6)
  colour = index; **the tilemap palettes (3, 7) are never written: zero**. The emulator's hard reset now holds exactly that state
  (it had identity for ULA entries 32-255 and for the tilemap palettes). A program that uses the tilemap writes its palette.

## 5. Still open (not done)

See the `[ ]` items of [TODO.md](TODO.md); the ones from this stretch: the UART items in 1; a per-sprite fill time instead of one
instant per row; the ULA+ 28 MHz wait; the CPU speed change within the instruction (taken at the frame start now); the older
N-series items (CTC, DivMMC direct paging, AY reset, Multiface RETN, hard / soft reset, core 3.0.x overscan, DMA WR3 start).
