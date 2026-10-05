# Recipe: network adapters (ZXNETUSB / W5300) and the virtual network

A ZX program reaches the network through an emulated adapter. Today that is
the **ZXNETUSB** card (NedoPC: WIZnet W5300 on the ZX-Bus, ports `#xxAB`),
the adapter the NedoOS W5300 kernel (`sd_boot.$C`) drives. Design:
[tdd-network.md](../../docs/inprogress/2026-09-30-nedoos-integration/tdd-network.md).

The card talks to a **virtual network** that belongs to the emulator
instance:

| Address | What answers |
|:--|:--|
| `10.0.2.2` | gateway: DHCP server (UDP 67), answers ping |
| `10.0.2.3` | DNS server; answers ping |
| `10.0.2.15` | the first DHCP lease (one per MAC) |
| UDP to port 53, any server | answered from the host resolver (`DnsMode=HOST`) or the `Hosts=` table |
| anything else | forwarded to the host network through ordinary host sockets |

Everything the host answers is a TTD input: a recorded session replays with
the network unplugged.

Frame-level cards (the Sprinter's NE2000 in ISA slot 2) reach the same
virtual network through the **Ethernet gateway** (a switch + router at
`10.0.2.2`, MAC `52:55:0A:00:02:02`): `state/network` lists them under
`slots` and `ethernet_gateway`, and `GET /network/frames` captures their
frames. Recipe: [machines/sprinter-network.md](../machines/sprinter-network.md).

Those frame cards can also be **bridged to a host adapter** instead (`ethernet_mode=bridge bridge_adapter=en0`):
the card gets its address from the real LAN. It needs the host's permission to read and write raw frames; on macOS
`sudo chmod o+rw /dev/bpf*` lasts **only until the next reboot** (Wireshark's ChmodBPF makes it permanent), on Linux
`setcap` is lost when the binary is replaced, on Windows Npcap must be installed. Reference:
[docs/features/network-bridge.md](../../docs/features/network-bridge.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) only inside host-side pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## Traffic: everything the adapters sent and received (verified 2026-10-04)

Every machine with a network adapter records its traffic at one point, the virtual network (design:
[network-traffic-debug/design.md](../../docs/inprogress/2026-10-04-network-traffic-debug/design.md)): frame cards'
Ethernet frames (NE2000, 3C509B, the bridge's LAN side) and socket adapters' operations (ZXNETUSB / W5300, ESP
modules, ZiFi, the Hayes modem: `connect`, `send`, `data`, `close`, ...) with their bytes, each with its TTD
position (`frame`, `t_in_frame`) and emulated time. An always-on ring keeps the newest 8 MiB; a recording into a
pcapng file runs from `start` to `stop` with no limit. Checked live on the Sprinter's NE2000 in NAT: the RTL kit's
`IFUP` and `NSLOOKUP` gave 14 records (8 frames of `isa2.eth`: DHCP, ARP, DNS; 6 socket operations of
`gateway-nat`: the router's own UDP), and a `start` / `PING` / `stop` wrote a pcapng of 6 packets.

```bash
curl -s "$B/$ID/network/traffic?last=8" | jq -r '.records[] | "#\(.index) f\(.frame) \(.adapter) \(.direction) \(.summary)"'
#  #12 f4504 isa2.eth out ARP who-has 10.0.2.2 tell 10.0.2.15
#  #41 f9122 zxnetusb in TCP data from 93.184.216.34:80, 1460 bytes: HTTP/1.0 200 OK
curl -s "$B/$ID/network/traffic?since=42"            # poll: pass the previous tap.next_index
curl -s "$B/$ID/network/traffic?format=pcapng" -o run.pcapng      # the ring for Wireshark: frames + socket operations as packets
curl -s -X POST $B/$ID/network/traffic -H 'Content-Type: application/json' -d '{"action":"start","path":"/abs/run.pcapng"}'
curl -s -X POST $B/$ID/network/traffic -d '{"action":"stop"}'
```

Filters: `adapter=isa2.eth` (or `zxnetusb`, `com.esp`, `isa1.esp`, `isa1.modem`, `gateway-nat`, `lan`),
`kind=frame|socket`, `last=N` (0 = the whole ring). CLI: `network traffic [adapter] [N] [file.pcapng]`,
`network traffic start <file.pcapng> | stop | clear`; Lua: `network_traffic{since=, adapter=, kind=, last=}`,
`network_traffic_control(action, path, value)`; Python: `network_traffic(...)`, `network_traffic_pcapng()`,
`network_traffic_control(...)`; MCP: `inspect_state` aspect `network` (its `traffic` part) and `invoke_api`.

**Live in Wireshark** (macOS, Linux, Windows): `POST .../network/traffic {"action":"stream","port":0}` (or `[NETWORK]
TrafficStream=auto`, CLI `network traffic stream`) serves the traffic as a pcapng stream on a TCP port; the reply's
`stream.wireshark` is the command: `wireshark -k -i TCP@127.0.0.1:<port>`. A reader gets the ring first, then every
packet as it passes. The extcap script [tools/wireshark/](../../tools/wireshark/README.md) lists running emulators in
Wireshark's interface list instead. `{"action":"stream-stop"}` ends it.

In the pcapng a socket adapter's operations are **synthetic packets** Wireshark decodes (HTTP, DNS, "Follow TCP
Stream"): a connect is a SYN / SYN-ACK / ACK, data are segments of up to 1460 bytes with chained sequence numbers, a
close a FIN, a reset an RST. The bytes are exact; the TCP framing and the adapter's address (10.0.2.15 and up, one per
adapter) are made up - each packet's comment says what really happened (`#41 frame 9122 t 1203 in - socket 3 data
(synthetic packet)`).

## Fitting the card

Machine config (`configs/<model>/unreal.ini`; shipped for ATM3, ATM710,
Pentagon, Scorpion, Profi):

```ini
[NETWORK]
Card=ZXNETUSB                  ; NONE = no card: the ports read #FF (default)
HostAccess=1                   ; 0 = internal services only (DHCP, hosts table, gateway ping)
DnsMode=HOST                   ; HOST | PASS
Hosts=next.zxart.ee=127.0.0.1  ; name=a.b.c.d,name=a.b.c.d
Forward=tcp:8080:80            ; guest servers: tcp:<hostport>:<guestport>,...
RemoteAccess=on                ; guest servers listen on 0.0.0.0 | off = 127.0.0.1 only
ConnectTimeoutMs=10000
```

The runtime feature `network` (alias `net`, on by default) unplugs a fitted
card and plugs it back: `PUT /api/v1/emulator/{id}/feature/network`.

The same settings change at runtime (the card is fitted again, every
connection closes; refused while a TTD recording runs):

```json
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
  "body": {"card": "zxnetusb", "host_access": true, "hosts": "next.zxart.ee=127.0.0.1"}}}
```

CLI `network set card=zxnetusb host_access=on`, Lua
`network_configure{card="zxnetusb"}`, Python `emu.network_configure(card="zxnetusb")`.

Guest servers (a NedoOS program in `LISTEN`) are reachable on the host:
guest ports 1024 and up on the same host port, lower ones only through a
`Forward=` rule.

### Remote access

`RemoteAccess` (runtime key `remote_access`, on by default) decides which host
address those listeners bind - every `Forward=` rule and every guest server,
on every card and ESP module (UDP forwards will follow the same setting):

| Setting | Listeners bind | Who can connect |
|:--|:--|:--|
| `on` (default) | `0.0.0.0` | this computer, and any other computer that can reach it (your LAN, a Windows PC for the ZiFi FTP / SMB servers) |
| `off` | `127.0.0.1` | this computer only |

Security note: the emulated servers (ZiFi FTP / WebDAV, a NedoOS program in
`LISTEN`, a modem that answers) have no real authentication. On an untrusted
network (cafe Wi-Fi, a hotel) turn remote access off.

```json
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
  "body": {"remote_access": false}}}
