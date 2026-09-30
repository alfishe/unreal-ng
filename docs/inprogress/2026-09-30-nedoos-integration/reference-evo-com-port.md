# Reference: the ZX-Evo COM port and the ZX-WiFi UART

What the network TDD's COM port (step N2, [tdd-network.md](tdd-network.md) §7)
is built on: the Kondratyev 16550 register set as the ZX-Evo BaseConf AVR
firmware emulates it, the ZX-WiFi card's real 16550, and how NedoOS and other
software drive them. Research of 2026-09-30; every fact carries its source.

Paths are relative to the emulator source collection (a local archive of
emulator and firmware sources outside this repository); every root has a
public upstream, so a cited file is one click away. Abbreviations:

| Tag | Path | Upstream | What it is |
|---|---|---|---|
| `BC-AVR` | `svn/pentevo/avr/baseconf/trunk/src/` | [pentevo/avr/baseconf/trunk/src](https://github.com/alfishe/pentevo/tree/master/avr/baseconf/trunk/src) | Official BaseConf AVR firmware (svn r1325, 2026-01). **This is the one that pairs with the BaseConf FPGA.** |
| `BC-FPGA` | `svn/pentevo/fpga/baseconf/trunk/` | [pentevo/fpga/baseconf/trunk](https://github.com/alfishe/pentevo/tree/master/fpga/baseconf/trunk) | Official BaseConf FPGA |
| `TS-AVR` | `github/zx-evo/pentevo/avr/current/` | [tslabs/zx-evo: pentevo/avr/current](https://github.com/tslabs/zx-evo/tree/master/pentevo/avr/current) | TSLabs AVR firmware (TS-Conf, also boots Base/Egg). Has ZiFi + larger buffers. |
| `TS-AVR@167199ba` | `git -C github/zx-evo show 167199ba:pentevo/avr/current/zx.c` | [zx.c at 167199ba](https://github.com/tslabs/zx-evo/blob/167199ba/pentevo/avr/current/zx.c) | Last revision that still contains `zx_wait_task()`; HEAD (e9256bd0, 2026-05-19) dropped the body while `zx.h:231-232` and `main.c:73-86` still reference it |
| `TS-FPGA` | `github/zx-evo/pentevo/fpga/current/` | [tslabs/zx-evo: pentevo/fpga/current](https://github.com/tslabs/zx-evo/tree/master/pentevo/fpga/current) | TS-Conf FPGA |
| `NOS` | `svn/nedoos/nedoos/` | [NedoOS](https://github.com/alfishe/NedoOS) (`src/`, `release/`) | NedoOS |

The pentevo tree is the NedoPC Subversion repository `svn://svn.nedopc.com/pentevo`
(mirrored on GitHub above; line numbers are for r1325, the mirror can be newer).

Other repositories cited by path below:

| Path prefix | Upstream |
|---|---|
| `github/zx-evo-docs/` | [tslabs/zx-evo-docs](https://github.com/tslabs/zx-evo-docs) |
| `github/zx-evo-unreal/` | [tslabs/zx-evo-unreal](https://github.com/tslabs/zx-evo-unreal) (TSLabs Unreal: `Unreal/zf232.cpp`, `Unreal/io.cpp`, `Unreal/zifi32/`) |
| `github/unreal-speccy/` | [unreal-speccy](https://github.com/alfishe/unreal-speccy) (`modem.cpp`) |
| `github/ZXMAK2/` | [zxmak/ZXMAK2](https://github.com/zxmak/ZXMAK2) (`src/ZXMAK2.Hardware/General/HayesModem.cs`) |
| `github/pico-spec/` | [drewpo28/pico-spec](https://github.com/drewpo28/pico-spec) (`src/Ports.cpp`, `src/ZiFi.cpp`, `src/ZiFiAT.cpp`) |
| Xpeccy `src/libxpeccy/` | [samstyle/Xpeccy](https://github.com/samstyle/Xpeccy/tree/master/src/libxpeccy) |
| `svn/pentevo/tools/unreal_fix/0.39.0/Unreal_NS/` | [Unreal_NS in pentevo](https://github.com/aaydev/zxevo.pentevo/tree/main/tools/unreal_fix/0.39.0/Unreal_NS) |
| NedoOS ESPNET firmware (`espnet/`, `PROTOCOL.md`, `pins.h`, `host_uart.cpp`) | [NOS/src/kapps/common/espnet](https://github.com/alfishe/NedoOS/tree/main/src/kapps/common/espnet) |

Files are CP866/CP1251. Line numbers are for the file as it is on disk.

---

## 1. Port map and decoding

### BaseConf (the machine we emulate)

- The FPGA claims **every port whose low byte is #EF**: `localparam COMPORT = 8'hEF; // F8EF..FFEF - rs232 ports` (`BC-FPGA/z80/zports.v:204`); `comport_wr/rd = (loa==COMPORT) && port_wr/rd` (`zports.v:469-470`), `loa = a[7:0]` (`zports.v:290`). The port is in `porthit` unconditionally, so it is not gated by shadow/DOS (`zports.v:314`).
- **The register index is A10..A8 only:** `comport_addr <= a[10:8]` (`zports.v:718-722`, a 3-bit register passed to the AVR through SPI register #42, `slave/slavespi.v:166,201`). **A15..A11 are not decoded.** So #F8EF = #00EF = #C0EF = #80EF (register 0), and so on. Every #xxEF access stalls the Z80 through the AVR. [consequence: ZiFi probes at #C7EF write the SCR on BaseConf]
- The read data for any #xxEF read is `wait_read` (the byte that the AVR shifted in) (`zports.v:431-433`).
- AVR side: `rs232_zx_read/write(index 0..7)` switch on 0..7 (`BC-AVR/rs232.c:204-301`, `304-375`).

| A10..A8 | Canonical port | DLAB=0 read | DLAB=0 write | DLAB=1 |
|---|---|---|---|---|
| 0 | #F8EF | RBR (RX FIFO pop) | THR (TX FIFO push) | DLL r/w |
| 1 | #F9EF | IER | IER (`&0x0F`) | DLM r/w |
| 2 | #FAEF | IIR (constant 0x01) | FCR | same (FCR/IIR are not DLAB-dependent) |
| 3 | #FBEF | LCR | LCR | LCR |
| 4 | #FCEF | MCR (`&0x1F`) | MCR | MCR |
| 5 | #FDEF | LSR | ignored | LSR |
| 6 | #FEEF | MSR (clears the delta bits) | ignored | MSR |
| 7 | #FFEF | SCR | SCR | SCR |

(`BC-AVR/rs232.c:204-301` for writes, `304-375` for reads.)

### Other decodings, for comparison

| Implementation | Decoding |
|---|---|
| TS-Conf FPGA + TS-AVR | Full high byte: `wait_addr <= a[15:8]` (`TS-FPGA/z80/zports.v:755-756`). The FPGA packs it into a 5-bit status code: `00..0F -> C0..CF`, `10 -> 00..BF`, `18..1F -> F8..FF` (`TS-FPGA/common/slavespi.v:12-22,95-99`). The AVR expands it back (`TS-AVR@167199ba zx.c:712-719`, table `comport_addr_unpack_tab`). #F8..#FF is the 16550, #00..#BF is the ZiFi/enhanced data register, #C0..#CF are the ZiFi control registers (`TS-AVR/rs232.h:56-121`). **Note:** the ranges #D0..#F7 map to 0x00 (the "data" code) [inferred from the table]. |
| Original Kondratyev ISA COM1 card (ZXMC) | `xxxxA000xxx0xxxx`: A10..A8 = register, A4=0, **A11 = IRQ4 enable (A=0 IRQ on)**, so #F0EF..#F7EF are the IRQ-enabled aliases (`github/zx-evo-docs/ZX/zx-ports-full-table.txt:300-309`, ZXMC listing `:236,251`). |
| ZXMAK2 | Full 16-bit #F8EF..#FEEF (`github/ZXMAK2/src/ZXMAK2.Hardware/General/HayesModem.cs:68-74`) |
| Unreal (TSLabs) | any `p1==0xEF`; `p2>=0xF8` -> 16550 `&7`; `p2<=0xBF` ZiFi DR; C0..C7 ZiFi regs (`github/zx-evo-unreal/Unreal/io.cpp:952-953,1424-1425`, `zf232.cpp:452-457,651-656`) |
| pico-spec | `p8==0xEF`: hi<=#C7 -> ZiFi, hi>=#F8 -> 16550, `&7` (`github/pico-spec/src/Ports.cpp:680-688`) |

---

## 2. The AVR 16550 emulation (BaseConf firmware `BC-AVR/rs232.c`)

Clock: ATmega128 `F_CPU=11059200` (`BC-AVR/../build/Makefile:24`). Physical port: AVR USART1, pins TXD PD3, CTS input PB6, RTS output PD5 (`BC-AVR/pins.h:88-95,161-177`). **There is no DTR, DSR, DCD or RI pin.**

### Reset values (`rs232_init`, `rs232.c:94-119`)

| Reg | Value | Note |
|---|---|---|
| DLL/DLM | 01 / 00 | 115200 |
| IER | 00 | |
| IIR ("ISR") | 01 | never changes afterwards |
| FCR (internal) | 01 | cannot be read back |
| LCR | 00 | **but the USART is set up as 8 data bits + 2 stop bits** (`UCSR1C = USBS|UCSZ0|UCSZ1`, `rs232.c:103-104`; the comment says "1stop"). Real framing is 8N2 until the Z80 writes LCR. |
| MCR | 00 | the RTS pin sits high (`PORTD=0xFF` pull-ups, `DDRD` bit5 output, `main.c:79-80`) = RTS deasserted |
| LSR | 0x60 | THRE+TEMT |
| MSR | 0xA0 | DCD=1, DSR=1, RI=0, CTS=0 until the first `rs232_task` pass |
| SCR | 0xFF | |

`rs232_init()` runs only at AVR start / after a hard reset (`main.c:97-101`, the loop exits on `FLAG_HARD_RESET`, `main.c:270`; set by ATX power-off `atx.c:117` or Ctrl+Alt+Del `zx.c:383`). [inferred] A plain Z80 reset does **not** reset the UART state.

### FIFOs

- `FIFO_SIZE 16`, used as a mask on a ring buffer (`rs232.c:60-61,86-92`).
- **TX:** a write is accepted when `FO_end != FO_start || (LSR & THRE)` (`rs232.c:215-223`), so it holds 16 bytes. When the FIFO is full, the byte is **dropped silently** (`:224-227`). Every accepted write clears **both THRE and TEMT** (`:222`). The main-loop `rs232_task` moves one byte to `UDR1` when `UDRE` is set. THRE and TEMT are set together only when the ring is empty again (`:395-409`). So THRE means "TX FIFO completely empty". TEMT does not mean the shifter is empty.
- **RX:** polled in the main loop (no RX interrupt). `rs232_task` reads `UDR1` when `RXC` is set (`:411-437`). If the ring is full (`end==start && DR`), it sets **OE and drops the byte** (`:415-420`). Otherwise it stores the byte and sets DR. It sets **LSR bit 7 = "HF" (half full, custom Evo flag)** when there are 8 or more bytes, or the ring is full (`:430-435`, `#define LSR_HF 7` `:32-33`). HF clears when there are fewer than 8 bytes after a read (`:329-333`). The capacity is 16 bytes.
- RBR read with DR=0 returns **0x00** (`data=0` default, `:306,316-335`). An RBR read pops a byte and clears DR when the ring is empty.

### LSR bits

| Bit | Set by | Cleared by |
|---|---|---|
| 0 DR | RX byte stored | the last byte read; FCR RX reset |
| 1 OE | RX byte arrives while the ring is full (`:419`) | **only FCR bit1 (RX reset)** (`:249-255`). Reading LSR does NOT clear it (changelog "Fix RS232 (clear overrun error flag)" 2013, `main.h:26`) |
| 2 PE | a live mirror of `UCSR1A.UPE` every `rs232_task` pass (`:450-458`) | the same |
| 3 FE | a live mirror of `UCSR1A.FE` (`:440-448`) | the same |
| 4 BI | never | – |
| 5 THRE | TX ring empty | any THR write |
| 6 TEMT | the same as THRE (not the real shifter state) | any THR write |
| 7 HF | 8 or more bytes in RX (Evo-specific, **not 16550 "RX FIFO error"**) | fewer than 8 bytes / RX reset |

LSR writes are ignored (`:290-292`).

### MSR

- Only **CTS (bit4)** is live: `CTS pin low -> bit4=1` (`:460-487`). **DCTS (bit0)** is set on any change and is cleared together with all the low nibble on an MSR read (`rs232_MSR &= 0xF0`, `:365-370`).
- DSR (bit5) and DCD (bit7) are always 1, RI (bit6) is always 0 (`:115`). DDSR/TERI/DDCD are never set. MSR writes are ignored (`:294-296`).

### IER / IIR, interrupts

- IER is stored `& 0x0F` and can be read back (`:241,345`). It has **no effect**. IIR always reads **0x01** ("no interrupt pending"), and bits 7:6 = 00, so software that probes for "FIFOs enabled" sees none [inferred] (`:111,349-351`).
- **The COM port raises no Z80 interrupt on BaseConf.** [inferred from the absence of any INT path in rs232.c/zports.v]

### FCR

- Write (`:245-267`): nothing happens unless bit0=1. Bit1 empties the RX ring and clears OE, DR and HF. Bit2 empties the TX ring and sets THRE|TEMT. The value is stored `&0xC9` but **never read** (a read of index 2 returns IIR). The trigger level is ignored.

### LCR and line format

- It is stored whole and can be read back. `rs232_set_format` (`:171-192`): LCR[1:0] (WLS) -> UCSZ0/1, LCR[2] (STB) -> USBS (so 5 to 8 data bits and 1 or 2 stop bits are real). Parity: `PEN` alone = odd, `PEN|EPS` = even. **Stick parity is not supported** (it falls back to no parity). **Break (bit6) is ignored.** Any LCR write reprograms `UCSR1C`, including the DLAB-set write.
- DLAB (bit7) switches index 0/1 to DLL/DLM (`:207,232,310,339`).

### Baud rate (`rs232_set_baud`, `:141-169`, called after every DLL or DLM write)

- `DLM|DLL == 0` -> the code's "256000" mode: `UBRR = (691200/256000)-1 = 1` -> **the actual rate is 345600 baud** [computed: 11059200/(16*2)].
- `DLM bit7 = 1` -> "AVR mode": `UBRR = ((DLM&0x7F)<<8)|DLL` loaded directly (baud = 691200/(UBRR+1)). The changelog is at `main.h:47`.
- otherwise ("16550 mode"): `baud = 115200 / divisor` (integer), `UBRR = 691200/baud - 1`. So the **base clock is 1.8432 MHz**: divisor 1 = 115200 (UBRR 5, exact), 2 = 57600, 3 = 38400, 6 = 19200, 12 = 9600. Divisors that do not divide evenly are approximated through two integer divisions.

### MCR

- Stored `& 0x1F` (bits 7:5 read back as 0, **so AFE (bit5) cannot be set**) (`:274-288`).
- **Only bit1 (RTS) does anything:** bit1=1 drives the PD5 pin low ("clear RTS" in the code comment = the line is asserted through the inverting RS-232 driver [inferred]). Bit1=0 drives it high. DTR (bit0), OUT1/OUT2 and LOOP (bit4) are stored but do nothing. **There is no loopback.**
- **RTS is purely software-controlled. There is no auto-RTS, and TX ignores CTS** (the UDRE path never looks at the CTS pin, `:394-409`). Software has to poll MSR.CTS itself (NedoOS does this for type 0, see section 5).

### SCR

It is a plain read/write byte, reset value 0xFF (`:116,298-300,372-374`).

### Firmware differences in TS-AVR (not BaseConf, for reference)

(`TS-AVR/rs232.c`, `rs232.h`, `rs232_asm.S`)

- Uses indices #F8..#FF (full high byte, `rs232.h:56-78`).
- RX is driven by an ISR into a **512-byte ring (511 usable)**, and TX is ISR-driven from a **256-byte ring (255 usable)** (`rs232.h:158-161`, `rs232_asm.S:6-97`).
- THRE and TEMT are cleared only when the TX ring is **full** (`rs232.c:311-312`). The UDRE ISR sets THRE after each byte (`rs232_asm.S:89-91`). TEMT is a mirror of `UCSR1A.TXC1` (`rs232.c:685-688`). [inferred from the AVR datasheet] The firmware never clears TXC1, so TEMT stays 1 after the first byte and reads 0 after power-on until then.
- An RBR read with nothing available returns 0 (`:550,554-570`). OE is set by the RX ISR on overflow (`rs232_asm.S:39-42`).
- There is no HF bit. 0 divisor = 230400 (`rs232.c:127-133`).
- Adds the ZiFi API (#00..#BF data, #C0..#C9 regs, IMR/ISR interrupts through the "wait port interrupt" `MODE_TS_WTP_INT`, `rs232.c:614-664`). Documented in `github/zx-evo-docs/ZiFi/zifi.md:5-110`.
- Caveat: `zx_wait_task_old` (Base/Egg FPGA) reads the address from SPI #41 (`TS-AVR@167199ba zx.c:796-835`), while BaseConf FPGA puts the COM address in #42 (`BC-FPGA/slave/slavespi.v:200-201`). [inferred] The COM port under TS-AVR + BaseConf FPGA is broken or mis-addressed. The official BaseConf pairing is `BC-AVR`.

---

## 3. Timing: every access stalls the Z80 on /WAIT

### FPGA side (BaseConf)

1. `port_rd/port_wr` is a one-`zclk` strobe at the first Z80-clock rising edge that sees IORQ plus RD or WR (`BC-FPGA/z80/zports.v:340-355`). `wait_start_comport = comport_rd||comport_wr` (`zports.v:743`). `wait_write <= din` latches the OUT data (`zports.v:733-734`).
2. `zwait.v`: an RS flip-flop `waits[1]` is set on `posedge wait_start_comport` and cleared by `wait_end` (`BC-FPGA/z80/zwait.v:63-67`). `spiint_n = ~|waits`, and **`wait_n = spiint_n ? Z : 0`** (`zwait.v:77-79`). The same signal goes to the Z80 /WAIT and to the AVR INT6 (`top.v:875,929-935`).
3. `wait_end = sel_waitreg && scs_n_01`: the release comes when the AVR **deasserts SPI CS after a transfer to SPI register #40** (`slave/slavespi.v:199,269`). During that transfer the AVR shifts in the read data (`wait_reg`, `slavespi.v:222-224,268`) and reads back `wait_write` (`slavespi.v:164`). The status byte is `{wr_n, waits[6:0]}` (bit7=1 means read) (`top.v:752`).

### AVR side (BaseConf)

- ISR INT6 only sets `FLAG_SPI_INT` (`BC-AVR/interrupts.c:324-329`). The **main loop** polls it after running `tape_task, ps2mouse_task, ps2keyboard_task, zx_task, zx_mouse_task, joystick_task, rs232_task` (`BC-AVR/main.c:247-266`). The wait can also be serviced earlier from inside `zx_spi_send` when a status byte shows a pending wait (`zx.c:54-69,288-295`).
- `zx_wait_task` (`BC-AVR/zx.c:578-622`): 2-byte SPI #42 (fetch A10..A8) -> `rs232_zx_read` if it is a read -> 2-byte SPI #40 (data out/in; **CS rising edge releases the Z80**) -> `rs232_zx_write` if it is a write. **The write is applied after the Z80 is already released.**
- SPI clock: `SPCR=0x70` (enabled, master, LSB-first, fosc/4) with `SPI2X=1` -> 5.53 MHz (`BC-AVR/spi.c:7-11`), about 1.45 µs per byte.

### Estimated stall length [inferred, no measured figure found in the sources]

- The service path is about 5 SPI bytes plus C overhead, roughly **8-15 µs** once the AVR notices the request. Main-loop latency adds anywhere from about 0 to tens of µs (PS/2 and tape tasks; no bound is documented).
- At 3.5 MHz that is roughly 30-60+ T-states per access, and 4x more at 14 MHz turbo.
- Indirect real-world figure: MRF with `evo-uart.asm` reached **4.52 KB/s** at 115200 over the Evo COM port (`NOS/src/mrabbit-fusion/README.md:88-90`). That is about 220 µs per received byte, including the LSR poll, RBR read and RTS pulses.

### Fast path

- **None on BaseConf.** The FPGA caches no status register, and every #xxEF read (including LSR polling) goes through the AVR round trip (`zports.v:431-433`).
- TS-FPGA is the same, just with a full 8-bit address (`TS-FPGA/z80/zwait.v:29-49`, `zports.v:763-764`).

---

## 4. ZX-WiFi card (izzx) [scarce local sources]

- It is a **real 16550 on the ZX bus at #F8EF..#FFEF**, with an **ESP-12F (ESP8266)** module. The 16550 is clocked at **1.8432 MHz, so divisor 1 = 115200**. NedoOS uses **`comType` 2 (Kondratyev AFC)** for it (`NOS/src/kapps/common/espnet/PROTOCOL.md:21-34`).
- Board revision v1.6: flashed through X2 (3.3 V TTL) with SW1 = ESP, and jumpers X5/X6 are opened while programming (`PROTOCOL.md:32-34`).
- Wiring: ESP TX GPIO1 -> 16550 SIN, ESP RX GPIO3 <- 16550 SOUT, ESP CTS GPIO13 <- 16550 RTS, ESP RTS GPIO15 -> 16550 CTS (`PROTOCOL.md:21-26`; `espnet/host_uart.cpp:89-91,163`; `espnet/pins.h:52-72`).
- Auto flow control: the drivers write `MCR=0x2F` (AFE bit5 + RTS + DTR + OUT1/OUT2) with the comment "Enable AFE" (`NOS/src/moon-rabbit-zx/drivers/zx-wifi.asm:21-31`; `mrabbit-fusion/drivers/uart-zxwifi.asm:22-31`). NedoOS type 2 never touches RTS and skips the software CTS wait (`NOS/src/_sdk/espnet.asm:1618-1650,1801-1810`). This implies a 16C550-class chip with AFE (TL16C550C / ST16C550 type) [inferred].
- **Exact chip part number, IRQ wiring and address decoding width: NOT found in local sources.** The drivers address it both as `ld bc,#xxEF / in a,(c)` and as `ld a,#xx / in a,(#EF)` (`zx-wifi.asm:62-67`). Neither form tells us about partial decoding.
- MRF README: "ZX-WIFI: 115200,8N1" (no ESP flow-control flag listed) vs "EVO: 115200,8N1 FC (AT+UART=115200,8,1,0,3)" (`NOS/src/mrabbit-fusion/README.md:170-174`).

---

## 5. How software drives the port

### NedoOS configuration (`NOS/release/ini/espcom.ini:1-19`)

- Ports RBR_THR=#F8EF … SR=#FFEF, `divider = 1`, `comType = 0`.
- comType values: `0` Kondratyev without AFC 115200 (the Evo AVR port), `1` ATM2 COM 38400, `2` Kondratyev with AFC 115200 (ZX-WiFi / real 16550), `3` ATM2IOESP.
- **There is no auto-detection between Evo and ZX-WiFi.** The user picks `comType` (`esp-com.c:891-916` reads the ini; `_sdk/espnet.asm:14,85`: "Evo/Pentagon: Kondratyev 0/2").

### Init (identical for type 0 and type 2)

- In C: `uart_init`, `NOS/src/kapps/common/esp-com.c:181-193`. In asm: `esp_uart_init`, `NOS/src/_sdk/espnet.asm:1979-2015`.

```
FCR  #FAEF <- 0x87   ; enable FIFO, reset RX+TX, trigger 8
LCR  #FBEF <- 0x83   ; 8N1, DLAB=1
DLL  #F8EF <- divider (1)
DLM  #F9EF <- 0x00
LCR  #FBEF <- 0x03   ; 8N1, DLAB=0
IER  #F9EF <- 0x00
MCR  #FCEF <- 0x2F   ; "Enable AFE" -> on Evo becomes 0x0F: RTS asserted
```

- MRF drivers first write `MCR=0x0D` ("Assert RTS") (`NOS/src/mrabbit-fusion/drivers/uart-evo.asm:30-65`, comment "In Evo only bit #1 is used", "AFE ... Not implemented in EVO").
- **Bug in `moon-rabbit-zx/drivers/evo-uart.asm:18-25`:** the `outp` macro loads BC but then does `out (port),a` (OUT (n),A). So the init writes to ports #xxF8..#xxFF, **not** #xxEF. On Evo it effectively leaves the AVR defaults in place (115200, 8N2) [inferred].

### TX (types 0 and 2)

- Poll `LSR & 0x20` (THRE), then `OUT (#F8EF)` (`esp-com.c:64-74`; `espnet.asm:1725-1740`, bounded by `esp_factor`).
- Type 0 additionally polls **MSR bit4 (CTS)** before each byte (`esp_wait_cts`, `espnet.asm:1801-1830`). Type 2 skips this.

### RX, type 0 (Evo, no AFC)

- While `LSR.DR==0`, **pulse RTS: `MCR<-2; MCR<-0`**, then retry. When DR=1, read RBR. This runs under DI (`esp-com.c:311-326`, `getdataEsp 391-414`; `espnet.asm:1500-1545 esp_fill0`).
- Comment: "Hold-RTS (no off between polls) hung: on this path RTS is a byte strobe (FPGA/ATmega), not ESP CTS level" (`espnet.asm:1500-1504`).
- The ESP is configured with hardware flow control (`AT+UART=115200,8,1,0,3`, `mrabbit-fusion/README.md:172`), so it sends only while RTS is asserted. Each pulse lets through about 1+ bytes, which the 16-byte AVR FIFO absorbs.
- MRF variant: `MCR<-2`, spin on LSR until DR, then `MCR<-0` (`uart-evo.asm:85-101`). moon-rabbit variant: `MCR<-2`, a delay of about 250×(NOP,NOP,DEC,JR) (~4k T), then `MCR<-0` (`evo-uart.asm:63-76,94-106`).

### RX, type 2 (ZX-WiFi, AFC)

- Spin on `LSR.DR` and read RBR. RTS is never touched: the 16550 AFE drives RTS from its FIFO level (`esp-com.c:350-365`; `espnet.asm:1618-1650`).
- Comment: "ZX-WiFi fill0 is ~15% slower than fill2 but still far above Kondratyev type 0" (`espnet.asm:1503-1504`). So type 0 also works on ZX-WiFi.

### ESPNET protocol notes

- 115200 8N1 with hardware RTS/CTS required. The host copies each frame under DI because "If RTS is asserted ... and the task YIELDs, incoming UART bytes are lost" (`PROTOCOL.md:9,36-55`).
- A zero byte read from RBR counts as payload. Empty is detected by LSR.DR only (`PROTOCOL.md:56-60`).

### Other software

- `cuart` (NedoOS UART terminal, [NOS/src/kapps/cuart/main.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/cuart/main.c), the UART calls from [NOS/src/kapps/common/esp-com.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/esp-com.c)): reads `ini/espcom.ini`, `uart_init(divider)` as above, sends with `uart_write` (THRE poll, then THR), receives in `getdata()`: when LSR.DR is 0 it pulses RTS once (`uart_setrts(2)`: DI, `MCR 2`, `MCR 0`, EI), then reads RBR while DR is 1. After start it shows a hello box and waits for one key (`getchar()`). F1..F10 set divisors 1, 2, 3, 4, 6, 8, 12, 24, 48, 96; PgUp / PgDn page the log (the shipped `cuart.txt` still says PgUp sends `AT+GMR`); End toggles sending each key at once.
- No `zx-net-tools` sources were found in the local collection.
- ZiFi-API software (TS firmware only): `SETAPI 0xF1` then `GETVER 0xFF` on #C7EF, then `INIR` from #xxEF (`zx-evo-docs/ZiFi/zifi.md:81-110`).

---

## 6. Other emulators

| Emulator | Model | Notes / quirks |
|---|---|---|
| Unreal (TSLabs, `github/zx-evo-unreal/Unreal/zf232.cpp`) | Kondratyev regs `rs_reg[8]` plus ZiFi, backed by a host COM port. 1 KB rings (`zf232.h:4-15`) | Active only when a host COM port is open (`io.cpp:952,1424`), **for any machine model** (no model gate). **No WAIT/timing.** LSR |= 0x60 whenever the TX ring is not full (`:337-338,477-486`). LSR read clears bits 1-3 (`:679-682`). IIR is computed from LSR by `setup_int` (6/4/2/1), not a constant 0x01 like the AVR (`:427-448`). MCR is stored unmasked. AFE (bit5) is mapped to host RTS_CONTROL_HANDSHAKE, otherwise bits 0/1 drive host DTR/RTS (`:549-579`). MSR bit4 = host CTS, bit0 = delta, computed only on read (`:684-697`). FCR bits1/2 purge (`:496-512`). Divisor 0 = 230400, otherwise 115200/div (`:463-474`). SCR is a plain byte. |
| unreal-speccy (`github/unreal-speccy/modem.cpp`) | Same `ISA_MODEM` lineage (register logic `:165-348`) | No `modem.read/write` call site was found in this copy's `io.cpp` [possibly not wired]. |
| ZXMAK2 (`HayesModem.cs`) | "Hayes Modem connected using Kondratyev's scheme" (`:20`). Full-decode #F8EF,#F9EF(w),#FBEF(w),#FCEF(w),#FDEF(r),#FEEF(r) (`:68-74`) | **No FCR, IIR or SCR.** LSR = DR if host bytes are waiting, 0x60 if the host TX queue is empty (`:128-141`). MSR = 0x10 only when the host TX is empty (`:143-149`). MCR bit1 -> RTS (`:151-154`). RBR with nothing waiting returns 0 (`:104-112`). |
| pico-spec (`src/ZiFi.cpp:814-863`) | 16550 window bridged to an ESP | LSR = 0x60 always, plus DR. IIR = 0x01. MSR = 0x30 (CTS+DSR). FCR is ignored. RBR empty returns 0xFF (**the AVR returns 0x00**). MCR/SCR are stored unmasked. |
| Xpeccy (`src/libxpeccy/uart.c`) | Generic 8250/8251 | Used only for IBM PC and PC-98 (`hardware/ibm_pc_at.c:209-210`, `pc9801.c:447-451`). **No ZX #xxEF COM port.** |

**Consensus / disagreements:**

- **RBR read when empty:** BaseConf AVR, TS AVR and ZXMAK2 return 0x00. Unreal returns the last byte (`rs_reg[0]`). pico-spec returns 0xFF. Follow the AVR (0x00).
- **IIR:** the AVR always returns 0x01. Unreal computes it from LSR.
- **OE clear:** on the AVR, only an FCR RX reset clears it. Unreal/unreal-speccy clear it on an LSR read (standard 16550).
- **THRE semantics:** on the BaseConf AVR, THRE means "TX FIFO empty" (it clears on every write). On the TS AVR and in Unreal, it means "not full".
- **Decoding width:** no emulator models the BaseConf A10..A8-only decoding. All of them decode the high byte (≥#F8) or the full 16 bits.
- **AFE (MCR bit5):** masked off on Evo. Real on a 16C550-class ZX-WiFi. Honored by Unreal (host handshake).

---

## Emulation checklist distilled (BaseConf Evo) [inferred from the above]

1. Decode `(port & 0xFF) == 0xEF`, `reg = (port >> 8) & 7`, ignoring A15..A11.
2. Each access inserts /WAIT for the AVR service time. Use a configurable latency; about 10-20 µs is the service floor. On writes, apply the effect after the release.
3. FIFOs: 16-byte RX (OE plus drop on overflow) and 16-byte TX (silent drop when full).
4. LSR: bit7 HF at 8 or more bytes, THRE=TEMT="TX ring empty", PE/FE live. OE is sticky until FCR bit1.
5. MSR: 0xA0 | CTS(bit4) | DCTS(bit0), and the low nibble clears on read. IIR is always 0x01. IER is stored `&0x0F`. MCR is stored `&0x1F` and only bit1 drives RTS. There is no auto flow control, and TX ignores CTS.
6. Baud: 115200/divisor. 0 = 345600 (sic). DLM bit7 = raw UBRR at 11.0592 MHz/16. The line format comes from LCR (the power-on USART framing is 8N2).
7. Reset values come from `rs232.c:107-118` and apply only on an AVR hard reset.

---

## 7. How the model follows it

`core/src/emulator/io/serial/uart16550.{h,cpp}` (flavor `EvoAvr` for the
ZX-Evo, `ZxWifi` elsewhere), `comport.{h,cpp}` (ports, wait, reset rule):

| Fact | Model |
|---|---|
| Low byte #EF, register = A10..A8 | `ComPort` claims low byte #EF on every model; `reg = (port >> 8) & 7` |
| Every access waits for the AVR | `accessWaitT = 52` base T-states (~15 us, estimated: no measurement in the sources) |
| IIR #01, IER without effect | `interrupts = false`: IIR constant, IER stored `& #0F` |
| OE sticky until FCR RX reset, LSR bit 7 = RX half full | as the firmware |
| THRE = TEMT = TX FIFO empty; RBR empty = #00 | as the firmware (ZX-WiFi: TEMT waits for the shifter, RBR keeps the last byte) |
| MSR: CTS live + DCTS, DSR = DCD = 1 | as the firmware |
| MCR `& #1F`, only RTS; no loopback, TX ignores CTS | as the firmware (ZX-WiFi: AFE auto-RTS / auto-CTS, loopback) |
| Baud 115200 / divisor, 0 = 345600, DLM bit 7 = raw UBRR | `Uart16550::Baud()` computes it the AVR's way |
| 8N2 until the first LCR write | `FrameBits()` |
| A Z80 reset does not reset the UART | `ComPort::Reset()` leaves the Evo UART alone (ZX-WiFi: ZX-Bus /RESET resets it) |

Not modeled: PE / FE / BI (a virtual line has no line errors; a host serial
device's errors are not passed through yet), the write taking effect after
the wait ends (the Z80 cannot see the difference: it is held until then),
the AVR main-loop jitter (not deterministic on hardware either; the model
uses the fixed service time). The peer starts a byte only while RTS is asserted and
finishes it whatever RTS does afterwards, so the NedoOS type 0 RTS pulse
(`MCR 2`, `MCR 0`) gets one byte per pulse.

---

## 8. Real ESP modules and adapters (sources for the SERIAL: peer)

The emulated ESP modules (network TDD step N3) are the main path. A real
module on the host through `SERIAL:` stays supported for firmware work
(the ESPNET firmware itself), real modems, a null-modem link to another
machine, and checking the emulation against hardware. **Testing with real
devices is postponed** until the hardware is on the desk, together with the
other real-device bridges (Greaseweazle / KryoFlux: PLAN #12).

### Boards and wiring

| What | Source | Notes |
|---|---|---|
| ZX-WiFi (16550 + ESP-12F on the ZX-Bus) pinout, flashing jumpers | [PROTOCOL.md](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/PROTOCOL.md) ("Pins"), [pins.h](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/pins.h), [host_uart.cpp](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/host_uart.cpp) | ESP TX GPIO1 -> 16550 SIN, RX GPIO3 <- SOUT, CTS GPIO13 <- 16550 RTS, RTS GPIO15 -> 16550 CTS; v1.6: flash via X2 (3.3 V TTL) with SW1 = ESP, X5 / X6 open while programming. No published schematic found; the community thread is [zx-pk.ru: NedoOS](https://zx-pk.ru/threads/30190-nedoos.html) (ESP on the ATM / Evo COM port, 16550 + ESP above 38400 baud) |
| ESPNET-capable boards: ESP32 WROOM, ESP32-C3 Super Mini, ESP8266 D1 mini | [PROTOCOL.md](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/PROTOCOL.md) "Pins" table, [README.txt](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/README.txt) (Arduino IDE setup, board packages) | UART pins per board; ESP8266 D1 mini uses the swapped UART (GPIO15 / 13) |
| USB-serial adapters and the auto-reset circuit (why RTS / DTR must not follow the ZX by default) | [esptool: Boot Mode Selection (ESP8266)](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html), [ESP32](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html); [Espressif's Automatic Reset](https://qsantos.fr/2025/05/09/espressifs-automatic-reset/) | DTR / RTS of the FTDI / CP210x / CH340 drive GPIO0 and EN through two cross-coupled transistors: DTR 1 RTS 0 holds the chip in reset, DTR 0 RTS 1 enters the bootloader. `ComModemLines=0` (the default) keeps the emulator off these lines |
| CH340 adapter with auto-reset, schematic | [USB-C-CH340K-Auto-Reset-Programmer](https://github.com/mariusmym/USB-C-CH340K-Auto-Reset-Programmer), [CH340C programmer with auto-reset (PCBWay)](https://www.pcbway.com/project/shareproject/CH340C_with_Auto_Reset_104b447f.html), [ESP-01 on a CH340 dongle](https://cmheong.blogspot.com/2018/05/using-ch340-usb-dongle-as-esp-01s.html) | the ESP-01 "USB programmer" boards; many need a mod to program, none to run |

### Firmware

| Firmware | Source | Used by |
|---|---|---|
| ESPNET 1.2x (binary socket protocol, frames `#A5`, 8 sockets on ESP32, 4 on ESP8266) | [NOS/src/kapps/common/espnet](https://github.com/alfishe/NedoOS/tree/main/src/kapps/common/espnet) ([espnet.ino](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/espnet.ino), [protocol.h](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/protocol.h), [PROTOCOL.md](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/PROTOCOL.md), [README.txt](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet/README.txt)) | NedoOS `sd_bootesp.$C`, [_sdk/espnet.asm](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/espnet.asm) (network TDD N3 `EspnetModule`) |
| Espressif ESP-AT (current AT firmware, all chips) | [espressif/esp-at](https://github.com/espressif/esp-at/releases) | NedoOS ESPCOM apps, Moon Rabbit, `cuart` (N3 `AtModule`) |
| Espressif NonOS AT 1.7.x (legacy ESP8266 AT, what most ZX software was written against) | [ESP8266_NONOS_SDK releases](https://github.com/espressif/ESP8266_NONOS_SDK/releases), [ESP8266 AT Instruction Set (PDF)](https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf); flashing notes: [AT firmware on an ESP-01S](https://www.sigmdel.ca/michel/ha/esp8266/ESP01_AT_Firmware_en.html) | the same software; `AT+GMR` answers `AT version:1.7.x` |
| ZiFi (TS-Conf AVR API over the same ports) | [ZiFi/zifi.md](https://github.com/tslabs/zx-evo-docs/blob/main/ZiFi/zifi.md) | TS-Conf software (network TDD N5) |

### Emulations to learn from (N3)

| Emulator | Source | What it has |
|---|---|---|
| TSLabs Unreal | [Unreal/zifi32](https://github.com/tslabs/zx-evo-unreal/tree/main/Unreal/zifi32) ([esp32_emul.cpp](https://github.com/tslabs/zx-evo-unreal/blob/main/Unreal/zifi32/esp32_emul.cpp), [zifi32.cpp](https://github.com/tslabs/zx-evo-unreal/blob/main/Unreal/zifi32/zifi32.cpp)) | an ESP32 emulation behind ZiFi |
| pico-spec | [ZiFiAT.cpp](https://github.com/drewpo28/pico-spec/blob/main/src/ZiFiAT.cpp), [ZiFiSock.cpp](https://github.com/drewpo28/pico-spec/blob/main/src/ZiFiSock.cpp) | an AT command emulation over sockets |
| Unreal_NS | [SRC/modem.h](https://github.com/aaydev/zxevo.pentevo/blob/main/tools/unreal_fix/0.39.0/Unreal_NS/SRC/modem.h), [SRC/config.cpp](https://github.com/aaydev/zxevo.pentevo/blob/main/tools/unreal_fix/0.39.0/Unreal_NS/SRC/config.cpp) (`[MISC] Modem=COMn`) | host COM port passthrough only, no ESP emulation: the reason real ESPs on USB were used with it |

