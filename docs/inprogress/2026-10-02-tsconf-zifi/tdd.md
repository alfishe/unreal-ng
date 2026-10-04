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
| `AT[,<firmware>][,baud]` | 1 (ESP-01, Espressif AT) | `AtModule`: the ESP-01 is an ESP8266 with 1 MB flash, so NonOS AT 1.7.4 unless `EspChip` or `<firmware>` names another ESP8266 build (`ESP8266-AT222`, `ESP8266-AT221`); §7.1 |
| `LOOPBACK`, `TCP:host:port`, `SERIAL:dev` | any | the existing peers (a real ESP on USB through `SERIAL:`) |
| `ZIFI-NATIVE[,S3\|ESP01S][,baud]` | 2 (ESP32-S3-Zero / ESP-01S native protocol) | `ZiFiNativeModule` (§7.2) |

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
| Z3 | `ZIFI-NATIVE`: the generation-2 protocol (frames, ACK, Wi-Fi, TCP client, HTTP GET, NTP, ping) - done (§7.2); the file bridge to the Z80 (FTP / SMB / WebDAV through VFS requests) is Z3b |
| Z4 | the ESP-AT 2.2.x dialect in `AtModule` (the Sprinter Wi-Fi driver's commands) - done (§7.1) |
| Z5 | end-to-end: HackerVBI `zifi.spg`, the new `zifi.spg`, the WC plugins |
| Z6 | ZiFi32 on the VDAC2 SPI, only on demand |

## 7. The ESP firmwares behind ZiFi (Z3, Z4; as built 2026-10-04, branch `tsconf-zifi`)

### 7.1 Z4: ESP-AT 2.2.x on the ESP-01

The 2.2.1 / 2.2.2 presets and most of their commands came with the Sprinter network work (SN3,
[sprinter-network tdd §8.3](../2026-10-02-sprinter-network/tdd.md)). Z4 makes the dialect Espressif's, per
primary source, and selectable per module:

- **Per-module firmware.** `ComPortSpec` takes `AT[,<firmware>][,<baud>]` (either order; `ESP32`, `ESP8266`,
  `ESP8266-AT221`, `ESP8266-AT222`). It overrides `[NETWORK] EspChip=` for that module alone, so the ZiFi ESP-01
  and, say, a ZX-WiFi card's ESP32 can run different builds. No new config key: `[NETWORK] ZiFi=`, the runtime
  key `zifi` and every surface take the new value (`zifi=at,esp8266-at222`).
- **The board's chip.** The ZiFi board carries an ESP-01: ESP8266, 1 MB. `ZiFi=AT` without a firmware runs the
  `EspChip` build if that is an ESP8266 one, else NonOS AT 1.7.4 (what original ZiFi users flashed; HackerVBI's
  `zifi.spg` needs its `_CUR` / `_DEF` forms). **Deviation from Z1**: Z1 ran `EspChip` (ESP32 by default). The AT
  module of a ZiFi board is told its flash is 1 MB (`AtModule::SetFlash`): ESP-AT's 1 MB build is the default one
  without OTA (`AT+CIUPDATE` does not exist) and `AT+GMR` says `Bin version:2.2.2(ESP8266_1MB)`.
- **One dialect table**, `core/src/emulator/io/serial/esp/atdialect.{h,cpp}` (`atdialect::Traits` per build),
  used by `AtModule`. What ESP8266 ESP-AT 2.2.2 does that the module did not:

  | Group | Behavior (2.2.2) | Source |
  |:--|:--|:--|
  | Basic | no `AT+X_CUR` / `AT+X_DEF` except `UART_CUR` / `UART_DEF`: `ERROR` | [AT Command Set Comparison](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/AT_Command_Set_Comparison.html) |
  | Basic | `AT+SYSLOG=1`: `ERR CODE:0x%08x` before `ERROR`; code = `0x01 << 24 \| subcategory << 16 \| extension` (`0x01090000` not supported, `0x01030000` no `AT`, `0x0107xxxx` parameter invalid, `0x010A0000` execution failed, `0x010C0000` wrong command type) | [Basic AT Commands, AT+SYSLOG](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/Basic_AT_Commands.html), [esp_at_core.h](https://github.com/espressif/esp-at/blob/v2.2.1.0_esp8266/components/at/include/esp_at_core.h) |
  | Basic | 1 MB build: no OTA | [module_esp8266_1mb sdkconfig](https://github.com/espressif/esp-at/blob/v2.2.1.0_esp8266/module_config/module_esp8266_1mb/sdkconfig.defaults) |
  | Wi-Fi | a failed `CWJAP`: `+CWJAP:<code>` then `ERROR` (NonOS: `FAIL`); `<jap_timeout>` (8th parameter, 3..600 s, default 15) bounds it; `AT+CWJAP` alone rejoins the last AP | [Wi-Fi AT Commands](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/Wi-Fi_AT_Commands.html) |
  | Wi-Fi | `CWJAP?` adds `pci_en, reconn_interval, listen_interval, scan_mode, pmf`; `AT+CWSTATE?` (2.2.0.0+); `+CWLAP` adds pairwise / group cipher, bgn, wps | same; [release notes](https://github.com/espressif/esp-at/releases) |
  | TCP/IP | `+CIPDOMAIN:"a.b.c.d"` (quoted since 2.2.0.0); `AT+CIPSTATE?` (2.2.2.0+); `+CIPRECVLEN:` leaves links that are not open empty; passive mode reports `+IPD,<len>` once until `CIPRECVDATA` read it | [TCP-IP AT Commands](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html), release notes |

- **The two "2.2.1"s.** Espressif's v2.2.1.0 has `AT+SYSSTORE` (since 2.1.0.0) and no `_CUR` forms. The
  Sprinter kit's README describes its "2.2.1" binary differently (the `_CUR` / `_DEF` and `AT+IPR` tokens, no
  `SYSSTORE`, no `CIPRECVMODE`) and its 2.2.1 profile depends on that. `ESP8266-AT221` stays the kit's
  description (the kit's NETUP probe works against it), with the 2.2 reply forms otherwise; noted as open below.
- ESP32 (AT 2.2.0) keeps its earlier forms (not in this scope).
- **TTD**: one more byte in `AtModule`'s part of the ESP blob (the links that owe a `CIPRECVDATA`), in the spare
  bytes; no new id, older blobs load as "nothing owed".

### 7.2 Z3: `ZIFI-NATIVE`

`core/src/emulator/io/serial/esp/zifinativemodule.{h,cpp}`, `ZiFiNativeModule : EspModule`, from the firmwares'
own sources (local mirrors of [ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero)
`2e5ba83` and [ZiFi-ESP-01S-Native-C-Project](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project)
`90834e4`: [docs/PROTOCOL.md](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/docs/PROTOCOL.md),
[include/zifi/protocol.hpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/include/zifi/protocol.hpp),
[src/main.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/main.cpp),
[src/net_client.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/net_client.cpp),
[src/ntp_client.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/ntp_client.cpp),
[src/config.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/config.cpp),
[E01 src/main.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/main.cpp),
[E01 src/net_client.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/net_client.cpp)).

| Part | As built |
|:--|:--|
| Variants | `S3` (ESP32-S3-Zero, `s3-native-0.6.94`, default) and `ESP01S` (ESP-01S, `native-0.2.2`): two real firmwares, both on the unchanged AVR pipe |
| Frame | `5A CMD LEN_L LEN_H DATA CSUM`, CSUM = XOR of CMD, LEN, DATA; LEN > 1024 resyncs after LEN_H; a bad checksum drops the frame; a frame unfinished for 500 ms is dropped (the parser's `checkTimeout`) |
| Replies | `FE` at once for long commands; `92` (NET_RECV) and `F0` (PING) without; `EE <text <= 48>` before a failed result; `GET_STEP` = last command + last error; unknown: `EE "unsupported:XX"` (S3) / `"unknown cmd XX"` (ESP01S) |
| Concurrency | S3: the network runs on its own core - ECHO / PING / GET_STEP / SYS_INFO / SYS_RESET are answered meanwhile, a second network command gets `FE`, `EE "network busy"` and its empty result. ESP01S: one loop - later bytes wait in the UART until the command is done |
| Wi-Fi | `WIFI_CONNECT ssid\0pass`, `WIFI_INI` (the whole `zifi.ini`: `ssid:`, `password:`, `time:`, S3 `proxy_ip` / `proxy_host` / `proxy_port`); the virtual AP `UnrealNG` joins in 1.5 s, any other SSID times out after 10 s (`wifi timeout`); the same SSID and password keep the link; `[ok][IPv4]` |
| TCP client | `NET_OPEN host\0[port]` (8 s), `NET_SEND`, `NET_RECV [max LE16]` (`[eof][data <= 1023]`, zero = nothing yet), `NET_CLOSE`; one client socket |
| HTTP GET | the firmware's HTTP/1.0 request; header <= 2048 bytes in 10 s; S3: up to 4 redirects, `chunked` refused, the body ends at Content-Length, the proxy when probed alive, port 443 = HTTPS = `EE "get:tls connect failed"` (no TLS in the virtual network); ESP01S: no redirects, no proxy, `Host` without the port |
| NET_PING | a TCP connect to port 80 (3 s), `[ok][ms LE16]` - not ICMP, as the firmware |
| NET_NTP | `pool.ntp.org`, 3 s; 14 ASCII digits in the `time:` zone; S3 validates leap / mode / stratum |
| NET_IP_CONFIG, NET_PROXY_STATUS (S3), SYS_INFO, SYS_RESET | address / mask / gateway / DNS; `[status][host:port]`; the firmware's text with a fresh module's fixed numbers and its `FW:`; ACK, 0.4 s restart, the saved `zifi.ini` rejoins, `RST:` says software reset |
| Not emulated | FTP / SMB / WebDAV servers (they call back into the Z80: VFS `40..5E`), the OTA listener, the online update, the WC updater, the weather: each answers the firmware's "could not start" result with `EE "<service>:not emulated"`; the stop commands succeed |
| TTD | no new id: the state is `ZiFiNativeModule`'s part (225 of 256 bytes) of the ESP module blob, ComPort peer kind 7; a request waiting for the network stays at the head of the receive buffer (saved with it) and a redirect rewrites it there |
| Status | `esp` node (shared `espdescribe::Describe`, also used by the SprinterESP card and every COM / ZX-WiFi / ATM2IOESP peer): firmware, Wi-Fi, `native_session` (activity, last step / error, client, zone, proxy, dropped frames) |

### 7.3 Tests

`atdialect_test.cpp` (the table, Basic, Wi-Fi, TCP/IP groups), `zifinativemodule_test.cpp` (frames, system,
Wi-Fi, TCP, HTTP, both variants' concurrency, NTP / IP / proxy, the left-out services, TTD),
`comportspec_test.cpp` (+2), `zifi_test.cpp` (+3: the ESP-01's build, the native module through the AVR
registers, TTD with peer kind 7).

**Smoke with the real programs** (2026-10-04, TS-Conf, an SD folder with `zifi/zifi.ini`, TTD recording on with a
3000-frame limit before each run, real time so the host network answers):
- the new `zifi.spg` (ESP-01S build, `ZiFi=ZIFI-NATIVE,ESP01S`): PING, WIFI_INI (joined, zone +3), SYS_INFO, its
  HTTP self-test `GET zifi.vtrd.in` (`94` success) and NTP (`A2`), no error;
- HackerVBI's `zifi.spg` 0.733 (`ZiFi=AT` = NonOS 1.7.4): `ATE0`, `CWMODE_DEF`, `CWJAP_CUR`, `CIPSTART`, `CIPSEND`,
  `+IPD` with `HTTP/1.1 200` from `zifi.vtrd.in`;
- the same binary on `ZiFi=AT,ESP8266-AT222`: `CWMODE_DEF` / `CWJAP_CUR` answer `ERROR`, as on an ESP-01 reflashed
  with ESP-AT 2.2.2 (the client needs NonOS or a port).
- Note: the zifi.spg key names are case-sensitive on the Z80 side (`SSID:`, as the shipped `zifi.ini`).

### 7.4 Open

- Which "2.2.1" the kit's author has (the binary is not published); if it is Espressif's v2.2.1.0, the
  `ESP8266-AT221` preset should get `SYSSTORE`, passive receive and no `_CUR` (§7.1).
- Z3b: the VFS file bridge (FTP / SMB / WebDAV through the Wild Commander plugins), OTA, weather.
- HTTPS for the S3's HTTP GET needs a TLS decision (reference-sprinter-wifi-driver open question 7).
- `ERR CODE` extensions for parameter errors carry the parameter index where the module knows it, else 0
  (Espressif documents the layout, not every command's index).
