# Reference: ZiFi on TS-Conf (the TS-Labs AVR FIFOs, the ESP behind them, ZiFi32)

Research notes, 2026-10-02. Scope: what the Z80 sees at `#xxEF` on a ZX-Evo running the TS-Labs
FPGA configuration (TS-Conf) and the TS-Labs AVR firmware, what sits behind it (an ESP module),
which software uses it, what other emulators do, and how it fits the unreal-ng network model.
Sibling document: [reference-sprinter-wifi-driver.md](reference-sprinter-wifi-driver.md) (the
Sprinter ESP-AT kit and porting it to ZiFi; it is where the "old ZiFi / new ZiFi" remark comes
from). Earlier research this builds on, without repeating it:
[reference-esp-modules.md](../2026-09-30-nedoos-integration/reference-esp-modules.md) Part 2 §3-4
(the ZiFi API and the AT dialect) and
[reference-evo-com-port.md](../2026-09-30-nedoos-integration/reference-evo-com-port.md) §1, §3, §9
(port decoding, /WAIT timing, AVR firmware releases).

Facts carry a `file:line` citation. **[inferred]** marks a conclusion drawn from code, not
stated by a source.

### Path prefixes

Local mirrors are named by their folder in the emulator-sources collection; each one maps to the
upstream repository given here. Line numbers are for the commit named.

