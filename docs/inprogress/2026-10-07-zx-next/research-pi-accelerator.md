# Research: the ZX Spectrum Next Raspberry Pi Zero accelerator ("Pi" slot)

Date: 2026-10-12. Read-only research for a future Pi peer model in unreal-ng. Nothing in the repository other than this file was changed.

Source tags used below:

- `[VHDL]` = `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/fpga/ZX_Spectrum_Next_FPGA/cores/zxnext/src/` (official core: `zxnext.vhd`, `zxnext_top_issue5.vhd`, `audio/i2s.vhd`, `audio/i2s/*.vhd`, `serial/uart*.vhd`)
- `[DIST]` = `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/documents/nextzxos-distribution-docs/` (NextZXOS distribution docs: `rpi/linux/*.txt`, `rpi/DotCommands/*.md`, `dotcommands/pisend.txt`, `extra-hw/rpi/PIreadme.txt`, `extra-hw/io-port-system/{ports,registers}.txt`)
- `[WIKI]` = `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/documents/specnext-wiki/` (local mirror of wiki.specnext.dev; Pi:* pages are `Pi_*.html`)
- `[EMU]` = `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/emulators/` (jnext 810cfdff22ac, zesarux 2d8dba1, MAME next driver f43983b62edf)
- `[WEB]` = fetched from the web on 2026-10-12 (GitHub API / raw files)
- `[UNG]` = this worktree, `/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/wt-next-copper2`

Nothing here is from the Pi-side NextPi image itself (the 6 GB image is not on this machine, and the main `StalePixels/NextPi` repo URL returned 404, so it is private or renamed). Everything about the Pi-side software comes from the shipped docs in `[DIST]`, the wiki, and the Next-side client sources on GitHub. Where that is a limit it is said so.

---

## 0. One-paragraph summary

The Pi Zero sits on a 2x20 header ("Accelerator Board", J-something) wired straight to 28 FPGA pins `accel_io[27:0]` = BCM GPIO0..GPIO27. There is **no** other link: no reset line, no memory/bus access, no interrupt line, no I2C EEPROM-style ID beyond GPIO0/1 (never driven). The FPGA muxes four peripherals onto those GPIOs under NR #A0 / #A2 (UART1 on GPIO14/15 with RTS/CTS on 16/17, I2C on 2/3, SPI0 on 7..11, I2S on 18..21) and otherwise exposes all 28 pins as plain GPIO through NR #90-#93 (output enable), #98-#9B (output value / pin read). The only protocol in production use is **UART1 at 115200 baud (default) or 2 Mbaud**, speaking to the **NextPi Supervisor ("SUP>" prompt, a root bash on /dev/ttyAMA0)**; file transfer uses the **NBN** binary block protocol (16 KB blocks, 8-bit additive checksum) and audio comes back over **I2S** (Pi is I2S master, Next is slave). A minimal useful emulated Pi is therefore "a serial peer that prints `SUP> ` after a newline and understands `nextpi-admin_version`, `nextpi-file_receive -nbn` and `nextpi-file_transmit -nbn`"; a faithful one is the real NextPi image under QEMU `raspi0` with its PL011 console on a TCP socket (already done by third parties for CSpect and jnext).

---

## 1. Physical and logical interface

### 1.1 Board level

