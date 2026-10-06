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
| Timing | `ComPort::AddAccessWait` -> the board's `EvoAvrWait` | every `#xxEF` access, ZiFi included, waits for the AVR (the BaseConf model; no TS measurement exists, §8 Q4); the Gluk data port `#BFF7` waits on the same AVR main loop (one phase for both, [reference-evo-com-port.md](../2026-09-30-nedoos-integration/reference-evo-com-port.md) §3.1) |

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
- Z3b: SMB, the online update (§7.5 "Open"). FTP, HTTPS and WC Update are done (§7.5).
- HTTPS for the S3's HTTP GET needs a TLS decision (reference-sprinter-wifi-driver open question 7).
- `ERR CODE` extensions for parameter errors carry the parameter index where the module knows it, else 0
  (Espressif documents the layout, not every command's index).

### 7.5 Z3b: the file bridge (as built 2026-10-04, branch `zifi-z3b`)

The direction is the firmware's: the ESP is the **server** (FTP; WebDAV on the ESP-01S), a PC's client connects
to it through the virtual network, and every file operation becomes **VFS request frames to the Z80** (`40..5E`),
answered by the Wild Commander plugin from the SD card (the Z5 file-bridge table). Sources (local mirrors at the
commits of §7.2): S3 [src/vfs_client.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/vfs_client.cpp),
[src/vfs_bridge.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/vfs_bridge.cpp),
[src/ftp_server.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/ftp_server.cpp),
[src/fat_time.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/fat_time.cpp),
[src/main.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/main.cpp) (FTP_START / STOP,
events, the RSSI bar, `configTime`), [docs/PROTOCOL.md](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/docs/PROTOCOL.md)
"VFS: команды ESP -> Z80"; E01 [src/vfs_client.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/vfs_client.cpp),
[src/ftp_server.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/ftp_server.cpp),
[src/main.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/main.cpp); the plugin side
`FTP Server/src/vfs.asm` and `shared/z80/zifi_uart.asm` of the S3 repository.

| Part | As built |
|:--|:--|
| `ZiFiVfsBridge` (`zifivfsbridge.{h,cpp}`) | the VFS client + bridge: one operation at a time (`Submit*` / `TakeResult`), the S3 64 KiB rings (VFS -> network for RETR, network -> VFS for STOR); STAT (FILEX metadata), OPENDIR / READDIR (S3 batches of 16 when OPENDIR announces them), OPEN modes 0-3 with the plugin's capabilities, READ 512 or READ_WINDOW 16 KiB (CRC-16/CCITT-FALSE), BLOCK 512 (fragments 248 / 252, each acknowledged) or WRITE_WINDOW 16 KiB (one acknowledgment), CLOSE commit / abort (`ingress-pending`), DELETE, MKDIR, RENAME, MOVE_RENAME, EXTEND, SEEK, SET_EOF, SET_METADATA; the client's timeouts (5 / 30 / 60 / 180 s) and error texts (`stat-4`, `block-status-34`, `timeout-40`, ...). E01: STAT / READDIR / OPEN 0-1 / READ 512 / BLOCK / CLOSE / DELETE / MKDIR, the E01 parsing |
| UART while the VFS waits | `waitFor`: the awaited frame goes to the client; PING (`F0`) and SYS_RESET are served; every other frame is lost (`dropped_while_waiting`). ESP01S: while an FTP command runs and no VFS answer is awaited, the UART is not read at all (one loop) |
| `ZiFiFtpServer` (`zififtpserver.{h,cpp}`) | each FTP command a job (the firmware's blocking call stack as data) that waits for the VFS result, the data connection or TCP bytes. S3: 3 sessions, passive 2122-2124, one VFS owner (a second session's file command queues, its control socket is then not read), the others' commands served meanwhile, FTP-level VFS timeouts 10 / 65 / 185 s (`bridge-timeout-N`), LIST dates as `ls -l` by the ESP's SNTP clock, MLSD / MLST / MDTM / MFMT in UTC by `time:`. E01: one session (421), passive 2122, LIST "Jan 01 00:00", STOR through four 256-byte slots. Both: the 220 banner with the firmware's version and a fresh module's free memory, events 60 (client state) / 61 (command, USER / PASS without argument), S3 66 every 2 s (`Wi-Fi [################] 100%`), FTP_RAM_STATS |
| Module (`zifinativemodule`) | FTP_START (`[port][user][password]`, defaults 21 zx / zx) closes the TCP client and restarts the file services, `86 [1][port]` or `EE "ftp:<reason>"` (E01 `ftp/webdav:`); NET_OPEN / HTTP GET / ping / OTA / online update / WC Update / SMB_START stop FTP (both firmwares); a rejoin to another network stops it on the ESP-01S (the S3 keeps listening, its connections end). S3: a network command during an FTP command gets its ACK and runs after it (`Op::Deferred`: the network core is inside the FTP server). Sockets: 16 slots (0 client, 1 probe, 2.. the servers: §zififtpserver.h; 12 / 13 / 15 WebDAV). S3 events through the 8-deep queue, sent only while the VFS does not wait. Output paced through a 4 KiB TX backlog (a 16 KiB window waits there, not in the TTD output buffer). S3 SNTP in the background after a join (`configTime`: pool.ntp.org, retry 15 s, then hourly; a user command takes the resolver first) |
| TTD | the ZiFi blob (id 40) is variable size now: `ZiFi::State` (32 bytes; `reserved[0] = 1` marks a bridge section) then `[length LE32][SaveBridge]`: the VFS client, the FTP server, the queues, the clock, the socket slots beyond the eighth (received bytes as journal references; raw only for bytes not from the journal), their re-arm; older 32-byte blobs load with no bridge. `EspStack` saves the re-arm mask only for slots 0..7 now (slot 9 used to alias slot 1) and gains `AcceptInto`, `RestoreSlot`, `RearmQueued`, `Network()`. Checked: seeks into a recorded STOR (VFS waiting for `#56`) and RETR restore the same bridge state every time |
| Status | `esp.native_session.file_bridge`: `ftp` (running, port, `host_port` / `host_port_note`, passive_ports, sessions, command, last command / reply, bytes, files), `vfs` (pending, waiting_for, last_error, requests, bytes, timeouts, dropped_while_waiting), `esp_clock_set`; WebAPI (OpenAPI text), MCP (`[zifi]` line: "FTP on 21 (host 2121), n session(s)"), CLI / Lua / Python through the same node, Qt Network window status tree |

**The TS AVR wait (a fix found by the real plugin).** The first real upload lost bytes in the ZiFi ring (`dropped`
532): a 16 KiB write window arrives at 115200 without pauses, and the plugin's INIR bursts paid a whole AVR
main-loop pass per byte (the BaseConf model, §2 Timing). The TS firmware calls `waittask()` after each of its 8
tasks since 2016-03 ([main.c:414-431](https://github.com/tslabs/zx-evo/blob/master/pentevo/avr/current/main.c),
commit 9a3b541b "ISRed ZiFi-UART"), so a wait is picked up at the next task boundary: `Uart16550::Params::
waitChecksPerLoop = 8` for `TS2016-04` [inferred: the 260-cycle pass split evenly]. A back-to-back read now costs
~31 us instead of ~52 us; the real uploads then ran with no drop.

**Results** (real plugins under WC, host Python ftplib, TTD with a 3000-frame limit): S3 v0.15 RETR 100 000 bytes
8.8 KB/s, STOR 70 000 bytes 9.2 KB/s; E01 v0.11 RETR 6.0 KB/s, STOR 2.6 KB/s; byte-exact both ways (also on the
exported card image), passive and active, MKD / CWD, 550 for a missing file, 530 / 421 for a bad login.

**Deviations** [inferred where noted]: free-memory numbers in the banner and RAM stats are a fresh module's
(fixed, as SYS_INFO); the S3 RSSI is the virtual AP's -48 dBm (100 %); a TCP send never blocks (the virtual
network queues), so `sendAll` fails only on a closed socket; network events reach the servers at the next frame
boundary (up to 20 ms); the S3 SNTP timing is lwIP's defaults [inferred]; the E01 diagnostics HTTP server (port
8268) is not emulated.

