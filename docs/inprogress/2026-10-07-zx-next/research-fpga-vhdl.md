# Research: reading the FPGA sources (N0, first pass)

Source: the authoritative VHDL, [ZX_Spectrum_Next_FPGA](https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA)
(`cores/zxnext/src`), commit `b047011`, 2025-11-06. Every statement below was read in that source; file and
construct names are given so the next reader can check. Where this note disagrees with an earlier note of this
folder, this note wins (the VHDL is the hardware).

## 1. Video frame timing (answers Q5)

`video/zxula_timing.vhd`. The counters run in the 7 MHz pixel domain: `hc` 0..`c_max_hc`, `vc` 0..`c_max_vc`.
One CPU T-state at 3.5 MHz is 2 `hc` counts.

| Timing | `hc` range | T per line | `vc` range | Lines | T per frame | INT point (`c_int_h`, `c_int_v`) | Paper starts |
|:--|:--|--:|:--|--:|--:|:--|:--|
| 48K, 50 Hz | 0..447 | 224 | 0..311 | 312 | 69888 | (116, 0) | hc 128, vc 64 |
| 48K, 60 Hz | 0..447 | 224 | 0..263 | 264 | 59136 | (116, 0) | hc 128, vc 40 |
| 128K, 50 Hz | 0..455 | 228 | 0..310 | **311** | 70908 | (128, 1) | hc 136, vc 64 |
| +3, 50 Hz | 0..455 | 228 | 0..310 | 311 | 70908 | (126, 1) | hc 136, vc 64 |
| 128K / +3, 60 Hz | 0..455 | 228 | 0..263 | 264 | 60192 | (128 / 126, 0) | hc 136, vc 40 |
| Pentagon | 0..447 | 224 | 0..319 | 320 | 71680 | (439, 319) | hc 128, vc 80 |