```

CLI `network set remote_access=off`, Lua `network_configure{remote_access=false}`,
Python `emu.network_configure(remote_access=False)`, Qt: Network window,
"Allow remote access (listen on all interfaces)". A change of `remote_access`
alone moves the listeners at once and keeps every connection and the cards
(other keys fit the devices again); like every network change it is refused
while a TTD recording runs. It does not affect a TTD replay: the journal
records the guest side, a replay opens no host listener.

Check: `GET /state/network` -> `settings.remote_access`,
`virtual_network.remote_access`, `virtual_network.listen_address`
(`0.0.0.0` / `127.0.0.1`) and `guest_servers[].host_address`; on the host
`lsof -nP -iTCP -sTCP:LISTEN | grep unreal` (Windows: `netstat -an`) shows
`*:2121` or `127.0.0.1:2121`.

## COM port (16550 UART)

A 16550 on `#F8EF..#FFEF` (register = A10..A8) with a peer on its other end.
On the ZX-Evo (ATM3) and TS-Conf (`TSL`) it is the AVR's COM port, always
there; `ComPort=` says what is plugged into it. Other machines get one with a ZX-WiFi card
(`Card=ZXWIFI`, or `ZXNETUSB,ZXWIFI` for both cards); `ZxWifi=` (default `AT`)
takes the same values for the card's side:

| `ComPort=` / `ZxWifi=` | Peer |
|:--|:--|
| `NONE` | nothing on the line (the ZX-Evo's registers still answer) |
| `LOOPBACK` | every byte the ZX sends comes back; CTS / DSR / DCD held active |
| `PLUG` | an RS-232 loopback test plug: bytes come back, the UART's own RTS drives CTS and DTR drives DSR / DCD (only the inputs the card wires to its connector) |
| `TCP:<host>:<port>` | a host TCP endpoint (telnet BBS, a test harness); the host is an address or a name (resolved through the virtual network's DNS: `Hosts=`, then the host resolver); reconnects every ~5 s after a drop |
| `ESPNET[,<baud>]` | an emulated ESP module with NedoOS's ESPNET firmware 1.27 (binary sockets; NedoOS `sd_bootesp.$C` kernel and `currentNetwork=2` apps); `<baud>` = the rate its firmware was built for, by default the port's (ATM Turbo 2+ controller 38400, else 115200) |
| `AT[,<firmware>][,<baud>]` | an emulated ESP module with Espressif's AT firmware (NedoOS `currentNetwork=1` apps, Moon Rabbit, Karabas net-tools); `<baud>` as for `ESPNET`; `<firmware>` (`ESP32`, `ESP8266` = NonOS 1.7.4, `ESP8266-AT221`, `ESP8266-AT222`) picks the build for this module alone, else `[NETWORK] EspChip=` |
| `ZIFI-NATIVE[,<variant>][,<baud>]` | the 2026 ZiFi firmware (binary frames, not AT): `S3` = ESP32-S3-Zero `s3-native-0.6.94` (default), `ESP01S` = ESP-01S `native-0.2.2`; see ZiFi below |
| `MODEM[,<guest port>]` | an emulated Hayes modem: `AT` commands, `ATDT <host>[:<port>]` (port 23 by default) or `ATDT <number>` from `[NETWORK] ModemPhonebook=5551234=bbs.example.org:23,...` dials through the virtual network; `CONNECT <rate>` / `BUSY` (refused) / `NO ANSWER` / `NO CARRIER`, DCD follows the call, `+++` (a second of silence around it) returns to command mode, `ATO` / `ATH`; with `<guest port>` a host client of that guest port (`Forward=`) rings it (`RING`, RI; `ATA` or `ATS0=n` answers). See [The Hayes modem](#the-hayes-modem) |
| `SERIAL:<device>[,<baud>]` | a host serial device (`/dev/tty.usbserial-0001`, `/dev/ttyUSB0`, `COM3`); it follows the rate and format the ZX programs (`<baud>` until then, 115200 by default); `ComModemLines=1` passes RTS / DTR and reports CTS / DSR / RI / DCD (off by default: USB ESP boards wire RTS / DTR to reset / boot) |

The ZX-Evo's UART behaves like the AVR firmware chosen by `[EVO] Avr=`
(default `BASECONF`, the newest NedoPC; every NedoPC and TS-Labs release
since 2010 is a preset: no interrupts, RTS by software, every access holds
the Z80 ~30-50 us); at runtime `avr_firmware=ts2013` etc. The ZX-WiFi card is a real 16550 (auto flow control with
MCR bit 5). A ZX-WiFi card on a ZX-Evo or TS-Conf is not fitted (#xxEF is
taken): `not_fitted` in `inspect_state network` says so. The
bytes that arrive are TTD input like the card's. Details:
[reference-evo-com-port.md](../../docs/inprogress/2026-09-30-nedoos-integration/reference-evo-com-port.md).

On the ATM Turbo 2+ (`ATM710`) the machine's serial port is the keyboard
controller's RS-232: `ComPort=` plugs into the MCU's own UART (firmware
`[ATM] Kbc=` V31 and later). The Z80 drives it through `IN #FE` commands
(`#55`, `#03 d` send, `#02` receive, `#C2` count, `#43 d` DTR / RTS, `#C3 d`
baud divisor); the peer only sends while RTS is asserted. It is not on
#xxEF, so a ZX-WiFi card fits beside it. `inspect_state network` shows it
as `machine_serial` (with `peer_baud`, the module's own rate); at runtime
`kbc_firmware=v31-11` etc. fits another controller.

An ESP module on this port ships at **38400** (`ESPNET` / `AT` without
`,<baud>`): NedoOS's ESPNET firmware is built for 38400 here ("ATM2COM"),
and the controller's receive does not keep up with 115200. NedoOS selects
the port with `/ini/espcom.ini` `comType = 1`, `divider = 3` and `espcfg`
(the floppy `osatm2esp.trd` runs `wizcfg` instead and stays at the kernel's
115200: the line then reads garbage). In 7 MHz turbo the board's RAM waits
(`atm710_turbo_waits` in the contention report) stretch the driver's
polling loop enough for the 8051's serial interrupt; with the `contention`
feature off they are gone and received bytes are lost (`lost` in
`machine_serial`; tdd-atm2-kbc.md §7.1). Details:
[tdd-atm2-kbc.md](../../docs/inprogress/2026-10-01-atm2-keyboard-controller/tdd-atm2-kbc.md).

On the ZX Profi v5 (`PROFI`, `PROFI-PLUS`) the machine's serial port is the board's 8251 USART (`#D3` data, `#F3`
control / status, clocked by an 8253 at `#8F..#EF`, extended port map only): `ComPort=` plugs into it, it is not on
#xxEF, `inspect_state network` shows it as `machine_serial` (flavor `usart8251`). Details:
[profi.md](../machines/profi.md#serial-port-com).

The other real-world way on the ATM Turbo 2+ is the **ATM2IOESP** card
(`Card=ATM2IOESP`, `Atm2IoEsp=AT|ESPNET|...`, `Atm2IoEspAddress=0xF0`, 0xF8
on Rev 1.0): a TL16C550C and an ESP32 on the INTERNAL I/O connector. The Z80
writes the register's bus address to `#FB` (`0xF0 + register`) and moves the
data through `#FA`; 115200, no interrupt, RTS pulsed by software. NedoOS:
`espcom.ini` `comType = 3`, the registers `0xF0..0xF7`, `divider = 1`. It does
not depend on the keyboard controller, so it does not lose bytes the way the
COM port can. `inspect_state network` shows it as `atm2ioesp`. Details:
[2026-10-02-atm2ioesp](../../docs/inprogress/2026-10-02-atm2ioesp/README.md).

**ZiFi** (TS-Conf; a ZX-Evo with `[EVO] Avr=TS2016-02` / `TS2016-04`): the
TS-Labs AVR firmware passes bytes between the Z80 and an ESP module on its own
UART (115200, no flow control) through two rings, 511 bytes in and 255 out.
`ZiFi=` says what is on that UART:

| `ZiFi=` | The board |
|:--|:--|
| `NONE` (default) | no ZiFi board |
| `AT` | the original board: an ESP-01 (ESP8266, 1 MB) with Espressif's AT firmware - NonOS AT 1.7.4 (HackerVBI's `zifi.spg` sends `AT+CWMODE_DEF`, `AT+CWJAP_CUR`), or the ESP8266 build `[NETWORK] EspChip=` names |
| `AT,ESP8266-AT222` | the same ESP-01 reflashed with ESP-AT 2.2.2 (1 MB build: no OTA). Espressif's dialect: no `_CUR` / `_DEF` forms (they answer `ERROR`), `+CWJAP:<code>` + `ERROR`, `AT+CWSTATE?`, `AT+CIPSTATE?`, quoted `+CIPDOMAIN:`, one passive `+IPD` per read, `ERR CODE:0x...` before `ERROR` after `AT+SYSLOG=1`. What a ZiFi port of the Sprinter ESP Network Kit needs (passive receive: the AVR has no flow control) |
| `AT,ESP8266-AT221` | the kit's "2.2.1" (its `_CUR` tokens, no `SYSSTORE`, no passive receive) |
| `ZIFI-NATIVE` / `ZIFI-NATIVE,ESP01S` | the 2026 "new ZiFi": an ESP32-S3-Zero (`s3-native-0.6.94`) or an ESP-01S (`native-0.2.2`) with the binary protocol of the new `zifi.spg` and the Wild Commander plugins |
| `LOOPBACK`, `TCP:...`, `SERIAL:...` | as for `ComPort=` (`SERIAL:` = a real ESP on a USB adapter) |

The Z80 sees the same `#xxEF` range as the COM port:

| Port | What |
|:--|:--|
| `#C7EF` | write: command (`#F1` API on, `#FF` version, `#00..#03` / `#04..#07` clear the ZiFi / RS-232 rings); read: the last result |
| `#C0EF` / `#C1EF` | ZiFi bytes waiting / room (capped at `#BF`); the read makes `#00EF..#BFEF` the ZiFi data register |
| `#C2EF` / `#C3EF` | the same for the COM port's rings (the "enhanced RS-232" data register) |
| `#00EF..#BFEF` | the data register: `INIR` from `#BFEF` with B = the count reads the whole batch |
| `#C4EF` | write: interrupt mask (OR-in, one-shot), read: sources (then cleared); TS-Conf raises the wait-port INT (vector `#F9`, INTMASK bit 3) while it is not zero |
| `#C5EF`, `#C6EF`, `#C8EF`, `#C9EF` | interrupt threshold (bytes) and timeout (ms) for ZiFi / RS-232 |
| `#F8EF..#FFEF` | the 16550 COM port |

Until `#F1` goes to `#C7EF` every ZiFi register reads `#FF`. Every access
holds the Z80 while the AVR answers. `inspect_state network` shows it as
`zifi` (API, which ring the data register reaches, IMR / ISR, ring fill, the
line's peer, `dropped` = bytes the full ring lost) and, for an ESP module, `esp`
(the firmware, Wi-Fi, the `at_session` or `native_session`, the last 32
exchanges); at runtime `network set zifi=at` / `zifi=at,esp8266-at222` /
`zifi=zifi-native`. Details:
[2026-10-02-tsconf-zifi](../../docs/inprogress/2026-10-02-tsconf-zifi/README.md).

**The native ZiFi protocol** (`ZIFI-NATIVE`): frames `5A CMD LEN_L LEN_H DATA
XOR` both ways (XOR of CMD, LEN and DATA, not the `5A`; at most 1024 bytes).
The module answers `FE` (ACK) at once and the result later (`90` NET_OPEN,
`92` NET_RECV - no ACK, `94` HTTP GET `[ok][code LE16][length LE32]`, `A2` NTP
`YYYYMMDDhhmmss`, `83` WIFI_INI `[ok][IPv4]`, `F0` for PING); a failure sends
`EE <text>` first, and `GET_STEP` (`05`) returns the last command and that
text. The Wi-Fi is the virtual access point: `zifi.ini` must say `ssid:
UnrealNG` (any password); another SSID ends in `EE "wifi timeout"` after
10 s. The FTP server runs (`FTP_START`, the Wild Commander plugin `ZIFIFTP.WMF`;
[demo below](#demo-the-zifi-ftp-server-host-client-to-the-zx-sd-card)): the ESP
listens on the virtual network and turns every file command into VFS request
frames to the Z80 (`40..5E`), which the plugin answers from the SD card. The
Wild Commander updater (`WCU_START`, `WCUPDATE.WMF`; [demo below](#demo-update-wild-commander-from-github))
compares the WC files with GitHub and replaces the ones the user marks. Not
emulated (they answer as when the service cannot start, `EE "smb:not
emulated"` etc.): SMB, OTA, the online update. HTTPS (S3: port
443 and redirects to `https://`) is TLS done by the host with OpenSSL, the
server's certificate checked against the host's trust store and the name
(`SSL_CERT_FILE` / `SSL_CERT_DIR` point OpenSSL elsewhere); the program sees
plaintext, which TTD records. A build without OpenSSL (`-DUNREAL_HOST_TLS=OFF`)
answers `get:tls connect failed` (`native_session.https` says which). `native_session.activity` shows the command waiting for the
network, `native_session.file_bridge` the FTP sessions and the VFS traffic.

The module comes up already on that access point (as a module that booted from its saved `zifi.ini`):
the first password it is given is taken as its own, so the plugins that send `WIFI_INI` and wait only briefly (the
ESP-01S `NTPTIME.WMF`) get `83` at once.

**Weather** (`WEATHER_GET` `24`, S3 only; the Wild Commander screen savers
`WEATHER.WMF` / `WEATHER2.WMF`): the place comes from the `zifi.ini` the plugin
sent with `WIFI_INI` - `city:` (any spelling the Open-Meteo geocoder knows,
Cyrillic in UTF-8 / CP866 / CP1251) and optional `country:` (ISO code), or
`country:` + `zip:` (api.zippopotam.us). The module fetches the place, then the
forecast from api.open-meteo.com over plain HTTP (through the `zifi.ini` proxy
when set), each request up to three times (1 s, 2 s apart, none after 30 s);
the answer `A4` is the 90-byte record (place in CP866, current weather, sunrise
/ sunset, six days), or `[0][1]` after `EE "weather:city: not found"`,
`"weather:no city in ini"`, `"weather:meteo: http 503"`, ... The place is kept
until `city:` / `country:` / `zip:` change: `native_session.weather`
(`location`, `place`, `latitude`, `longitude`; `unknown` = not asked again).

**WebDAV** (ESP-01S only; `ZIFIWDAV.WMF`): `FTP_START` (`06`) also starts a
WebDAV server on guest port 80 (`FTP_STOP` stops both): one client at a time,
one request per connection, `OPTIONS PROPFIND GET HEAD PUT DELETE MKCOL`
(PROPFIND lists one level; PUT needs `Content-Length`), no authentication. The
files are the Z80's: every request becomes VFS requests the plugin answers
through the Wild Commander file API. From the host it needs a forward
(`[NETWORK] Forward=tcp:8080:80`), then e.g.
`curl -X PROPFIND -H "Depth: 1" http://127.0.0.1:8080/` or
`curl -T file.txt http://127.0.0.1:8080/file.txt`. State:
`native_session.webdav` (`running`, `client`, `request`, `last_request`,
`last_status`, byte counts).

Check by hand (the API on, PING, SYS_INFO):

```text
network set zifi=zifi-native
out #C7EF,#F1 ; in #C1EF            -- API on, ZiFi selected for #00EF..#BFEF
out #BFEF: 5A 04 00 00 04           -- PING
in  #C0EF -> 5 ; INIR from #BFEF: 5A F0 00 00 F0
```

```json
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
  "body": {"zifi": "zifi-native,s3"}}}
{"tool": "inspect_state", "arguments": {"target": "auto", "aspects": ["network"]}}
```

The `[zifi]` line then ends `[ZIFI-NATIVE S3 (s3-native-0.6.94), idle]`.

#### Demo: the ZiFi browser on the real internet

The new `zifi.spg` (a catalog browser: vtrd.in, zxart.ee, hype) fetches a ZX picture and a game from the
internet and saves the game to the SD card. Results of every program and firmware:
[TODO.md, Z5 results](../../docs/inprogress/2026-10-02-tsconf-zifi/TODO.md#z5-results-2026-10-04).

1. An SD folder: Wild Commander (`boot.$C`, `WC/` from
   `testdata/machines/tsconf/wildcommander/wc-improved-v1.11i/`), and a folder `zifi/` with
   [`zifi.spg`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/ZiFi%20SPG/build/zifi.spg)
   and `zifi.ini`:

   ```text
   SSID: UnrealNG
   password: zx
   time: +3
   ```

2. Machine TS-Conf (`TSL`), `network set zifi=zifi-native,s3` (`[NETWORK] ZiFi=ZIFI-NATIVE,S3`), the folder
   into `sd.zc`, reset: Wild Commander comes up. (HackerVBI's original
   [`zifi.spg`](https://github.com/HackerVBI/ZiFi/tree/master/_Current_version_executable) does the same with
   `zifi=at`.)
3. Start a TTD recording with a history limit (`POST /ttd/start`, then `/ttd/history-limit {"frames":3000}`).
4. In WC: cursor to `zifi`, Enter, cursor to `zifi.spg`, Enter. The console (bottom) says "HTTP test OK, server
   code 200", "Clock set from NTP", "Startup finished, menu active".
5. The program is driven by the mouse (in unreal-qt: capture the mouse in the window). The menu is the top bar
   (320 x 240 pointer space): DOWNLOADS (x 88-167, y 0-15), GRAPHICS (x 88-167, y 16-31), MUSIC / PRESS
   (x 184-231, y 0-15 / 16-31). List items are 16 pixels high from y = 32.
   - GRAPHICS, then "Most popular" (second item): the zxart.ee list; click an entry: the picture (for example
     "Baking Soda by Grongy") fills the screen. A key returns.
   - DOWNLOADS, "Games: vtrd.in", the second entry: the game is downloaded through `zifi.vtrd.in` and saved as
     `zifi/downloads/<date>/<name>.scl`.
   - PRESS, "Hype": the newest hype.retroscene.org articles.
6. Evidence: `inspect_state network` (`zifi.esp.exchanges`: `14` HTTP GET -> `94`, `12` NET_RECV -> `92`;
   `virtual_network.recent_activity`: `connect` / `connected` to the site), `POST /ttd/dump`, and
   `POST /media/sd.zc/export {"path":"card.img"}` for the saved file (a folder is never written:
   `mdir -i card.img ::/zifi/downloads`).

Scripted (WebAPI): the pointer is the program's own position, so move it with small `mouse/glide` steps and wait
until a glide is done before the next one (the browser keeps the position in its variables; `memory/find`
`01 DF FB ED 78` finds its mouse routine, the position follows it). The main screen switches video modes per
line: if a screenshot shows only the bottom bar, read the text page instead (`GET /memory/ram/216/0?len=16384`,
256 bytes per row, characters in the first 128, cp866).

```json
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
  "body": {"com_port": "tcp:127.0.0.1:2323"}}}
```

CLI `network set com_port=loopback` (or `network set card=zxwifi zx_wifi=espnet` on a Pentagon), Lua `network_configure{com_port="serial:COM3"}`,
Python `emu.network_configure(com_port="tcp:127.0.0.1:2323")`. The state is
`com_port` in `inspect_state network` (registers, FIFO levels, peer, link
`phase` and `error`, bytes). A machine reset keeps the link. `settings` there
shows every setting in force and `host_serial_devices` what `serial:` can
open. In the Qt UI: Tools > Network.

NedoOS: `cuart` (`bin/cuart.com`) is a terminal for the port; `ini/espcom.ini`
`comType = 0` on the ZX-Evo (RTS pulses), `2` for a ZX-WiFi. The first key
after start closes its hello box; F1..F10 pick the divisor.

The ESP modules sit on the virtual network: joined to the access point
`UnrealNG` with a DHCP lease (10.0.2.15 first), DNS through the virtual
network, every answer journaled. `EspChip=ESP32` (8 ESPNET sockets, AT 2.x
answers), `ESP8266` (4 sockets, NonOS AT 1.7.4), `ESP8266-AT221` or
`ESP8266-AT222` (Espressif ESP-AT 2.2.1 / 2.2.2 for the ESP8266: `AT+SYSSTORE`
only on 2.2.2, passive receive refused on 2.2.1, `+PING:` / `+CIPRECVDATA:<len>,`
reply forms, `CWMODE=1,0`, `CWLAPOPT`, `CIPDNS`, `SYSLOG`, `CIPTCPOPT`). The
Sprinter's SprinterESP card (ISA slot, [sprinter-network.md](../machines/sprinter-network.md))
takes an ESP8266 build from `EspChip`, ESP-AT 2.2.2 otherwise. `com_port.recent_exchanges`
in the network state lists the last requests and replies (ESPNET frames by
name, AT lines as text) - the first place to look when a program and the
module disagree. Details: [reference-esp-modules.md](../../docs/inprogress/2026-09-30-nedoos-integration/reference-esp-modules.md).

NedoOS with an ESP module: boot `sd_bootesp.$C` for the kernel driver (ESPNET;
`ini/network.ini currentNetwork=0`), or set `currentNetwork=1` (AT) / `2`
(userland ESPNET) for the C apps (zxdb, gopher, girc, time2, ...).

#### Demo: the ZiFi FTP server (host client to the ZX SD card)

The ESP is the server: a PC's FTP client logs in, and the plugin in Wild Commander serves the SD card through
the ESP's VFS requests. Both firmwares: S3 (`s3-native-0.6.94`, plugin `ZIFIFTP.WMF` v0.15: three sessions,
passive ports 2122-2124, `PORT` / `EPRT`, `MLSD` / `MDTM` / `MFMT`, 16 KiB VFS windows) and ESP-01S
(`native-0.2.2`, plugin v0.11: one session, passive port 2122, 512-byte VFS blocks). Plugins:
[ZiFi-ESP32-S3-Zero `FTP Server/`](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/tree/main/FTP%20Server)
and [ZiFi-ESP-01S-Native-C-Project `FTP Server/`](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/tree/main/FTP%20Server).

1. The SD folder of the browser demo (Wild Commander, `zifi/zifi.ini`) with the firmware's `ZIFIFTP.WMF` in `WC/`
   (listed under `[PLUGINS]` in `WC/wc.ini`), and a file to download, for example `TEST.BIN`.
2. TS-Conf, the ZiFi module and a host port for the guest's port 21 (ports from 1024 up, like the passive
   2122-2124, are reachable on the same host port without a rule):

   ```json
   {"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
     "body": {"zifi": "zifi-native,s3", "forward": "tcp:2121:21", "host_access": true}}}
   ```

   (`zifi-native,esp01s` for the ESP-01S and its v0.11 plugin.) Insert the folder into `sd.zc`, reset: WC.
3. TTD with a history limit: `POST /ttd/start`, `POST /ttd/history-limit {"frames":3000}`.
4. In WC: F10 (RUN PLUGIN), cursor to "ZiFi FTP Server v0.15" (the tenth line), Enter. The plugin's window says
   `Status : Wi-Fi [################] 100%` (S3) or `Listening` (ESP-01S), `IP : 10.0.2.15`, `Port : 21`.
   `inspect_state network`: `zifi.esp.native_session.file_bridge.ftp` `running: true`, `host_port: 2121`
   (`host_port_note` says when a rule is missing).
5. From the host, user `zx`, password `zx` (the plugin's defaults):

   ```bash
   python3 - <<'PY'
   import ftplib, io
   ftp = ftplib.FTP(); ftp.connect("127.0.0.1", 2121); ftp.login("zx", "zx")
   ftp.retrlines("LIST")                      # the SD card's root through VFS READDIR
   buf = io.BytesIO(); ftp.retrbinary("RETR TEST.BIN", buf.write)    # download
   ftp.storbinary("STOR UP.BIN", io.BytesIO(open("upload.bin", "rb").read()))   # upload
   ftp.set_pasv(False); ftp.retrlines("LIST")   # active mode: the ESP connects back to 127.0.0.1
   ftp.quit()
   PY
   ```

   Python's ftplib takes the passive data address from the control connection (the 227 reply names the guest's
   10.0.2.15); curl needs `--ftp-skip-pasv-ip`, or use EPSV.
6. Esc in the plugin stops the server (`FTP_STOP`); WC re-reads its panels and shows the uploaded file. Check it on
   the card: `POST /media/sd.zc/export {"path":"card.img"}`, then `mcopy -i card.img ::/UP.BIN .` (a folder is
   never written).

Measured (2026-10-04, real time with TTD on): S3 RETR 100 000 bytes 8.8 KB/s, STOR 70 000 bytes 9.2 KB/s;
ESP-01S RETR 6.0 KB/s, STOR 2.6 KB/s; byte-exact both ways, passive and active, the ZiFi ring never overflowed.
While the ESP waits for the Z80's VFS answer the UART serves only PING and SYS_RESET (other commands are lost, as
on the firmware: `file_bridge.vfs.dropped_while_waiting`).

#### Demo: update Wild Commander from GitHub

`WCUPDATE.WMF` (S3, from [ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero)) asks the
ESP to compare the SD card's Wild Commander with
[Wild-Commander-Improved](https://github.com/andrewinsidelazarev/Wild-Commander-Improved) (`main`, folder `exe`):
the ESP reads every file through the plugin (VFS) and hashes it the way git does, fetches the list from
api.github.com over HTTPS (TLS by the host), and writes each marked file through a checked copy.

1. The SD folder of the FTP demo with `WCUPDATE.WMF` in `WC/` (and in `[PLUGINS]` of `WC/wc.ini`); TS-Conf with
   `{"zifi": "zifi-native,s3", "host_access": true}`; insert the folder into `sd.zc`, reset: WC.
2. TTD with a history limit: `POST /ttd/start`, `POST /ttd/history-limit {"frames":3000}`.
3. F10, cursor to "ZiFi WC Update v1.0", Enter. After a few seconds: "N to update from GitHub f06f9c5" and the
   list: `same`, `DIFFERS`, `NEW`, `SD only` (the card has it, GitHub not), `kept` (a protected file, `WC/wc.ini`:
   never replaced). `inspect_state network`: `zifi.esp.native_session.file_bridge.wc_update` (`running`,
   `activity`, `state`, `commit`, `files`).
4. Space marks a line, A marks all, Enter updates the marked files: each line turns `UPDATED` (or `FAILED` with the
   reason in the state line); "- restart WC" when WC's own files changed. Esc leaves (`WCU_STOP`).
5. Check on the card: `POST /media/sd.zc/export {"path":"card.img"}`, `mcopy -i card.img ::/WC_todo.txt .`, compare
   with `https://raw.githubusercontent.com/andrewinsidelazarev/Wild-Commander-Improved/<commit>/exe/WC_todo.txt`.

Measured (2026-10-04): the check of 45 entries in a few seconds; a 2 KB file updated in 2.6 s (download, copy
written and read back, MOVE, read back); all four updated files byte-exact. GitHub's API allows 60 unauthenticated
requests an hour per address: more gives "GitHub API limit, retry later" (each check makes two).

### The Hayes modem

`MODEM` is a peer like the others: it fits on every serial port (ZX-Evo / TS-Conf COM port, ZX-WiFi, ATM2IOESP,
the ATM Turbo 2+ controller's RS-232, the Sprinter's ISA modem card and SprinterSerial). There is no telephone
network: a number is a host endpoint. A dialed string with letters, `.` or `:` is a host name or address
(`ATDT bbs.example.org:2323`, `ATDT 192.168.1.20`); a string of digits (`-`, `(`, `)`, spaces and the pause
modifiers ignored) is looked up in the phone book (`ModemPhonebook=`, runtime `modem_phonebook`); an unknown number
gets `NO CARRIER`. Commands: `E Q V X` (results: verbose `CR LF text CR LF` or numeric), `Sn=v` / `Sn?` (S0 rings
to answer, S2 escape character, S3 / S4 / S5 CR LF BS, S7 seconds to wait for the carrier, S12 escape guard in
1/50 s), `I0-I4`, `Z`, `&F`, `&C` (DCD: 0 always on, 1 follows the call - default), `&D` (DTR dropping: 0 ignored,
1 command mode, 2 hang up - default, 3 reset), `&S`, `A/`; init-string settings (`L M &K &Q &W \N %C +...`) are
accepted. CTS is always on (the modem buffers), DSR on (`&S0`). Everything from the host is journaled: a TTD
replay repeats a call byte for byte with no host. The network state shows it as `modem` (in `com`, or in the
slot row): `mode` (command / dialing / online / online_command), `lines` (CTS, DSR, DCD, RI, the ZX's DTR),
`call` (dialed, link phase, remote, ringing, held bytes), `last_result`, `settings`, `phonebook`, `counters` and a
`journal` of commands and results.

Worked example (a ZX-Evo, a telnet BBS on the host's port 2323): `network set com_port=modem
modem_phonebook=1=127.0.0.1:2323`; in a terminal program at any rate: `ATZ` -> `OK`, `ATDT1` -> `CONNECT 115200`,
the BBS's text; `+++` -> `OK`; `ATH` -> `OK`.

A quick check without software: `com_port=loopback`, then from Z80 code
`LCR=3` (`#FBEF`), `MCR=2` (RTS, `#FCEF`), a byte to `#F8EF`, wait for LSR
bit 0 (`#FDEF`), read `#F8EF`: the same byte.

## NedoOS setup

| Kernel | Adapter | Notes |
|:--|:--|:--|
| `sd_boot.$C` (NedoOS release, the ZX-Evo W5300 kernel) | ZXNETUSB | `autoexec.bat` runs `wizcfg.com`: it finds the card, gets a DHCP lease, programs the chip |
| `sd_bootesp.$C` | ESP on the COM port | the ESPNET module (`com_port=espnet`) answers; see the COM port section above |

`bin/net.ini` `DHCP 1` (the default) takes the lease from the virtual
network; a static `IP=` / `GW=` / `MASK=` works too (the virtual network does
not route by the guest's address). Without the card, every NedoOS network
call hangs the whole OS (a NedoOS bug:
[nedoos-bugs.md](../../docs/inprogress/2026-09-30-nedoos-integration/nedoos-bugs.md) B-2).

## MCP (preferred)

```json
{"tool": "inspect_state", "arguments": {"target": "auto", "aspects": ["network"]}}
```

Answer: `card` (ports `#83AB/#82AB/#81AB`, W5300 running or held in reset,
`w5300_int` / `int_to_z80` (the chip's INT, the card's /INT to the Z80),
the chip's mac / ip / gateway / mask, per socket mode / state / ports /
buffers) and `virtual_network` (leases, sockets, guest servers, counters,
`recent_activity`: the last 64 socket actions).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
curl -s "$BASE/emulator/$EMU_ID/state/network" | jq '.card.ip, .virtual_network.dhcp_leases, .virtual_network.recent_activity[-5:]'
```

404 with the reason when no adapter is fitted.

## CLI, Lua, Python

```text
network                      # CLI (alias net)
net = network_state()        -- Lua
emu.network_state()          # Python
```

## Checking a network program

1. Fit the card, boot NedoOS (ZX-Evo: insert the SD card folder, reset, `5`).
2. `inspect_state network`: `card.ip` = `10.0.2.15` after `wizcfg` ran.
3. Run the program; `recent_activity` shows `dns`, `connect`, `connected` /
   `connect-failed` (with `status`), `peer-closed`, `close`.
4. A connect that fails with `timeout` while the host itself can reach the
   address: check the host's firewall (Little Snitch and similar block a new
   emulator build until allowed).
5. For a hermetic check, point the name at a local server:
   `Hosts=name=127.0.0.1` and run the server on the host.