| Prefix | Local folder | Upstream (commit) |
|---|---|---|
| `TS-AVR/` | `github/zx-evo` | [tslabs/zx-evo `pentevo/avr/current/`](https://github.com/tslabs/zx-evo/tree/master/pentevo/avr/current) (`9ce7544a`, 2026-09-27) |
| `TS-FPGA/` | `github/zx-evo` | [tslabs/zx-evo `pentevo/fpga/current/`](https://github.com/tslabs/zx-evo/tree/master/pentevo/fpga/current) |
| `TS-ESP32/` | `github/zx-evo` | [tslabs/zx-evo `pentevo/esp32/`](https://github.com/tslabs/zx-evo/tree/master/pentevo/esp32) (ZiFi32 firmware) |
| `TS-SDK/` | `github/zx-evo` | [tslabs/zx-evo `pentevo/sdk/ft812sdk/`](https://github.com/tslabs/zx-evo/tree/master/pentevo/sdk/ft812sdk/lib/zifi) |
| `TS-DOC/` | `github/zx-evo` | [tslabs/zx-evo `pentevo/docs/`](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md) (also mirrored in [tslabs/zx-evo-docs](https://github.com/tslabs/zx-evo-docs/blob/main/ZiFi/zifi.md)) |
| `ZEU/` | `github/zx-evo-unreal` | [tslabs/zx-evo-unreal `Unreal/`](https://github.com/tslabs/zx-evo-unreal) (`86fd99b`, 2025-08-26) |
| `VBI/` | `github/ZX-Spectrum-Projects/ZiFi` | [HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi) via the fork [andrewinsidelazarev/ZiFi](https://github.com/andrewinsidelazarev/ZiFi) (`790451a`) |
| `S3Z/` | `github/ZX-Spectrum-Projects/ZiFi-ESP32-S3-Zero` | [andrewinsidelazarev/ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero) |
| `E01/` | `github/ZX-Spectrum-Projects/ZiFi-ESP-01S-Native-C-Project` | [andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project) |
| `MAME/` | `github/mame` | [mamedev/mame `src/mame/sinclair/evo/`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf_rs232.cpp) (`f43983b6`) |
| `ZSX/` | ZEsarUX checkout | [chernandezba/zesarux `src/machines/tsconf.c`](https://github.com/chernandezba/zesarux) (`main`; local copy is older, `src/tsconf.c`) |
| `KP/` | `github/karabas-pro` | [andykarpov/karabas-pro](https://github.com/andykarpov/karabas-pro) |
| `PS/` | `github/pico-spec` | [drewpo28/pico-spec `src/`](https://github.com/drewpo28/pico-spec/blob/main/src/ZiFi.cpp) |
| `NOS/` | `github/NedoOS`, `svn/nedoos` | [alfishe/NedoOS `src/`](https://github.com/alfishe/NedoOS/tree/main/src) |

---

## 1. Summary

1. **ZiFi is a function of the TS-Labs AVR firmware, not of a chip.** Since 2016-02-27
   ([d84c13a7](https://github.com/tslabs/zx-evo/commit/d84c13a7) "FPGA, AVR: added ZiFi version 1")
   the ATmega128 serves the whole `#xxEF` range as a /WAIT port. It runs **two** UARTs:
   - USART0 is the ZiFi line to the ESP module.
   - USART1 is the ordinary RS-232 (the COM header), which the same firmware also exposes as the
     Kondratyev 16550 at `#F8EF..#FFEF`.
   The ZiFi registers live at `#00EF..#CFEF`. The data register is shared between ZiFi and an
   "enhanced RS-232" view of the COM rings, and **which one it reaches is decided by the last
   FIFO-status register read** (`TS-AVR/rs232.c:172-206, 419-545`).
2. **The ZiFi API has only ever been version 1** (`ZF_VER 0x01`, `TS-AVR/rs232.h:146`): API mode 1
   is "transparent", a raw byte pipe at 115200 with **no flow control**. There is no AVR-side
   "ZiFi 2" protocol: every later change lives in the ESP firmware or moves the ESP off the AVR.
3. **Three generations sit behind the name** (§3):
   - **Old ZiFi (2016):** ESP-01 / ESP8266 with Espressif's **AT** firmware on the AVR UART0,
     driven by HackerVBI's `zifi.spg`.
   - **New ZiFi (2026, community):** the same AVR pipe and the same Z80 register code. An
     ESP-01S or an ESP32-S3-Zero on an ESP-01 adapter runs a **custom binary protocol**
     (`5A CMD LEN DATA XOR`), with a new `zifi.spg` and Wild Commander plugins.
   - **ZiFi32 (TS-Labs, 2024+):** an ESP32-S3 on the VDAC2 board's **SPI** bus (FPGA build
     `ESP32_SPI`, "VDAC3"), selected through the Z-Controller port `#77` bit 4. It has its own
     register/command protocol and nothing to do with `#xxEF`.

   The remark "the old one works as is, the new one needs substantial work on the host side and
   in the ESP32 firmware" fits generation 2 (see the sibling document) **[inferred]**. ZiFi32 is
   the other candidate.
4. **Fidelity details that matter for software** (all from the AVR source):
   - FIFO counts are capped at `#BF`, so `INIR` with `B = count` stays in the data area.
   - Every ZiFi register reads `#FF` while the API is off.
   - Overflow in either direction is silent.
   - A threshold/timeout interrupt reaches the Z80 as the TS-Conf "wait-port" INT (vector
     `#F9`, `INTMASK` bit 3).
   - The TS-Conf DMA can stream from a wait port (`DMA_WPD #25AF`, `DMA_WPA #2DAF`, device `7`).
   - The FPGA's 5-bit address packing makes `#D0..#EF` aliases of `#C0..#CF` and `#F0..#F7`
     aliases of the data area.
5. **Emulators:**
   - TS Unreal bridges ZiFi and the COM port to host COM ports, and separately emulates ZiFi32 as
     a fake ESP32 on SPI.
   - MAME has a minimal `tsconf_rs232` device with one serial line.
   - ZEsarUX has a stub.
   - pico-spec bridges to a real ESP.
   - Karabas Pro implements a ZiFi clone in VHDL.
   - None models the interrupt, the timeouts, the DMA path or the aliasing.
6. **For unreal-ng [inferred]:**
   - A `TsAvrSerial` device on `#xxEF` should own the existing `Uart16550` (TS 2016-04 flavor)
     **plus** a ZiFi API block with its own 511/255-byte rings.
   - It needs two peers: the ZiFi peer (`AtModule` for old ZiFi; a new `ZiFiNativeModule` for
     new ZiFi) and the COM peer.
   - The register-level journal stays as it is for the COM port. ZiFi32 would be a separate SPI
     device on the Z-Controller bus.

---

## 2. Hardware and firmware

### 2.1 The physical path

```text
Z80 --IN/OUT #xxEF--> FPGA (TS-Conf) --/WAIT + SPI slave--> ATmega128 (TS firmware)
                                                             |-- USART0 115200 --> ESP module (ZiFi)
                                                             '-- USART1 (16550 emul) --> COM header
```

- **The ZiFi board.** A small P-CAD board carries an ESP-01 footprint, a 5x2 / 4x2 header and,
  from 2016-07, a 3.5" floppy power connector. Its schematic part list names `ESP-01` and the
  net `UART0` (`github/zx-evo/pentevo/pcad/zifi/zifi.sch`;
  [pcad/zifi](https://github.com/tslabs/zx-evo/tree/master/pentevo/pcad/zifi); commits `6cc416d0`
  2016-02-17, `5dfe51d6` 2016-07-12).
- **Two UARTs.** The firmware opens USART0 for ZiFi and USART1 for RS-232, both at 115200
  (`TS-AVR/rs232.c:92-103`).
  - Frame format: `UCSR0C = USBS0 | UCSZ00 | UCSZ01`, meaning 8 data bits; per the source
    comment, 2 stop bits apply to TX only.
  - **The ZiFi UART never changes format or speed.** The divisor / LCR logic touches only UBRR1
    and UCSR1C (`:107-157`).
- **No flow control on ZiFi.** USART0 has no RTS/CTS handling at all. RTS/CTS exist only for
  USART1 (MCR bit 1 → `RS232RTS`, CTS polled into MSR; `:360-373, 692-710`).
- **Clocks.**
  - `F_CPU = 11059200` (`TS-AVR/default/Makefile:45`), so `UBRR115200 = 5`, which is exactly
    115200.
  - The 1 ms SysTick is Timer1 with `OCR1A = 11059` (`TS-AVR/main.c:348-350`). It increments
    the ZiFi and RS timeout counters, saturating at 255 (`TS-AVR/interrupts.c:224-238`).

### 2.2 Port decoding (FPGA → AVR)

- **Every access waits.** `COMPORT = 8'hEF`: any `IN`/`OUT` with low byte `#EF` latches
  `wait_addr <= a[15:8]` and the OUT data, and starts a wait (`TS-FPGA/z80/zports.v:288, 735-764`).
  `zwait.v` holds /WAIT low until the AVR ends the transfer (`TS-FPGA/z80/zwait.v:31-50`).
  **Every `#xxEF` access, ZiFi or 16550, stalls the Z80 on /WAIT.**
- **Packing.** The FPGA hands the AVR a status byte `{wr_n, addr5, status[1:0]}`. The 5-bit
  address code is:

  ```verilog
  wire [4:0] status_addr_com = ~&wait_addr[7:6] ? 5'h10 : {&wait_addr[7:4], wait_addr[3:0]};
  ```
  (`TS-FPGA/common/slavespi.v:96`)

  The AVR expands the code with `comport_addr_unpack_tab` (`TS-AVR/zx.c:963-969`):
  codes `00..0F` → `C0..CF`, `10..17` → `00` (data), `18..1F` → `F8..FF`.

  | Z80 high byte | Code | AVR register index | Meaning |
  |---|---|---|---|
  | `#00..#BF` | `10` | `#00` | ZiFi / enhanced RS-232 data register (DR) |
  | `#C0..#CF` | `00..0F` | `#C0..#CF` | ZiFi control registers |
  | `#D0..#DF`, `#E0..#EF` | `00..0F` | `#C0..#CF` | **aliases of `#C0..#CF`** [inferred from the formula: `&a[7:4]` is 0 for `D` and `E`] |
  | `#F0..#F7` | `10..17` | `#00` | **aliases of DR** [inferred, same formula and table] |
  | `#F8..#FF` | `18..1F` | `#F8..#FF` | Kondratyev 16550 |

  Note: [reference-evo-com-port.md](../2026-09-30-nedoos-integration/reference-evo-com-port.md)
  §1 says "#D0..#F7 map to 0x00". By the formula above only `#F0..#F7` do; `#D0..#EF` reach the
  control registers. The same document's §9.3 (BaseConf FPGA + TS firmware) is a different path
  and is not affected.
- **Data index.** The AVR sees **one index (`#00`) for the whole data area**. Software can
  therefore `INIR` from `#BFEF` downwards: B is decremented after each IN and the port's high
  byte runs `#BF, #BE, ... #01`, all of them DR.
- **Writes land after the release.** The TS wait service (`zx_wait_task`, `TS-AVR/zx.c:973-1049`)
  works in this order:
  1. Fetch the status with a CS toggle.
  2. Send `SPI_WAIT_DATA`.
  3. For a read, compute the data (`rs232_zx_read`).
  4. Exchange the data byte. The CS rising edge releases the Z80.
  5. For a write, call `rs232_zx_write` only then.
- **The wait is serviced often.** The TS main loop calls `waittask()` **after every task**
  (`TS-AVR/main.c:412-421`), not once per pass as BaseConf does. INT6 only sets
  `wait_irq_flag` (`TS-AVR/interrupts.c:217-222`).
  - **[inferred]** The wait is about 3 SPI bytes plus `rs232_zx_read` plus the remaining time
    of whichever task is running.
  - That puts it in the same order as the BaseConf figures (≈ 27-55 µs typical, with rare
    ≈ 0.1-0.45 ms outliers; reference-evo-com-port §3), with a shorter phase term. No measured
    TS number exists.
  - A 191-byte `INIR` then takes ≈ 5-8 ms, about 25-35 KB/s. That is faster than the 11.5 KB/s
    UART, so **the UART is the bottleneck, not the /WAIT** [inferred].

### 2.3 Register map (Z80 view, TS firmware)

Doc: `TS-DOC/ZiFi/zifi.md:5-19`. Behavior: `TS-AVR/rs232.c:172-545`, constants in `TS-AVR/rs232.h:82-146`.

| Port | R/W | Name | Behavior (AVR source) |
|---|---|---|---|
| `#00EF..#BFEF` (+ `#F0EF..#F7EF`) | R | DR | Selection decides the source:<br>- `select_zf=1` and API = 1: pop the ZiFi RX ring; empty → `#FF`.<br>- `select_zf=0` and API ≠ 0: pop the **RS-232 RX ring**, the same ring the 16550 RBR reads; empty → `#FF`.<br>- API = 0: `#FF` (`:428-462`). |
| same | W | DR | - `select_zf=1` and API = 1: push to the ZiFi TX ring; **dropped if full** (`:177-190`).<br>- `select_zf=0` and API ≠ 0: push to the RS-232 TX ring. When that becomes full it also clears 16550 LSR THRE/TEMT (`:192-205`).<br>- API = 0: dropped. |
| `#C0EF` | R | ZIFR | `min(zifi_rx_used, #BF)`; sets `select_zf=1` (`:469-475`). |
| `#C1EF` | R | ZOFR | `min(zifi_tx_free, #BF)`; sets `select_zf=1` (`:478-489`). |
| `#C2EF` | R | RIFR | `min(rs_rx_used, #BF)`; sets `select_zf=0` (`:513-519`). |
| `#C3EF` | R | ROFR | `min(rs_tx_free, #BF)`; sets `select_zf=0` (`:522-533`). |
| `#C4EF` | W | IMR | `int_mask |= value` (OR-in, one-shot bits) (`:268-270`). |
| `#C4EF` | R | ISR | Returns `int_src`, **then clears it** (`:492-495`). |
| `#C5EF` / `#C6EF` | RW | ZIBTR / ZITOR | Threshold in bytes (reset `#80`); timeout in ms (reset `#01`) (`:89-90, 273-280, 498-505`). |
| `#C7EF` | W | CR | Command (§2.4). |
| `#C7EF` | R | ER | Last result (`:508-510`). |
| `#C8EF` / `#C9EF` | RW | RIBTR / RITOR | The same pair for RS-232 (`:76-77, 283-290, 536-543`). |
| `#CAEF..#CFEF` | R | — | `#FF` (`:424-426`). |
| `#CAEF..#CFEF` | W | — | Ignored (they fall into the Kondratyev `switch`, which has no case for them, `:292-388`). |
| `#C0EF..#CFEF` | R, **API = 0** | — | `#FF` for everything, including ER. Reading ZIFR/ZOFR with the API off does **not** change `select_zf` (`:465`). |
| `#F8EF..#FFEF` | RW | 16550 | Kondratyev registers. Unaffected by the API, except that DR writes and reads share its rings (`:292-388, 547-607`). |

Reset values (`rs232_init`, `TS-AVR/rs232.c:55-104`): `select_zf=0`, `api=0`, `err=0`, `imr=isr=0`,
all rings empty, thresholds `#80`, timeouts `1`. `rs232_init` runs from `zx` initialization at
AVR start (`TS-AVR/zx.c:274`), not on a Z80 reset **[inferred: the same as for the 16550, see
reference-evo-com-port §9.2]**.

**Doc vs code:** `zifi.md:8-11` says the FIFO registers return "255 - full/empty". The firmware
caps at `#BF` (191), and so does ZEU (`ZEU/zf232.cpp:36-51` header, `:727-760` code). Software
written for the doc (Karabas Pro's VHDL clone returns up to 255, `KP/firmware/src/fpga/profi/rtl/uart/zifi.vhd:299-306`)
clamps itself before `INIR` (`S3Z/shared/z80/zifi_uart.asm:123-136`, comment `:117-122`).

### 2.4 Commands and errors (`#C7EF`)

`TS-AVR/rs232.c:211-265`; doc `TS-DOC/ZiFi/zifi.md:28-54`.

| Code | Name | Accepted when | Effect | ER after |
|---|---|---|---|---|
| `11110mmm` | SETAPI | always | `api = m`; `m > 1` → `api = 0` | `#00` |
| `11111111` | GETVER | API ≠ 0 | — | `#01` |
| `000000oi` | ZFCLRFIFO | API ≠ 0 | i: ZiFi RX ring empty; o: ZiFi TX ring empty (atomic) | unchanged |
| `000001oi` | RSCLRFIFO | API ≠ 0 | i / o: RS-232 rings empty. LSR is **not** updated, unlike FCR (`:244-263` vs `:330-353`) | unchanged |
| anything else | — | — | ignored | unchanged |

- The documented `0xFF REJ` response (`zifi.md:51-54`) is never produced by the code. A rejected
  command (the API off, or an unknown code) just leaves ER as it was. ER reads `#FF` while the
  API is off, which is what "REJ" amounts to in practice.
- `zifi.md:76-80` says "until initialization ... the only command recognized is Set API mode".
  The code agrees: the FT812 SDK's `zifi_init` sends `CLRFIFO` **before** `SETAPI`, and that
  command is silently lost (`TS-SDK/lib/zifi/zifi.c:8-16`).
- Clearing the ZiFi TX ring drops bytes not yet shifted out. A byte already in the USART data
  register still goes out **[inferred]**.

### 2.5 FIFOs

| Ring | Buffer (in `dbuf`) | Index | Usable | On overflow |
|---|---|---|---|---|
| ZiFi RX (ESP → Z80) | `dbuf+1024`, 512 B | 9-bit `zf_rx_hd/tl` | 511 | byte dropped, no flag (`TS-AVR/rs232_asm.S:101-143`) |
| ZiFi TX (Z80 → ESP) | `dbuf+256`, 256 B | 8-bit | 255 | Z80 write dropped (`TS-AVR/rs232.c:183-189`) |
| RS RX | `dbuf+512`, 512 B | 9-bit | 511 | dropped, LSR OE set (`rs232_asm.S:6-54`) |
| RS TX | `dbuf+0`, 256 B | 8-bit | 255 | dropped |

Layout: `TS-AVR/rs232.h:157-161`.

- Every received ZiFi byte resets `zf_tmo_cnt` to 0 (`rs232_asm.S:132-134`).
- TX drains by the UDRE interrupt (`:147-180`).
- **[inferred]** At 115200 8N1 an ESP can deliver 11 520 B/s. A Z80 that does not poll within
  about 44 ms of the ring starting to fill (511 B) loses data with no indication. Every protocol
  above therefore has its own end-to-end checks (AT `+IPD` lengths, native XOR/ACK).

### 2.6 Interrupts and the timeout

`rs232_task` runs once per AVR main-loop pass (`TS-AVR/rs232.c:614-664`).

- **IBT.** If an IMR bit is armed and the RX used count is ≥ IBTR, the bit is cleared from IMR
  and set in ISR.
- **ITO.** If armed, the timeout counter is ≥ ITOR **and** the ring is not empty, the bit moves
  from IMR to ISR. The counter counts ms since the last received byte.
- **The interrupt.** While `int_src ≠ 0` the AVR writes the FPGA config register with
  `MODE_TS_WTP_INT` set (`cb_zx_set_config`). The FPGA turns that into a one-clock strobe
  (`int_wtp = scs_n_01 && sel_cfg0 && shift_in[5]`, `TS-FPGA/common/slavespi.v:131`). It sets the
  TS-Conf **wait-port INT**:
  - vector `#F9`, `INTMASK` bit 3, lowest priority;
  - cleared by an acknowledge or by masking (`TS-FPGA/z80/zint.v:74-98, 130, 169-179`);
  - the bit is defined at `TS-AVR/main.h:166-167`.
- **[inferred]** Because the AVR re-pulses on every pass while ISR is non-zero, an ISR that does
  not read `#C4EF` is re-entered after its `EI` within one AVR loop pass (tens of µs).
- No ZiFi software found uses the interrupt (§4). Every client polls.

### 2.7 DMA from the wait port

TS-Conf's DMA has a device `0111` = "WTP → RAM", direction device-to-RAM only
(`TS-FPGA/common/dma.v:102, 123-125, 142, 437-439`).

- `DMA_WPD` (`#25AF`, bits 1:0) picks which wait device: `0` = Gluk, `1` = COM.
- `DMA_WPA` (`#2DAF`) loads `wait_addr` directly (`TS-FPGA/z80/zports.v:210, 217, 599-600, 758-759`).
- Each byte is an AVR wait cycle with `wr_n` forced to read (`TS-FPGA/z80/zwait.v:35-36`).
- With `WPD = 1`, `WPA = #00..#BF` it reads ZiFi DR, as long as ZIFR was read earlier to set
  `select_zf` **[inferred]**.
- The SDK has the constants (`TS_DMA_WTP_RAM 0x07`, `TS_WPD_COM 1`, `TS-SDK/lib/tsconf/ts.h:11-14`)
  and a **commented-out** `zifi_rd_fifo_dma` (`TS-SDK/lib/zifi/zifi.c:159-168`). No live user
  was found.
- In unreal-ng this device code currently "hangs" by design
  (`core/src/emulator/platforms/tsconf/tsconfdma.cpp:68-69`).

### 2.8 Interaction with the 16550 on the same range

- The enhanced RS-232 DR and the 16550 RBR/THR are **two views of the same two rings**. A byte
  read through one is gone from the other.
- `RIFR/ROFR` give FIFO-depth counts that the 16550 view lacks (only LSR DR/THRE).
- The AVR's own `printf` (debug) also writes the RS TX ring (`stdout_putchar`, `TS-AVR/rs232.c:159-168`).
- **On TS-Conf the board has both a ZiFi channel and a COM channel at the same time.** NedoOS-style
  16550 drivers at `#F8EF` keep working next to ZiFi.

---

## 3. Old vs new ZiFi

| Generation | ESP side | Link to Z80 | Z80 software | Status |
|---|---|---|---|---|
| **0. Cable** (2016) | none: PC socket server `ic.exe` (PSB) | 16550 at `#F8EF` or ZiFi DR, to a PC COM port | `zifi.spg` built with `cable_zifi=1` (`VBI/zifi.asm:47`) | historical |
| **1. Old ZiFi** (2016-04 → ) | ESP-01 / ESP8266 with Espressif **AT** firmware (0.60 / SDK 1.5.2 in the author's log) | AVR ZiFi API v1, USART0 115200, no flow control | HackerVBI `zifi.spg` v0.61 (2016-04-09, [hype](https://hype.retroscene.org/blog/dev/391.html)) to v0.733; Karabas net-tools (§4) | works with any AT ESP |
| **2. New ZiFi** (2026, A. Lazarev) | ESP-01S **native C++** (`E01/`, replaced an earlier MicroPython build) or **ESP32-S3-Zero** on an ESP-01 footprint adapter (`S3Z/`); custom **binary protocol** `5A CMD LEN_L LEN_H DATA XOR`, payload ≤ 1024 (`S3Z/docs/PROTOCOL.md:6-14`) | **unchanged**: the same AVR API v1, the same `#BFEF/#C0EF/#C1EF/#C7EF` (`S3Z/shared/z80/zifi_uart.asm:12-15`) | new `zifi.spg`, WC plugins ZIFIFTP / ZIFISMB / NTPTIME / WCUPDATE / WEATHER(2) / ZIFIUPD (`S3Z/README.md`) | active (s3-native-0.6.94, 2026-09-29) |
| **3. ZiFi32** (TS-Labs, 2024-07 → ) | ESP32-S3 module on the VDAC2 card ("VDAC3"), TS-Labs IDF firmware `TS-ESP32/src` (console prompt `zifi32`, `TS-ESP32/src/main/console.cpp:38`) | **SPI**: Z-Controller `#57` data, `#77` config bit 4 = ESP CS (`TS-FPGA/z80/zports.v:285-311, 663-712`), MISO muxed with the FT812 (`TS-FPGA/top.v:1174-1178`); FPGA define `ESP32_SPI` "requires IDE_VDAC2" (`TS-FPGA/quartus_vdac2/tune.v:12`) | FT812 SDK tests `utest_esp32`, `test_spi_esp32`; [AlexKorochinskiy/xmplayer](https://github.com/AlexKorochinskiy/xmplayer) WC plugin | active (2026-04 commits) |

What changed for software:

- **1 → 2:**
  - The register layer is identical, so a generation-1 binary still runs its port I/O. The ESP
    no longer speaks AT, however, so the old `zifi.spg` and any AT client fail against a
    generation-2 module (and vice versa).
  - The new firmware also **calls back into the Z80**: VFS requests `40..5E`, FTP events `60..68`.
    A Wild Commander plugin answers them while it waits (`S3Z/docs/PROTOCOL.md:310-320`;
    `E01/docs/PROTOCOL.md:40-45`).
  - Commands arriving while the ESP waits for a Z80 VFS reply are dropped. Clients retry after
    0.5 s with no ACK (`S3Z/docs/PROTOCOL.md:315-320`).
  - Flashing Espressif's stock ESP32-S3 AT firmware turns a generation-2 board back into an old
    ZiFi (sibling document).
- **3:**
  - A completely separate interface: a 64-byte register window (`COMMAND`, `STATUS`, params),
    with transactions `01` WR_REGS, `02` RD_REGS, `03` WR_DATA, `04` RD_DATA
    (`TS-ESP32/doc/esp32-spi-wifi.md:13-160`).
  - The SDK selects the ESP with `SPI_CTRL = 0x13` (`TS-SDK/lib/esp32/esp32.c:2-3`). The doc's
    `0x0B` is stale: bit 3 is the second SD CS (`TS-FPGA/z80/zports.v:304-310, 691`).
  - `#77` read bit 7 = `espcs_int`: the ESP pulled its CS while the bus was handed to it
    (`zports.v:461-462, 684-707`). With `D0 = 0`, a `#77` write hands the FT812/ESP SPI to the
    ESP (`esp_ft_spi_dis`, `top.v:496-500`).
  - The command set has grown far past the doc:
    - HTTP / HTTPS / Gopher streams `20..27`;
    - tracker and SFX players `A0..B6`;
    - **ELF loading and remote function calls `D0..D5`**;
    - object store `E0..E7` (`TS-ESP32/src/main/esp_spi_defs.h:87-174`).
  - Doc and code disagree on codes, for example `GET_INFO` `02` (doc) vs `GET_INFO_STR` `01` /
    `GET_VER` `02` (code), and `DATA_END D0` (doc) vs `LOAD_ELF D0` (code).
- **AVR side:** no change since 2018-04 (`eb65bafa` "added wait ports interrupt, updated ZiFi")
  apart from the 2021 wait-protocol rework (`21924ab1`). `ZF_VER` is still 1.

---

## 4. Software usage

| Program | Generation | Port access (quote) |
|---|---|---|
| HackerVBI `zifi.spg` | 1 (AT) | - Equates `zifi_command_reg equ #C7EF`, `zifi_data_reg equ #BFEF`, `zifi_input_fifo_status equ #C0EF`, `zifi_output_fifo_status equ #C1EF` (`VBI/zifi.asm:4480-4484`).<br>- Init: `ld bc,0xc7ef / ld de,0xfff1 / out (c),e ;Set API mode 1 / out (c),d ;Get Version / in a,(c) / cp 0xff / jp z,nozifi` (`:4509-4515`); `nozifi` prints "Error: Please update TS Conf." (`:4566-4571`).<br>- Bulk read: `ld bc,zifi_data_reg / inir` always 191 bytes, empty slots read `#FF` (`:817-825`).<br>- Send: `call check_output_fifo_status` (reads `#C1EF`, selecting ZiFi), clear TX, then `out (c),a` per byte with no further checks (`:880-896, 914-920`).<br>- Waits ≤ 256 frames for input (`:898-912`). |
| New `zifi.spg` and WC plugins (`S3Z/shared/z80/zifi_uart.asm`) | 2 (native) | - `ZiFi_Init`: `#F1`, `#FF` to `#C7EF`, poll ER ≠ `#FF` (`:19-44`).<br>- `ZiFi_GetChar`: `#C0EF`, then `#BFEF` (`:107-115`).<br>- `ZiFi_ReadBurst`: clamp to `#BF`, `ld c,#EF / inir` (`:123-144`).<br>- `ZiFi_PutChar`: polls `#C1EF` per byte, then `out` `#BFEF` (`:149-175`).<br>- The header explains why OUTI is avoided: a changing high byte "may hit other devices with incomplete decoding" (`:1-4`). |
| Karabas Pro net-tools (wget, browser, weather, alerts) | 1 (AT) | - `uartBegin`: `#F1`, `#03` (clear both), `#FF`, `in` `#C7EF` (`KP/software/profi/net-tools/src/pqdos/wget/zifi-uart.asm:17-32`).<br>- `uartWriteByte`: `ld bc, ZIFI_DATA_REG : out (c), a` **without** reading ZOFR (`:46-50`).<br>- **[inferred]** On the TS AVR, writes before the first ZIFR/ZOFR read go to the RS-232 ring, because `select_zf` resets to 0. Written for the Karabas VHDL clone, which has no selector. |
| FT812 SDK `zifi.lib` | 1 | - `zifi_rd` / `zifi_wr` use `ld b,reg / ld c,#EF / in/out (c)`; `zifi_rd_fifo` uses `inir` (`TS-SDK/lib/zifi/zifi.c:102-157`).<br>- The high-level calls (`zifi_connect_ap`, ...) are empty stubs returning true (`:42-99`). |
| FT812 SDK `esp32.c`, `utest_esp32`, `test_spi_esp32`; xmplayer | 3 (SPI) | `SPI_CTRL (#77) = 0x13`, `SPI_DATA (#57)` transactions (`TS-SDK/lib/esp32/esp32.c:1-95`). |
| NedoOS | none | - No ZiFi access anywhere in `NOS/` (no `#C0EF..#C7EF`).<br>- `kapps/zifi` is a catalog **application named ZiFi** that runs over ESPNET or AT on the 16550 / ATM2 ports (`NOS/kapps/zifi/main.c:1-14`, Makefile pulls `esp-com.c`, `espnet.c`).<br>- `espnet.asm` knows comTypes 0-3 only (`NOS/_sdk/espnet.asm:14-15`).<br>- There is no TS-Conf kernel build (`mkevo.bat` is `atm=1` BaseConf).<br>- On a ZX-Evo with the TS firmware, NedoOS uses `#F8EF` (comType 0/2), which the TS AVR still serves. |
| Moon Rabbit (MRZ/MRF) | none | No ZiFi build (reference-esp-modules Part 2 §1.0). |
| "uzifi" | — | Not found in any local tree or by search. |

---

## 5. Other emulators

| Emulator | ZiFi model | Notes |
|---|---|---|
| **TS Unreal** (`ZEU/zf232.cpp`, `zf232.h`) | ZiFi API 1 and Kondratyev 16550 bridged to **two host COM ports** (`[MISC] ZiFi=COMn`, `Modem=COMn`, `ZEU/cfg/Unreal.ini:73-76`) | - Rings of 1 KB (`zf232.h:4-15`).<br>- ZiFi COM forced to 115200 8N1, no flow control (`zf232.cpp:243`).<br>- Moves data once per frame (`mainloop.cpp:36-37`).<br>- Decodes any `p1==#EF` on **every** model while a port is open (`io.cpp:952-953, 1424-1425`); high byte used whole, so no `#D0..#F7` aliasing.<br>- Implements DR, ZIFR/ZOFR/RIFR/ROFR with the `#BF` cap and the selector, CR/ER (`zf232.cpp:582-783`).<br>- **No IMR/ISR/thresholds/timeouts, no WTP INT, no wait-port DMA.** |
| TS Unreal **ZiFi32** (`ZEU/zifi32/`) | ESP32 on SPI: `TS_ZIFI32=EMUL` fake device or `USB` (FT232H bridge to a real ESP32) (`cfg/Unreal.ini:25-29`) | - `#77` bit 4 = ESP CS, `#57` data, DMA hooks (`ZEU/zc.cpp:17-105`).<br>- The fake implements an old command subset (GET_INFO, WSCAN, AP_CONNECT, GET_IP, RND, TEST2/3, RESET) with a fixed AP and IP and **no sockets** (`ZEU/zifi32/esp32_emul.h:24-89`, `esp32_emul.cpp:91-260`). |
| **MAME** (`MAME/tsconf_rs232.cpp`) | `tsconf_rs232_device`: the AVR logic ported (API, selector, `#BF` cap, CR) on **one** MAME serial line shared by ZiFi and RS-232 (whichever `select_zf` picks), 115200 8N1 only while API ≠ 0 | - Map: `#00EF` mirror `#FF00` → DR, `#C0EF` select `#0F00` → regs; `#D0..#FF` unmapped, so no 16550 window (`MAME/tsconf.cpp:93-97, 301-306`).<br>- Stores IMR/thresholds but never raises an interrupt.<br>- Clearing RS TX also clears RS RX (bug) (`tsconf_rs232.cpp:286-296`).<br>- Rings 512/512. |
| **ZEsarUX** (`ZSX/src/machines/tsconf.c:3187-3255`) | Stub on top of its "uartbridge" | - Only the exact ports `#C7EF/#BFEF/#C0EF/#C1EF`.<br>- ZIFR returns 0/1 (data available), ZOFR always 1, ER always 0, CR ignored.<br>- Logs "Unemulated dma type ... 02H" for `zifi-2.spg` (`:642`). |
| **Xpeccy** | none | No `#xxEF` handling in `Xpeccy/src/libxpeccy/hardware/tslab.c`. |
| **pico-spec** (`PS/Ports.cpp:680-688`, `ZiFi.cpp`) | Bridge to a real ESP-01S | - `#00..#C7` → ZiFi, `#F8..#FF` → 16550.<br>- Known deviations: ZOFR = fill, ER constant `#10` (reference-esp-modules Part 2 §4). |
| **Karabas Pro** FPGA (`KP/.../uart/zifi.vhd`) | Hardware clone (no AVR) | - Exact ports `#BFEF`, `#C0EF`, `#C1EF`, `#C7EF` only (`:54-59`).<br>- Counts up to 255; extra commands `03` clear both, `04/05` UART select, `F0`/`F1` (`:272-306`). |

---

## 6. Proposed emulation (unreal-ng) [inferred]

### 6.1 Where it stands

- `PortDecoder_TSConf` reserves low byte `#EF` and reports `SerialPort::ZiFi`
  (`core/src/emulator/ports/models/portdecoder_tsconf.h:95-103`). `NetworkManager::MakePlan`
  notes "ZiFi is not emulated yet" (`core/src/emulator/io/network/networkmanager.cpp:67-68`).
  On TS-Conf, therefore, `#xxEF` currently reads `#FF` and even the TS firmware's 16550 is
  missing.
- `ComPort` already understands `ComPortRegister::kDataRegion` / `kZiFiRegister`: both read `#FF`,
  writes are dropped, and each access charges the AVR wait
  (`core/src/emulator/io/serial/uart16550.h:26-30`, `comport.cpp:226-245`). That covers "API off".
- `Uart16550` already has the `EvoAvr` flavor with the TS 2016-04 firmware variant (511/255
  rings, divisor 0 = 230400) (`uart16550.h:36-109`).
- The ESP side exists: `EspModule` (UART side, Wi-Fi on the virtual AP), `EspStack` (sockets),
  `AtModule`, `EspnetModule` (`core/src/emulator/io/serial/esp/`).
- The TS-Conf interrupt controller lists the wait-port source as "not emulated"
  (`platforms/tsconf/tsconfinterrupts.h:37`). The DMA treats device 7 as a hang
  (`tsconfdma.cpp:68-69`).
- `ZControllerSpi` already supports extra chip selects in slots 1..3 (FT812 on D2)
  (`core/src/emulator/io/spi/zcontrollerspi.h:21-31`). That is the ZiFi32 attachment point.

### 6.2 Model

1. **`TsAvrSerial`: one `PortDevice` on `#xxEF` for the TS firmware.** It is used for TS-Conf
   and also for BaseConf with the TS firmware (the `TS2016-04` preset).
   - **Decode** by the FPGA formula of §2.2. The 5-bit code mapping is a pure function, so the
     aliasing is exact.
   - **16550 part:** the existing `Uart16550` in `EvoAvr / Ts2016-04` flavor, attached to the
     **COM peer** (`[NETWORK] ComPort=`).
   - **ZiFi API block** (new, about 150 lines): `api`, `err`, `selectZf`, `imr`, `isr`,
     `zibtr/zitor/ribtr/ritor`, `zfTmoMs`, `rsTmoMs`, ZiFi RX ring 512 (511) and TX ring 256 (255).
     The register semantics are exactly §2.3-2.4. Copy the AVR, not the doc (cap `#BF`, no REJ).
   - The **enhanced RS-232 DR operates on the `Uart16550`'s own rings**. It needs accessors
     `PopRx` / `PushTx` / `RxUsed` / `TxFree` that bypass register side effects, except the
     THRE/TEMT clearing quoted in §2.3.
   - **ZiFi line to the peer:** a fixed 115200 8N1 (AVR TX 8N2) byte clock in emulated time,
     like the 16550's. It drains the TX ring at one byte per 11 bit times (8N2, open question 3) and accepts RX bytes no faster than the peer's 10-bit rate. There
     is no RTS: the peer is told `HonorsRts() == false`; overflow drops silently.
   - **Timing:** reuse `ComPort::AddAccessWait` for every `#xxEF` access, ZiFi included.
   - **Interrupt:** in the AVR "task" step (frame boundary or byte events), evaluate IBT/ITO as in
     §2.6. While `isr ≠ 0`, raise the TS-Conf wait-port INT (vector `#F9`, `INTMASK` bit 3)
     through `TsConfInterrupts`, re-arming after acknowledge as the AVR does.
   - **Wait-port DMA:** let `TsConfDma` device 7 call `TsAvrSerial::DmaRead(wpd, wpa)`, which does
     the same read as a Z80 `IN` with that high byte, and charge one AVR wait per byte.
     Low priority: no software uses it.
2. **ZiFi peers.** The ZiFi channel gets its own peer setting. **Suggestion:** `[NETWORK] ZiFi=`,
   with the same values as `ComPort=` plus the personalities:
   - `esp-at` (old ZiFi): the existing `AtModule` in the ESP8266 NonOS AT 1.x flavor. HackerVBI
     `zifi.spg` needs `ATE0`, `CWMODE_DEF`, `CWAUTOCONN`, `CIPMUX=0`, `CWJAP_CUR` with an extra
     blank line, `CIPSTART`/`CIPSEND`/`+IPD`/`CLOSED`, optionally `CIPRECVMODE=1` + `CIPRECVDATA`
     (reference-esp-modules Part 2 §1). "Works as is" for this generation means the existing AT
     personality plus the ZiFi pipe.
   - `zifi-native` (new ZiFi): a new `ZiFiNativeModule : EspModule`.
     - Frame parser `5A CMD LEN DATA XOR` and ACK `FE`.
     - `WIFI_INI` / `WIFI_CONNECT` onto the virtual AP.
     - `NET_OPEN/SEND/RECV/CLOSE`, `NET_HTTP_GET` (ESP-side HTTP client: header parsing, status
       code and Content-Length returned, body via `NET_RECV`, HTTPS optional), `NET_NTP`,
       `NET_IP_CONFIG`, `SYS_INFO`, `PING`, `ECHO`, `SYS_RESET`.
     - The FTP/SMB/WebDAV servers issue VFS requests **to the Z80** (the WC plugin is the file
       system), so the emulated ESP needs no host file system. It needs only listening sockets on
       the virtual network plus host port forwarding (TDD §5.3). This corrects the earlier
       "needs host filesystem access" note in reference-esp-modules Part 2 §4.
     - OTA (`UPDATE_*`, `ONLINE_UPDATE`, `WCU_*`) can answer "not supported".
   - `espnet` does not apply: NedoOS never uses ZiFi.
3. **ZiFi32 (generation 3), separately.** An `Esp32SpiDevice : SpiDevice` in a `ZControllerSpi`
   slot on config bit 4 (active high, as in the FPGA `~din[4]`), plus `#77` read bit 7 and the
   `D0 = 0` bus-handover bit.
   - The personality follows `TS-ESP32/src/main/esp_spi_defs.h`: registers, status machine,
     Wi-Fi and HTTP/stream commands on `EspStack`.
   - Out of reach: the tracker/SFX players, ELF loading (`D0..D5`, Xtensa code) and the
     ESP-driven FT812 path. Answer them with `ERROR INVALID_COMMAND`. Only worth doing once a
     concrete program needs it.
4. **TTD (NET-6).**
   - Journal ESP → AVR bytes with emulated timestamps, as the COM port journals peer bytes;
     the ESP is inert on replay (sealed replay).
   - State blob: the ZiFi API block (registers, both rings by value, both ms counters, the
     pending INT) next to `netstate::Com`. A new blob or a version bump of the COM blob,
     either way the TTD fixtures must be re-recorded.
   - The ZiFi32 SPI device journals its MISO bytes or the command results.
5. **Settings and automation.** On TS-Conf, the `MakePlan` note is replaced by a real plan:
   machine serial = ZiFi + COM. `ZiFi=` and `ComPort=` are independent.
   - Expose: ZiFi register state, ring fill, the selector, and the API mode in the network
     debugger (DBG-2 "later ZiFi").
   - Expose on every automation surface, per the automation-parity rule.

### 6.3 Tests to write

Unit (CUT on `TsAvrSerial`, no ROM):

1. **Decode.** Each high byte `#00..#FF` → expected AVR index (`#00`, `#C0..#CF` incl. the
   `#D0..#EF` aliases, `#00` for `#F0..#F7`, `#F8..#FF`).
2. **API off.**
   - Every `#00..#CF` register reads `#FF`; DR writes are dropped.
   - A ZIFR read does not change `select_zf`.
   - CLRFIFO before SETAPI is ignored (the `zifi.c:10` case).
3. **SETAPI/GETVER.**
   - `#F1` → ER `#00`; `#FF` → ER `#01`.
   - `#F2..#F7` → API 0.
   - An unknown command leaves ER unchanged.
4. **Selector.**
   - After reset, a DR write goes to the RS-232 TX ring, visible as a 16550 transmit.
   - After a `#C1EF` read it goes to the ZiFi peer.
   - A `#C2EF` read switches back.
5. **Counts.**
   - ZIFR/ZOFR/RIFR/ROFR capped at `#BF`.
   - ZOFR = 255 - used, empty = `#BF`.
   - `INIR` of B = count from `#BFEF` drains exactly count bytes.
   - An empty DR reads `#FF`.
6. **Overflow.**
   - The 512th RX byte is dropped with no flag.
   - The 256th TX byte is dropped.
   - An RS-side full TX clears LSR THRE/TEMT.
7. **Shared rings.** A byte received from the COM peer is readable once, via RBR **or** the
   enhanced DR.
8. **Interrupts.**
   - IMR bits are OR-in and one-shot.
   - IBT at the threshold; ITO after ZITOR ms of silence with a non-empty ring.
   - ISR clears on read.
   - The WTP INT asserts with vector `#F9`, only with `INTMASK` bit 3, and re-asserts until
     `#C4EF` is read.
9. **Line pacing.** 115200 8N1: 511 bytes from the peer arrive over ≈ 44.4 ms emulated time, not
   at once (the pico-spec lesson).
10. **Wait cost.** Every `#xxEF` access adds the AVR wait (shared with the COM tests).
11. **TTD.** Save → load mid-transfer gives a bit-exact continuation; the peer is inert on replay.

End-to-end (boot the TS-Conf ROM; slower, justified per the test guidelines):

12. **Old ZiFi.** HackerVBI `zifi.spg` with `zifi.ini` against `esp-at` on the virtual network
    with an internal HTTP service. Checks the init path, then one catalog page fetched
    (`+IPD`/`CLOSED` flow).
13. **Karabas net-tools wget** (if buildable for TS). Checks the selector gotcha above. Expected
    on real hardware: the first byte goes to RS-232 **[inferred]**. The test pins the AVR
    behavior, not the program's success.
14. **New ZiFi.** `S3Z` `zifi.spg` (`NET_HTTP_GET` + `NET_RECV`) and the `NTPTIME.WMF` plugin
    under Wild Commander against `zifi-native`.
15. **ZiFi32** (only if built). FT812 SDK `utest_esp32` against `Esp32SpiDevice`.

---

## 7. Sources

### Online (curl, browser User-Agent, 2026-10-02: HTTP 200 unless noted)

TS-Labs:

- AVR firmware:
  - [rs232.c](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232.c)
  - [rs232.h](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232.h)
  - [rs232_asm.S](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/rs232_asm.S)
  - [interrupts.c](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/interrupts.c)
  - [zx.h](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/zx.h)
  - `zx.c` and `main.c`: the GitHub blob pages returned 429 (rate limit), the raw files
    returned 200: [zx.c](https://raw.githubusercontent.com/tslabs/zx-evo/master/pentevo/avr/current/zx.c),
    [main.c](https://raw.githubusercontent.com/tslabs/zx-evo/master/pentevo/avr/current/main.c)
- FPGA:
  - [zports.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zports.v)
  - [zwait.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zwait.v)
  - [zint.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zint.v)
  - [slavespi.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/common/slavespi.v)
  - [dma.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/common/dma.v)
  - [top.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/top.v)
  - [quartus_vdac2/tune.v](https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/quartus_vdac2/tune.v)
- Docs and hardware:
  - [ZiFi/zifi.md](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md)
    (also [zx-evo-docs](https://github.com/tslabs/zx-evo-docs/blob/main/ZiFi/zifi.md))
  - [TSconf/tsconf_en.md](https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/TSconf/tsconf_en.md)
    (the wait-port INT and DMAWPD/DMAWPA sections are headings only)
  - [pcad/zifi](https://github.com/tslabs/zx-evo/tree/master/pentevo/pcad/zifi)
- ZiFi32:
  - [esp32/](https://github.com/tslabs/zx-evo/tree/master/pentevo/esp32)
  - [esp32-spi-wifi.md](https://github.com/tslabs/zx-evo/blob/master/pentevo/esp32/doc/esp32-spi-wifi.md)
  - [esp_spi_defs.h](https://github.com/tslabs/zx-evo/blob/master/pentevo/esp32/src/main/esp_spi_defs.h)
- SDK:
  - [lib/zifi/zifi.c](https://github.com/tslabs/zx-evo/blob/master/pentevo/sdk/ft812sdk/lib/zifi/zifi.c)
  - [lib/esp32/esp32.c](https://github.com/tslabs/zx-evo/blob/master/pentevo/sdk/ft812sdk/lib/esp32/esp32.c)
  - [lib/tsconf/ts.h](https://github.com/tslabs/zx-evo/blob/master/pentevo/sdk/ft812sdk/lib/tsconf/ts.h)
- TS Unreal:
  - [zf232.cpp](https://github.com/tslabs/zx-evo-unreal/blob/main/Unreal/zf232.cpp)
  - [io.cpp](https://github.com/tslabs/zx-evo-unreal/blob/main/Unreal/io.cpp)
  - [zc.cpp](https://github.com/tslabs/zx-evo-unreal/blob/main/Unreal/zc.cpp)
  - [zifi32/esp32_emul.cpp](https://github.com/tslabs/zx-evo-unreal/blob/main/Unreal/zifi32/esp32_emul.cpp)
  - The in-tree copy is [pentevo/unreal/Unreal/zf232.cpp](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/zf232.cpp).

ZiFi software:

- [HackerVBI/ZiFi](https://github.com/HackerVBI/ZiFi) ([zifi.asm](https://github.com/HackerVBI/ZiFi/blob/master/zifi.asm))
- fork [andrewinsidelazarev/ZiFi](https://github.com/andrewinsidelazarev/ZiFi)
- [ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero):
  [docs/PROTOCOL.md](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/docs/PROTOCOL.md),
  [shared/z80/zifi_uart.asm](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/shared/z80/zifi_uart.asm)
- [ZiFi-ESP-01S-Native-C-Project](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project):
  - [ZiFi/ORIGINAL_AT_COMMANDS.md](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/ZiFi/ORIGINAL_AT_COMMANDS.md)
  - `docs/PROTOCOL.md`: the blob page returned 429, the
    [raw file](https://raw.githubusercontent.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/main/docs/PROTOCOL.md) returned 200
- [AlexKorochinskiy/xmplayer](https://github.com/AlexKorochinskiy/xmplayer) (ZiFi32 WC plugin)
- Karabas Pro:
  [zifi.vhd](https://github.com/andykarpov/karabas-pro/blob/master/firmware/src/fpga/profi/rtl/uart/zifi.vhd),
  [net-tools wget zifi-uart.asm](https://github.com/andykarpov/karabas-pro/blob/master/software/profi/net-tools/src/pqdos/wget/zifi-uart.asm)
- NedoOS:
  [kapps/zifi/main.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/zifi/main.c),
  [_sdk/espnet.asm](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/espnet.asm)

Emulators:

- [MAME tsconf_rs232.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf_rs232.cpp),
  [tsconf.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf.cpp)
- [ZEsarUX](https://github.com/chernandezba/zesarux) (`src/machines/tsconf.c` on `main`: the
  blob page answered 429 on the second try, the content was read through raw.githubusercontent.com)
- [pico-spec ZiFi.cpp](https://github.com/drewpo28/pico-spec/blob/main/src/ZiFi.cpp),
  [Ports.cpp](https://github.com/drewpo28/pico-spec/blob/main/src/Ports.cpp)
- [Xpeccy](https://github.com/samstyle/Xpeccy)

History and forums:

- [ZiFi is released! (hype, 2016-04-09)](https://hype.retroscene.org/blog/dev/391.html)
- [ZiFi or the Internet for the Z80 (hype)](https://hype.retroscene.org/blog/dev/363.html)
- [zifi.vtrd.in](http://zifi.vtrd.in)
- [zx-pk.ru: "Internet for ZX Evo? Take it!"](https://zx-pk.ru/content/152-Internet-dlia-ZX-Evo-Beri!)
- [zx-pk.ru: Video DAC for ZX-Evolution under TS-Config](https://zx-pk.ru/threads/23627-videotsap-dlya-zx-evolution-pod-ts-config.html)
- [TS Forum t=654](https://forum.tslabs.info/viewtopic.php?f=31&t=654)

### Could not open

- `http://ts.retropc.ru/` (old ZiFi site): no response (curl code 000). The
  [web.archive.org copy](https://web.archive.org/web/2024/http://ts.retropc.ru/) returned 200.
- Repeated 429 rate limits on some github.com blob pages. Each was retried, or the raw file was
  fetched instead (noted above).
- No zx-pk.ru thread dedicated to "ZiFi" or "ZiFi32" turned up in search. The ZiFi32 discussion
  appears to live in the VDAC thread (linked above, not read in full).

### Local only

- `github/ZX-Spectrum-Projects/ZiFi/_esp/esp_init.txt`: an AT 0.60 terminal log.
- `ZiFi/_rs232/`: the cable-version socket server and the `zifi232` sources.

---

## 8. Open questions

1. **Which "new ZiFi"?**
   - Generation 2 (the ESP32-S3-Zero native protocol over the unchanged AVR pipe) is the
     likely meaning [inferred, as in the sibling document].
   - Generation 3 (TS-Labs ZiFi32 on SPI) also involves "ESP32 firmware" plus host-side work.
   - Recommendation: build the AVR ZiFi block plus `esp-at` first (it serves both old ZiFi and
     a Sprinter-kit port), then `zifi-native`; ZiFi32 only on demand.
2. **ESP reset line.** Does the ZiFi board wire the ESP-01 RST or CH_PD to anything the Z80 or
   the AVR can drive? The firmware has no such control, so the emulated module should power up
   with the machine and survive a Z80 reset. Needs the schematic, which is P-CAD binary and
   could not be read here.
3. **TX stop bits.** `USBS0` gives the AVR → ESP direction 2 stop bits (≈ 10.5 KB/s vs 11.5 KB/s
   RX). Confirm that the datasheet semantics apply (ATmega128 USBS = TX stop bits only). This
   affects only pacing.
4. **TS wait length.** No TS-specific measurement exists. Reuse the BaseConf model (§3 of
   reference-evo-com-port) or re-derive it from a TS `.lss`; none is in the tree.
5. **WTP INT re-trigger rate and the first-pass latency.** Derived from the code, not measured.
6. **AVR reset vs Z80 reset.** Is `rs232_init` re-run on a "soft reset" from the Evo reset
   button (the `FLAG_HARD_RESET` loop restart, `TS-AVR/main.c:433-435`)? If yes, the ZiFi API
   turns off on that reset.
7. **Karabas net-tools on TS-Conf.** Do they really misroute the first bytes (the selector), or
   do they read `#C0EF` first in a path not inspected?
8. **ZiFi32 doc drift.** The current firmware's command codes differ from the doc and from TS
   Unreal's fake. If ZiFi32 is wanted, which firmware version is the target?