(`c_int_h` is written `136+4-12` etc. in the source; the values above are the sums.) So Q5 is settled: the
128K frame is **311 lines** (jnext is right, MAME's 312 is not the VHDL). The 128K and +3 differ by two pixel
clocks in the interrupt point and in contention (section 2). Only 50/60 Hz and the three timing families exist;
the hdmi window constants (`c_hdmi_*`) are output scaling, not machine behavior.

## 2. Contention (answers Q6)

`zxnext.vhd` around the `zxula` instance, and `video/zxula.vhd` (`o_cpu_contend`).

- Contention is enabled only when **all** hold: NR `#08` bit 6 (`nr_08_contention_disable`, effective value) is 0,
  the timing is not Pentagon, and the CPU speed is 3.5 MHz (`cpu_speed = "00"`). At 7, 14 and 28 MHz there is no
  contention at all.
- Memory contention applies only to 16K banks 0-7 (8K page high nibble 0). 48K timing contends bank 5 only; 128K
  timing contends the odd banks (1, 3, 5, 7); +3 timing contends banks 4-7. (Page number here is the MMU 8K page
  number: bank n = pages 2n and 2n+1.)
- Port contention: `port_contend = not A0 or port_7ffd or port_bf3b or port_ff3b` (the ULA+ ports are contended
  "because the UNO requires it"; the `#1FFD` and +3 floating-bus terms are commented out).
- The delay window (`video/zxula.vhd`, `wait_s`): with `hc_adj = (hc & 15) + 1`, the CPU is held when
  `hc_adj(3:2) /= 00` (12 of every 16 pixel clocks, i.e. the classic "6,5,4,3,2,1,0,0" pattern per 8 T-states),
  and, for +3 timing only, also when `hc_adj(3:1) = 000`; only while `hc(8) = 0` (the first 256 pixel clocks of the
  line) and the line is a paper line (`border_active_v = 0`). In 48K / 128K timing the contention is applied by
  holding the CPU clock high (`o_cpu_contend`) on memory accesses to a contended bank (`mreq` not yet seen in
  the cycle) and on contended IO (T-state 3 handling, `ioreqtw3_n`). In +3 timing it is a real WAIT line
  (`o_cpu_wait_n`) on memory accesses only; IO is not contended by this path (the IO term is commented out).
- The Next is a synchronous machine: it decides "in the previous `hc` cycle" whether the CPU clock may go low
  (contend 3-14), where the original ULAs OR into the clock in the current cycle (contend 4-15). Software cannot
  see the difference, but a T-state-exact emulator must hold the Next pattern, and the test programs in
  ZXSpectrumNextTests measure it.
- The +3 floating bus (`port_p3_floating_bus_dat`) returns the last data byte on a contended access
  (`p3_floating_bus_dat` latched from `cpu_di` / `cpu_do`), `#FF` when `#7FFD` is locked; ports `#FF` floating bus
  works in 48K and 128K timing only.

## 3. Memory decode (affects design-core.md)

`zxnext.vhd`, section "memory decode order".

- The CPU address space is eight 8K slots with registers `MMU0..MMU7` (8-bit page numbers). The physical SRAM
  address is `A21..A13 = (0001 + page[7:5]) & page[4:0]`, i.e. **8K page p lives at physical 8K page p + 32**;
  the first 256K of the 2 MB SRAM (physical pages 0-31) holds ROM and system areas, and MMU page `#E0` and above
  (the sum overflows into bit 8) selects the **ROM** instead of RAM. Pages `#0A/#0B` and `#0E` are special-cased as
  "bank 5" and "bank 7" (the ULA screens), and are served from the ULA-visible RAM port rather than the plain
  SRAM path.
- Decode priority at `#0000-#3FFF`, highest first: boot ROM (in config mode), Multiface (`#0000` pair mapped at
  physical page 5), DivMMC (ROM page 8, RAM pages `"00001" & bank`), Layer 2 mapping (read/write windows),
  the MMU when its page is RAM (`< #E0`), config-mode `NR #04` ROM/RAM bank, expansion-bus ROMCS (DivMMC banks
  14-15 area), and finally the ROM: `"000000" & rom(2) & A13` (rom 0-3) or the alternate ROM
  (`"0000011" & alt128 & A13`).
- At `#4000-#BFFF`: Layer 2 mapping first (only for slots selected by `port_123b` segment), then the MMU.
  At `#C000-#FFFF`: the MMU only.
- ROM selection (`sram_rom`): 48K machine type: ROM 0, alt-ROM select = `NR #8C` bits; +3 type: `NR #8C` lock bits
  if non-zero, else `#1FFD` ROM bits; 128K / Pentagon type: bit from `#7FFD` ROM bit (`port_1ffd_rom(0)`
  derived), with the same alt-ROM override. The ROM is read-only unless `NR #8C` alt-ROM is enabled with the
  read/write bit set.
- Layer 2 write/read windows: `layer2_active_page = (active_bank + offset) & A13`, where the active bank is
  `NR #12` (or `NR #13` shadow when `port_123b` shadow bit) and the offset comes from `port_123b`.

## 4. Port decode (answers part of Q2 and the ports list)

`zxnext.vhd` "port" processes. Selected exact decodes (A = address bus):

- `#xxFE`: `A0 = 0`. `#xxFF`: low byte `#FF` (Timex / floating bus).
- `#7FFD`: `A15 = 0` and (`A14 = 1` or not +3 timing) and `A1:A0 = 01` and not `#1FFD`; `#DFFD`: `A15:A12 = 1101`,
  `A1:A0 = 01`. `#1FFD`: `A15:A14 = 00`, `A13:A12 = 01`, `A1:A0 = 01`. `#2FFD` / `#3FFD` (FDC trap, NR `#D8`):
  `A13:A12 = 10 / 11`. `#EFF7`: `A15:A12 = 1110`, low byte `#F7`.
- `#243B` / `#253B` NextREG select / data. `#103B` / `#113B` I2C. `#123B` Layer 2. `#303B` sprite slot,
  `#57` sprite attributes, `#5B` sprite patterns. `#BF3B` / `#FF3B` ULA+.
- UART (ESP and Pi): `A15:A11 = 00010`, `A10 xor (A9 and A8) = 1`, low byte `#3B`. CTC: `A15:A11 = 00011`, low `#3B`.
- DMA: low byte `#6B` (zxnDMA) and `#0B` (DATAGEAR/Z80-DMA compatible), each separately enabled.
- AY: `A15:A14 = 11 / 10`, `A2 = 1`, `A1:A0 = 01` (`#FFFD` select, `#BFFD` data; `#BFF5` is the data port with `A3 = 0`).
- Audio DAC aliases: mono A+D `#FB`, `#DF`; mono B+C `#B3`; SoundDrive 1 `#1F #0F #4F #5F`; SoundDrive 2
  `#F1 #F3 #F9 #FB`; stereo `#3F #5F` (A, D) and `#0F #4F` (B, C).
- Kempston mouse: `#FADF #FBDF #FFDF`. Kempston joystick 1 `#1F` (also `#DF` when the mouse and DAC `#DF` are off);
  `#37` is the second Kempston.
- SPI: `#E7` (chip select) and `#EB` (data). DivMMC control `#E3`. Multiface enable / disable ports move with
  `NR #0A` bits 7:6 (`#3F/#BF`, `#BF/#3F`, `#9F/#1F`).
- Every port group is individually gated by the internal enable registers `NR #82-#85` (one bit per device), and by
  `NR #86-#89` for the expansion bus pass-through; `NR #85` / `NR #89` bit 7 chooses whether the enables reset on
  a soft or hard reset. This is why a faithful port table is data: a `bool enabled` per device group, defaults all 1.

## 5. NextREG reset values and write semantics

`zxnext.vhd` "state" process. Reset values worth putting in tests (test name suggestions in tdd-plan.md):

| Register | Reset | Notes |
|:--|:--|:--|
| NR `#12` / `#13` | `0001000` (8) / `0001011` (11) | Layer 2 active / shadow 16K banks |
| NR `#14` | `#E3` | global transparency (RGB332) |
| NR `#18-#1B` clips | ULA/L2/sprite `0,255,0,191`; tilemap `0,159,0,255` | four writes cycle x1,x2,y1,y2; NR `#1C` resets the index |
| NR `#4A` | `#E3` | fallback colour (the commented line shows `#00` in older cores) |
| NR `#4B` | `#E3`, NR `#4C` | `#0F` | sprite and tilemap transparent index |
| NR `#68` | ULA enabled, no blend | bit 7 is "disable ULA" |
| NR `#6E` / `#6F` | `101100` / `001100` | tilemap map / tile definition base, in 256-byte units from `#4000` (to be confirmed in the tilemap pass) |
| NR `#C0` | im2 vector 0, stackless NMI off | hardware IM2 mode off (pulse mode) |
| NR `#C4` | bit 0 = 1 | expansion bus INT enabled at reset |

Notable behaviors: NR `#03` writes switch the machine type / timing (timing `000/001`=48K ... as in the source),
bit 3 toggles the user-timing lock; NR `#11` (video timing) is writable only in config mode; the palette index
auto-increments on NR `#41` and on every second NR `#44` write; NR `#60/#63` copper data is staged until the
odd byte arrives (16-bit instruction written as two bytes, `nr_copper_addr(0)`), NR `#61/#62` set the 11-bit
copper address, NR `#62` bits 7:6 are the copper mode.

## 6. Z80N microcode facts (answers Q7)

`cpu/t80n_mcode.vhd`, `cpu/t80n.vhd`.

- `ED B7` **LDPIRX** (get byte from `(HL & #FFF8) | (DE & 7)`, store to `(DE)` if not equal to A, DE++, BC--,
  repeat) is implemented, 4 M-cycles, 2 extra T in M3 and M4 (`TStates "101"`), like the other block ops.
- `ED B6` **LDIRSCALE**: the opcode is decoded and its M-cycles run, but the register effects that would make it
  different from `LDIRX` (add BC' to HL_A', DE += DE') are **commented out** in `t80n.vhd`. As built, the
  instruction increments HL/DE and loads like `LDIRX`. No shipped software can depend on it. **Decision input:**
  do not implement it as a scaling copy; implement the same as `LDIRX`, document it as "decoded, not scaling in
  hardware core 3.x", and put a test that pins this behavior.
- `ED A5` **LDWS**: `INC L`, then `(DE) <- (HL)`, then `INC D`, 3 M-cycles.
- `ED A4/B4` LDIX/LDIRX copy unless the byte equals A; `ED AC/BC` LDDX/LDDRX the same backwards. `ED 90` OUTINB.
- Also present: `NEXTREG n,A`, `NEXTREG n,nn`, `MUL D,E`, `ADD HL/DE/BC,A`, `ADD HL/DE/BC,nnnn`, `SWAPNIB`,
  `MIRROR A`, `TEST nn`, `BSLA/BSRA/BSRL/BSRF/BRLC DE,B`, `PIXELDN`, `PIXELAD`, `SETAE`, `PUSH nnnn`, `JP (C)`.
  (`MIRROR DE` is commented out in the microcode.)

## 7. Physical memory map and the boot ROM window

`zxnext.vhd` comment "MEMORY DECODING" (A20:A16 are the SRAM address bits; byte offsets):

| SRAM offset | Size | Content |
|:--|--:|:--|
| `#000000-#00FFFF` | 64K | ZX Spectrum ROM (4 x 16K: 48K / 128K / +3 pages; `sram_rom` selects the 16K) |
| `#010000-#011FFF` | 8K | DivMMC ROM |
| `#014000-#017FFF` | 16K | Multiface ROM / RAM (8K ROM + 8K RAM at physical page 5 of the 8K numbering) |
| `#018000-#01BFFF` | 16K | alternate ROM 0 (128K) |
| `#01C000-#01FFFF` | 16K | alternate ROM 1 (48K) |
| `#020000-#03FFFF` | 128K | DivMMC RAM (16 x 8K banks) |
| `#040000-#05FFFF` | 128K | ZX Spectrum RAM (MMU pages 0-15; page `#0A/#0B` = bank 5, `#0E/#0F` = bank 7) |
| `#060000-#07FFFF` | 128K | extra RAM |
| `#080000-#1FFFFF` | 3 x 512K | extra IC RAM (1st at `#080000`, 2nd at `#100000`, 3rd at `#180000`), if fitted |

So the system area is the first **256K** (up to `#03FFFF`), RAM starts at `#040000` and **page numbers 0..223 span
`#040000..#1FFFFF`** (224 x 8K = 1792K). The issue-5 top level wires one 2M x 16 SRAM (22-bit byte address), the
earlier boards have two chips; a smaller RAM population simply leaves the upper pages unmapped (the top 4 bits
of the page sum overflowing bit 8 decide RAM vs ROM, so reads from a page past the populated RAM are the
board's concern, not a decode rule).

- The boot ROM (`bootrom.vhd`, 8K, `ADDR` 13 bits) is mapped at `#0000-#1FFF` while `bootrom_en = 1`; it is
  switched on by a reset in config mode and off by **any write to NR `#03`** (`bootrom_en <= '0'`). The 8192 bytes
  are in the VHDL as a plain byte array and can be extracted by a script (MD5 `8c4f0c1b77db8de9ed857f2c873250b8`
  at this commit). The `tbblue_loader.rom` that ships inside ZEsarUX is a **different, older** image (6822 of
  8192 bytes differ), so a ROM taken from ZEsarUX must not be assumed to match the current FPGA.
- The first bytes of the boot ROM are `DI; IM 1; JP #0080`, a `RETI` at `#0008-#0038` and a `RETN` at `#0066`, and
  `#0080` sets `SP` and calls into the loader (`#1CC7`, `#0145`, `#0100`). 28 MHz wait states apply to SRAM
  **reads** only (`sram_wait_n`, when `cpu_speed = "11"`), a one-cycle hold that cycle-exact 28 MHz emulation
  must reproduce.

## 8. Tilemap (`video/tilemap.vhd`)

- Control NR `#6B`: bit 7 enable (separate), bit 6 `80x32` instead of `40x32`, bit 5 strip flags (no attribute
  byte, NR `#6C` supplies it), bit 3 text mode (palette offset widened to 7 bits, no rotate / mirror, 1 bpp tiles),
  bit 1 `512` tile mode, bit 0 "tilemap always on top of ULA". Bits 4 and 2 are "not implemented" in the source
  (reduced area `32x24` / split addressing).
- Map entry: 2 bytes per cell (tile number, then attributes: palette offset in bits 7:4, bit 3 x mirror, bit 2 y
  mirror, bit 1 rotate, bit 0 ULA-below / tile bit 8 in 512 mode); with strip flags 1 byte per cell. Cell address in
  the 16K bank: `(row * cells_per_row + col) * bytes_per_cell`, base from NR `#6E` (bit 6 chooses bank 7 instead of
  bank 5, bits 5:0 are 256-byte units; reset `101100` = `#6C00` in the CPU's bank-5 view, i.e. offset `#2C00`).
- Tile definitions: 4 bits per pixel, 8 x 8 = 32 bytes per tile, base NR `#6F` the same way (reset `001100` =
  offset `#0C00`, CPU view `#4C00`); transformed read: `(rotate ? swap(x, y) : (x, y))` after the mirrors, where
  rotation inverts the x mirror (`tm_effective_x_mirror = attr3 xor attr1`).
- Scroll: NR `#2F/#30` x (10 bits: `0..319` in 40-wide mode, `0..639` in 80-wide), NR `#31` y (8 bits); the wrap
  corrections `1100 / 0001 / 0110 / 1011` in `tm_x_correction` implement the non-power-of-two width (320 / 640).
  Clip NR `#1B` is compared in 2x-x units. Transparency: tile pixel (low 4 bits after the palette offset) equal to
  NR `#4C` is transparent; the tilemap pixel for a textmode cell is always opaque.
- The fetch is a 4-state machine per cell (`IDLE`, `READ_TILE_0`, `READ_TILE_1`, `READ_PIXELS`) that reads from the
  ULA-shared bank 5 / 7 RAM port, one cell ahead of the beam, into a 2 x 8 pixel line buffer.

## 9. Layer 2 (`video/layer2.vhd`)

- Resolutions (NR `#70` bits 5:4): `00` = 256 x 192 x 8 bpp (49152 bytes, address `y * 256 + x`), `01` = 320 x 256
  x 8 bpp (81920 bytes, address `x * 256 + y`, **column-major**), `1x` = 640 x 256 x 4 bpp (the same column-major
  layout, two pixels per byte, high nibble first). Palette offset NR `#70` bits 3:0 is added to the high nibble of
  the pixel (`(pixel[7:4] + offset) & pixel[3:0]`); in 4 bpp mode the nibble becomes the low 4 bits and the high
  4 bits come from the offset.
- Scroll NR `#16` (x low) + NR `#71` bit 0 (x bit 8) + NR `#17` (y); at 256 x 192 the x sum wraps modulo 256 and
  the y sum wraps at 192 (`y_pre` over 191 adds 64 so the 192 rows wrap inside the 3 x 16K window); in the wide modes
  x wraps modulo 320 / 640 and y modulo 256. Clip NR `#18`: in 256 x 192 mode the values are 1 pixel units; in
  the wide modes x1 is doubled and x2 doubled + 1 (so 640-wide pixels use half-pixel coordinates).
- Source bank: `layer2_active_bank` = NR `#12` (7 bits, 16K bank number) plus the three-bank offset, physical
  address `(bank_eff + addr[16:14]) & addr[13:0]` where `bank_eff = (bank[6:4] + 1) & bank[3:0]` (the +1 adds the
  256K system offset, 16K units). The layer is enabled by `port_123b` bit 1 or NR `#69` bit 7.

## 10. Sprites (`video/sprites.vhd`)

- 128 sprites, 64 patterns of 256 bytes (16K pattern RAM, `pattern_index` is 14 bits), 5 attribute bytes per sprite:
  A0 x low, A1 y low, A2 palette offset (7:4) / x mirror (3) / y mirror (2) / rotate (1) / x MSB (0), A3 visible (7) /
  fifth-byte present (6) / pattern number (5:0), A4 4-bit pattern (7) / pattern bit 6 (6) / x scale (4:3) / y scale
  (2:1) / y MSB (0). A sprite is *relative* when A3 bit 6 = 1 and A4 bits 7:6 = `01`; its x / y are signed offsets (rotated, mirrored
  and scaled by the preceding anchor's flags, then added to the anchor position), its palette offset is added to the
  anchor's when A2 bit 0 = 1, and the anchor's A4 bit 5 chooses the *composite* (0) or *unified* (1) relative type.
  Pattern address = `(pattern(5:0), N6) * 128 bytes` for 4-bit patterns, `* 256` for 8-bit ones.
- Sprite number and attribute upload: port `#303B` sets the sprite number (and the attribute index to byte 0 of
  that sprite); port `#57` writes the attributes (auto-increment, 4 bytes, or 5 when A3 bit 6 of the written byte
  says a fifth byte follows); port `#5B` writes pattern bytes (auto-increment through 16K). The NextREG mirror
  `#34-#39` and `#75-#79` writes the same memories ("tied" when NR `#09` bit 4 is 0).
- Per-line engine: one line is prepared in the 320-pixel line buffer while the previous line is drawn
  (double buffer). The engine has a finite per-line time budget; when it is still busy at the line change
  (`sprites_overtime`), status bit 1 of port `#303B` ("max sprites per line") is set; bit 0 is "collision" (a
  non-transparent write onto an already non-transparent pixel). A read of `#303B` returns the status and clears it.
  **Open (N7):** the exact cost per sprite pixel / per sprite and the budget in 28 MHz clocks are needed to set the
  overtime flag the same way; until then a software estimate with a documented constant is acceptable.

## 11. Copper (`device/copper.vhd`)

- Program memory: 1024 x 16-bit instructions (2048 bytes) addressed by NR `#61` (low 8) and NR `#62` bits 2:0
  (bits 10:8); written through NR `#60` (byte) or NR `#63` (the 16-bit instruction is assembled from two
  successive byte writes; address increments after each byte).
- Instruction: `bit 15 = 1` WAIT: line = bits 8:0, horizontal = bits 14:9 (in units of 8 pixel clocks); the
  copper moves on when `vcount = line` and `hcount >= H * 8 + 12`. `bit 15 = 0` MOVE: register = bits 14:8,
  value = bits 7:0; `MOVE 0,0` is a NOP that produces no write.
- Modes NR `#62` bits 7:6: `00` stopped, `01` run from the start (address reset on entering the mode), `10` run
  from the current address (does not reset), `11` run, and restart at every frame start (`vcount = 0, hcount = 0`).
  Entering any new mode restarts the instruction decode (`copper_dout_s` cleared).
- The copper runs on the 28 MHz clock (one instruction per clock, a WAIT re-evaluated every clock), so it is cycle
  accurate to the pixel; `vcount` is the copper-offset vertical count (NR `#64` shifts it).

## 12. Interrupts (IM2 controller) and the pulse interrupt

- 14 hardware interrupt sources in a fixed daisy chain, bit 0 highest priority, and the **vector number is the
  source number**: 0 line, 1 UART0 RX, 2 UART1 RX, 3-10 CTC channels 0-7, 11 ULA, 12 UART0 TX, 13 UART1 TX.
  The vector is `NR #C0[7:5] & source(4 bits) & 0` (so the table is 16 words at an 8-aligned address).
- Two modes (NR `#C0` bit 0): pulse mode (reset default; the classic ULA-style INT of 32 CPU cycles on 48K / +3
  timing, 36 on 128K / Pentagon timing) and hardware IM2 mode (each source holds `/INT` until acknowledged,
  nesting by `RETI` decode; `im2_control` decodes `IM n`, `RETI`, `RETN` from the bus to drive the chain).
- Per-source enables NR `#C4` (line, ULA, expansion bus), `#C5` (CTC), `#C6` (UARTs); status NR `#C8-#CA` (read) and
  clear by writing 1; DMA-on-interrupt enables NR `#CC-#CE` ("im2_dma_int_en": the DMA starts when that source
  fires; `im2_dma_delay` holds it off while an interrupt service routine is running, until `RETI`).
- Stackless NMI (NR `#C0` bit 3): on NMI acknowledge the return address goes to NR `#C2/#C3` instead of the stack;
  `RETN` reads it back.

## 13. DMA (`device/dma.vhd`, "Loosely based on Z80C10. There are differences!")

- Two ports: `#6B` (zxnDMA: every control byte handled the ZX Next way) and `#0B` (Z80-DMA compatible mode,
  `dma_mode_i`). WR0-WR6 command bytes are parsed by a sequencer (`reg_wr_seq_t`), `WR6 #C3` is reset, the read-mask
  register (`R6_read_mask_s`, reset `0111 1111`) selects which status / counter bytes the read sequence returns
  (status, block counter low / high, port A address low / high, port B address low / high).
- Transfer: read then write cycles, each lasting 2, 3 or 4 DMA clocks according to the port timing byte
  (`R1_portA_timming_byte_s` / `R2_portB_timming_byte_s` bits 1:0 = `00` 4, `01` 3, `10` 2, `11` 4). A port can be
  memory or I/O, with address increment, decrement or fixed. The prescaler (WR4/WR5 prescaler byte in port B
  `R2_portB_preescaler_s`) inserts waiting between bytes, measured by a timer that counts 8, 4, 2, 1 per clock at
  3.5 / 7 / 14 / 28 MHz, so the **transfer rate is independent of the CPU speed** (a fixed ~875 kHz base, scaled
  by the prescaler). In burst mode the bus is released during the wait.
- The DMA takes the bus from the CPU with `/BUSREQ`, daisy-chained with the expansion bus (`bus_busreq_n_i`,
  `cpu_bai_n`), and **waits while the CPU is servicing an interrupt that has DMA-on-interrupt set**.

## 14. Boot chain (answers Q8 for the real path; design-boot-and-firmware.md uses it)

Read in the firmware sources ([tbblue](https://gitlab.com/thesmog358/tbblue), commit `5cd10da`, 2026-09-28):

1. Power on: the boot ROM (8K, in the FPGA) runs; its loader (`src/firmware/loader`) initializes the SD card over
   SPI (`MMC_Init`, `FindDrive`; FAT16/FAT32 by its own tiny reader), opens **`TBBLUE.FW`** in the card's root
   directory, reads its 512-byte index (4 bytes per module: first block, block count), and copies the chosen
   module to `#6000` (module 0 `boot` by default, `editor` if SPACE is held, `cores` on C, `updater` if
   `TBBLUE.TBU` exists and U is held), then `JP #6000`.
2. `boot.bin` (`app/src/boot.c`): sets 28 MHz (NR `#07` = 3), reads `/machines/next/config.ini` and `menu.ini`
   (default `menu.def`), shows the boot screen, waits ~65K loops for SPACE / C / U keys (editor, cores, updater),
   loads `keymap.bin` and the key-joystick table, then **`load_roms`**: it maps SRAM pages through NR `#04`
   (`REG_RAMPAGE`; with the boot ROM off, writes at `#0000-#3FFF` go to the ROM area page chosen by NR `#04`)
   and reads `esxmmc.bin` (DivMMC ROM, 1 x 8K), the Multiface ROM (1 x 8K), and the personality ROM
   (`menu.ini` `romfile`: 1 x 16K for 48K, 4 x 16K for +3, 2 x 16K for 128K / Pentagon) from `/machines/next/`.
3. `init_registers`: NR `#05`, `#06`, `#08`, `#09`, `#0A` from `config.ini` flags (joysticks, 50/60 Hz,
   scandoubler, speed hotkey, beeper, DivMMC, Multiface, PS/2, AY mode, DAC, Timex, TurboSound, issue-2 keyboard,
   scanlines, HDMI sound, mouse DPI) and the four port-enable registers NR `#82-#85` per machine mode (48K:
   `#C0`, 128K: `#C2`, +3: `#DA`, Pentagon: `#C6` for NR `#82`, with `#83` DivMMC / MF / UART-I2C / mouse bits,
   `#84` AY + DACs, `#85` ULA+ and DMA).
4. `NR #03` = `#80 | ((mode + 1) << 4) | (mode + 1)` (machine type and timing = 48K, 128K, +3, Pentagon), then
   `NR #02` = soft reset: the personality ROM at physical `#000000` starts. For the "Next" personality
   (`enNextZX.rom` and the NextZXOS image) the ROM then loads `/nextzxos/*` files (the OS image is outside the
   firmware; this repository's `nextzxos/` and `machines/next/` trees hold ROMs and the OS).
5. **Consequences for the design:** (a) a *firmware-free direct start* is exactly steps 2-4 done by the host: place
   the ROM pages, set the registers, soft reset. That is a small, deterministic list and is the right "bare"
   personality (N2). (b) the *real boot* needs the SPI SD card (`#E7`/`#EB`), the boot ROM and a mounted FAT image
   with `TBBLUE.FW`; the bring-up checklist is the sequence above. (c) The firmware-index format of `TBBLUE.FW` is
   documented by `firmware/src/main.c` (`addFile`): index in the first 512 bytes, 8 module slots x (block, count),
   blocks of 512 bytes.

## 15. Sources of tests found

[ZXSpectrumNextTests](https://github.com/MrKWatkins/ZXSpectrumNextTests) (commit `98bb90c`, 2026-06-03): every test
ships as source plus a prebuilt `.snx` in `release/`, with photos of the same screen on **real boards** (e.g.
core 3.1.5) and on MAME, CSpect and ZEsarUX. The list: base (Copper, DMA, NextReg_defaults, Z80N, Z80Nc2),
Graphics (Layer2Colours, Layer2Port, Layer2Scroll, LayersMixing HiCol / HiRes / LoRes, LightenDarken_L2_ULA,
NextReg0x69), Interrupts (HaltAfterDisable), Misc (DmaInteractive, ZilogDMA), Sprites (BigSprite, BigSprite4b,
Relative, ScanlineDelay, Transparency), Timing (Changing8kBank, Changing8kBank_NoContention,
ScanlineReadingAndInterrupt), ULA (BorderTransparencyFallback, ChangePaletteTransparency, v2) and four classic
48/128 TAPs (block instruction flags, interrupt skip with prefixes, CCF/SCF flags, Sinclair joystick ports). The
`NextReg_defaults` test colour-codes a 16 x 16 grid of registers (default value and read / write survival) and
states the expected start state (ZX48 personality, turbo off, contention as ZX48); it is the acceptance test for
the NextREG table (N3). ZEsarUX also ships a prepared 64 MB SD image (`tbblue.mmc`) that is useful only as a
cross-check, not as a repository fixture.

## 16. ULA, Timex modes, ULA+, ULAnext and LoRes (`video/zxula.vhd`, `video/lores.vhd`)

- **ULA fetch** is a 16-pixel-clock cycle (`hc & 15`): bytes are fetched at cycle positions `8,9` and `C,D` (pixel)
  and `A,B`, `E,F` (attribute) of every 8-pixel pair; the address pattern is the classic
  `py(7:6) py(2:0) py(5:3) px(7:3)` for pixels and `110 py(7:3) px(7:3)` for attributes. The ULA reads from bank 5
  (or the shadow bank 7 when `#7FFD` bit 3 is set, limited to the standard mode there: "bank 7 only has 8K of BRAM").
  **Scroll** (NR `#26` x, NR `#27` y) and **fine scroll x** (NR `#68` bit 2) are folded into `px` / `py`; `py`
  wraps over the 192 lines (`+64` correction for `py >= 192`).
- **Timex modes** from port `#FF` bits 2:0 (`screen_mode`): bit 0 selects the second screen file (`#6000`), bit 1 the
  hi-colour mode (attributes read from the second file, 8 x 1 colour cells), bit 2 the hi-res mode (512 x 192,
  each pixel byte pair fetched from both files interleaved; the colours come from port `#FF` bits 5:3 as `01 !c c`).
  The Timex border colour in hi-res is `border_clr_tmx = 01 & not(ff[5:3]) & ff[5:3]`.
- **Border**: `border_active = phc(8) or border_active_v`, where `border_active_v = vc(8) or (vc(7) and vc(6))` (the
  paper window is 256 x 192; the border pixels use the port `#FE` colour in the same palette entry as the ink /
  paper colour of that value).
- **Flash** counter: a 5-bit counter incremented once per frame; flash bit = `flash_cnt(4)` (so the flash toggles
  every 16 frames, 1.5625 Hz at 50 Hz), and flash is disabled in ULAnext and ULA+ modes.
- **Standard pixel colour index** (before the palette): `000 P B ccc` where `P` = 1 for a paper pixel (0 for ink),
  `B` = the attribute's BRIGHT bit and `ccc` the 3-bit ink or paper colour (`ula_pixel(7:3) = 000 & not pixel_en &
  attr(6)`). So ULA palette entries 0-7 are normal ink, 8-15 bright ink, 16-23 normal paper, 24-31 bright paper.
- **ULA+** (port `#BF3B` select / `#FF3B` data, 64-entry palette stored at indices 192-255 of the ULA palette):
  pixel index = `11 & flash/bright bits(7:6) & (hi-res or paper) & 3 bits`; reading `#FF3B` returns the palette
  byte, in group "mode" the enable bit; the 9th blue bit is the OR of the two stored blue bits. Contention for
  these two ports as in section 2.
- **ULAnext** (NR `#43` bit 0, mask in NR `#42`): the attribute byte is an index into the ULA palette; `ink_mask`
  `#01 / #03 / #07 / #0F / #1F / #3F / #7F` (formats with `#FF`: border and paper select the **background** colour
  = the fallback colour NR `#4A`), ink = `attr & mask`, paper = `128 + (attr >> bits)`.
- **LoRes** (NR `#15` bit 7): 128 x 96 pixels, 8 bpp, each pixel doubled; the display area is two 6K halves at
  `#4000` and `#6000` of bank 5 (the address pattern is `y(7:1) & x(7:1)` with a `+1` on the high bits for the
  second half); NR `#32` / `#33` scroll; palette offset in NR `#6A` bits 3:0 (high nibble added). **Radastan**
  mode (NR `#6A` bit 5): 128 x 96 x 4 bpp, address `dfile & y(7:1) & x(7:2)` (display file chosen by port `#FF`
  bit 0 xor NR `#6A` bit 4), palette offset in the high nibble (and `11 & offset(1:0)` when ULA+ is on). LoRes and
  the ULA share one pixel stream: where the LoRes pixel is enabled it replaces the ULA pixel and the ULA's clipped
  flag is cleared.

## 17. Palettes and the compositor (`zxnext.vhd` ~6700-7300)

- **Palettes:** four palettes in two RAMs of 2 x 256 x 9 bits plus a priority bit (bit 15 of the stored word):
  ULA (two sets), tilemap (two sets), Layer 2 (two sets), sprites (two sets). NR `#43` bits 6:4 select which one a
  `#41` / `#44` write goes to (`000` ULA first, `001` Layer 2 first, `010` sprites first, `011` tilemap first, `1xx`
  the second set of the same order) and bits 3:1 select the **active** first-or-second set per layer (sprites, Layer 2, ULA; the tilemap's choice is NR `#6B` bit 4). NR `#40`
  index, NR `#41` 8-bit write (blue's LSB = OR of the other two blue bits), NR `#44` 9-bit write in two bytes
  (first the high 8 bits, second bit 0 = blue LSB and bits 7:6 = the Layer 2 "priority" bit of NR `#44`); the index
  auto-increments after `#41` and after the second `#44` write unless NR `#43` bit 7 disables it.
- **Order of colour sources** per pixel after the palette lookup (stage 2): fallback colour NR `#4A` if nothing is
  opaque; layers S (sprites), L (Layer 2), U (ULA / LoRes / tilemap) in the order NR `#15` bits 4:2:
  `000 SLU`, `001 LSU`, `010 SUL`, `011 LUS`, `100 USL`, `101 ULS`, `110` blend `(U|T) S (T|U) (B+L)` (colours added
  and clamped to 7 per channel), `111` blend with `-5` (`B + L - 5`, clamped to `[0, 7]`); a Layer 2 pixel whose
  priority bit is set wins over everything in modes `000`, `010`, `100`, `101`, `110`, `111`.
- **ULA / tilemap combination** (NR `#68` bits 6:5 blend mode, bit 0 stencil, bit 7 disable ULA): `00` = ULA vs
  tilemap by the tilemap's "below" bit; `10` = the combined ULA/tilemap colour is the blend input; `11` = tilemap
  as the mix input; `01` = the blend picks top / bottom by the below bit (modes 6 / 7 only). **Stencil** mode (NR
  `#68` bit 0, ULA and tilemap both enabled) shows the **bitwise AND** of the two colours when both are opaque.
- **Transparency:** global transparent colour NR `#14` (an 8-bit RGB332 compared with the top 8 bits of the 9-bit
  colour), sprites use NR `#4B` index, tilemap NR `#4C` 4-bit index, Layer 2 uses the global colour; a clipped ULA
  pixel is transparent; in a LUS / USL / ULS order the ULA border over a transparent tilemap does not hide a sprite.
- The pipeline adds a fixed delay (4 pixel clocks at 7 MHz) between the raster counters and the output; for a
  frame-level emulator the delay only matters for beam-exact register changes (copper, line interrupts) and is
  accounted for by the compare positions in sections 1 and 11.

## 18. Audio (`audio/*.vhd`, `zxnext.vhd` AUDIO block)

- **Three AY / YM chips** (turbosound): the YM2149-derived core `ym2149.vhd` is instantiated three times
  (ids `11`, `10`, `01`, i.e. AY #0, #1, #2). Port `#FFFD` write with `bit 7 = 1` and `bits 4:2 = 111` selects the
  chip (`bits 1:0`: `11` = AY 0, `10` = AY 1, `01` = AY 2; `00` = AY 0) and the stereo pan (`bits 6:5` = left / right
  enable) when turbosound is enabled (NR `#08` bit 1); otherwise a register select writes (`bits 7:5 = 000`) go to
  the selected chip. Chips share the data bus; the read returns the selected chip's register.
- **AY vs YM mode** (NR `#06` bits 1:0, `00` = YM, `01` = AY, `11` = reset): in AY mode the unused bits of the
  registers read as 0 and the envelope / noise follow the AY-3-8910 limits (the chips' register read masks:
  registers 1, 3, 5, 13 keep 4 bits, 6, 8, 9, 10 five bits); in YM mode they read back what was written. Noise is
  a 17-bit LFSR (`poly17`), the envelope has 5-bit volume steps and the clock enable `i_CLK_PSG_EN` comes from the top-level clock block (not read; the AY clock of the 128K is
  1.7734 MHz and does not change with the CPU speed).
- **Stereo mixing per chip:** ABC (default) puts A left, C right, B both; ACB (NR `#08` bit 5) swaps B and C;
  mono (NR `#09` bits 7:5, one bit per chip) sums all three channels into both sides.
- **DAC** (`soundrive.vhd`): four unsigned 8-bit channels reset to `#80`; channel A, D mono ports `#FB` and
  `#DF`; B, C `#B3`; SoundDrive group 1 `#1F #0F #4F #5F` (A B C D), group 2 `#F1 #F3 #F9 #FB`; stereo pairs
  `#3F / #5F` (A, D) and `#0F / #4F` (B, C); NR `#2C` (B / left), `#2D` (A, D / mono), `#2E` (C / right) mirror
  writes. Left = A + B (9 bits), right = C + D. Enabled by NR `#08` bit 3 (`reset = reset or not dac_en`).
- **Beeper and tape:** EAR output (port `#FE` bit 4) adds 512 (of 8192) to both sides when the internal speaker
  is not excluded, MIC (bit 3) adds 128; the final mix is
  `ear + mic + ay + (dac << 2) + i2s`, 13 bits, max ~5998; the host scales to its sample format. `exc_i =
  NR #06 bit 6 and NR #08 bit 4` mutes the internal speaker path.

## 19. CTC, UART, SPI, I2C

- **CTC:** the live instance has **four channels** (`NUM_CTC => 4`, the wrapper decodes 3 address bits so the
  ports `#183B-#1F3B` exist for channels 0-7 but 4-7 are unimplemented: their interrupt bits are tied to 0,
  although `nextreg.txt` still documents eight). Z80-CTC model: control word (`bit 0 = 1`), time constant, interrupt
  vector written only through channel 0, timer / counter mode, prescaler 16 or 256, clock/trigger edge select,
  chaining (channel n's trigger is channel n-1's ZC/TO, channel 0's from channel 3). A write to the interrupt-enable
  NextREG (`#C5`) does not overlap with port writes. ZC/TO is one clock wide; the interrupt source is the channel's
  zero-count.
- **SPI** (ports `#E7`, `#EB`): see [esxdos-and-sd.md](esxdos-and-sd.md) section 3. 16 CPU clocks per byte, mode 0,
  CPU clock / 2, **a write begun before the previous byte ended is ignored** (the CPU is not held; only the DMA is).
- **I2C** (bit-banged): port `#103B` (SCL) / `#113B` (SDA) write bit 0, read returns `1111111 & line`.
- **UART x 2** (ESP and Pi, same ports): `#153B` select (bit 6: 0 = ESP, 1 = Pi; bit 4 + bits 2:0 write the upper
  prescaler bits), `#163B` frame (reset `#18` = 8N1: bit 7 reset FIFOs, bit 6 break, bit 5 hw flow control, bits 4:3
  length, bit 2 parity on, bit 1 odd, bit 0 two stops), `#133B` Tx (read: status bits 7 break, 6 framing error, 5
  next byte after an error, 4 Tx empty, 3 Rx near full (3/4), 2 Rx overflow, 1 Tx full, 0 Rx available; write: send a
  byte; 64-byte Tx FIFO), `#143B` Rx (read: next byte, 0 when empty; 512-byte FIFO; write: baud prescaler, 14 bits in
  two writes, bit 7 selects the upper half). `prescaler = Fsys / baud` with `Fsys` from NR `#11` (27 MHz on HDMI
  timing). Either UART can be routed to the joystick ports (NR `#0B` bits 5:4, 0). **The ESP UART is the hook for
  the existing `ZiFi` / network adapter work**: an AT-command ESP model attaches here.

## 20. DivMMC and Multiface (`device/divmmc.vhd`, `zxnext.vhd` MEMORY / NMI blocks)

Full treatment with the esxDOS flow: [esxdos-and-sd.md](esxdos-and-sd.md). Facts in short:

- Register `#E3` (read/write): bit 7 CONMEM, bit 6 MAPRAM (**sticky**: a write cannot clear it; only NR `#09` bit 3
  written as 1, or a reset, clears it), bits 3:0 RAM bank (128K = 16 x 8K banks). Reset value `#00`.
- Memory effect: `#0000-#1FFF` = DivMMC ROM (page `#08`... physical `#010000`) when mapped and MAPRAM = 0, else RAM
  bank 3 read-only; `#2000-#3FFF` = RAM bank `e3(3:0)` (read-only while the bank is 3 and MAPRAM = 1). "Mapped" =
  CONMEM or the automap latch. Priority in `#0000-#3FFF` is above Layer 2, MMU and ROM.
- Automap entry points are data: NR `#B8` (which RST address), `#B9` (valid: always vs only with ROM 3 present),
  `#BA` (timing: 1 instant, 0 delayed), `#BB` (the extra ones: `#3Dxx`, `#1FF8-#1FFF` off, `#056A`, `#04D7`,
  `#0562`, `#04C6`, NMI `#0066` instant / delayed). Reset `#B8 = #83`, `#B9 = #01`, `#BA = #00`, `#BB = #CD`.
  Automap is decided by the **previous** M1: the decode samples M1 on the falling edge of the 28 MHz clock while
  MREQ is high and the latch changes at the next M1's MREQ edge (the "instant" entries add the effect to the same
  fetch). `RETN` (seen on the bus, and not while the Multiface is active) clears the latch and the button NMI.
- **NMI**: Multiface button F9, DivMMC drive button F10, expansion bus NMI (first come first served: MF, then
  DivMMC, then expansion); `nmi_state` machine IDLE -> FETCH (waits for an M1 fetch at `#0066`) -> HOLD (until the
  source releases) -> END (waits for the I/O write that ends the handler). NR `#C0` bit 3 selects the stackless
  NMI response (section 12).
- **Multiface**: ports move with NR `#0A` bits 7:6 (type: `#3F/#BF`, `#BF/#3F` or `#9F/#1F`), 16K ROM / RAM at
  physical page `#05` of the 8K numbering; the NMI source above; not yet read: the paged-in view and the
  `mf_is_active` hold rules (a task for the Multiface phase).

## 21. Keyboard and joysticks

- Port `#FE` read: `bit 7 = 1`, `bit 6 = EAR (or the port's own EAR bit)`, `bit 5 = 1`, `bits 4:0 = membrane columns` for the
  rows selected by the address high byte (a 0 bit selects a row, rows are ANDed); issue-2 keyboard behaviour (NR
  `#08` bit 0) makes bit 6 follow the MIC output.
- The Next keyboard has extra keys: NR `#B0` / `#B1` (read) expose `; " , . UP DOWN LEFT RIGHT` and `DELETE EDIT
  BREAK INV TRUE GRAPH CAPS-LOCK EXTEND`; unless NR `#68` bit 4 is set they also appear in the 8 x 5 matrix as the
  Caps-Shift / Symbol-Shift combinations a Spectrum 128 would give. For an emulator: the host key goes to the
  matrix through one table and to NR `#B0` / `#B1` through another.
- PS/2 keyboard and mouse use a keymap RAM loaded through NR `#28`-`#2B` (the firmware loads `keymap.bin`; the
  same registers load the key-joystick table); NR `#06` bit 2 is the PS/2 mode (keyboard first or mouse first).
- Joysticks: NR `#05` bits 7:6 and 3 (left) / 5:4 and 1 (right) select the type: Sinclair 2, Kempston 1 (`#1F`),
  Cursor, Sinclair 1, Kempston 2 (`#37`), MD 1 / MD 2 (3 or 6 button, extra buttons in NR `#B2`), both in I/O mode.
  The key-joystick table (NR `#28` bit 7 = 1) maps keys to a joystick.
- Hotkeys in hardware: F1 hard reset, F2 scandoubler, F3 50 / 60 Hz, F4 soft reset, F5 / F6 expansion bus on / off,
  F7 scanlines, F8 CPU speed, F9 Multiface NMI, F10 DivMMC NMI, gated by NR `#06` bits 7, 5, 4, 3.

## 22. Differences found against ZEsarUX (`machines/tbblue.c`, commit `2d8dba1`) and MAME

| Item | FPGA (this note) | ZEsarUX | Action |
|:--|:--|:--|:--|
| Copper modes (NR `#62` bits 7:6) | `01` from index 0 and loop, `10` from the last point and loop, `11` from 0 and restart at (0,0); 1024 x 16-bit entries, the 10-bit address wraps; same value written again does not restart; one instruction per 28 MHz clock; WAIT compares `hcount >= H*8 + 12` | modes named "run", "loop", "loop reset", "VBI"; `copper_pc` steps over `TBBLUE_COPPER_MEMORY`; the comment still says "stop at last address"; cost per MOVE 2 T and WAIT 1 T in CPU T-states | follow the FPGA; test with Tests/base/Copper |
| Sprites per line | a time budget (the engine is still busy at the next line start), flag in `#303B` bit 1 | a count `MAX_SPRITES_PER_LINE = 100` | start with a count, replace by the budget in N7 once the per-sprite cost is read |
| `LDIRSCALE` | decoded, acts as `LDIRX` | not implemented (no grep hit) | as FPGA |
| Boot | the real boot ROM, firmware | `tbblue_fast_boot_mode` skips the firmware (NR `#03` = 3, NR `#08` = `#1A`, NR `#50`/`#51` = `#FF`), also ships a boot ROM that is **older** than the current one | the bare personality does the same register set; use the FPGA's boot ROM for the real boot |
| Contention | 3.5 MHz only | tables per machine type (`contend_pages_128k_p2a`), turbo handled by `tbblue_set_emulator_setting_turbo` | follow the FPGA rules above, then compare |
| MAME | `specnext.cpp` 128K screen height 312 | | the FPGA says 311 |

## 23. Still unread (after this pass)

Multiface internals (paged-in view, `mf_is_active`), the Pi GPIO / I2S block, the expansion bus control
(`nr_80`-`nr_8a`, `i_BUS_*`), the hdmi path (not needed), `membrane.vhd` scan timing and `membrane_stick.vhd`,
the keymap RAM format, the remaining NextREG read-back table (`port_253b_dat` multiplexer at `zxnext.vhd`
~5800-6300), and `ym2149.vhd`'s exact volume table. ZEsarUX `tbblue.c` (9005 lines) was sampled (reset, boot,
copper, sprites, contention), not read through. CSpect is closed source. NextZXOS itself is a binary distribution; the
parts that matter for the machine (the card driver in ROM page 2 and the DivMMC ROM) were disassembled from the
shipped images ([esxdos-and-sd.md](esxdos-and-sd.md)).

## 24. Core version history that matters for emulation (`changelog.md`, 3.00 to 3.02.03)

The VHDL read here is the head of the repository, core **3.02.03** (issue 5 bitstreams added; the repository changelog
ends there). Behaviors changed between releases; boards in the field run anything from 3.01.x to 3.02.x. The rows below
are the entries that change observable behavior; "from" is the core that has it.

| From | Change | Emulator consequence |
|:--|:--|:--|
| 3.01.01 | interrupt time in 48K / 128K frames was off by one cycle; port decode style follows the *current video timing*, not the machine type | the INT column (`c_int_h`) of section 1 is the corrected one |
| 3.01.01 | NextREG `#8E` (fast bank switching), NextREG `#85` / `#89` bit 7 (soft or hard reset for the port-enable registers) | included in section 4 |
| 3.01.08 | **CTC**: eight channels at `#183B`-`#1F3B`; tilemap base / definition addresses may point into the first 8K of bank 7; Multiface type from the ROM file name; `ED 7E` is `IM 2` | |
| 3.01.09 | Profi memory mapping removed from NR `#8F`; AY register select readable through `#BFF5`; **stackless NMI** (NR `#C2` / `#C3`); `RETN` disables Multiface and DivMMC; NR `#02` bits generate NMIs; **hardware IM2 mode, 14 devices**; NMI acknowledge timing fix | sections 12, 18, 20 |
| 3.01.10 | **CTC reduced to four channels "temporarily, for space reasons"**; UART rewrite (512-byte Rx, 64-byte Tx, programmable frames, flow control); DivMMC breakpoints programmable at all RST locations (NR `#B8`-`#BB`); I/O traps for the +3 FDC (`#2FFD` / `#3FFD`, NR `#D8`-`#DA`); MD pad all buttons, user-defined key joystick; Z80 fixes: IM2 vector response, RLD / RRD cycles, idle address bus | the live ports of section 19: four CTCs in the current head |
| 3.02.00 | one code base for issue 2 (Spartan 6) and issue 4 (Artix 7), NR `#0F` identifies the board; **DivMMC: bank 3 is always read-only when MAPRAM is set and fills the lower 8K even with CONMEM**; software NMI faster (seen before the end of the instruction), copper can raise NMI through NR `#02`; port `#DF` as Kempston 1 alias (when the mouse is off); RETI / RETN recognized even with DMA cycles between the opcode bytes; **extra half cycle on MISO** (slow SD cards); EAR schmitt trigger on issue 4 | section 20 matches |
| 3.02.01 | HDMI timing corrected for 50 / 60 Hz 48K, 50 / 60 Hz 128K and 50 Hz Pentagon | output only |
| 3.02.02 | **Z80: `EX (SP),HL` cycles fixed; `LDPIRX` works at all CPU speeds. DMA: transfers correct at all speeds (before: the read byte was one read cycle behind at 3.5 and 7 MHz). ULA: contention when I/O and memory contention coincide fixed. CTC: counter does not pass through zero; time constant can be written while counting. SPRITES: the early termination logic is removed, "the sprite hardware will always use as much time as possible", the limit is **at least 100 sprites per line**; before it, drawing stopped around 70** | the reference for N1 / N8 / N7: take 3.02.02 or later; a pre-3.02.02 board would show older behavior in the hardware probe programs |
| 3.02.03 | issue 5 bitstreams; **NR `#0A` bit 5 swaps SD0 / SD1**; NR `#81` bit 3 enables the +3 FDC control signals (issue 5 only) | sections 4 and 20 |

So **core 3.02.03 is the reference and the only emulated core for now** (owner decision 2026-10-08, Q2): older cores are kept in
mind but not built; the table is the list of what older boards did differently. The hardware probe programs print the core version on their second row so results are filed per core.