- A 40-pin (20x2) male header on the main board labelled "Accelerator Board" takes a Raspberry Pi Zero fitted with a female header on its underside. The Pi has its own SD card and boots on its own. [WIKI Pi_Main_Page.html, Pi_.html]
- The FPGA top level exposes `accel_io : inout std_logic_vector(27 downto 0)` = BCM GPIO0..GPIO27 one to one. [VHDL zxnext_top_issue5.vhd:154-155, 2293-2338; zxnext.vhd:233-236 `i_GPIO`, `o_GPIO`, `o_GPIO_EN`]
- Inputs go through one flop stage (`accel_i_q <= accel_io`, top:801); outputs are `'Z' when pi_gpio_en(n) = '0' else pi_gpio_o(n)`, registered on CLK_28.
- The Pi's own power comes from the Next's 5 V rail (board wiring; not in the FPGA). The J15 socket on the Next board (ESP/I2C pins, 12 V) is a different connector; the Pi header is not J15. [WIKI GPIO_Socket_(J15).html]
- **There is no Pi reset or power-enable pin in the FPGA.** `o_RESET_PERIPHERAL <= nr_02_bus_reset` (NR #02 bit 7) goes to the expansion bus and the ESP (per jnext's reading of `nextreg.txt`, `[EMU] jnext/doc/design/ESP01-EMULATOR-DESIGN.md` section 4.2); nothing in `zxnext_top_issue5.vhd` ties it to `accel_io`. I did not find any statement that it resets the Pi, and the Pi docs say "no other existing hardware control points exist between the two devices". [DIST rpi/linux/Devices.txt]
- Documented as "no hardware link outside UART/I2S/GPIO": [DIST rpi/linux/Devices.txt; WIKI Pi_Devices.html, Pi_Basic_Concepts.html].
- Early cores (before 3.00) had a "TK-PI" bus monitor and used the Pi for HDMI; removed. I2C bit-banging and SPI notes are historical. [DIST extra-hw/rpi/PIreadme.txt]

### 1.2 Pin-by-pin mux (zxnext.vhd:2268-2373)

| BCM GPIO | Default (NR #A0/#A2 off) | With peripheral enabled | Direction seen from Next | VHDL signals |
|---|---|---|---|---|
| 0, 1 | `pi_gpio_o(1:0)`, but output enable bits 1:0 are forced 0 on write of NR #90 (`nr_wr_dat(7:2) & "00"`) | same | input only | zxnext.vhd:5536-5538 |
| 2 (SDA) | GPIO via `pi_gpio_en(2)`/`pi_gpio_o(2)` | `pi_i2c1_en`=1: open drain, drives low when `i2c_sda_o`=0 (`gpio_02_en <= not i2c_sda_o`, `gpio_02 <= '0'`) | in/out | 2311-2312 |
| 3 (SCL) | GPIO | `pi_i2c1_en`=1: open drain on `i2c_scl_o` | in/out | 2314-2315 |
| 7 (CE1) | GPIO | `pi_spi0_en`=1: output = `spi_ss_rpi1_n` = port #E7 bit 3 | out | 2297-2298, 3329 |
| 8 (CE0) | GPIO | `pi_spi0_en`=1: output = `spi_ss_rpi0_n` = port #E7 bit 2 | out | 2294-2295, 3330 |
| 9 (MISO) | GPIO | `pi_spi0_en`=1: input, `pi_spi0_miso <= i_GPIO(9)` | in | 2300-2301, 3279 |
| 10 (MOSI) | GPIO | `pi_spi0_en`=1: `spi_mosi` | out | 2303-2304 |
| 11 (SCLK) | GPIO | `pi_spi0_en`=1: `spi_sck` | out | 2306-2307 |
| 14 | GPIO | `pi_uart_en`=1: output `uart1_tx_pi` when `pi_uart_rxtx`=0 (enable = `not pi_uart_rxtx`); input (Next RX) when `pi_uart_rxtx`=1 | see 1.3 | 2323, 2328, 2333 |
| 15 | GPIO | `pi_uart_en`=1: output `uart1_tx_pi` when `pi_uart_rxtx`=1; input when 0 | see 1.3 | 2324, 2329, 2333 |
| 16 | GPIO | UART HW flow control (`uart1_hwflow_en`=1): output `pi_uart_rtr_n` when `pi_uart_rxtx`=1; CTS input when `pi_uart_rxtx`=0 | | 2325, 2330, 2334 |
| 17 | GPIO | HW flow control: output `pi_uart_rtr_n` when `pi_uart_rxtx`=0; CTS input when 1 | | 2326, 2331, 2334 |
| 18 | GPIO | `pi_i2s_en`=1: always input = I2S SCK from Pi (`pi_si2s_sck`) | in | 2340, 2355 |
| 19 | GPIO | `pi_i2s_en`=1: always input = I2S WS (`pi_si2s_ws`) | in | 2341, 2356 |
| 20 | GPIO | `pi_i2s_en`=1: output `pi_i2s_sd_o` if NR #A2 bit 4 = 1, else input | | 2342, 2347, 2350 |
| 21 | GPIO | `pi_i2s_en`=1: output if NR #A2 bit 4 = 0, else input (`pi_i2s_sd_i`) | | 2343, 2348, 2350 |
| 4-6, 12-13, 22-27 | plain GPIO | plain GPIO | in/out | 1709-1710 |

Notes:

- The RX/TX crossing: the Pi's TXD is GPIO14 and RXD GPIO15, so to talk to a Pi the Next's RX must sit on GPIO14 and TX on GPIO15 (`pi_uart_rxtx` = 1, the "Pi" setting; 0 is for "Pi hats" which have the Next-as-host orientation). Comment in VHDL: "uart 14,15(,16,17) (need to cross wires for pi!) connect Rx to GPIO14 if pi_uart_rxtx = 1" (2319-2321).
- With `pi_uart_en`=0 the Next's UART1 RX input is tied to `'1'` (idle) and CTS to `'0'` (2333-2334). TX does not reach the pins. So a UART1 write with the Pi UART disabled goes nowhere (and nothing arrives).
- The 28 `GPIO` outputs `o_GPIO` are concatenated per pin at zxnext.vhd:1709-1710.

### 1.3 Register bits (every bit)

Source of truth is the VHDL; the written docs disagree with it on NR #A0 bits 4/5 (see the callout).

**NR #A0 (Pi peripheral enable)**, R/W, reset 0 (any reset, `reset='1'` branch at zxnext.vhd:5080). Write: whole byte stored (5560-5561). Read: `"00" & bits(5:3) & "00" & bit(0)` (6188-6189), so bits 7,6,2,1 always read 0.

| Bit | VHDL signal | Effect | Board wiring only? |
|---|---|---|---|
| 5 | `pi_uart_rxtx` | 1: UART1 RX on GPIO14, TX on GPIO15 ("for comm with Pi"); 0: RX on GPIO15, TX on GPIO14 ("for Pi hats"). Also swaps which of GPIO16/17 is RTR vs CTS | no, FPGA mux |
| 4 | `pi_uart_en` | 1: UART1 owns GPIO14/15 (and 16/17 if hw flow on); GPIO output enables for those pins are overridden. 0: UART1 RX idles high | no |
| 3 | `pi_i2c1_en` | 1: I2C bus (the same bit-bang I2C as the RTC, ports #103B/#113B) is mirrored on GPIO2/3; `pi_i2c1_sda/scl` = `i_GPIO(2/3)` ANDed into the port read. 0: those inputs read `'1'` | no |
| 0 | `pi_spi0_en` | 1: SPI master (ports #E7 CS select, #EB data) drives GPIO7/8/10/11, MISO from GPIO9; Pi chip select is port #E7 value `#F7` (CE1/bit 3) or `#FB` (CE0/bit 2) | no |
| 7,6,2,1 | | reserved, read 0 | |

> **Doc/VHDL conflict (verified against VHDL).** `registers.txt` [DIST extra-hw/io-port-system/registers.txt:921-930] and the wiki page NextReg $A0 [WIKI NextReg_$A0.html] say bit 5 = "Enable UART" and bit 4 = Rx/Tx orientation. The VHDL has the opposite: `pi_uart_rxtx <= nr_a0(5)`, `pi_uart_en <= nr_a0(4)` (zxnext.vhd:2278-2279), and the NextPi doc agrees with the VHDL: `0 0 RXTX UART I2C1 0 0 SPI0` [DIST rpi/linux/UART.txt]. Real software always writes `#30` (both bits) so the difference is invisible in practice: `ZXN_NEXTREG(0xA0, 0x30); IO_UART_CONTROL = 0x40;` [WEB StalePixels/PiTools `src/lib/piUartSwitch.c`]. A model should follow the VHDL.

**NR #A2 (Pi I2S control)**, R/W, reset 0. Write stores the whole byte (5563-5564). Read: `a2(7:6) & '0' & a2(4:2) & '1' & a2(0)` (6191-6192), so bit 5 reads 0 and bit 1 reads **1** regardless.

| Bit | VHDL | Effect |
|---|---|---|
| 7 | `pi_i2s_enL` (and part of `pi_i2s_en`) | left channel enable |
| 6 | `pi_i2s_enR` | right channel enable. `pi_i2s_en` = bit7 or bit6; both 0 = I2S off, GPIO18-21 are plain GPIO. 11 = stereo, 10 = mono from left, 01 = mono from right (the missing channel gets the other's sample, 2358-2359) |
| 5 | | reserved, reads 0 |
| 4 | `pi_i2s_inout` | 1 = "Pi": Next outputs data on GPIO20 and reads data from GPIO21 (Pi's PCM_DIN = GPIO20, PCM_DOUT = GPIO21); 0 = "hats": reversed |
| 3 | `pi_i2s_muteL` | mute left (forces mid-scale 0x200) |
| 2 | `pi_i2s_muteR` | mute right |
| 1 | (`pi_i2s_slave`, commented out) | reserved; reads 1. "Master" mode was removed in core 3.1.5; SCK/WS are always supplied by the Pi |
| 0 | `pi_i2s_ear` | 1: Pi audio is not mixed, instead thresholded into the EAR input of port #FE (`pi_fe_ear`, Schmitt: set when both sample MSBs = 11, clear at 00; 2361-2373) and the L/R mix inputs are forced mid-scale |

NR #A3 (I2S clock divide, master mode) is commented out of the VHDL (5082, 5567, 6195) and documented as removed. Model: ignore (reads as whatever the generic default is; not in this core).

**NR #90, #91, #92, #93 (GPIO output enable)**, R/W, reset 0. #90 write stores `nr_wr_dat(7 downto 2) & "00"`; #91, #92 store the byte; #93 stores bits 3:0. Reads return the stored value (#93 as `0000 & bits`). 1 = FPGA drives the pin, 0 = high-Z (input). When a peripheral (UART/I2C/SPI/I2S) is enabled it overrides the enable for its pins. Pin n = register `(n/8)` bit `(n%8)`. (zxnext.vhd:2275, 5536-5546, 6163-6174.)

**NR #98, #99, #9A, #9B (GPIO value)**, write: stored (reset values: #98=#FF, #99=#01, #9A=#00, #9B=#0 = `0x00001FF`; 5070-5073); #9B stores bits 3:0. **Read returns `i_GPIO` (the pin state seen by the FPGA), not the stored output latch** (6176-6189: `port_253b_dat <= i_GPIO(7 downto 0)` etc.). This is a hardware-observable difference: with the output enabled the read-back shows what is actually on the wire (the Pi's pull or drive wins if both drive), and without enabling it shows what the Pi drives. The wiki says only "read/write the pin state". [WIKI NextReg_$98.html]

**NR #A8 / #A9**: ESP GPIO0 output enable / value; **not Pi** (`o_ESP_GPIO_0*`, zxnext.vhd:1706-1707). #A9 read = `00000 & i_ESP_GPIO_20(2) & '0' & i_ESP_GPIO_20(0)`. Already modelled in `[UNG] core/src/emulator/io/z80n/nextboard.cpp:156-160`.

**NR #0B** (joystick I/O mode) can steal UART1: with `joy_iomode_uart_en` and `nr_0b_joy_iomode_0`=1, UART1 RX/CTS come from the right joystick port and `pi_uart_rtr_n`/`uart1_tx_pi` are forced idle (zxnext.vhd:3341-3350). Bit 0 of NR #0B selects which UART (0 = ESP, 1 = Pi) is redirected. [DIST registers.txt:221-231]

### 1.4 UART port block (already modelled in `[UNG] core/src/emulator/io/z80n/nextuart.*`)

- Shared ports #133B (Tx data/status), #143B (Rx data / prescaler), #153B (select bit 6: 0 = ESP, 1 = **Pi**; bit 4 = prescaler MSBs write enable), #163B (frame). 512 byte Rx FIFO, 64 byte Tx FIFO each. [DIST extra-hw/io-port-system/ports.txt:368-430; VHDL serial/uart.vhd]
- Baud: `prescaler = Fsys / baud` with Fsys from the video timing (28 MHz derived, NR #11); 115200 -> 243 in the 28 MHz case, 2 Mbaud -> ~14. PiTools computes it from a table `uart_clock[NR #11]` (`uartSetBaud.c`: write low 7 bits, then upper 7 bits with bit 7 set).
- Hardware flow control (frame bit 5) is honoured for the Pi: GPIO16/17 carry RTS/CTS (see table). The ESP ignores it on boards before issue 4. For Pi traffic the NextPi tools do not turn it on (they rely on 16 KB block ack/retry), but the Pi's PL011 supports it.
- Interrupts: im2 vectors 2 (UART1 Rx) and 13 (UART1 Tx), enables in NR #C6; `uart1_rx_near_full or (uart1_rx_avail and not nr_c6(1))`, `uart1_tx_empty` (zxnext.vhd:1941-1950). Already modelled.

### 1.5 I2S timing and format (audio/i2s.vhd, audio/i2s/*.vhd)

- The core is **slave only**: Pi supplies SCK (GPIO18) and WS (GPIO19); `o_i2s_sck/ws` are just re-synchronised copies. The header comment still mentions a "sample rate = 538461 / (divider+1)" for the removed master mode (default ~44871 Hz). [VHDL audio/i2s.vhd:1-45]
- LR width is 13 bits per channel (`LR_WIDTH => 13`), signed two's complement. Pi -> Next: the top 10 bits are used (`o_audio_pi_L <= (not a(12)) & a(11:3)`: converted to unsigned offset binary, 10 bits "same loudness as a single 8-bit DAC channel"). Next -> Pi: 13-bit unsigned (`i_audio_zxn_L(12:0)`, MSB inverted to signed) = the mixed PCM (`pcm_audio_L/R`).
- Pi audio is added into the output mix (`pi_i2s_L_i`, zxnext.vhd:6530-6531) and is readable as samples through NR #2C/#2D/#2E (`port_253b_dat <= pi_audio_L(9:2)`, `nr_2d_i2s_sample` = low 2 bits; 6007-6015), documented as "current I2S (pi) left/right sample" [DIST registers.txt:438-451]. These NRs are another Pi-facing read path.

---

## 2. Protocol over the UART

### 2.1 Layers

1. **Physical**: UART1 <-> Pi PL011 (`/dev/ttyAMA0`, miniuart is moved to Bluetooth by `dtoverlay=pi3-miniuart-bt`; `core_freq=400`, `enable_uart=1` in `/boot/config.txt`) [DIST rpi/linux/config.txt]. 8N1, 115200 baud by default; 2 Mbaud ("2Mbps comms between Next and Pi" in 1.92D+ per web search summary; the `.PIGET/.PIPUT` docs say "load the UART speed into NextReg 0x7F manually (2=115.2KBit, 8=2Mbit)") [DIST rpi/DotCommands/PIGET.md]. The wiki/docs write "115,000" meaning 115200 [DIST rpi/linux/Devices.txt, Supervisor.txt].
2. **Terminal**: the NextPi Supervisor ("SUP") is the getty on `/dev/ttyAMA0` (`/opt/nextpi/supervisor/supervisor`, "has replaced your default GETTY on /dev/ttyS0@115200 (ttyAMA0 on most SBCs)") [DIST rpi/linux/Developers.txt]. It is a bash running as root with a prebuilt PATH of `nextpi-*` commands. Serial in = STDIN, STDOUT = serial out, so a command's text output goes straight back to the Next. [DIST rpi/linux/Supervisor.txt]
3. **Application**: ASCII commands and binary payloads inside a command's I/O (NBN, base64).

### 2.2 Supervisor handshake (from the docs and the PiTools sources)

1. Select and enable: `NEXTREG #A0 <- #30; OUT #153B <- #40` (select the Pi UART). [WEB PiTools `piUartSwitch.c`]
2. Baud: `.pisend -q` probes the line at both speeds and stores the result in **NR #7F (User Register 0)**: 2 = 115200, 8 = 2 Mbaud. `.PIGET/.PIPUT/.PIVER` read NR #7F and call `uartSetBaud(115200 | 2000000)`; any other value -> exit code 20 "No SYS: Pi not found". [DIST rpi/DotCommands/*.md; WEB PiTools `piget.c`] The probe algorithm itself (`.pisend -q`) is not published in what I could reach; the dot command is binary only in `[DIST]`.
3. Drain the Rx FIFO (`uartDrain`: 100 scan-line waits, discarding bytes while `IUS_RX_AVAIL`), send `\n`, wait for the string `SUP>` (`uartWaitStr`, which skips CR (13) bytes). If the drain found junk, it sends `Ctrl-D` (4) and retries up to 2 times (`piSupReset`). [WEB PiTools `piSupReset.c`, `uartDrain.c`, `uartWaitStr.c`]
4. Send the command line + `\n`; the Pi echoes it (tty echo), so the client waits for its own text back (`uartSendCmd`: drain, send, wait for the same string). [WEB `uartSendCmd.c`]
5. A command is aborted with `^C` (0x03); the prompt is `SUP> ` (after ready), `>` in the prompt marks a read-only root, `#` a read-write one. [DIST Supervisor.txt]
6. Example from the docs:
   ```
   NEXT > PI  '\n'
   NEXT < PI  'SUP> '
   NEXT > PI  'nextpi-play_sid /PCache/tests/monty.sid\n'
   NEXT < PI  (any output of the CLI)
   NEXT > PI  '\03'
   NEXT < PI  (trailing bytes) 'SUP> '
   ```

The ordering matters for an emulated peer: boot takes about a minute under QEMU (and ~10-20 s on real hardware; "wait for it to come online and bomb after 10 seconds" [DIST extra-hw/rpi/PIreadme.txt]), so during boot the peer emits kernel/boot text which clients must drain; the sandbox's `run-cspect` explicitly discards boot output and waits for `SUP>` before launching so that `.pisend -q` finds the prompt [WEB nextpi-sandbox README].

### 2.3 NBN transfer (PIPUT / PIGET)

Wire format as implemented by `piput.c`/`piget.c`/`nbn*.c` in `[WEB] StalePixels/PiTools` (AGPL-3.0). The prose protocol document `StalePixels/NBNtools/PROTOCOL.md` is a stub ("TODO Will be done for V0.0.2"); the code is the spec.

- Upload (Next -> Pi): send `nextpi-file_receive -nbn 256\n`, wait for the `OK\r\n` (`uartWaitOK`: sees `K`,`\r`/`\n` after `O`; `RO` + CR LF = error 254). Then a header, big-endian: `u32 size`, `u32 blocks` (= size / 16384), `u16 remainder` (= size mod 16384), then the file name as ASCII and a `0` terminator. [`nbnSendHeader.c`]
- Then, for each full 16 KB block: payload bytes, then one checksum byte = 8-bit sum of the payload (`nbnSendBlock`), then the Pi answers one byte: `'!'` (0x21, success) or `'?'` (0x3F, fail). Up to 3 retries per block (`NBN_RETRIES`). Then the final partial block (`remainder` bytes) the same way.
- Download (Pi -> Next): send `nextpi-file_transmit -nbn\n`, wait `OK`, send `0`, path string, `0`. The Pi replies with the same header (size/blocks/remainder), size 0 meaning "not found" (client exit code 5). For each block the Next first sends a status byte (`'!'` or `'?'` for the previous block), the Pi sends `16384` payload bytes plus a checksum byte; the Next recomputes the sum and compares. [`nbnGetHeader.c`, `nbnReceiveBlock.c`, `piget.c`]
- Block constants: `NBN_MAX_BLOCKSIZE 16384`, `NBN_RETRIES 3`, `NBN_BLOCK_SUCCESS '!'`, `NBN_BLOCK_FAIL '?'` [`nbn.h`].
- Older `.PISEND` (dot command) uploads **base64** text: ".pisend will base64 encode and transmit... to / 49152 with 512 bytes"; `.pisend -c <cmd>` sends a raw command, `-r` flushes the Rx buffer, `-q` discovers the Pi and sets the baud, and sets register `f` (NR #7F) on exit. The Pi side decodes via `nextpi-file_stream` / `nextpi-file_receive` (base64). [DIST dotcommands/pisend.txt; WIKI Dot_pisend.html; DIST rpi/linux/CHANGELOG1.txt "nextpi-file_stream system method to base64 decode"]
- `.PIVER` does `nextpi-admin_version\n` and reads the reply up to CR (max 16 bytes), format `M.mmP` e.g. `1.93D`; it caches it in `C:/tmp/piver.cache`. A reply starting with `b` means "bash: command not found" -> error. [WEB `piver.c`; DIST rpi/DotCommands/PIVER.md]
- `.CRC32` is a Next-side Slicing-by-8 implementation for the Persistent Cache. [WEB PiTools README]

### 2.4 What else uses the link on the Next side

- **NextZXOS TZX/TAP loader** uploads the tape file with `.piput` (NextPi 1.91+) or `.pisend`, runs `nextpi-tzx_load` (tape2wav, libspectrum 1.5.0/1.6) on the Pi, and receives the audio as I2S -> NR #A2 bit 0 (EAR) with sample rate adjusted by `tape2wav`. [DIST nextzxos-changelog.txt:13-20 (v2.09) and more at 1002, 1081-1094; WIKI Pi_NextPi.html "TZX tape emulation up to x8"]
- **NextPi UI** (`NextPiUI`, `nextpi-ui_file`, `nextpi-ui_texteditor`, `nextpi-admin_uihelper`) - Next-side file browser over the same SUP link. [DIST CHANGELOG2.txt, WIKI Pi_CLI.html]
- **Terminex, NEXTCOM, NBNshell/NBNget** - serial terminal tools. [DIST CHANGELOG1.txt; WEB StalePixels repos]
- CSpect plugin author notes "the NextPi SUPervisor uses non-ASCII control bytes" and a "proposed NextPi GPIO protocol to control the SUPervisor and applications when the UART is otherwise engaged" (not implemented to my knowledge). [WEB/local `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/tools/Threetwosevensixseven-CSpectPlugins/README.md`]

### 2.5 The Pi side

- OS: NextPi = a customised **DietPi** (Raspbian 9 "stretch", armhf, ARMv6) on a read-only ext4 root, with writable FAT32 user partition `/NextPi`, persistent cache `/PCache`, volatile `/ram`. NextPi 1 = 1 GB SD card (0.99D/E for KS1), NextPi 2 = 16 GB card image (1.9x; public stable 1.92D, 1.93D images on `https://zx.xalior.com/NextPi2`, private testing 1.93A at the time of the wiki snapshot). [WIKI Pi_NextPi.html, Pi_1.92D.html, Pi_v0.99D.html; DIST rpi/linux/{Partitions,INSTALL.md,dietpi.txt,CHANGELOG1,2}.txt]
- Supervisor + `nextpi-*` CLI API (frontends in `/opt/nextpi/bin`, extension dirs searched in reverse order `/ram/nextpi`, `/mnt/dongle/nextpi`, `/PlusPack/*/nextpi`, `/PCache/nextpi/bin`, `/NextPi/nextpi`, `/opt/nextpi/bin`). Shell scripts around system tools (fbv, fbset, fluidsynth, sc68, csid, mpg123, espeak-ng, tape2wav, alsa, python2.7, sjasmplus, z88dk, pasmo...). Backends are partly proprietary binaries ("Binary: Proprietory"). Error format `ERROR:INT string`, codes 0-3. [DIST rpi/linux/CLI.txt, cli_error_codes.txt; WIKI Pi_CLI.html, Pi_gpio_mode.html]
- Source: GitHub `StalePixels` org (PiTools, NBNtools, sc68, csid-mod, pi-play_cassette, SPUN, WildFire). The main `NextPi` repo is referenced by `bugs.txt` ("https://github.com/StalePixels/NextPi/issues") but the API/pages return 404 from here. [DIST rpi/linux/bugs.txt; WEB `api.github.com/orgs/StalePixels/repos`]

---

## 3. What the Pi side implements today vs planned

Version snapshot: latest public stable **NextPi2 1.92D** on the wiki; **1.93D** in the distribution docs (CHANGELOG2 "1.93D") and as the jnext default release `1_93D`; PiTools pushed 2026-09-28, NBNtools 2026-10-04 (active). [WIKI Pi_NextPi.html; DIST CHANGELOG2.txt; WEB]

**Implemented (shipped, documented):**

| Area | Commands / features | Source |
|---|---|---|
| Supervisor | `SUP>` prompt, bash root, `^C`, tab completion, RO/RW prompt marker | Supervisor.txt |
| File transfer | `nextpi-file_receive` (Next->Pi, `-nbn`, base64), `nextpi-file_transmit` (Pi->Next, `-nbn`), `nextpi-file_stream`, `nextpi-file_delete`, `nextpi-file_rename`; Next side `.PIPUT`, `.PIGET`, `.PISEND` | CLI list, PIGET.md, PIPUT.md |
| Persistent cache | `nextpi-cache`, `nextpi-cache_bucket` (SPACE, PURGE, ADMIN_*), CRC32 | CHANGELOG2, Pi_CLI |
| Audio | `nextpi-play_mod|mp3|sid|sndh|midiLQ|MQ|HQ|speech|stock|silence|stop`, `nextpi-audio_loopback`; ALSA default card is an I2S "DAC" (`dtoverlay=specnext-dac`) | Pi_CLI, config.txt, Devices.txt |
| Tape | `nextpi-tzx_load` (TZX/TAP/PZX via libspectrum, accelerated up to x8 per the wiki) | Pi_NextPi |
| Video (Pi HDMI "debug video") | `nextpi-console_resolution` (fbset), `nextpi-console_image` (fbv), splash; 720p framebuffer `/dev/fb0` | 1.92D notes, Framebuffer.txt |
| Input | `nextpi-input_xbox` (USB Xbox pad via xboxdrv) | Pi_CLI |
| GPIO | `nextpi-gpio_mode -p pin -a alt -i/-o` (proprietary, listed prerequisite NextPi 1.93A; "Potential features: bank presets") | Pi_gpio_mode.html |
| Admin | `nextpi-admin_*` (enable/disable RW, version, report, update via `.PUP` with 512-byte header "PUP"/254/version 1, pupextract, uihelper, clean), `nextpi-plus_*` (PlusPack images), `nextpi-dongle_*`, `nextpi-echo_*`, `nextpi-z88dk_switch` | Pi_CLI, admin_pupextract.html |
| Dev | python 2.7, sjasmplus 1.20.x, multiple z88dk, pasmo, libusb1 | CHANGELOG2, Developers.txt |

**Missing / planned / not shipping:**

- Network is not supported as a Next network interface: "At this stage the Raspberry Pi cannot be used as a network interface for the Next"; Pi0W WiFi unsupported. [WIKI Pi_Main_Page.html]
- GPIO protocol for control while UART is busy is only "proposed" (CSpect plugins README); `nextpi-gpio_mode` future "bank presets". The custom GPIO protocols are left to developers. [WIKI Pi_Devices.html; WEB README]
- Bare-metal Pi kernels were prototyped but none reached parity; Linux is the only supported option. [WIKI Pi_Basic_Concepts.html]
- ToDo: error checking in CLI tools, disable network package updates, "disable linux trying to set volume on Next's DAC". [DIST rpi/linux/ToDo.txt]
- No hardware path for memory, direct device access, copper-style tricks: "no direct memory, no physical device access". [DIST Devices.txt]
- I2C-based plan in the old readme (command codes 11100001 keyboard, 11100100 ESP pipe, 11100110 bare-metal switch, 11100111 shutdown) was a design sketch and is "historical". [DIST extra-hw/rpi/PIreadme.txt]

**Consequence for an emulated peer.** Anything outside "Supervisor + NBN + base64 + audio over I2S" is optional. The commands used by NextZXOS itself (documented in the NextZXOS changelog and the PiTools sources) are only: `nextpi-admin_version`, `nextpi-file_receive -nbn 256`, `nextpi-file_transmit -nbn`, `nextpi-tzx_load` (via the TZX loader) and arbitrary `nextpi-*` via `.pisend -c`.

---

## 4. What the Next side must model

Already in unreal-ng (`[UNG] core/src/emulator/io/z80n/nextuart.{h,cpp}`, `nextboard.cpp`): the UART block, ports, FIFOs, interrupts, UART1 as `ISerialPeer` (`SetPeer(1, ...)`), `ComPort2=` spec kinds `Loopback|Plug|Tcp|Serial|Espnet|At|Modem|ZiFiNative` (`core/src/emulator/io/serial/comportspec.h:42-53`), NR #A0 readback mask 0x39, NR #A2 readback (`& 0xDD | 0x02`), NR #A8/#A9.

Gaps (what a Pi model needs):

1. **Gate UART1 by NR #A0 bit 4 (`pi_uart_en`).** With it clear, UART1 RX must see idle and TX must not be delivered (VHDL 2333). Today the peer is attached unconditionally (I did not find any reference to NR #A0 in `nextuart.cpp`). With `pi_uart_rxtx` (bit 5) = 0 ("hats" orientation) a real Pi wired as the Pi header would see its TX on the wrong pin: the pair does not communicate. A faithful model: connected only when bit 4 = 1 **and** bit 5 = 1 for a Pi peer; bit 5 = 0 for a hat peer. (Needs a decision, see Open questions.)
2. **Hardware flow control through `pi_uart_cts_n` / `pi_uart_rtr_n`** if the peer offers flow control: frame bit 5 + GPIO16/17. Not needed for NextPi tools.
3. **NR #0B joystick UART redirect** takes UART1 away from the Pi (zxnext.vhd:3341-3350); the existing UART code may or may not do this for UART0 only; check.
4. **NR #90-#93 / #98-#9B**: store with the masks above (#90 keeps bits 1:0 = 0; #93/#9B only 4 bits); reads of #98-#9B must return **pin state** (peer-driven inputs OR'd with own output where enabled; at least 0 for undriven/floating pins... the pin level of an undriven pin is board pull: not documented; use 0 or last driven). Reset values of #98-#9B: `#FF, #01, #00, #0`; of #90-#93, #A0, #A2: 0. These are reset by any reset (`reset='1'`), not only hard reset.
5. **GPIO bus object**: a 28-bit `i_GPIO` / `o_GPIO` / `o_GPIO_EN` pair so a Pi peer can drive pins, and UART/I2C/SPI/I2S overrides apply per the table in 1.2. A Pi peer that does GPIO protocols ("proposed NextPi GPIO protocol") would use this path.
6. **I2C mirror (NR #A0 bit 3)**: `pi_i2c1_sda/scl` ANDed into port #103B/#113B reads; if the peer pulls low it is visible; else `'1'`. jnext models this with `pi_i2c1_scl/sda` inputs gated by `pi_i2c1_en`. [EMU jnext/src/peripheral/i2c.h:135-215]. Check unreal-ng's RTC/I2C read does the same.
7. **SPI to the Pi (NR #A0 bit 0, port #E7 value #F7/#FB, MISO from GPIO9)**: the VHDL routes the Next's SPI master to GPIO7-11. Nobody ships software using it ("The documentation says: SPI port 0... I have not tested"; "Pi does not have a way to use a hardware slave" [DIST extra-hw/rpi/PIreadme.txt]). Low priority; a model only needs to select the right MISO source (the Pi's, default high) so SD card reads do not break when #E7 selects a Pi CS (SD ignores).
8. **I2S**: if a Pi peer is to deliver sound: NR #A2 enable/mute/EAR semantics, NR #2C-#2E sample reads, mixing into the PCM output, EAR threshold for TZX loading. The stream is slave-timed: the Pi emits SCK/WS (~44.1-48 kHz, 13 bit/channel frame per the core), so the emulator can simply push 10-bit sample pairs at the Pi's own rate into the mixer; there is no need to emulate SCK.
9. **Pi power/reset**: nothing on the Next side. A peer that is "booting" should simply be silent (or emit boot text) for a model-defined time; guest cold reset need not reset it (the real Pi is on 5 V and keeps running through a Next reset; jnext's PR notes "the Pi deliberately survives cold boots"). [WEB jorgegv/jnext#310]
10. **Timing**: byte-level via `ISerialPeer` at the UART's bit time (existing design). Baud 115200 / 2 Mbaud both appear.

---

## 5. Existing emulators

| Emulator | Pi treatment | Evidence |
|---|---|---|
| **CSpect** (closed source) | Emulates both UARTs internally; Pi UART via `-com2` (ESP via `-com`), restricted to ASCII 0x00-0x3F internally; the `UARTReplacement` plugin replaces them with binary buffered serial ports that follow baud writes (prescaler from video timing), drives DTR/RTS from NR #A8/#A9/#02, and notes Pi GPIO NRs #90-#9B for a "proposed NextPi GPIO protocol". `nextpi-sandbox/bin/run-cspect` bridges `-com2` to a QEMU NextPi via `socat` PTY. | `[tools/Threetwosevensixseven-CSpectPlugins/README.md]`, `[WEB vmorilla/nextpi-sandbox README]` |
| **jnext** (C++/Qt, local copy 810cfdff 2026-08-13) | Local copy: UART 1 is "Raspberry Pi" with no device (TX loops back into RX), NR #A0 stored but "Pi-GPIO mux + NR 0xA0 handler missing (G135)", I2C Pi bus 1 inputs implemented (`pi_i2c1_scl/sda`, gated by NR #A0 bit 3). **Upstream** (PR jorgegv/jnext#310, closed/merged): `--nextpi` runs the real NextPi image under `qemu-system-arm -M raspi0` with the PL011 on a UART/PTY wired to UART 1 ("jnext does not emulate the Pi"), config section `[nextpi]` (`enabled`, `dir`, `release` default `1_93D`, `qemu_binary`, `audio`); I2S audio goes to host speakers, not through the Next mixer. | `[EMU jnext/src/peripheral/uart_device.h]`, `[EMU jnext/doc/design/ESP01-EMULATOR-DESIGN.md:290-295]`, `[tests/jnext-vhdl-derived/test-plans/UART-I2C-TEST-PLAN-DESIGN.md:432-444, 616]`, `[WEB api.github.com/repos/jorgegv/jnext/pulls/310]` |
| **nextpi-sandbox** (vmorilla, third party) | Runs the real NextPi image under QEMU: `qemu-system-arm -M raspi0 -kernel boot/kernel.img -dtb boot/bcm2708-rpi-zero.dtb -drive file=overlay.qcow2,if=sd -append "console=ttyAMA0,115200 earlyprintk root=/dev/mmcblk0p2 rootfstype=ext4 rootwait fsck.repair=yes dwc_otg.fiq_enable=0 dwc_otg.fiq_fsm_enable=0 dwc_otg.nak_holdoff=0 snd_usb_audio.index=0" -audiodev ... -device usb-audio -serial tcp:127.0.0.1:5577,server=on,wait=off -display none`. 16 GB qcow2 overlay over a ~15 GB raw image (~6 GB archive, MD5-checked) from `https://zx.xalior.com/NextPi2/NextPi-<ver>.tar.gz`. Not emulated: network, I2S into Next mixer (NR #A2 has no effect), real-time speed. Boot to `SUP>` about 1 minute. | `[WEB raw.githubusercontent.com/vmorilla/nextpi-sandbox/master/{README.md,bin/run-nextpi,bin/setup-nextpi}]` |
| **ZEsarUX** (local 2d8dba1) | No Pi. A single host "UART bridge" (`uartbridge.c`, a real serial device such as `/dev/ttyUSB0`, meant for an ESP), mapped to ports #133B/#143B only; no #153B select, no NR #A0/#A2/#90-#9B, no I2S input. | `[EMU zesarux/src/uartbridge.c; src/machines/tbblue.h:112-119; operaciones.c:7347-7353, 9569]` |
| **MAME `specnext`** (local f43983b6) | Two `specnext_uart_device`s; UART1 is wired to an `rs232_port_device` tagged `"rs232_rpi"` with slot options `null_modem` and `pty` at 115200; NR #90-#93, #98-#9B, #A0, #A2, #A8, #A9 are stored; GPIO read-backs and I2S samples return 0 (`//i_GPIO(7 downto 0)` stubs). Pi NR #A0 gating of UART1 is not implemented. | `[EMU mame-next-device-sources/specnext.cpp:1886-1916, 2576-2609, 4200-4213, 1747-1755]` |
| **MiSTer Next core** | The real VHDL, GPIO unconnected on the DE10: same RTL (`zxnext.vhd` `i_GPIO`/`o_GPIO`), no Pi hardware; README repeats the Pi I2S bullet. | `[fpga/MiSTer-devel-ZXNext_MISTer/rtl/zxnext.vhd:235-240; README.md:69]` |
| **FPGA reference core** | Real hardware path as in section 1. | `[VHDL]` |

Summary: nobody emulates the Pi in-process; the working approach is a serial bridge to a real NextPi under QEMU.

---

## 6. Recommendation

### 6.1 Smallest useful emulated Pi peer

Two tiers:

**Tier A (cheap, in-process, no QEMU)**: an `ISerialPeer` "NextPi stub" attached to UART 1 that:

1. Is "booted" (silent, then prints `SUP> `) a configurable number of frames after emulator start; during boot Rx FIFO gets nothing (real hardware prints kernel text; not needed).
2. Implements the Supervisor loop: echo typed characters (tty echo), on `\n` print `\r\nSUP> ` for an empty line, `^C` (0x03) aborts, `^D` (0x04) resets to the prompt.
3. Command table: `nextpi-admin_version` -> `1.93D\r\n`; `nextpi-file_receive -nbn 256` -> `OK\r\n`, then NBN receive (header u32 size, u32 blocks, u16 rem, name\0; 16 KB blocks + additive checksum; reply `'!'`/`'?'`; store into a host directory); `nextpi-file_transmit -nbn` -> `OK\r\n`, read `\0path\0`, send header + blocks, honour `'!'`/`'?'`; `.pisend -c` free-form commands -> `ERROR` text.
4. Lets `.pisend -q`, `.PIVER`, `.PIPUT`, `.PIGET` succeed at both 115200 and 2 Mbaud. The `.pisend -q` baud probe's exact bytes are unknown (binary dot command, no source found), so the stub should answer the prompt at any baud (the peer does not care about rate; the UART timing is the Next's prescaler). Risk: `.pisend -q` may verify by trying several prescalers and watching for `SUP>`; a peer that answers at every baud would make it select the first one tried. If the probe tries 2 Mbaud first the guest would pick 8. Not harmful.

**Tier B (faithful)**: reuse the third-party path: `ComPort2=tcp:127.0.0.1:5577` (kind `Tcp` exists, `core/src/emulator/io/serial/comportspec.h`) against `nextpi-sandbox/bin/run-nextpi --tcp 5577`, i.e. real NextPi 1.93D under `qemu-system-arm -M raspi0`. This already works for CSpect and jnext and gives all `nextpi-*` commands, speech/MIDI/SID/TZX players (audio to host, not to the Next mixer). Cost: ~22 GB disk, 1 minute boot, `qemu-system-arm` dependency. An unreal-ng automation test needs a sentinel (`SUP>`), not wall-clock waits. Check whether the `Tcp` kind connects out (it must; QEMU `server=on,wait=off` listens).

### 6.2 NextREGs to route

Must: **NR #A0** (gate UART1 by bit 4, orientation bit 5; I2C bit 3 -> existing RTC I2C inputs; SPI bit 0 -> MISO select), **NR #90-#93 and #98-#9B** (28-bit GPIO model with `i_GPIO` read-back), **NR #A2** (enable/mute/EAR + a PCM mixer input; start with "off").
Nice: NR #2C/#2D/#2E I2S sample reads, I2S -> EAR (TZX loader), NR #0B joystick UART redirect for UART1.
Not Pi: NR #A8/#A9 (ESP GPIO0) stay as modelled. NR #A3 does not exist in the shipping core.

### 6.3 Verification targets

- **Real NextZXOS dot commands** from `[DIST]`/PiTools builds (`build/PIGET`, `build/PIPUT`, `build/PIVER` binaries exist in the PiTools repo under `build/`); NextZXOS `.pisend`.
- **FPGA-derived test plan** `[tests/jnext-vhdl-derived/test-plans/UART-I2C-TEST-PLAN-DESIGN.md]` Group 13 "NR 0xA0 Pi Peripheral Enable" (NR_A0-02/03) and DUAL-05 (UART1 = Pi), I2C-11 (Pi I2C1 AND gating): reusable test ideas.
- **MrKWatkins / Threetwosevensixseven ZX Next tests**: `tests/MrKWatkins-ZXSpectrumNextTests` (check `nextreg.txt` coverage for #90-#9B/#A0/#A2; I did not open these in this pass).
- NBN framing: simulate with the PiTools sources (C, z88dk) or `NBNtools/clients/common/nbn.c`, `uart.c`, and the NodeJS `server/` as a reference NBN-over-TCP server (a different, network-based NBN; the UART variant is the one above).
- Golden real-Pi capture: not available; a QEMU-run session can serve as the oracle (log the bytes with the existing UART trace).

### 6.4 Risks / unknowns

- `.pisend -q` baud discovery algorithm is not published; behaviour with a stub must be tested against the real dot command. `[DIST dotcommands/pisend.txt]` is only the help.
- The NextPi Supervisor and `nextpi-file_receive`/`transmit` sources are not available; the exact `OK` line and `-nbn 256` argument meaning are inferred from the Next-side clients. "256" is passed as an argument of `nextpi-file_receive -nbn`; its meaning is not documented (possibly a Pi-side ack timeout or block count). Test against real NextPi.
- NR #A0 bit 4/5 documentation conflict (use VHDL). Whether software ever writes only one of the two bits is unknown.
- What a real "undriven GPIO input" reads on NR #98-#9B (floating pins, Pi pull-ups/downs; PL011 pins at boot) is a board property, not in the FPGA. The Pi's pins after Linux boot have pull configurations set by the device tree; a model can only pick 0.
- QEMU `raspi0` is much slower than a real Pi Zero for heavy commands (MIDI, TTS); the 2 Mbaud rate stresses the UART model's Rx FIFO (512) because QEMU console data arrives in bursts. Flow control via GPIO16/17 is not used by NextPi tools, so overflow is possible (drop-newest per VHDL).
- Licensing: PiTools/NBNtools are AGPL-3.0, NextPi image redistribution rules: "Skip licenses" owner decision recorded in memory; downloading from `zx.xalior.com` at run time (not bundling) is what jnext does.
- Next-side software reads `NR #7F` as scratch between dot commands; the model must keep NR #7F user register behaviour (plain R/W) unchanged.

---

## Open questions

1. Should an unreal-ng Pi peer require NR #A0 bit 5 (RXTX) = 1 to talk (faithful to the "Pi" orientation), or only bit 4 (like a lenient model)? Real software always writes #30, so both pass; a hat peer would need bit 5 = 0.
2. Do we want the QEMU/`nextpi-sandbox` bridge (Tier B) as the shipped answer, or only a scripted stub (Tier A), or both selectable through `ComPort2=`? Tier B needs a spec kind that launches and supervises `qemu-system-arm` (jnext does) or documents a manual `run-nextpi --tcp`.
3. What exact bytes does `.pisend -q` send/expect to discover the Pi and its speed? Needs a UART trace of the real dot command against the sandbox (with the existing UART logger) to derive the stub's behaviour.
4. What is the argument `256` in `nextpi-file_receive -nbn 256`?
5. How is NR #98-#9B read-back defined for pins that are enabled as outputs while the peer also drives (contention)? The VHDL returns `i_GPIO`, i.e. the pad level; the model needs a drive-resolution rule (output wins? AND of both?).
6. Does the unreal-ng RTC/I2C implementation already AND in a Pi I2C1 input (NR #A0 bit 3, ports #103B/#113B)?
7. Should NR #02 bit 7 (bus reset) reset the Pi peer? Evidence says it does not reach the Pi header; confirm with the schematic in `[DIST schematics]` (not read in this pass).
8. I2S: is delivering Pi audio into the emulated mixer in scope (needs Next-rate resampling of the Pi sample stream and the mixer's 10-bit pi_audio inputs), or only EAR-thresholding for TZX?
9. Which `MrKWatkins-ZXSpectrumNextTests` cover NR #90-#9B/#A0/#A2 (not checked here)?
10. The `StalePixels/NextPi` repository: private or renamed? A source read of the Supervisor and `libnextpi` would remove the inferences in sections 2.2-2.3 and 6.4.

---

## Source list

Local:
- `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/fpga/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd` (lines 233-236, 394-454, 798-820, 1217-1245, 1709-1710, 1941-1950, 2268-2373, 3255-3350, 3381-3416, 5070-5085, 5536-5575, 6007-6015, 6163-6201, 6465-6531)
- `.../zxnext_top_issue5.vhd` (154-155, 801, 2293-2338), `.../audio/i2s.vhd`, `.../audio/i2s/`
- `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/documents/nextzxos-distribution-docs/rpi/linux/*.txt`, `rpi/DotCommands/{PIGET,PIPUT,PIVER}.md`, `dotcommands/pisend.txt`, `extra-hw/rpi/PIreadme.txt`, `extra-hw/io-port-system/{ports,registers}.txt`, `nextzxos-changelog.txt`
- `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/documents/specnext-wiki/Pi_*.html`, `NextReg_$A0.html`, `$A2`, `$A3`, `$90`, `$98`, `Dot_pisend.html`, `GPIO_Socket_(J15).html`
- `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/emulators/jnext/{src/peripheral/uart_device.h,i2c.h; doc/design/ESP01-EMULATOR-DESIGN.md; src/doc/developer-guide/03-subsystems/06-peripherals.md}`, `/emulators/mame-next-device-sources/specnext.cpp`, `/emulators/zesarux/src/{uartbridge.c,machines/tbblue.h,operaciones.c}`
- `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/tests/jnext-vhdl-derived/test-plans/UART-I2C-TEST-PLAN-DESIGN.md`
- `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/tools/Threetwosevensixseven-CSpectPlugins/README.md`
- `[UNG] core/src/emulator/io/z80n/{nextuart.h,nextuart.cpp,nextboard.cpp}`, `core/src/emulator/io/serial/comportspec.h`, `core/src/emulator/io/network/networkmanager.{h,cpp}`

Web (fetched 2026-10-12):
- https://github.com/vmorilla/nextpi-sandbox (README.md, bin/run-nextpi, bin/setup-nextpi)
- https://github.com/jorgegv/jnext/pull/310 (via api.github.com)
- https://github.com/StalePixels/PiTools (src/piget.c, piput.c, piver.c, lib/{piUartSwitch,piSupReset,uartSetBaud,uartSendCmd,uartWaitStr,uartWaitOK,uartDrain,nbnSendHeader,nbnGetHeader,nbnSendBlock,nbnReceiveBlock}.c, inc/nbn.h)
- https://github.com/StalePixels/NBNtools (PROTOCOL.md is a TODO stub)
- https://api.github.com/orgs/StalePixels/repos
