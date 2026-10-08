# ZX Spectrum Next: peripherals

**Date:** 2026-10-07 · part of [README.md](README.md) · ports in [research-nextreg-and-ports.md](research-nextreg-and-ports.md)

Every device is a plain class in `core/src/emulator/io/next/`, owned by `PortDecoderNext`, with `Reset(kind)`,
`Save/Load` into `NextState` (or its own small blob when large) and a `Describe()` for the reports. Register
facts come from NR-TXT / PORTS-TXT; behavior that is only in MAME/jnext is marked with that source; **N-read**
means the phase named reads the VHDL/wiki page and writes the rule into the tests first.

## 1. zxnDMA (ports `#0B` z80dma mode, `#6B` zxn mode)

| Item | Rule |
|:--|:--|
| Model | the Zilog Z80 DMA register set (WR0-WR6, read mask, status byte) plus a "zxn" mode with a prescaler register (WR2 third byte in MAME's source: `ZXN_PRESCALER REG(2,2)`) and a burst mode "primarily used to play digital music" (NR-TXT text). Accessing the DMA through `#0B` or `#6B` selects the mode |
| Transfer | memory to memory, memory to port, port to memory, port to port; fixed or incrementing addresses per side; byte, continuous and burst operation modes; the end-of-block raises the DMA interrupt source (NR `#CC`-`#CE` enables) |
| Quirks | MAME: a WR0 with transfer-mode bits unset is forced to "Transfer" (`data = (data \| 1) & ~2` when `(data & 0x82) == 2`); MAME's file says z80dma mode was done "based on intensive testing of the device" |
| Bus | the DMA reads and writes through the slot table (so the Layer 2 write overlay and the DivMMC page apply as for the CPU, except where the VHDL differs: **N-read (N6)**) and the port decoder (so it can feed the DACs and the sprite pattern port); the CPU does not step meanwhile ([design-cpu.md](design-cpu.md) section 5) |
| Timing | 28 MHz-tick cost per byte: 2 CPU-cycle reads/writes at the current speed per the MAME clock (`28 MHz / 8` scaled), burst delay from the prescaler. **N-read (N6)**: exact cycles per byte for memory and for port operands |
| Pacing | `dma_delay_w` in MAME: a DMA may be delayed until the bus is free; we model the delay as a flag the machine step reads |
| TTD | state blob; DMA writes to RAM are why the port-read journal cannot isolate the machine ([design-ttd.md](design-ttd.md) section 4) |
| Tests | ZXSpectrumNextTests `Tests/` DMA programs if present (N-read: the folder was only listed); register-level tests from the Zilog DMA datasheet |

## 2. CTC, IM2 and the interrupt source

| Item | Rule |
|:--|:--|
| CTC | four channels at ports `#183B`-`#1B3B` (FPGA analysis in jnext), Z80 CTC semantics, clocked by the 28 MHz system clock via the prescaler, zero-count outputs chained (MAME wires ZC/TO of channel n to the trigger of channel n+1), interrupt vector from the CTC base. Reuse: `Z84Ctc` from `core/src/3rdparty/z84c15/` is a CTC with the same counter modes and a configurable clock; whether to reuse it or write a 100-line plain CTC is decided in N5 by reading `z84ctc.cpp` (naive: write the plain one) |
| IM2 controller | hardware vector mode (NR `#C0` bits 7:5 + 3-bit source index shifted, MAME `m_nr_c0_im2_vector`), 14 sources in priority order (MAME enum `INT_PRIORITY_*`: line, ULA, CTC 0-7, UART0 RX/TX, UART1 RX/TX, ...). Enables in NR `#C4` (ULA/line/expansion), `#C5` (CTC), `#C6` (UARTs), `#CC`-`#CE` (DMA interrupts per source); status in NR `#C8`-`#CA`; pulse-mode interrupt otherwise |
| Source | `NextInterruptSource : IInterruptSource` (TSConf/Sprinter precedent): `IsAsserted()`, `Vector()`, `OnReti()`; the ULA/line sources are driven by the raster counter ([design-video-timing.md](design-video-timing.md) section 4) |
| DMA interrupts | per source enables NR `#CC`-`#CE`: the DMA can raise its end-of-transfer on the same controller |

## 3. SPI, SD cards, DivMMC, flash

Storage-manager designs apply unchanged ([integration-next.md](../2026-09-28-storage-manager/integration-next.md),
[storage survey zx-next.md](../2026-09-28-storage-controllers-survey/zx-next.md)). Summary of the machine side:

| Item | Rule |
|:--|:--|
| `#E7` select | write-only; the bit that is 0 chooses the device: bit 0 SD0, bit 1 SD1, bits 2-3 the Pi SPI, bit 7 the FPGA flash (config mode only); if not exactly one bit is 0, no device. NR `#0A` bit 5 swaps SD0 and SD1 (core 3.02.03; writable only in config mode; cleared by hard reset) |
| `#EB` data | a write is a full-duplex exchange; a read returns the byte of the previous exchange and starts a new one sending `#FF` (the `ZControllerSpi` rule) |
| Cards | `SdCardSpi` x 2 in slots `sd.next0` (`required`) and `sd.next1`; SDHC; the card gets exchange byte-by-byte; speed is instant (MAME optionally scales by CPU clock; unneeded) |
| One card, two selects | a card is never visible on both sockets (jnext's phantom-card loop); the slot planner already refuses the same source in two slots |
| Flash | a 16 MB persistent blob (config mode access only); initial content is an anti-brick + core image placeholder: our machine does not run the FPGA configuration, so flash only needs to answer the JEDEC id and read commands for the updater paths. Deferred: N11 |
| Pi SPI | selected-and-ignored: reads give `#FF` |
| DivMMC | `#E3` readable, bits 3:0 bank (16 x 8K = 128K), bit 6 MAPRAM sticky until NR `#09` bit 3 or reset (here: reset clears `#E3`, unlike DivIDE), bit 7 CONMEM; paging: `#0000-#1FFF` ROM (or bank 3 with MAPRAM), `#2000-#3FFF` the bank, read-only when MAPRAM and bank 3. Automap by M1 fetch at programmable entry points NR `#B8`-`#BB` (defaults `B8=#83 B9=#01 BA=#00 BB=#CD`), "ROM 3 only" conditions, RETN clears the hold, instant entry on `#3Dxx`; enable by NR `#0A` bit 4 (NextZXOS keeps it off). All its ports are gated by NR `#83` bits. (storage survey table; MAME `specnext_divmmc.cpp`) |
| esxDOS | not a device: the DivMMC ROM is `enNxtmmc.rom` loaded from the card by `TBBLUE.FW` into the system area ([design-boot-and-firmware.md](design-boot-and-firmware.md)) |
| Framework | the shared DivMMC/DivIDE framework of the storage survey (8K windows, `DivPaging`, `SpiPort`) is built **for the Next first** because no other machine has a use for it in this program; its generic parts stay generic |

## 4. I2C, RTC

| Item | Rule |
|:--|:--|
| Ports | `#103B` write bit 0 = SCL; `#113B` read/write bit 0 = SDA (open-drain); the CPU bit-bangs |
| Devices | a DS1307 at 7-bit address `#68` (MAME `I2C_DS1307`); the I2C bus also goes to the Pi GPIO (NR `#A0` bit 3) and an internal connector (HDMI DDC, ignored here); `RTC.SYS` of esxDOS keeps a signature in the chip's RAM (storage-manager doc) |
| Time | the DS1307 core is new (a 56-byte RAM, a time register set, the BCD format, the CH bit); clock source = host time by default, fixed time with `[NEXT] RtcFixed=` for deterministic tests (jnext's `--rtc`), and **emulated time while TTD records** (the DS12887 rule) |
| Persistence | the RAM goes into a small blob; `RtcFile=` for a persistent image like the other RTCs |
| Fitted | optional on the board: `NextBoard::rtcFitted`; absent = NACK on address `#68` |

## 5. UART, ESP, Pi

| Item | Rule |
|:--|:--|
| Ports | `#133B` TX data (write) / status (read: bit 0 receive buffer has bytes, bit 1 transmitter busy, bit 2 receive buffer full), `#143B` RX data (read) / prescaler low or high (write, bit 7 selects the half), `#153B` control (bit 6 selects the Pi UART instead of the ESP, bit 4 = write of the prescaler MSB bits 2:0) |
| Baud | `prescaler = Fsys / baud` with Fsys from NR `#11` (28 MHz base: 27 MHz for HDMI timing; 115200 baud = 234 for HDMI per NR-TXT); the UART clocks bytes in emulated time: one byte takes `10 * prescaler` Fsys ticks (start + 8 + stop; N-read) |
| FIFOs | receive FIFO with "near full" interrupt and TX-empty interrupt (MAME wires both into IM2 sources); size N-read |
| Peers | the ESP side is an AT-command stub (off by default, `[NEXT] Esp=`): a minimal ESP-01 as in jnext is a later phase (N10); the Pi side is unattached. Both attach through the existing serial-peer abstraction used by the Sprinter's SprinterESP slot card (`netstate::SerialPort`, TTD ids `SlotSerial1/2`) |
| Joystick UART | `#37` write bits 7:6 mode 2 = UART redirects the ESP or Pi UART to the joystick connector (bit 0 chooses); NR `#0B` is the I/O mode enable. Needed by DeZog serial builds; N10 |
| Gating | NR `#82`-`#85` bit 12 (UART ports) and bit 10 (I2C) |

## 6. Audio: AY, DAC, beeper

| Item | Rule |
|:--|:--|
| AY | three AY-3-8912 / YM2149 chips at 1.75 MHz (MAME: `14 MHz / 8`) sharing `#FFFD`/`#BFFD`; selection by writing `#FFFD` with the pattern in PORTS-TXT: bit 7 = 1, bit 6 left enable, bit 5 right enable, bits 4:2 = 111, bits 1:0 the chip (11 = AY0, 10 = AY1, 01 = AY2, 00 reserved); the register-select form is a write with bits 7:4 = 0; the active chip persists; with TurboSound off (NR `#08` bit 1) the current chip freezes; stereo ABC or ACB (NR `#08` bit 5), per-chip mono (NR `#09` 7:5), AY or YM mode and "hold in reset" (NR `#06` 1:0) |
| Ports | `#FFFD` read returns the selected register of the active chip, `#BFFD` data write and (on +3 timing only per PORTS-TXT) read |
| DAC | four 8-bit channels A,B (left) C,D (right) written through seven alias sets; each alias set has an enable bit in NR `#82`-`#85` (bits 17-23) and all need NR `#08` bit 3; soft reset value `#80`; NR `#2C`-`#2E` write the DACs directly and `#2D` sets A and D |
| Beeper | EAR/MIC bits of `#FE`; internal speaker enable NR `#08` bit 4; "divert BEEP only to the internal speaker" NR `#06` bit 6; I2S direct to EAR |
| I2S | Pi audio input NR `#A2`: accepted and silent in this design |
| Mixing | into the existing `AudioMixer`: 3 AY (each stereo), 4 DAC channels, beeper; same sample rate as the machine; per-source gains and mute follow the existing audio channel names; MAME mixes AY channels A 0.5L, B 0.25L+0.25R, C 0.5R, DACs 0.75, speaker 0.5 as a starting point only |
| Reuse | the AY chip classes of `core/src/emulator/sound/chips/` and the `TurboSound` blob; DACs as a covox-like 8-bit device (`covox.cpp`); N4 checks that three chips fit the sound manager |
| TTD | existing ids (`TurboSound`, `Covox`) cover the AYs and a DAC; a `NextAudio` blob adds the DAC aliases state, the active chip and the stereo flags |

## 7. Keyboard, PS/2, mouse, joysticks

| Item | Rule |
|:--|:--|
| Matrix | the 8x5 ZX matrix read through `#FE` with the existing keyboard class; the issue-2 keyboard bit (NR `#08` bit 0) selects the matrix variant (the rule is N-read) |
| Extended keys | 16 extra keys (cursor keys, DELETE, EDIT, BREAK, INV VIDEO, TRUE VIDEO, GRAPH, CAPS LOCK, EXTEND, `;`, `"`, `,`, `.`) as NR `#B0`/`#B1` bits, and the matrix mapping (`NR #68` bit 4 cancels the 8x5 entries) |
| PS/2 | the host keyboard maps to the matrix through the firmware-loaded keymap (NR `#28`-`#2B` write the 1K keymap table; the table is applied by the machine, not by the host layer). PS/2 mode primary = keyboard or mouse (NR `#06` bit 2, config mode) |
| Mouse | Kempston mouse ports `#FBDF` X, `#FFDF` Y, `#FADF` wheel (7:4) + buttons (2:0), DPI shift NR `#0A` bits 1:0, button swap bit 2; the existing Kempston mouse device (TTD id 7) is reused with the Next's port addresses |
| Joysticks | modes from NR `#05`: Sinclair 1/2, Kempston 1/2 (`#1F`, `#37`), Cursor, MD 3/6-button (extra bits in the same ports), I/O mode; Sinclair/Cursor sticks appear as key presses; reads outside the mode return 0 |
| Host mapping | the existing joystick and keyboard automation (`joystick_input`, `type_input`, `mouse_input`) is unchanged; a "joystick connector" selector (1 or 2) is added to the joystick call for the Next (N8) |
| Hotkeys | F1 hard reset, F4 soft reset (nextreg.txt), F3 50/60 Hz and F8 CPU speed (NR `#06` bits 5 and 7). The keys for the M1 and DRIVE buttons are N-read (the user manual, N0). Host-side shortcuts become Qt menu entries and automation verbs, not keyboard grabs |

## 8. Multiface

A +3-type Multiface (ports per PORTS-TXT: enable `#3F`, disable `#BF`; the file lists the three MF types and says the Next is the +3 type); its 8K ROM is loaded from the card by the firmware (`enNextMf.rom`), its 8K RAM is part of the system area. The M1 button (NR `#06` bit 3) raises an NMI and pages the Multiface in at `#0000-#3FFF` (decode order above DivMMC). The port enable is NR `#82`-`#85` bit 9. MAME has `specnext_multiface` (114 lines); N8 reads it and TASK-8-MULTIFACE-PLAN.md of jnext.

## 9. Expansion bus and slot cards

NR `#80`-`#8A` decide which internal ports answer and which I/O is propagated to the bus. The Next gets a ZX-bus slot in the slot system (MAME: one `ZXBUS_SLOT`, `zxbus:1`), with the existing card catalog (General Sound, NeoGS, ...). The slot planner test says Next is "not a creatable model" today; the phase that makes the model creatable updates that test. ROMCS (NR `#80` bit 6, `#81` bit 7): a card that replaces ROM; deferred.

## 10. Raspberry Pi accelerator

NR `#90`-`#9B`, `#A0`, `#A2` (GPIO, peripheral enable, I2S) are stored and read back; no emulated Pi.
