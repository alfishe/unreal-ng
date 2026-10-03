# ZiFi: technical design

Research: [reference-zifi.md](reference-zifi.md) (sections cited as §). Network stack:
[tdd-network.md](../2026-09-30-nedoos-integration/tdd-network.md).

## 1. What the Z80 sees

`#xxEF` on TS-Conf: every access holds the Z80 on /WAIT while the AVR serves it (§2.2). The FPGA packs
the high byte into a 5-bit code the AVR expands:

| High byte | AVR index | Device |
|:--|:--|:--|
| `#00..#BF`, `#F0..#F7` | `#00` | the data register (DR): ZiFi or "enhanced RS-232", by the selector |
| `#C0..#CF`, `#D0..#EF` (aliases) | `#C0..#CF` | ZiFi registers |
| `#F8..#FF` | `#F8..#FF` | the 16550 emulation (the existing `Uart16550`, flavor `EvoAvr`, firmware `TS2016-04`) |

ZiFi registers (§2.3, §2.4): ZIFR / ZOFR / RIFR / ROFR (counts capped at `#BF`, the read sets the
selector), IMR / ISR at `#C4`, the thresholds and timeouts `#C5`, `#C6`, `#C8`, `#C9`, the command /
result register `#C7` (SETAPI, GETVER, CLRFIFO for both channels). With the API off every ZiFi
register and DR read `#FF` and DR writes are dropped. The code, not the document, is the reference
(cap `#BF`, no REJ).

## 2. Model

| Part | Where | What |
|:--|:--|:--|
| `ZiFi` | `core/src/emulator/io/network/zifi.{h,cpp}` | the API block: `api`, `err`, `selectZf`, `imr`, `isr`, `zibtr`, `zitor`, `ribtr`, `ritor`, the last-byte times; `Read(index)` / `Write(index)`; `Task(now)` evaluates IBT / ITO and raises the interrupt |
| ZiFi line | a `ComPort` inside `ZiFi`, never on ports | the AVR's USART0: `Uart16550` (`EvoAvr`, TS2016-04 rings 511 / 255), 115200, TX 8N2, RX 8N1 (the ESP's frames), RTS always asserted (no flow control), overflow silent; its peer from `[NETWORK] ZiFi=` |
| The `#xxEF` device | `ComPort` (the COM port, peer `ComPort=`) | `RegisterOf` returns `0..7` (16550), `kDataRegion`, or `kZiFiBase + n` (register `#C0 + n`); data and ZiFi registers go to the attached `ZiFi` |
| Enhanced RS-232 DR | `Uart16550::DataRead` / `DataWrite`, `ClearRxRing` / `ClearTxRing` | the same rings as RBR / THR; empty reads `#FF`; RSCLRFIFO leaves LSR alone |
| Interrupt | `NetworkCapabilities::waitPortInterrupt` (TS-Conf: `TsConfInterrupts::RaiseWaitPort`) | while ISR is not zero the AVR pulses the wait-port INT (vector `#F9`, `INTMASK` bit 3) on every pass: raised at every `#xxEF` access, byte arrival and frame boundary until ISR is read. IBT fires at the byte that reaches the threshold; ITO (silence) is noticed at the next of those points, up to a frame after its deadline (Z2: at the deadline). With nothing on either line (no network) there is no frame-boundary work: nothing can arrive, and that work runs after the TTD checkpoint, so it must not change state a replay cannot repeat |
| Timing | `ComPort::AddAccessWait` | every `#xxEF` access, ZiFi included, waits for the AVR (the BaseConf model; no TS measurement exists, §8 Q4) |

The AVR keeps its state through a Z80 reset (`rs232_init` runs only at AVR start); the ESP keeps
its link.

Machines: TS-Conf (always: the TS firmware is part of the configuration; the 16550 is there with
`ComPort=NONE` too, as on the ZX-Evo) and ZX-Evo BaseConf with `[EVO] Avr=TS2016-04` (the ZiFi
registers reached through the Gluk address since the 2021 firmware; no wait-port INT on BaseConf).

## 3. The ESP side

| `ZiFi=` | Generation | Peer |
|:--|:--|:--|
| `NONE` (default) | no board | the AVR's UART runs, bytes go nowhere |
| `AT[,baud]` | 1 (ESP-01, Espressif AT) | `AtModule` (the existing personality) |
| `LOOPBACK`, `TCP:host:port`, `SERIAL:dev` | any | the existing peers (a real ESP on USB through `SERIAL:`) |
| `ZIFI-NATIVE` | 2 (ESP32-S3 / ESP-01S native protocol) | later: `ZiFiNativeModule` (phase Z3) |

## 4. TTD

- Blob 38 `ZiFiLine`: the line's UART and peer (`netstate::SerialPort` through `TTDSerialPort`).
- Blob 39 `ZiFi`: the API registers and the last-byte times (fixed size, versioned).
- Virtual-network guest 5 (`SerialGuests::zifi`).

## 5. Surfaces

`[NETWORK] ZiFi=`; runtime key `zifi` (`POST /network/config`, CLI `network set zifi`, MCP, Lua,
Python); status block `zifi` (registers, selector, ring fill, the line, the peer) in
`GET /state/network`, the MCP / CLI views and the Qt Network window.

## 6. Phases

| Phase | Content |
|:--|:--|
| Z1 | the `#xxEF` device on TS-Conf (16550 + ZiFi API + DR), the line with `AT` / stream peers, the wait-port INT, TTD, settings and status on every surface, the recipe; unit tests §6.3 1-11 |
| Z2 | TS-Conf DMA device 7 (wait port to RAM, `#25AF` / `#2DAF`) |
| Z3 | `ZIFI-NATIVE`: the generation-2 protocol (frames, ACK, Wi-Fi, TCP client, HTTP GET, NTP, ping; the file bridge to the Z80) |
| Z4 | the ESP-AT 2.2.x dialect in `AtModule` (the Sprinter Wi-Fi driver's commands) |
| Z5 | end-to-end: HackerVBI `zifi.spg`, the new `zifi.spg`, the WC plugins |
| Z6 | ZiFi32 on the VDAC2 SPI, only on demand |