**HTTPS (S3).** The firmware's `NetClient::httpGet` uses `WiFiClientSecure` (mbedTLS, its CA bundle) for port 443
and after a redirect to `https://` ([src/net_client.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/net_client.cpp)
`openTls`, `applyRedirect`: a downgrade to `http://` is "redirect tls downgrade", the proxy is never used for
HTTPS, the Host field omits the default port 443). Emulated with TLS **done by the host**: `IHostNet::TcpConnectTls`
(`HostNetBridge` + `common/network/hosttls.{h,cpp}`, OpenSSL through memory BIOs, TLS 1.2+, the server's
certificate checked against the host's trust store and the name, SNI) and `VirtualNetwork::ConnectTls` /
`EspStack::Connect(..., tlsServerName)`. The guest slot carries the **plaintext**, which is what the journal
records: a replay needs no host and no keys, and the TTD state is unchanged (the module's flag byte bit 64 =
`_httpTls`). Without OpenSSL (`-DUNREAL_HOST_TLS=OFF`) the connect fails as `NetEventStatus::TlsFailed` and the
program sees `get:tls connect failed`, the firmware's answer when the handshake fails. Timeout: 24 s for connect +
handshake (`connect(host, port, 12000)` + `setHandshakeTimeout(12)`) [inferred: the two added]. Deviation: the
error text is always "tls connect failed" (the firmware may add mbedTLS's own reason as "tls: <reason>"); the host
trust store stands in for the firmware's bundle. Checked with the real S3 `zifi.spg` 0.733: "Demos:
bbb.retroscene.org" (301 to `https://`, then the list over TLS 1.3) and a demo saved to the SD card byte-exact.

**WC Update (S3).** `WCU_START` (`25`, `repo\0branch\0dir\0[protected path\0...]\0`), `WCU_APPLY` (`26`, file
indices), `WCU_STOP` (`27`), `WCU_SYNC` (`28`); events `67` state (`[phase][current LE16][total LE16][percent][text]`)
and `68` list line (`[index][status][flags][SD size LE24][GitHub size LE24][path tail]`). The firmware
([src/wc_updater.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/wc_updater.cpp),
[src/wc_update_service.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/wc_update_service.cpp),
[src/main.cpp](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/main.cpp) `processWcu*`) runs
a task of its own: the branch's commit and the recursive tree from api.github.com, the SD side listed and every
common file hashed as a git blob (SHA-1 over VFS reads), then per marked file: download from
raw.githubusercontent.com (3 attempts, length + SHA checked), a copy `WCUPD.TMP` written and read back for its SHA,
FILEX `MOVE_RENAME` with REPLACE onto the name (or, plugin answering `FE`, the RENAME fallback through `WCUPD.OLD`),
the result read back once more; protected files (`wc.ini`) only installed when missing. `ZiFiWcUpdater` keeps that
control flow as a **C++20 coroutine** (the firmware's blocking calls become `co_await` of a *primitive*: a VFS
request with the updater's own wait 15 / 70 / 190 s, an event with up to 5 s for queue room, a GitHub fetch, a
download, the ring and SHA steps, the time, the stop flag, the next command). Every primitive's result is logged:
the TTD state is WCU_START's payload + that log + the primitive in flight, and a load re-runs the coroutine against
the log with no side effects, leaving it waiting where it was (saved in the bridge section before the plugins tail;
`kBridgeVersion` 2). `ZiFiHttpFetch` is the firmware's `NetWcFetcher` (HTTP/1.0 GET over host TLS on socket slot 10,
its own DNS lookup `EspStack::ResolveAux`, up to 4 redirects, header within 10 s / 2048 bytes, chunked refused,
body to EOF or Content-Length, the body kept by journal reference). The service: WCU_START closes the TCP client and
stops FTP; `wcu_.stop(20 s)` before WCU_START, FTP_START and SMB_START holds the request (`Op::WcuStop`) until the
session ends (timeout: `wcu:previous stopping` / `ftp:wc update stopping` / `smb:wc update stopping`, `A7 [0]`);
WCU_START's answer leaves before the session's first event and APPLY / SYNC are taken at the next poll (the
plugin's `waitFor` drops any other frame). **Deviations** [inferred]: the session is polled at frame boundaries and
on every VFS answer (the firmware's task runs as soon as it can); a downloaded file's buffer is dropped when the
file is done (`DropDownload`), but a TTD checkpoint inside one file's processing whose download is older than the
history limit cannot restore the session (it comes back idle). Checked with the real `WCUPDATE.WMF` v1.0 against
GitHub (TODO.md Z5 table).

**Open:** SMB (S3: the firmware is libsmb2 in server mode plus a 10 000-line adapter, NBNS / LLMNR / WS-Discovery),
the online update (its check is a manifest over HTTPS; the
install would run a downloaded ESP32-S3 image, which an emulated module cannot).
