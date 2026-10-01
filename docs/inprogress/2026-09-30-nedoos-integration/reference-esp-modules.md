# Reference: emulated ESP modules (ESPNET and AT firmware)

What the COM port's ESP modules (network TDD step N3,
[tdd-network.md](tdd-network.md) §7.2) are built on: NedoOS's binary ESPNET
protocol (firmware 1.27, both host drivers), the Espressif AT dialect that ZX
software actually sends, the ZiFi API (TS-Conf, a later step) and other
emulators' ESP code. Research of 2026-10-01; every fact carries its source,
`[inferred]` marks conclusions. The UART these modules sit behind:
[reference-evo-com-port.md](reference-evo-com-port.md).

## Part 1. ESPNET protocol (NedoOS ESPNET firmware 1.27)

Research date 2026-10-01. Read-only analysis of the NedoOS sources. Every fact has a
`file:line` citation; `[inferred]` marks conclusions not stated directly in a source.

### 0. Source map and citation prefixes

| Prefix | Upstream | What |
|---|---|---|
| `FW/` | [NedoOS src/kapps/common/espnet/](https://github.com/alfishe/NedoOS/tree/main/src/kapps/common/espnet) | Firmware 1.27 (Arduino sketch) |
| `SDK` | [src/_sdk/espnet.asm](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/espnet.asm) (CP866) | Z80 asm driver; the kernel includes it with `DEFINE ESPNET_KERNEL` |
| `KESP` | [src/kernel/espnet.asm](https://github.com/alfishe/NedoOS/blob/main/src/kernel/espnet.asm) | Kernel BDOS glue (INETDRV==2) |
| `KBSS` | [src/kernel/espnet_bss.asm](https://github.com/alfishe/NedoOS/blob/main/src/kernel/espnet_bss.asm) | Kernel BSS |
| `SYSK` | [src/kernel/syskrnl.asm](https://github.com/alfishe/NedoOS/blob/main/src/kernel/syskrnl.asm) | Kernel helpers (getinfo, pktMax, busy lock) |
| `SYSB` | [src/kernel/sysbdos.asm](https://github.com/alfishe/NedoOS/blob/main/src/kernel/sysbdos.asm) | BDOS entry points |
| `SYSH` | [src/_sdk/sys_h.asm](https://github.com/alfishe/NedoOS/blob/main/src/_sdk/sys_h.asm) | OS_* macros (L sub-functions) |
| `CLIB` | [src/kapps/common/espnet.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet.c) | IAR C userland driver (OS_ESP*) |
| `CNET` | [src/kapps/common/espnet-net.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/espnet-net.c) | C wrappers (EspOpenSock, EspConnect, ...) |
| `ECOM` | [src/kapps/common/esp-com.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/common/esp-com.c) | UART primitives per comType |
| `ESPCFG`, `ENET` | [src/kapps/espcfg/main.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/espcfg/main.c), [src/kapps/enet/main.c](https://github.com/alfishe/NedoOS/blob/main/src/kapps/enet/main.c) | Kernel-path config tools |
| `AVR` | [pentevo avr/baseconf/trunk/src/rs232.c](https://github.com/alfishe/pentevo/blob/master/avr/baseconf/trunk/src/rs232.c) | ZX-Evo AVR 16550 emulation (BaseConf) |
| `REL/` | [NedoOS release/](https://github.com/alfishe/NedoOS/tree/main/release) | Shipped binaries and ini files |

Line numbers are for the NedoOS Subversion working copy of 2026-09-30 (the
GitHub mirror above carries the same files: `diff -rq` showed no difference
for `kapps/common/espnet`, the kernel and SDK drivers).

Two independent host drivers speak the same wire protocol:

* **Kernel driver** (`sd_bootesp.$C`, `osatm2*esp.$C`): `INETDRV=0x02` ([`src/kernel/build_kernel_evo_esp.bat:9`](https://github.com/alfishe/NedoOS/blob/main/src/kernel/build_kernel_evo_esp.bat)),
  `bdospg2.asm:171-172` includes `KESP`, which includes `SDK` with `ESPNET_KERNEL` (`KESP:9-11`). All WIZNET-API apps
  (browser, 3ws, scrnet, enet, espcfg, anything using `OS_NETSOCKET`/`OS_WIZNETREAD`) go through it.
* **Userland C driver** `CLIB` (OS_ESP*), linked into C apps when `/ini/network.ini currentNetwork=2`
  (zxdb, gopher, girc, svnesp, time2, zifi, getpic, ...; `FW/README.txt:192-195` (CP866), `CLIB:1-7`). It programs the
  UART itself (`CLIB:610-628`). Asm apps can also link `SDK` without `ESPNET_KERNEL` (userland flavor, `SYSH:795-831`).

The firmware only sees frames; it cannot tell the drivers apart. Each driver keeps its own `seq` counter.

---

### 1. Framing

### 1.1 Byte stream

| Item | Value | Source |
|---|---|---|
| Line | 115200 8N1, hardware RTS/CTS required | `FW/PROTOCOL.md:9`, `FW/pins.h:6`, `FW/host_uart.cpp:151-154` |
| SOF | `0xA5`, sent once, unescaped | `FW/protocol.h:20`, `FW/codec.cpp:138` |
| Stuffing / escaping | none; `0x00`, `0xA5`, `0xFF` are legal in header and payload | `FW/protocol.h:8-11`, `FW/PROTOCOL.md:57-63` |
| Endianness | all multi-byte header fields little-endian; `sockaddr_in` port is **big-endian** | `FW/protocol.h:5-6`, `FW/codec.cpp:19-44`, `FW/sockets.cpp:38-58` |
| Max payload | 2048 (`ESPNET_MAX_PAYLOAD`) | `FW/protocol.h:23` |
| Frame end | header + LEN bytes (+1 CRC byte only if request cmd bit7 set) | `FW/codec.cpp:88-111` |

### 1.2 Request (host -> ESP): SOF + 6-byte header + payload

| Off | Size | Field | Notes | Source |
|---|---|---|---|---|
| 0 | 1 | cmd | bit7 = optional CRC flag; NedoOS never sets it | `FW/protocol.h:27,59-60`, `SDK:31-32` |
| 1 | 1 | sock | `0xFF` (`ESPNET_SOCK_NONE`) when unused | `FW/protocol.h:22,28` |
| 2 | 1 | arg | SOCKET: proto; SHUTDOWN: 0/1; UART: 0 GET / 1 SET | `FW/sockets.cpp:321,368`, `FW/wifi_cmd.cpp:327` |
| 3 | 1 | seq | echoed in reply | `FW/protocol.h:30` |
| 4 | 2 | len | payload length, LE | `FW/protocol.h:31` |
| 6 | len | payload | | `FW/protocol.h:32` |
| 6+len | 1 | crc8 | only if cmd bit7; XOR of header+payload | `FW/codec.cpp:11-17,95-109` |

### 1.3 Response (ESP -> host): SOF + 8-byte header + payload

| Off | Size | Field | Notes | Source |
|---|---|---|---|---|
| 0 | 1 | cmd | request cmd (without bit7 unless the request had CRC); unknown cmd echoed as `cmd & 0x7F` | `FW/sockets.cpp:765,790-791`, `FW/codec.cpp:152-158` |
| 1 | 1 | sock | see per-command table (new id for SOCKET/ACCEPT; `0xFF` for Wi-Fi/DNS/INFO/UART) | `FW/sockets.cpp:301-316`, `FW/wifi_cmd.cpp:153-156` |
| 2 | 1 | status | 0 or errno (§2.2) | `FW/protocol.h:36` |
| 3 | 1 | seq | copy of request seq | `FW/protocol.h:37` |
| 4 | 2 | result | LE; byte count (READ/WRITE/ECHO), AP count (SCAN), else 0 | `FW/protocol.h:38`, `FW/PROTOCOL.md:92` |
| 6 | 2 | len | payload length, LE | `FW/protocol.h:39` |
| 8 | len | payload | error replies always have len 0 (`rsp_err`) | `FW/sockets.cpp:313-316`, `FW/wifi_cmd.cpp:153-156` |

The firmware sends a reply only when `ESPNET_RSP_HDR <= n <= sizeof(s_rsp)-1` (`FW/espnet.ino:70-73`); every
handler returns at least a header, so every complete request gets exactly one reply.

### 1.4 CRC (optional, unused by NedoOS)

* CRC-8 = XOR of all header+payload bytes (not SOF, not CRC) (`FW/codec.cpp:11-17`, `FW/PROTOCOL.md:108-111`).
* A request with bit7 gets a reply with bit7 and a trailing CRC (`FW/codec.cpp:152-158`). Bad CRC: frame dropped, no reply (`FW/codec.cpp:103-106`, `FW/espnet.ino:58-62`).
* INFO caps byte bit0 = `ESPNET_CAP_CRC` ("can do", not "is on") (`FW/wifi_cmd.cpp:171`, `FW/PROTOCOL.md:106-107`).
* NedoOS host never sets bit7 and compares cmd as-is (`SDK:31-32`, `FW/protocol.h:12-14`). An emulator may implement CRC for completeness; NedoOS does not exercise it.

### 1.5 Receiver rules (ESP side) — `FW/codec.cpp:75-113`

1. Outside a frame every byte except `0xA5` is ignored (`:77-84`).
2. After SOF, collect bytes; when 6 header bytes are in, read `len`; `len > 2048` → reset, return -1, **no reply**, counted as resync (`:88-93`, `FW/espnet.ino:58-62`).
3. When `6+len(+1)` bytes are in, the frame is dispatched (`:99-111`). Inside a frame `0xA5` is data (no mid-frame resync, `FW/PROTOCOL.md:61-63`).
4. **No inter-byte timeout** on the ESP: a partial frame waits indefinitely for more bytes [inferred: no timer anywhere in `FW/codec.cpp`].
5. Requests are processed strictly one at a time inside `loop()`; the reply is fully written to the UART before the next byte is parsed (`FW/espnet.ino:52-78`).

### 1.6 Receiver rules (host side)

* SOF search: read/discard bytes until `0xA5` (`SDK:1317-1466`, `CLIB:209-268`). Garbage before a reply is harmless.
* Then 8 header bytes, check `cmd == request cmd` and `seq == g_seq` (`SDK:1158-1172`, `CLIB:447-466`; also `SDK:964-971`, `CLIB:602-603`). Mismatch → `rx_drain` + errno 4 (INTR).
* Payload size limits on host:
  * READ path: `len <= 2048` else INTR (`SDK:1173-1183`, `CLIB:467-472`).
  * Generic `xfer` reply (non READ/SCAN/ECHO): kernel accepts `len` 0..255 or exactly 256 (`SDK:1110-1138`) into a buffer of `ESPNET_RSP_MAX` = **64** in the kernel (`SDK:23`, `KBSS:55`), 256 in userland (`SDK:26`, `CLIB:23-25`). So replies to SOCKET/CONNECT/... must carry no payload, INFO 53, WIFI_STATUS 45, UART 8, GETDNS/DNSRESOLVE 4 — exactly what firmware sends.
* **rx_drain** (resync): RTS pulsed while polling; stop after 80 consecutive empty polls or 1000 polls total (`SDK:1252-1288`, `CLIB:427-444`).
* Per-byte timeout inside a frame: `esp_spin`=65000 empty polls (type 0 pulses RTS on every empty poll) (`SDK:123-125,1505-1541`, `CLIB:281-302,617`). Type 1 header uses 800 (`SDK:1142-1155`).
* `recv_rsp` keeps waiting after SOF until the per-byte spin expires; a mid-frame stall does not abort (`FW/PROTOCOL.md:251-253`).

### 1.7 SOF wait budgets (how long the ESP may take to answer)

Userland: `sof_ticks` in 50 Hz timer ticks (`CLIB:204-207,223,262`); kernel: `esp_deadline` decremented once per 256 empty polls (`SDK:1353-1355,1420-1431`), no YIELD.

| Command | Userland C (`CLIB:589-596,955`) | Kernel / asm (`SDK:913-948`, `SDK:69-73`) |
|---|---|---|
| DNSRESOLVE, CONNECT, WIFI_CONNECT | 3000 ticks = 60 s | `SOF_TICKS_LONG` 3000 x256 polls |
| WIFI_SCAN | 60 s (`CLIB:955`) | LONG (`SDK:579-580`) |
| INFO | 500 ticks = 10 s | LONG (`SDK:926-927`) |
| WRITE (TCP) | 10 s | LONG (types 0/2/3); 16 for type 1 (`SDK:928-935`) |
| ACCEPT | 10 s | `SOF_TICKS_UDP` 75 x256 polls (`SDK:922-923,942-945`) |
| UDP READ / UDP WRITE | 10 s | 75 (`SDK:724-725,835-836`) |
| everything else | 10 s | 500 (`SDK:69,946-948`) |

[inferred] On Evo type 0 an empty poll is one LSR read plus two MCR writes through the AVR (~52 T wait each in the
current emulator model, `unreal-ng core/src/emulator/io/serial/uart16550.cpp:30-31`), so 500x256 polls is several
seconds, LONG roughly a minute, 75 about a second. An emulated ESP that replies within ~100 ms is always inside budget.
Firmware's own worst cases: CONNECT 8 s, retried once (`FW/protocol.h:133`, `FW/sockets.cpp:413-442`); TCP WRITE 3 s
wait for sndbuf progress (`FW/protocol.h:135`, `FW/sockets.cpp:663-691`); DNS 25 s on ESP8266 (`FW/protocol.h:137`,
`FW/sockets.cpp:726-733`).

---

### 2. Commands

### 2.1 Command list (`FW/protocol.h:42-60`, `FW/PROTOCOL.md:117-147`)

| Code | Name | Handled in | Request sock / arg / payload | Response sock / result / payload |
|---|---|---|---|---|
| 0x01 | SOCKET | `FW/sockets.cpp:318-362` | sock any (host sends FF), arg=proto (1 TCP, 3 UDP), payload `family` (1 byte; if len 0 → AF_INET assumed) | sock=new id (FF on error), result 0, len 0 |
| 0x02 | SHUTDOWN | `:364-391` | sock id or FF (=close all), arg 0 abort / 1 graceful, len 0 | sock echoed, len 0 |
| 0x03 | CONNECT | `:393-452` | sock id, payload sockaddr 15 (family ignored) | sock echoed, len 0 |
| 0x04 | ACCEPT | `:527-552` | sock = listening id, len 0 | sock = **new** id, len 0 |
| 0x05 | BIND | `:454-477` | sock id, payload sockaddr 15 (only port used) | sock echoed |
| 0x06 | LISTEN | `:479-525` | sock id (must be TCP and BIND port != 0) | sock echoed |
| 0x07 | READ | `:554-612` | sock id, payload u16 maxlen (if len<2 → 2048) | TCP: result=n, payload n bytes; UDP: result=n, payload 15-byte sockaddr + n bytes |
| 0x08 | WRITE | `:614-697` | TCP: payload data; UDP: sockaddr 15 + data | result = bytes sent, len 0 |
| 0x09 | GETDNS | `:699-709` | len 0 | sock FF, payload 4-byte DNS server IP (`WiFi.dnsIP()`) |
| 0x0A | DNSRESOLVE | `:711-742` | payload hostname, no NUL, 1..64 bytes (host sends ≤63) | sock FF, payload 4-byte IP |
| 0x10 | INFO | `FW/wifi_cmd.cpp:158-180` | len 0 | sock FF, payload 53 bytes (§5.2) |
| 0x11 | WIFI_SCAN | `:182-254` | len 0 | sock FF, result=N (≤24), payload N×42 |
| 0x12 | WIFI_CONNECT | `:256-293` | payload ssid[33]+pass[65] = 98 | sock FF, len 0 (ACK only, join is async) |
| 0x13 | WIFI_DISC | `:295-304` | len 0 | sock FF, len 0 |
| 0x14 | WIFI_STATUS | `:306-322` | len 0 | sock FF, payload 45 bytes |
| 0x15 | UART | `:324-350` | arg 0 GET (len 0) / 1 SET (8-byte payload) | sock FF, payload 8 bytes |
| 0x7E | ECHO | `FW/sockets.cpp:744-756` | any payload ≤2048 | sock echoed, result=len, payload copy |
| other | — | `FW/sockets.cpp:790-791` | — | status 41 (PROTOTYPE), cmd=`cmd&0x7F`, sock echoed |

Dispatch order: `wifi_handle` first (0x10-0x15), then `sockets_handle` (`FW/espnet.ino:68-69`, `FW/wifi_cmd.cpp:352-377`).
Wi-Fi/INFO/UART ignore the request sock field and reply with sock `0xFF` (`FW/wifi_cmd.cpp:153-156,179`).

### 2.2 Status codes (`FW/protocol.h:68-80`; errno values from NedoOS api_net)

| errno | Name | Emitted when |
|---|---|---|
| 0 | OK | success |
| 4 | INTR | SOCKET with req_n<6 (cannot happen); CONNECT/BIND payload <15 (`FW/sockets.cpp:326-327,407-408,463-464`). Host also synthesizes 4 for timeouts/desync (`SDK:1187-1190`) |
| 23 | NFILE | SOCKET with no free slot after reaping dead TCP (`:336-342`); LISTEN `new` failed (`:517-518`) |
| 35 | EAGAIN | READ: no data on a live TCP / no UDP datagram / maxlen 0 (`:567-568,576-577,602`); WRITE: TCP sent 0 within 3 s, UDP endPacket fail (`:646-647,692-693`); ACCEPT: no free slot or no pending client (`:538-543`); SHUTDOWN arg1 on ESP32 with 0<sndbuf<1024 (`:377-385`). Kernel also returns 35 without wire traffic when the UART lock is held (`SYSK:1373-1390`) |
| 37 | ALREADY | CONNECT or LISTEN on a connected TCP slot (`:405-406,488-489`) |
| 38 | NOTSOCK | sock ≥ max or slot free (all socket cmds), CONNECT/LISTEN on non-TCP slot, LISTEN with local_port 0, ACCEPT on non-listening slot (`:164-167,375-376,403-404,485-491,534-535`) |
| 40 | EMSGSIZE | WRITE len 0 (TCP) / UDP payload <15 / UDP empty datagram (sent, then 40) (`:632-633,648-649,658-659`); WIFI_CONNECT payload <98; UART bad arg/len/baud (`FW/wifi_cmd.cpp:265-266,339-345`) |
| 41 | PROTOTYPE | SOCKET proto ICMP(2) or not 1/3 (`:332-335`); unknown cmd (`:790-791`) |
| 47 | AFNOSUPPORT | SOCKET family ≠ 2 (`:330-331`) |
| 53 | ECONNABORTED | UDP `begin()` failed (SOCKET, BIND, WRITE re-begin) (`:352-358,472-473,638-639`) |
| 54 | CONNRESET | defined, never emitted [grep: only `FW/protocol.h:78`] |
| 57 | NOTCONN | READ with empty buffer on a slot not in ESTABLISHED (closed, CLOSE_WAIT after drain, never connected, listening) (`:597-601`); WRITE on non-established TCP (`:654-655`) |
| 65 | HOSTUNREACH | CONNECT failed twice / Wi-Fi down; DNSRESOLVE fail or empty name; UDP beginPacket fail after re-begin (`:443-444,719-720,731-733,641-642`) |

### 2.3 Socket table semantics (`FW/sockets.cpp`)

* Slots: 8 on ESP32/ESP32-C3, 4 on ESP8266 (`FW/protocol.h:24-25`, `FW/pins.h:37,49,76`); ids 0..n-1, lowest free first (`:154-162`). INFO byte 3 reports the count (`FW/wifi_cmd.cpp:167`). The kernel owner table always has 8 entries and rejects sock ≥8 locally (`KESP:25-26,42-65`); on an ESP8266, ids 4..7 reach the firmware and get NOTSOCK.
* Slot states: FREE 0, TCP_IDLE 1 (after SOCKET TCP), TCP 2 (connected/accepted), UDP 3, LISTEN 4 (`:19-23`).
* Each TCP slot has a 2048-byte receive buffer filled from lwIP by `sockets_pump()` every loop and every 256 TX bytes (`:218-248`, `FW/codec.cpp:143-147`, `FW/espnet.ino:51`). Bytes stay buffered after FIN until READ drains them (`:229`). The rest stays in lwIP; TCP window throttles the server (`FW/PROTOCOL.md:226-228`).
* SOCKET UDP immediately does `udp.begin(0)` (ephemeral port) so `WRITE` works without BIND (`:350-361`); kernel never BINDs UDP (`KESP:185-187`, `SYSK:1273-1274`).
* SOCKET with no free slot first reaps TCP slots that are disconnected and have no buffered data (`:264-272,337-340`).
* BIND: stores `local_port` for any proto; for UDP: port 0 on an already-begun slot is a no-op; other ports do `stop()+begin(port)` (`:466-476`). TCP BIND does not open anything.
* LISTEN: requires TCP proto, not connected, `local_port != 0`; frees any other slot that has a server on the same port ("steal", for stale servers after a ZX reboot); creates `WiFiServer(port, backlog=max_socks)`; state LISTEN (`:479-525`).
* ACCEPT (1.27): needs a free slot **before** dequeuing a client, else EAGAIN; no pending client → EAGAIN; else new slot TCP, keepalive tuned, reply sock = new id. The listening slot keeps listening (`:527-552`).
* CONNECT: allowed on TCP_IDLE, or TCP not connected; `tcp.stop()` then blocking connect with 8 s timeout; on failure reap dead slots and retry once if Wi-Fi is connected; success → state TCP, rx buffer cleared, keepalive 15 s idle / 2 s × 4, NoDelay (`:393-452,103-128`).
* READ (TCP): pump, then if buffer empty: NOTCONN if state≠TCP or not ESTABLISHED (FIN seen via `recv(MSG_PEEK)==0` or tcpi_state≠4), else EAGAIN; otherwise copy `min(rx_len, maxlen)`; never returns 0 bytes with status 0 (`:596-611,65-101`, `FW/PROTOCOL.md:97`).
* READ (UDP): one datagram per call; payload = sockaddr(family 2, source port BE, source IP, 8 zeros) + `min(dgram, maxlen, 2033)` bytes; the rest of a longer datagram is discarded (`:573-594`).
* WRITE (TCP): must be ESTABLISHED; loops writing, waiting up to 3 s without progress; partial result is success; zero sent → EAGAIN (`:654-696`).
* WRITE (UDP): first 15 payload bytes = destination sockaddr; sends one datagram; result = data length (`:630-652`).
* SHUTDOWN sock 0xFF: free every slot (`:370-374`). Otherwise NOTSOCK if free/invalid; arg 1 on connected TCP: flush (ESP32 may return EAGAIN when 0<sndbuf<1024); then free the slot (UDP free delays 15 ms, listen 20 ms) (`:375-390,130-152`).
* GETDNS ignores sock; returns the STA DNS (0.0.0.0 when not connected) (`:699-709`).

### 2.4 sockaddr_in (15 bytes)

`family(1)=2, port_hi, port_lo, ip0, ip1, ip2, ip3, zero[8]` (`FW/sockets.cpp:38-58`, `FW/PROTOCOL.md:68-70`, `SYSH:723`).
Kernel forces family=AF_INET before CONNECT (`SYSK:1265-1272`); firmware ignores family on CONNECT/BIND anyway.

---

### 3. Host call sequences

### 3.1 Boot / probe / handshake

* There is **no handshake and no probe**. The ESP never sends unsolicited data (`FW/PROTOCOL.md:6`); NedoOS never waits for a banner.
* Kernel: the first BDOS net call runs `espk_ensure` → `esp_init`: default ports `#F8EF..#FFEF`, divider 1, comType 0 on Evo (1 on ATM2), program the 16550, RTS off. No bytes are sent (`KESP:28-39,135-136`, `SDK:103-135,2200-2237`).
* `esp_init` in the kernel skips the 0.5 s settle; userland `OS_ESPINIT` waits ~0.5 s after `uart_init` (`SDK:128-131`, `CLIB:610-628`).
* `espcfg.com` (if run): `OS_GETUART` → if config differs: `uart_init`, 0.5 s, `OS_SETUART`, 0.5 s → (non-silent) `OS_GETINFO` = wire **INFO** (`ESPCFG:373-399`). `SETUART`/`GETUART` (L=9/10) are local only, never on the wire (`KESP:266-334`).
* `KESP:309-312` intends to send `SHUTDOWN sock=0xFF` on the first SETUART after a ZX reboot, but `wiznet_open` calls `espk_ensure` first, which sets `esp_inited=1` (`KESP:135-136`, `SDK:126-127`), so the condition `esp_inited==0` (`KESP:293-308`) is never true. [inferred: the close-all is dead code in the svn kernel; the 1.24 kernel sent it on every SETUART — see §6.]
* Release default: `REL/bin/autoexec.bat` runs `wizcfg.com -S` (sets DNS via L=7 locally), not espcfg; `REL/ini/network.ini` has `currentNetwork=0`; `REL/ini/espcom.ini` has comType 0, divider 1, pktMax 1500. Without espcfg the kernel per-call cap is 192 (`SYSK:1275-1281`, `SDK:22`).
* So the first frame on the wire is whatever the first app sends: typically INFO (espcfg/enet) or SOCKET.

Example INFO (first command after init, seq=1):

```
host: A5 10 FF 00 01 00 00
esp : A5 10 FF 00 01 00 00 35 00
      01 1B 20 08 02 C4 00 01  C0 A8 01 64  24 6F 28 AA BB CC
      'u' 'n' 'r' 'e' 'a' 'l' 00 ... (33 bytes SSID)  A0 86
```
(ver 1.27, chip 32=ESP32, 8 sockets, wifi 2 GOT_IP, rssi -60, sockmask 0, caps 1, IP 192.168.1.100, MAC, SSID, heap 34464.)

### 3.2 TCP client (kernel path; userland C is the same on the wire except READ pipelining)

Kernel BDOS: `OS_NETSOCKET` L=1 D=2 E=1 → `esp_socket` (`KESP:171-188`, `SDK:140-160`); CONNECT L=3 (`KESP:205-212`);
`OS_WIZNETWRITE`/`READ` (`KESP:340-395`); close via `OS_WIZNETCLOSE` → SHUTDOWN (`KESP:336-338`).

```
SOCKET TCP     host A5 01 FF 01 02 01 00 02            esp A5 01 00 00 02 00 00 00 00      (sock 0)
CONNECT        host A5 03 00 00 03 0F 00 02 00 50 5D B8 D8 22 00 00 00 00 00 00 00 00
                                                        esp A5 03 00 00 03 00 00 00 00
WRITE 18 bytes host A5 08 00 00 04 12 00 'GET / HTTP/1.0\r\n\r\n'
                                                        esp A5 08 00 00 04 12 00 00 00      (result 18)
READ max 192   host A5 07 00 00 05 02 00 C0 00          esp A5 07 00 23 05 00 00 00 00      (EAGAIN 35)
READ max 192   host A5 07 00 00 06 02 00 C0 00          esp A5 07 00 00 06 C0 00 C0 00 <192 bytes>
...                                                      esp A5 07 00 39 xx 00 00 00 00      (NOTCONN 57 after FIN+drain)
SHUTDOWN       host A5 02 00 00 08 00 00                esp A5 02 00 00 08 00 00 00 00
```

* Kernel READ: one READ per BDOS call, no pipelining; `maxlen = min(user size, pktMax)` (`SDK:224-326`, `SDK:328-332`, `SYSK:1275-1281`).
* Kernel WRITE: chunks of `min(left, pktMax)`; on EAGAIN retries the same chunk up to 8 times; stops at the first short write (`SDK:371-458`).
* Userland `OS_ESPREAD` **pipelines**: after any READ result (data, EAGAIN, or empty) it immediately sends the next READ and sets `g_armed` (`CLIB:686-751`). The next call collects that reply instead of sending; any other command first reads and discards the pending reply (`drop_armed`, `CLIB:552-575,588`). Emulation consequence: a READ request may sit answered but unread for a long time (host busy writing a file); the ESP reply waits in the flow-controlled TX path (§4) and data consumed into that reply is gone even if the host later discards it [inferred from `CLIB:555-575`].
* C wrappers retry: `EspConnect` 3 tries 1 s apart (`CNET:45-59`); `EspDnsResolve` 1 s pause, then 4 tries 1 s apart (`CNET:184-220`); `EspGetDns` 3 tries (`CNET:222-245`).

### 3.3 UDP

Kernel/WIZNET apps (browser DNS, network.c `dnsResolve`, time) do DNS themselves over UDP to the kernel's static DNS
(`8.8.4.4` default, or set by L=7) — the kernel never sends GETDNS on the wire (`KESP:23-24,241-264`, `kapps/common/network.c:415-510`).

```
SOCKET UDP   host A5 01 FF 03 01 01 00 02                    esp A5 01 01 00 01 00 00 00 00   (sock 1, already begin(0))
WRITE        host A5 08 01 00 02 LL LL 02 00 35 08 08 04 04 00x8 <dns query>
                                                              esp A5 08 01 00 02 qq qq 00 00   (result = query length)
READ         host A5 07 01 00 03 02 00 mm mm                  esp A5 07 01 23 03 00 00 00 00   (EAGAIN until reply)
READ         host A5 07 01 00 04 02 00 mm mm                  esp A5 07 01 00 04 nn nn (15+nn) 02 00 35 08 08 04 04 00x8 <nn bytes>
SHUTDOWN     host A5 02 01 00 05 00 00                        esp A5 02 01 00 05 00 00 00 00
```
* Kernel UDP read: `maxlen = min(size, pktMax-15)`; sockaddr copied back to DE unless DE==IX (`KESP:340-368`, `SYSK:1245-1264`, `SYSK:1282-1289`).
* UDP WRITE larger than `pktMax-15` (kernel) or 2033 (userland) is rejected locally with EMSGSIZE (`SDK:807-832`, `CLIB:860-862`).
* Userland UDP: `OS_ESPWRITE_UDP` / `OS_ESPREAD_UDP` (`CLIB:793-880`); e.g. time2 NTP (`kapps/time2/main.c:241-285`).

### 3.4 DNS on the ESP (userland only)

```
host A5 0A FF 00 07 0B 00 'example.com'        (no NUL, len = strlen, ≤63: SDK:485-511, CLIB:895-910)
esp  A5 0A FF 00 07 00 00 04 00 5D B8 D8 22    (or A5 0A FF 41 07 00 00 00 00 = HOSTUNREACH)
```
GETDNS (userland `EspGetDns`, girc/svnesp startup): `A5 09 FF 00 s 00 00` → `A5 09 FF 00 s 00 00 04 00 a b c d`.

### 3.5 Listen / accept (3ws, scrnet; `scrnet/main.asm:2913-2923,184,257`)

```
SOCKET TCP   host A5 01 FF 01 s 01 00 02                     esp A5 01 00 00 s 00 00 00 00      (sock 0)
BIND :4444   host A5 05 00 00 s 0F 00 02 11 5C 00 00 00 00 00x8
                                                              esp A5 05 00 00 s 00 00 00 00
LISTEN       host A5 06 00 00 s 00 00                        esp A5 06 00 00 s 00 00 00 00
ACCEPT poll  host A5 04 00 00 s 00 00                        esp A5 04 00 23 s 00 00 00 00      (EAGAIN, no client)
ACCEPT       host A5 04 00 00 s 00 00                        esp A5 04 01 00 s 00 00 00 00      (new sock 1)
READ/WRITE on sock 1 as in §3.2; SHUTDOWN sock 1 when done; sock 0 keeps listening.
```
Kernel ACCEPT records the new id as owned by the calling task (`KESP:229-239`). Kernel ACCEPT waits only the short
budget (`SDK:940-945`); the app polls.

### 3.6 Wi-Fi status / scan / connect (kernel path, enet; `SYSH:758-782`, `KESP:397-422`, `ENET:307-330,719-845`)

* enet start: `OS_GETUART` (local) → `OS_GETINFO` (INFO) → `OS_WIFISTATUS` (WIFI_STATUS) (`ENET:958-965,307-324`).
* While waiting for a link (`g_watch`), enet repeats INFO + WIFI_STATUS every 16 main-loop passes until `WSTAT flags & HASIP` (`ENET:576-598`).
* Scan: WIFI_SCAN into a 24×42 buffer; result = AP count (`KESP:402-405`, `SDK:570-598`, `ENET:719-740`).
* Connect: WIFI_CONNECT with ssid[33]+pass[65], zero-padded (`KESP:406-420`, `ENET:761-796`); success only means "join started"; enet then polls for DHCP (`ENET:790-795`).
* Disconnect: WIFI_DISC (`KESP:421-422`, `ENET:830-845`).

```
WIFI_STATUS  host A5 14 FF 00 s 00 00
             esp  A5 14 FF 00 s 00 00 2D 00  C0 A8 01 64  24 6F 28 AA BB CC  <ssid 33>  C4  03
WIFI_SCAN    host A5 11 FF 00 s 00 00
             esp  A5 11 FF 00 s 02 00 54 00  <42-byte rec> <42-byte rec>
WIFI_CONNECT host A5 12 FF 00 s 62 00 <ssid 33 bytes, NUL padded> <pass 65 bytes, NUL padded>
             esp  A5 12 FF 00 s 00 00 00 00
```

### 3.7 UART baud (CMD_UART 0x15)

GET: `A5 15 FF 00 s 00 00` → 8 bytes `baud u32 LE, flags (bit0 = saved value equals current), 3×0` (`FW/wifi_cmd.cpp:332-338`).
SET: arg 1, payload `baud u32 LE, flags bit0 persist, 3×0`; baud must be 9600/19200/38400/57600/115200 else EMSGSIZE;
reply echoes baud+persist **at the old baud**, then the ESP waits 50 ms and switches (`FW/wifi_cmd.cpp:339-349`,
`FW/host_uart.cpp:20-24,111-131`, `FW/espnet.ino:75`). Host side: `OS_ESPUART` waits 6 ticks (`CLIB:998-1020`).
**No NedoOS app in svn sends CMD_UART**: `OS_ESPUART` has no caller and the kernel/asm only defines the constant
(grep `OS_ESPUART|CMD_UART` → only `iarlib/espnet.h`, `SDK:48`, firmware files). enet's port screen only reprograms the
16550 and espcom.ini (`ENET:285-302,918-933`) despite its help text (`ENET:562`). Divider mapping 1=115200, 2=57600,
3=38400, 6=19200, 12=9600 (`ENET:560`).

---

### 4. Flow control and timing

### 4.1 Lines

* ZX RTS → ESP CTS ("host may receive"); ESP RTS → ZX CTS ("ESP may receive") (`FW/PROTOCOL.md:36-40`).
* ESP32: hardware CTS/RTS, RX threshold 122, RX ring 4096, TX buffer 1024 (`FW/host_uart.cpp:149-154,101-103`). ESP8266: TX CTS flow + RX RTS flow at 64 bytes, RX buffer 4096 (`FW/host_uart.cpp:75-93,157-165`). So the ESP RX side is almost never full: MSR CTS is asserted practically always [inferred]. (The comment "ESP RX FIFO is 1-2 bytes" at `CLIB:87-89` is stale.)
* Firmware TX: 64-byte bursts, blocks (with `yield`) while the UART TX path is full, i.e. while ZX RTS is off (`FW/codec.cpp:115-150`, `FW/host_uart.cpp:205-235`, `FW/PROTOCOL.md:230-233`). The ESP does nothing else while a reply is stuck (single-threaded loop) [inferred from `FW/espnet.ino:52-78`].

### 4.2 Host TX (to ESP)

* Types 0 and 3 wait for MSR bit4 (CTS) before each byte; gives up after 20001 spins and sends anyway (`SDK:1801-1846`, `CLIB:90-118`). Types 1 and 2 skip the check.
* Then wait LSR THRE (bit5), write THR (`SDK:1725-1740`, `ECOM:64-74`).
* Before sending a request, RTS is set off (types 0/2/3; type 1 untouched) (`SDK:1025-1034`, `CLIB:539-540`).

### 4.3 Host RX on Evo, comType 0 ("Kondratyev without AFC")

* Init: FCR `0x87`, LCR `0x83`, DLL=divider, IER 0, LCR 3, IER 0, MCR `0x2F` (`SDK:1992-2014`, `ECOM:185-194`). Then `setrts(0)` writes MCR=0 (`SDK:121-122,1945-1948`). The Evo AVR keeps `MCR & 0x1F` and drives only RTS (`AVR:274-287`).
* RTS is a **strobe**, not a level: on every empty LSR poll the host writes MCR=2 then MCR=0 (with DI around the pair) (`SDK:1403-1411,1515-1529,2130-2139`, `CLIB:151-158,229-238,286-296`). "Hold-RTS hung: on this path RTS is a byte strobe" (`SDK:1500-1504`).
* Whole frame payload is read under DI on types 0/1/3 (`SDK:1505-1541`, `CLIB:281-302`; rationale `FW/PROTOCOL.md:41-48`); no YIELD mid-frame. YIELD only while waiting for SOF/CTS in userland (`SDK:1449-1452`, `CLIB:256-261`).
* Bytes per pulse: not stated numerically in NedoOS. `FW/PROTOCOL.md:50-52`: "pulse RTS only while LSR says empty ... If more than one byte is already in RBR, LSR keeps DR set until they are read." The AVR has a 16-byte RX FIFO (`AVR:60-61,412-437`). [inferred] A pulse is two AVR-served port writes (~tens of µs) — shorter than one character at 115200 (86.8 µs), and a flow-controlled UART only samples CTS before starting a character, so **one byte per pulse** (occasionally two at a boundary). unreal-ng already models it this way (`unreal-ng core/src/emulator/io/serial/uart16550.cpp:286-306`). Any count ≤16 is tolerated by the host.
* comType 2 (AFC): no pulses, hardware RTS (`SDK:1617-1652`, `ECOM:153-154`). comType 1 (ATM2 COM via #55FE/#xxFE command protocol) and 3 (ATM2IOESP, #FB index/#FA data) are ATM-only (`SDK:14-15,84-97`).

### 4.4 Retries and settling delays the host relies on

* `espRetry`/`espType` from espcom.ini are parsed but unused by ESPNET (`SDK:2566-2569`); `factor` forced to 65000 (`CLIB:615-617`, `SDK:123-125`). They feed only the AT driver's `uartBench` (`ECOM:629-711`).
* Kernel WRITE: up to 8 EAGAIN retries per chunk inside one BDOS call (`SDK:398-408`).
* Kernel UART lock: one BDOS net call at a time; others get EAGAIN 35 immediately (`SYSB:57-115`, `SYSK:1367-1390`); SETUART/GETUART bypass it (`SYSB:59-70`).
* Delays: 0.5 s after init (`CLIB:621-626`, `ESPCFG:325-337,383-385`), 1 s between DNS/CONNECT retries (`CNET:18-25,55-56,203,217`), 120 ms after UART SET (`CLIB:1012-1018`).

---

### 5. Firmware behavior an emulation must reproduce

### 5.1 General

1. Host is master; never send anything unsolicited (`FW/PROTOCOL.md:6`).
2. No boot banner on the ZX UART: "ESPNET ready", AT+GMR etc. go to the debug port (USB/UART0 on ESP32, Serial1/GPIO2 on ESP8266) (`FW/espnet.ino:35-37`, `FW/host_uart.cpp:237-244`). [inferred, general ESP8266 knowledge, not in sources: on ZX-WiFi native wiring the ESP8266 ROM bootloader prints a 74880-baud boot message on GPIO1 = ZX RX at power-on; the host's SOF search discards it.]
3. ESP state survives a Z80 reset (separate module): sockets, listeners, Wi-Fi link remain (`FW/sockets.cpp:370,492-494`, `KESP:309`).
4. Incoming TCP is buffered on the ESP until READ: up to 2048 bytes per socket in the slot, the rest in the TCP stack (window) (`FW/PROTOCOL.md:7,226-228`, `FW/sockets.cpp:218-241`).
5. Non-blocking semantics: READ/ACCEPT/WRITE return EAGAIN instead of waiting; READ/WRITE never return status 0 with count 0 (`FW/PROTOCOL.md:97,261-277`).
6. TCP lifecycle as seen by READ: EAGAIN while ESTABLISHED and empty (also during a Wi-Fi blip, `FW/PROTOCOL.md:235-240,272-273`); after peer FIN: buffered data first, then NOTCONN (`FW/sockets.cpp:85-92,596-603`). A closed TCP slot stays allocated until SHUTDOWN or until SOCKET needs a slot and reaps it (`FW/sockets.cpp:264-272,337-340`).
7. Request processing is blocking per request: CONNECT up to 8 s (+8 s retry), DNS up to 25 s, WRITE up to 3 s, SCAN seconds (`FW/protocol.h:133-141`, `FW/wifi_cmd.cpp:193-215`). Bytes the host sends meanwhile wait in the 4 KB RX buffer.

### 5.2 INFO payload (53 bytes; `FW/protocol.h:97-108`, `FW/wifi_cmd.cpp:158-180`)

| Off | Size | Field | Firmware value |
|---|---|---|---|
| 0 | 1 | ver_major | 1 (`FW/protocol.h:17`) |
| 1 | 1 | ver_minor | 27 (`FW/protocol.h:18`) |
| 2 | 1 | chip | 32 ESP32, 3 ESP32-C3, 86 ESP8266 (`FW/protocol.h:115-117`) |
| 3 | 1 | max_socks | 8 or 4 |
| 4 | 1 | wifi | 0 IDLE, 1 CONNECTING, 2 GOT_IP; 3 (AP) defined but never produced (`FW/wifi_cmd.cpp:99-111`) |
| 5 | 1 | rssi | signed dBm; 0 when not WL_CONNECTED (`FW/wifi_cmd.cpp:113-118`) |
| 6 | 1 | sock_mask | bit i = slot i not free (`FW/sockets.cpp:186-195`) |
| 7 | 1 | caps | `0x01` (`FW/wifi_cmd.cpp:171`) |
| 8 | 4 | ip | `WiFi.localIP()` |
| 12 | 6 | mac | STA MAC |
| 18 | 33 | ssid | NUL padded, ≤32 chars |
| 51 | 2 | free_heap | LE, clamped to 65535 |

Displayed by espcfg/enet (`ESPCFG:282-320`, `ENET:354-355`); no host code checks the version.

### 5.3 WIFI_STATUS payload (45 bytes; `FW/protocol.h:125-131`, `FW/wifi_cmd.cpp:306-322`)

`ip[4] @0, mac[6] @4, ssid[33] @10, rssi @43, flags @44` with flags bit0 CONNECTED (WL_CONNECTED and IP≠0), bit1 HASIP (IP≠0).

### 5.4 WIFI_SCAN record (42 bytes; `FW/protocol.h:119-123`, `FW/wifi_cmd.cpp:221-241`)

`ssid[33] @0 (≤32 chars), rssi @33 (signed), enc @34 (Arduino encryptionType), bssid[6] @35, channel @41`; at most 24 records (`FW/protocol.h:142`).
Side effect: scan drops STA (autoReconnect off, disconnect), then restarts the saved AP in the background unless WIFI_DISC is in force (`FW/wifi_cmd.cpp:193-250`). The reply itself takes ~0.2-3 s.

### 5.5 Wi-Fi state machine (1.27)

* Boot: STA mode, persistent, autoReconnect, `WiFi.begin()` with the saved AP (`FW/wifi_cmd.cpp:52-72`).
* `wifi_status_code`: WL_CONNECTED + IP → 2; WL_CONNECTED without IP → 1; IDLE/DISCONNECTED/CONNECTION_LOST/NO_SSID_AVAIL → 0; any other (e.g. connect failed, scan completed) → 1 (`FW/wifi_cmd.cpp:99-111`).
* WIFI_CONNECT: payload <98 → EMSGSIZE; else copy ssid (32 max) and pass (64 max), clear hold, `WiFi.begin(ssid, pass or NULL)`, **reply status 0 immediately** (no join wait, no validation) (`FW/wifi_cmd.cpp:256-293`, `FW/protocol.h:138-141`). The host polls INFO/WIFI_STATUS.
* WIFI_DISC: hold-disconnect flag, autoReconnect off, stop all TCP/UDP sockets (slots remain allocated; subsequent READ → NOTCONN after drain), disconnect (`FW/wifi_cmd.cpp:295-304`, `FW/sockets.cpp:250-262`). Saved AP kept (`FW/PROTOCOL.md:206-210`).
* Radio blip: TCP not stopped; EAGAIN until recovered or lwIP gives up (`FW/PROTOCOL.md:235-240`).

**Virtual connected AP recommendation [inferred]**: boot in state 2 with a fixed SSID (e.g. the configured virtual AP name),
an IP/DNS from the virtual network, a fixed MAC, rssi around -50; WIFI_SCAN returns that AP (plus optional fakes) with
channel/BSSID; WIFI_CONNECT always ACKs, then STATUS goes 1 (CONNECTING) for a short emulated time and 2 with the IP
(or stays 0 if the SSID/password does not match the virtual AP, to mimic a failed join); WIFI_DISC → 0 and sockets stop;
GETDNS returns the virtual DNS.

### 5.6 OTA / web UI / USB AT (not needed for emulation)

ArduinoOTA after IP, HTTP dashboard only after USB `AT+WEB`, mDNS `espnet-ESP32`/`-C3`/`-8266`, password `espnet`;
during OTA the ZX UART is not served (`FW/espnet.ino:42-49`, `FW/PROTOCOL.md:193-204`, `FW/webui.cpp:2-4,329-375,433-449`).
USB AT commands: `AT+GMR AT+STATUS AT+SOCKS AT+UART[=baud] AT+WEB AT+HELP` (`FW/wifi_cmd.cpp:475-526`). AT text must not be
sent on the ZX UART (`FW/PROTOCOL.md:199`).

---

### 6. Version differences

| Copy | Firmware | Host |
|---|---|---|
| svn (2026-09-30) | 1.27 | current kernel/SDK/C described above |
| [alfishe/NedoOS](https://github.com/alfishe/NedoOS) | identical to svn (`diff -rq` empty for `kapps/common/espnet`; md5 equal for `kernel/espnet*.asm`, `_sdk/espnet.asm`, `espnet.c`); git `4aa24778` "espnet 1.27 Исправлено подключение к wifi" 2026-09-21 | same |
| [alfishe/NedoOS-dev](https://github.com/alfishe/NedoOS-dev) | 1.24 (git `e480df29`) | older host |

Firmware 1.24 → 1.27 differences (diff of `FW/` against NedoOS-dev):

| Area | 1.24 | 1.27 |
|---|---|---|
| WIFI_CONNECT | blocks up to 35 s until GOT_IP; on failure replies HOSTUNREACH 65; uses channel+BSSID from last scan; resets radio with delays | ACK immediately, join async (`FW/wifi_cmd.cpp:256-293`) |
| WIFI_SCAN end | leaves `s_pause_sta=1` (STA down until power cycle) | restarts STA in background (`FW/wifi_cmd.cpp:243-250`) |
| ACCEPT, no free slot | listening slot is turned into the connection (reply sock = listen id) | EAGAIN, client stays queued (`FW/sockets.cpp:536-540`) |
| TCP established check (ESP32) | TCP_INFO first, then MSG_PEEK | MSG_PEEK FIN check first (`FW/sockets.cpp:85-92`) |
| TCP WRITE (ESP32) | waits while `availableForWrite()<=0` (always 0 on Arduino 3.x → EAGAIN) | clips only when >0 (`FW/sockets.cpp:673-681`) |
| SHUTDOWN arg 1 (ESP32) | EAGAIN if sndbuf <1024 | only if 0<sndbuf<1024 (`FW/sockets.cpp:379-385`) |

Host svn vs NedoOS-dev: dev host masks reply cmd with `0x7F`; dev kernel has no Wi-Fi BDOS calls L=12..15, sends
`SHUTDOWN 0xFF` on every SETUART, no 8× EAGAIN WRITE retry, no short ACCEPT/UDP budgets (`diff` of `SDK` and `KESP`).
Intermediate releases per git log: 1.25 (ATM2COM works), 1.26 (incoming connections for 3ws/scrnet).
`FW/PROTOCOL.md:1,11` still says "firmware 1.20" (stale); `FW/protocol.h:17-18` is authoritative (1.27).
The svn release (`REL/sd_bootesp.$C`, 2026-09-30) targets **1.27**: the async WIFI_CONNECT comment in `FW/protocol.h:138-140`
matches the kernel's polling enet flow (`ENET:790-795`) and the LONG budget for WIFI_CONNECT (`SDK:915-921`).
Emulate 1.27.

---

### 7. Open questions / ambiguities

1. **Bytes per RTS pulse on Evo** is not specified numerically anywhere; "about one" is inferred from pulse width vs character time and the AVR FIFO (§4.3). Real timing of AVR-served MCR writes is unmeasured.
2. **Close-all on reboot**: `SHUTDOWN sock=0xFF` is effectively never sent by the svn kernel (dead branch, §3.1); after a ZX reset stale ESP sockets persist until LISTEN steals the port or SOCKET reaps dead TCP. Confirm with the author whether this is intended.
3. **Kernel RSP buffer**: `esp_recv_rsp` accepts generic replies up to 256 bytes into a 64-byte kernel buffer (`SDK:1110-1134`, `SDK:23`). Firmware never sends more than 53 on those paths, so an emulator must keep to the firmware's exact payload sizes.
4. **UDP datagrams > maxlen**: firmware drops the remainder (`FW/sockets.cpp:588-589`); WIZNET semantics differ (see `docs/inprogress/2026-09-30-nedoos-integration/nedoos-bugs.md`). Emulate the firmware.
5. **UDP WRITE with 0 data bytes**: firmware sends an empty datagram and still reports EMSGSIZE (`FW/sockets.cpp:644-649`).
6. **WIFI_CONNECT failure reporting** in 1.27: no error path; a wrong password only shows as status staying 0/1. `wifi_status_code` maps WL_CONNECT_FAILED to 1 (CONNECTING), so a failed join may look like "connecting" forever [inferred from `FW/wifi_cmd.cpp:99-111`].
7. **Concurrent drivers**: a userland OS_ESP* app and the kernel driver can interleave frames on the same UART; no shared lock or seq (`CLIB:51`, `KBSS:58`). Real hardware would desync too; the emulator only needs per-frame correctness.
8. **ESP8266 boot output** on ZX-WiFi native wiring (74880 baud ROM text) is not covered by the sources; only matters if the emulator models module power-up with that wiring.
9. **ACCEPT reply sock** in 1.24 could equal the listening id; NedoOS 3ws treats datasoc==0 as "no client" (`FW/sockets.cpp:536-537`). Moot for 1.27.
10. **EAGAIN for SHUTDOWN arg 1** depends on ESP32 sndbuf state; NedoOS mostly sends arg 0 (`CNET` callers pass 0, `KESP:121`, `SYSH:822-825`); an emulator can always succeed.

---

## Part 2. The ESP AT dialect ZX software uses, the ZiFi API, existing emulations

Research date: 2026-10-01.

Facts carry a `path:line` citation. Anything marked **[inferred]** is a conclusion drawn from code, not something stated directly in a source.

### Path prefixes

| Prefix | Upstream |
|---|---|
| `NOS/` | [NedoOS src/](https://github.com/alfishe/NedoOS/tree/main/src) |
| `MRZ/` | [NOS/moon-rabbit-zx/](https://github.com/alfishe/NedoOS/tree/main/src/moon-rabbit-zx) |
| `MRF/` | [NOS/mrabbit-fusion/](https://github.com/alfishe/NedoOS/tree/main/src/mrabbit-fusion) |
| `KAP/` | [NOS/kapps/](https://github.com/alfishe/NedoOS/tree/main/src/kapps) |
| `KNT/` | [karabas-pro: software/profi/net-tools/src/](https://github.com/andykarpov/karabas-pro/tree/master/software/profi/net-tools/src) |
| `ZSP/` | [andrewinsidelazarev](https://github.com/andrewinsidelazarev) projects: [ZiFi](https://github.com/andrewinsidelazarev/ZiFi), [ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero), [ZiFi-ESP-01S-Native-C-Project](https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project) (`ZSP/<project>/...`) |
| `ZEU/` | [tslabs/zx-evo-unreal: Unreal/](https://github.com/tslabs/zx-evo-unreal/tree/main/Unreal) |
| `PS/` | [drewpo28/pico-spec: src/](https://github.com/drewpo28/pico-spec/tree/main/src) |
| `JN/` | [jorgegv/jnext](https://github.com/jorgegv/jnext) (GPLv3: concepts only) |
| `AVR/` | [tslabs/zx-evo: pentevo/avr/current/](https://github.com/tslabs/zx-evo/tree/master/pentevo/avr/current) - the **TS-Conf** AVR firmware, not BaseConf (see Part 3) |
| `KZ/` | [kozynax/kozynax: src/](https://github.com/kozynax/kozynax/tree/master/src) |
| `ATDOC` | Espressif *ESP8266 AT Instruction Set v1.5* (2016-01-29): [current edition (PDF)](https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf); the cited copy is in [ZiFi/_src/ESP8266 docs](https://github.com/andrewinsidelazarev/ZiFi/tree/master/_src), cited by page (`ATDOC p.N`) |
| `ATLOG` | [ZiFi/_src/ESP8266 docs/esp_init.txt](https://github.com/andrewinsidelazarev/ZiFi/tree/master/_src): a real terminal log against AT 0.60.0.0 / SDK 1.5.2 |

### 0. Executive summary

1. **The dialect is small and uniform.** Every ZX AT client uses this set:
   - single connection: `AT+CIPMUX=0`, `AT+CIPDINFO=0`
   - `ATE0`
   - `AT+CIPSTART="TCP","host",port`
   - `AT+CIPSEND=<n>`, then wait for `>`, then the raw payload
   - active-mode `+IPD,<len>:<data>`
   - unsolicited `CLOSED` at end of connection
   - `AT+CIPCLOSE`

   NedoOS C apps add the following:
   - `AT+RST` and a wait for `WIFI GOT IP`
   - `AT+CIPSERVER=0`
   - `AT+CIPRECVMODE=0`
   - in `time2` only, `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME?`

   **No ZX client uses any of these:**
   - `CIPMODE=1`, `+++` (defined once in KNT, never used), UDP or SSL
   - `AT+GMR` (and no version-string check exists)
   - `CIFSR`, `CIPSTATUS`, `CIPRECVDATA`
   - `UART_CUR`, which appears only in comments and READMEs as a manual setup step
2. **Wi-Fi association is out of band.** Clients assume the module already joined an AP, either with stored `CWJAP_DEF` or a user-edited `auth.pwd`. The exception is the original HackerVBI ZiFi client, which sends `AT+CWJAP_CUR` itself.
3. **The ZX-Evo has no automatic RTS/CTS.** Software pulses MCR bit 1 (RTS) to let the ESP send about one byte per pulse. The ESP must be configured with hardware flow control (`AT+UART_CUR=115200,8,1,0,3`), and the emulator must gate ESP→16550 bytes on RTS.
4. **On TS-Conf, ZiFi is a raw byte pipe.** In API mode 1 the AVR forwards bytes unchanged to a 115200 UART with no flow control. The only AT client on it is the original HackerVBI `zifi.spg`. **Both new firmware projects drop AT for a custom binary framed protocol** (`5A CMD LEN DATA XOR`), so their Z80 clients need a different emulated device.
5. **Existing emulations:**
   - jnext `src/esp01` is the only real in-process AT emulator with host sockets: very good architecture, narrow and deliberately non-faithful AT surface, GPLv3.
   - zx-evo-unreal and pico-spec only bridge bytes to a real COM port or ESP. pico-spec's host-side AT client is a useful oracle of real firmware output.
   - kozynax (a ZXMAK2 fork) has a 160-line toy AT emulator for the ZX-Uno UART.

---

### 1. AT command subset used by ZX software

### 1.0 Clients

| Client | Transport | AT engine | Notes |
|---|---|---|---|
| NedoOS C apps: getpic, calendar, zxart-radio, updater, svnesp, zxdb, zifi (app), girc, gopher, time2, cuart | 16550 #F8EF..#FFEF (comType 0 = Evo, 2 = AFC, 3 = ATM2IOESP), ATM2 COM (comType 1) | `KAP/common/esp-com.c`, `esp-com2.c` | Used only when `network.ini` `netDriver==1`. `netDriver==2` is ESPNET, a binary protocol (`KAP/common/espnet/PROTOCOL.md:199` "Do not send AT text") |
| Moon Rabbit (MRZ) | Evo 16550, ATM2, ZX-Uno, MB03, AY | `MRZ/drivers/wifi.asm` | No ZiFi build |
| Moon Rabbit Fusion (MRF) | Evo, ZX-WiFi (AFE), ATM2, AY 9600/57600, ZX-Uno | `MRF/drivers/wifi.asm` | Builds are listed in `MRF/Makefile`; TR-GZ has `-DNOINIT` (no init AT) |
| Karabas net-tools: alerts, weather, wget, browser | ZX-Uno UART or ZiFi (#xxEF) | `KNT/alerts/wifi.asm`, `http.asm`, `KNT/pqdos/browser/drivers/wifi.asm` | Browser is a copy of MRZ |
| Original ZiFi client (HackerVBI/DDp) | TS-Conf ZiFi, or 16550 with `cable_zifi=1` | `ZSP/ZiFi/zifi.asm` | Only AT client on TS-Conf ZiFi |
| `espprob1.a80` test tool | 16550 | `ZSP/ZiFi/_src/ESP8266 docs/espprob1.a80` | Interactive probe |

`NOS/_sdk/espnet.asm` is **not** AT: "Binary UART protocol only, no AT" (`NOS/_sdk/espnet.asm:1`).

### 1.1 Line framing and echo

- **Commands end with CRLF:**
  - C: `sendcommand()` appends `\r\n` (`KAP/common/esp-com.c:546-549`); `sendcommandNrn()` sends none (`:555-585`).
  - MRZ/MRF: the `EspCmd` macro appends `db 13,10` (`MRZ/drivers/utils.asm:12-21`).
  - ATDOC requires capitals and a trailing `\r\n`, at 115200 (ATDOC p.10).
- **Echo:**
  - Stock firmware boots with echo **on**. pico-spec masks the password in logs because the ESP echoes CWJAP (`PS/ZiFiAT.cpp:18-27`). ATLOG shows echoed commands.
  - Every client sends `ATE0` first and assumes echo is off afterwards:
    - C `espReBoot`: `KAP/common/esp-com.c:749`
    - MRZ: `MRZ/drivers/wifi.asm:10`
    - MRF: `MRF/drivers/wifi.asm:12`
    - KNT: `KNT/alerts/wifi.asm:2-16`
    - ZiFi client: `ZSP/ZiFi/zifi.asm:4507-4550`
  - The NedoOS line reader `getAnswer3()` takes the first non-empty line as the answer and does not skip echo (`KAP/common/esp-com.c:587-626`). **Echo must really turn off after ATE0.**
  - The C `ATE0` handler reads bytes until a `K`, then exactly 2 more bytes, CR and LF (`KAP/common/esp-com.c:749-769`). This works whether echo is on (the echoed "ATE0" has no K) or off.
- **Response framing:** stock firmware wraps replies as `\r\n<text>\r\n`.
  - ATLOG shows blank lines before `OK` after data lines.
  - jnext encodes this as `"\r\nOK\r\n"` (`JN/src/esp01/src/esp_at.cpp:920-921`).
  - `getAnswer3` skips leading CR/LF, reads up to CR, then consumes one byte as LF (`KAP/common/esp-com.c:587-626`).
  - **[inferred]** Every line must end with exactly `\r\n`. A bare LF or a missing LF would desync `getAnswer3`.

### 1.2 Per-command reference

#### `AT+RST`

- **Sent by:**
  - C `espReBoot` (`KAP/common/esp-com.c:715`). Called by getpic, calendar, zxart-radio, updater, svnesp, zxdb, zifi, girc, gopher, time2; see §1.4.
  - KNT, only after an HTTP error (`KNT/alerts/http.asm:160-168`).
- **Stock response:** `OK`, then a ROM boot log at 74880 baud (garbage at 115200), then `ready`. With a stored AP and autoconnect it continues with `WIFI CONNECTED` and `WIFI GOT IP`.
  - **[inferred]** The boot sequence comes from general Espressif behaviour. ATDOC p.12 says only "Response OK", and the boot log example is on ATDOC p.69.
- **What clients wait for:**
  - C waits for the substring `WIFI GOT IP`, using the global `gotWiFi[]` (e.g. `KAP/getpic/main.c:64`). The matcher is naive: count++ on a match, reset on a mismatch (`KAP/common/esp-com.c:738-746`).
  - The 10 s deadline (`finish = time()+10*50`, `:717`) is checked only when a `uartReadBlock` times out (`:722-735`). **If nothing says `WIFI GOT IP`, espReBoot fails.** Apps mostly just print an error (`KAP/getpic/main.c:1768-1773`).
  - KNT waits for `ready` (`KNT/alerts/http.asm:160-168`, string `:137`).
- **Side effect before the reset:** `uartBench()` runs right before `AT+RST`. It does 2000 RTS pulses and RBR reads and discards the data (`KAP/common/esp-com.c:638-649, 711`). Unsolicited bytes pending at that moment are thrown away.

#### `ATE0` / `ATE1`

- `ATE0` is sent by everyone (§1.1). `ATE1` is never sent.
- Response: `\r\nOK\r\n`. The C handler needs `K`, CR, LF; MRZ/MRF `checkOkErr` needs `O`,`K`,CR (`MRZ/drivers/wifi.asm:57-87`).

#### `AT`

- Sent only through MRF's `auth.pwd` default (`MRF/data/auth.pwd` = `AT\r\n`), and as a baud probe by the pico-spec host (`PS/ZiFi.cpp:330-394`).

#### `AT+GMR`

- **Not sent by any ZX code.**
  - It is commented out in the ZiFi client (`ZSP/ZiFi/zifi.asm:4520-4523`).
  - `cuart.txt:11` claims PgUp sends it, but the code uses PgUp for paging (`KAP/cuart/main.c:700-706`).
- There are no version-string checks anywhere.
  - The only capability check: `time2` treats `ERROR` on `AT+CIPSNTPCFG` as "update your AT-Firmware" (`KAP/time2/main.c:330-336`).
  - `espType==32` (ESP32 AT) makes `time2` expect an extra line (§SNTP).
- Format reference:
  - ATLOG: `AT version:0.60.0.0(Jan 29 2016 15:10:17)` / `SDK version:1.5.2(80914727)` / `compile time:...` / `OK`.
  - jnext emits `AT version:1.7.4.0(...)`, `SDK version:3.0.4(...)`, `compile time:...`, `Bin version(Wroom 02):1.7.4` (`JN/src/esp01/src/esp_at.cpp:838-847`).

#### `AT+CWMODE_DEF=1`

- Sent by the ZiFi client (`ZSP/ZiFi/zifi.asm:3768-3806`, init at `:4507-4550`).
- Defined but unused in KNT (`KNT/alerts/wifi.asm:119`).
- Comment-only in MRZ (`MRZ/drivers/evo-uart.asm:3-5`).
- Response: `OK`. ATLOG shows `AT+CWMODE_DEF?` answered `busy p...`, then `+CWMODE_DEF:1`, then `OK`.

#### `AT+CWAUTOCONN=0`

- Sent by the ZiFi client (same block). Response: `OK`.

#### `AT+CWJAP_CUR="ssid","pwd"`

- **ZiFi client:** the string is built from `zifi.ini` and followed by an **extra blank line** (`CR LF CR LF`) (`ZSP/ZiFi/zifi.asm:4037-4062`). It loops until `OK` appears in the buffer.
  - **[inferred]** The emulator must ignore, or answer ERROR to, the empty line without breaking.
  - jnext answers ERROR to empty lines (`JN/src/esp01/src/esp_at.cpp:237-240`).
- **MRF:** the user can put `AT+CWJAP="SSID","pwd"` (deprecated non-suffixed form) in `auth.pwd` (`MRF/README.md:147,168`; `MRF/drivers/wifi.asm:16-29`). It is checked with `checkOkErr`.
- **KNT:** defined but unused (`KNT/alerts/wifi.asm:124-126`).
- **Stock response:**
  - Success: `WIFI CONNECTED`, `WIFI GOT IP`, `OK` (ATLOG; `PS/ZiFiAT.cpp:116-153` expects exactly that).
  - Failure: `+CWJAP:<code>` then `FAIL` (ATDOC p.25). MRF/MRZ `checkOkErr` treats `FAIL` as an error (`MRZ/drivers/wifi.asm:57-87`).
  - A `WIFI DISCONNECT` line may also appear on re-join. **[inferred]**

#### `AT+CWQAP`, `AT+CWLAP`

- `AT+CWQAP`: defined but unused in KNT (`KNT/alerts/wifi.asm:121`).
- `AT+CWLAP`: used only by the `espprob1.a80` probe (`ZSP/ZiFi/_src/ESP8266 docs/1.utf8.txt` describes the sequence).
- Format per ATLOG: `+CWLAP:(3,"win",-77,"08:60:6e:24:89:70",13,85,0)`.

#### `AT+CIFSR`, `AT+CIPSTA`

- **Not sent by ZX software.**
- Formats for completeness:
  - `+CIFSR:STAIP,"a.b.c.d"` and `+CIFSR:STAMAC,"..."` (`PS/ZiFiAT.cpp:454-482`; `JN/src/esp01/src/esp_at.cpp:862-873`).
  - `+CIPSTA:ip:"..."` / `gateway` / `netmask` (ATLOG).

#### `AT+CIPMUX=0`

Sent by C `espReBoot` (`KAP/common/esp-com.c:775`), MRZ `:15`, MRF `:31-37`, KNT `:120`, ZiFi client. Response `OK`; MRZ/MRF require OK.

#### `AT+CIPDINFO=0`

Sent by C (`KAP/common/esp-com.c:773`), MRZ `:18`, MRF, KNT `:122`. Response `OK`.

#### `AT+CIPSERVER=0`

- Sent by C (`KAP/common/esp-com.c:777`), MRZ `:13`, MRF.
- **[inferred]** Stock firmware answers with no server running: on AT 1.x NonOS, `OK` or `ERROR` depending on version.
- jnext answers ERROR here (`JN/src/esp01/src/esp_at.cpp:680-706`).
- C reads one line and ignores its content. MRZ ignores the result. Either answer is safe.

#### `AT+CIPRECVMODE=0`

- Sent by C (`KAP/common/esp-com.c:779`) to force active mode.
- AT 1.x answers OK. Old firmware without the command answers `ERROR` (`PS/ZiFiSock.cpp:330` falls back on ERROR). C ignores the content. Not in ATDOC v1.5.
- `CIPRECVDATA` is never sent by ZX code; it appears only in a comment (`KAP/common/esp-com.c:793`).
- A variant of the ZiFi client listed in `ZSP/ZiFi-ESP-01S-Native-C-Project/ZiFi/ORIGINAL_AT_COMMANDS.md:16-51` uses `AT+CIPRECVMODE=1` + `+CIPRECVDATA`. That client source is not in the local HackerVBI repo.
- Passive formats per pico-spec:
  - notice `+IPD,<id>,<len>\r\n`
  - read reply `+CIPRECVDATA,<len>:<data>` then OK (`PS/ZiFiSock.cpp:190-198, 219-223, 469-491`)

#### `AT+CIPCLOSE` (no link id)

- **Sent:**
  - C init (`KAP/common/esp-com.c:771`)
  - MRZ init and before every CIPSTART (`MRZ/drivers/wifi.asm:14, 39`)
  - getpic/updater/calendar/zxart-radio/zxdb at end of body (`KAP/getpic/main.c:430-444`; `KAP/calendar/main.c:947-949`)
  - girc (`KAP/girc/main.c:1163`), svnesp (`KAP/svnesp/main.c:602`, reply unread)
  - KNT (`:132`), and the ZiFi client on CIPSTART error
- **Stock response:**
  - Link open: `CLOSED` then `OK`. jnext: `\r\nCLOSED\r\n\r\nOK\r\n` (`JN/src/esp01/src/esp_at.cpp:526-538`). ATDOC p.60 says "OK".
  - Nothing open: `ERROR` (ATDOC p.60; `PS/ZiFiSock.cpp:525-539`).
- **Client expectations:**
  - getpic reads one line; if it contains `CLOSED` it reads OK, otherwise it assumes ERROR (`KAP/getpic/main.c:430-440`).
  - calendar/zxart-radio/zxdb read exactly two lines, commented "CLOSED" and "OK" (`KAP/calendar/main.c:947-949`).
  - **[inferred]** After a peer-close, the unsolicited `CLOSED` was already emitted, so the reply is just `ERROR`. That is still two lines for the two-line readers: the stale `CLOSED` plus `ERROR`.

#### `AT+CIPSTART="TCP","<host>",<port>`

- **Exact literals:**

  | Literal | Location |
  |---|---|
  | `"AT+CIPSTART=\"TCP\",\"zxart.ee\",80"` | `KAP/getpic/main.c:337`; `KAP/zxart-radio/main.c:1093,1637` |
  | `...\"xmlcalendar.ru\",80` | `KAP/calendar/main.c:885` |
  | `...\"%s\",80` | `KAP/updater/main.c:779` |
  | `...\"%s\",%u` | `KAP/svnesp/main.c:557`, `KAP/zxdb/main.c:952,1349`, `KAP/zifi/main.c:989`, `KAP/girc/main.c:1271`, `KAP/gopher/main.c:1310` |
  | asm, host and port copied from a gopher row | `MRZ/drivers/wifi.asm:40-47` |
  | proxy `AT+CIPSTART="TCP","138.68.76.243",6912` | `MRZ/drivers/proxy.asm:12-13` |
  | KNT `...",80\r\n` | `KNT/alerts/http.asm:203-216` |
  | ZiFi client `...",80` | `ZSP/ZiFi/zifi.asm:1667-1668` |

  The host is always a name or dotted IP, so **the ESP performs DNS**. Only TCP is used; UDP and SSL never appear.
- **Stock response:**
  - Success: `CONNECT\r\n\r\nOK\r\n` (ATLOG shows `busy p...` before `CONNECT` while DNS/connect is in progress).
  - Failure: `ERROR`, possibly preceded by `DNS Fail` (kozynax emits `DNS Fail\r\n\r\nERROR`, `KZ/ZXMAK2.Hardware.Circuits/Network/Esp8266AtFirmware.cs:79`), or `CLOSED` then `ERROR` **[inferred]**.
  - Already connected: `ALREADY CONNECTED` then `ERROR` **[inferred, AT 1.x]**; ATDOC p.53 writes `ALREADY CONNECT`.
- **Client handling:**
  - getpic/updater: the first line must contain `CONNECT`, else espReBoot + 1 s + retry forever. getpic then reads `OK`; updater does not (`KAP/getpic/main.c:339-363`; `KAP/updater/main.c:782-799`).
  - calendar/zxart-radio re-send CIPSTART until a line contains `CONNECT` (`KAP/calendar/main.c:877-890`).
  - zxdb/gopher/zifi read lines until `CONNECT` or `ERROR` (skipping `DNS Fail` etc.), then one OK line (`KAP/gopher/main.c:1313-1329`).
  - girc: one line; ERROR retries up to 3 times with `uartFlush(200)`; any other line re-sends (`KAP/girc/main.c:1265-1294`). **A `busy p...` line makes girc re-send. [inferred]**
  - svnesp: one line, up to 3 attempts (`KAP/svnesp/main.c:557-571`).
  - MRZ/MRF `checkOkErr` skips everything until `OK\r`, `ERROR` or `FAIL` (`MRZ/drivers/wifi.asm:57-87`).
  - KNT/ZiFi client: ring-buffer suffix match on `OK\r\n` / `\r\nERROR\r\n` (`KNT/alerts/wifi.asm:24-43, 138-140`). The ZiFi client checks `ERROR` and then sends CIPCLOSE and retries (`ZSP/ZiFi/zifi.asm:528-662`).
  - `strstr(...,"CONNECT")` also matches `ALREADY CONNECTED`, so C apps treat it as success.

#### `AT+CIPSEND=<len>` (no link id)

- **Format variants:**
  - `%u` (C)
  - **5 digits with leading zeros**, e.g. `AT+CIPSEND=00012` (MRZ/MRF `hlToNumEsp`, `MRZ/drivers/wifi.asm:185-203`; `MRF/drivers/wifi.asm:226-244`)
  - KNT: 3-4 digits with possible leading zero, e.g. `AT+CIPSEND=085` (`KNT/alerts/http.asm:59-85`)
  - Proxy: `AT+CIPSEND=1` (`MRZ/drivers/proxy.asm:34-40`)

  **The parser must accept leading zeros.**
- **Length conventions:**
  - Several clients set len = strlen+2 and send the payload with a trailing CRLF: getpic, updater, calendar, zxart-radio, zxdb (`KAP/getpic/main.c:365,380`), and MRZ/MRF (`MRZ/drivers/wifi.asm:119`).
  - gopher and zifi send exactly strlen with no CRLF (`KAP/gopher/main.c:1330,1338`).
  - Max length is 2048 (ATDOC p.55).
- **Stock response:** `\r\nOK\r\n> ` (prompt `>` plus a space, no CRLF). After exactly len bytes: `\r\nRecv <n> bytes\r\n\r\nSEND OK\r\n`. ATLOG shows `OK`, `> `, `Recv 4 bytes`, `SEND OK`. Stock firmware can also answer `busy s...` while a previous send is in flight. **[inferred]**
- **Client handling:**
  - **The `OK` line must come before `>`.** The `getAnswer3`-first clients block otherwise: calendar, zxart-radio, zxdb, gopher, zifi (`KAP/gopher/main.c:1332-1336`), MRZ/MRF (checkOkErr then spin for `>`, `MRZ/drivers/wifi.asm:121-125`), KNT (`KNT/alerts/http.asm:186-194`).
  - getpic, updater and svnesp scan raw bytes for `>`; svnesp uses at most 200 reads (`KAP/svnesp/main.c:450-454`).
  - girc scans for `>`. If `+` arrives first it parses that `+IPD`, so a `+IPD` racing the prompt is tolerated (`KAP/girc/main.c:1086-1111`).
  - **After the payload:**
    - zxart-radio requires `SEND OK` and then exactly CR LF before any `+IPD` (`KAP/zxart-radio/main.c:1108-1122`).
    - MRZ/MRF/KNT `checkOkErr` matches the `OK\r` inside `SEND OK` (`MRZ/drivers/wifi.asm:135`; `KNT/alerts/http.asm:195-196`).
    - The ZiFi client waits for the characters `O`,`K` (`ZSP/ZiFi/zifi.asm:569-578`).
    - Other C apps skip it: `recvHead` scans to `,` and `Recv N bytes` contains no comma.
  - `SEND FAIL` is never handled.

#### `+IPD` (unsolicited data)

- **Required format:** `\r\n+IPD,<len>:<len raw bytes>`, i.e. CIPMUX=0 and CIPDINFO=0 (ATDOC p.67).
  - **Multiplexed (`+IPD,<id>,<len>:`) and remote-IP (`+IPD,<len>,<ip>,<port>:`) forms break MRZ/MRF** (`MRZ/drivers/wifi.asm:137-180`).
  - C `recvHead` atoi's the text after the first comma (`KAP/common/esp-com.c:837-851`). The CIPDINFO form would happen to work; the mux form would not.
- **KNT requires the CRLF before `+IPD`** (suffix `\r\n+IPD,`, `KNT/alerts/wifi.asm:135`).
- **Parser details:**
  - C `recvHead` aborts on the `CLOSED` or `ERROR` substring while scanning (`KAP/common/esp-com.c:809-835`).
  - MRF fusion's verification compares are commented out (`;` before `cp`), so any `O` + 4 bytes counts as CLOSED and any `+` + 4 bytes counts as an `+IPD,` header (`MRF/drivers/wifi.asm:153-205`). **Between CIPSEND and CLOSED the module must emit only `+IPD` frames and `CLOSED`. Any other unsolicited text containing `O` or `+` desyncs MRF.**
- **Chunk sizes:**
  - KNT reads into a 4096-byte buffer (`KNT/alerts/wifi.asm:154`).
  - ZiFi client: header must fit the first IPD; it scans `\r\n\r\n` inside the first packet (`ZSP/ZiFi/zifi.asm:581-605`; same for KNT, `KNT/alerts/http.asm:105-143`).
  - **[inferred]** Stock ESP8266 delivers TCP segments as received, typically ≤1460 bytes per `+IPD`. jnext caps at 2048 (`JN/src/esp01/include/esp01/esp_at.h:522`); kozynax at 4096 (`Esp8266AtFirmware.cs:133`).
  - **Recommendation [inferred]:** cap at 1460, and coalesce so a short first segment (HTTP status + headers) arrives as one `+IPD`.

#### `CLOSED` (unsolicited)

- **Clients that depend on it:**
  - gopher and zifi (C) loop `recvHead` until `CLOSED` and never send CIPCLOSE (`KAP/gopher/main.c:1343-1366`; `KAP/zifi/main.c:1010-1024`).
  - MRZ/MRF `getPacket` uses it as end of stream (`MRZ/drivers/wifi.asm:137-180`).
  - KNT calls `closed_callback` (`KNT/alerts/wifi.asm:50-76`, string `:134`).
  - ZiFi client ends the download on `CLOSED\r\n` (`ZSP/ZiFi/zifi.asm:679-682`).
- **Rules:**
  - **It must be emitted only after all buffered `+IPD` data**, as jnext does (`JN/src/esp01/src/esp_at.cpp:1208-1229`).
  - Real active-mode firmware sometimes discards the last frames on peer close (`PS/ZiFiSock.cpp:80-87`). Do **not** reproduce that.
- **Format:** `CLOSED\r\n`; with CIPMUX=1 it would be `<id>,CLOSED`. jnext and ATDOC p.63 frame it as `\r\nCLOSED\r\n`. The C/asm matchers accept either.

#### `AT+CIPMODE=1` / `+++`

- **Not used by any ZX client.**
  - `+++` is defined but unreferenced in `KNT/alerts/wifi.asm:116`.
  - `CIPMODE` appears only in a comment (`KAP/girc/main.c:1113`).
- Semantics for completeness: transparent mode allows only a single connection. A packet containing only `+++` exits; wait 1 s before the next command (ATDOC p.55, p.63).

#### `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME?` (`time2`, `cuart`)

- **time2 sequence:**
  1. `espReBoot`.
  2. Send `"AT+CIPSNTPCFG=1,%u,\"%s\",\"time.google.com\""` with GMT 3 and `2.ru.pool.ntp.org` by default (`KAP/time2/main.c:322`).
  3. Read one line. `ERROR` means exit (`:324-336`).
  4. Delay 250 ms.
  5. If `espType==32` (the default in code and in the shipped `espcom.ini`), read one more line, `+TIME_UPDATED` (ESP32 AT). On an 8266 this just times out non-fatally (`:340-347`).
  6. Delay 300 ms, send `AT+CIPSNTPTIME?` (`:351-353`).
  7. The first line must contain `+CIPSNTPTIME:` and is parsed at fixed offsets: `+CIPSNTPTIME:Thu Aug 04 14:48:05 2016`. Weekday at [13..15], month [17..19], day [21..22], time [24..32], year = atoi([35..36]) + 100 (`:362-431`).
  8. Read one more line (OK) (`:433`).
  9. If the year is `70`, retry up to 10 times at 500 ms intervals (`:439-453`).
- **Format requirements:**
  - Exactly one space between fields and a 2-digit zero-padded day (pico-spec sees `+CIPSNTPTIME:Mon Jan 06 18:30:45 2026`, `PS/ZiFiAT.cpp:163-213`).
  - Report 1970 until the first "sync". **[inferred]** Report synced time immediately, or after 1-2 queries.
- **cuart Ctrl+T:** sends `AT+CIPSNTPTIME?`, then `AT+CIPSNTPCFG=1,300,"0.pool.ntp.org","://google.com"`, then `AT+CIPSNTPTIME?`, 500 ms apart, display only (`KAP/cuart/main.c:441-451`).
- **[inferred]** On an 8266 with AT ≥ 1.6, `AT+CIPSNTPCFG=1,<tz>,...` returns `OK` only. The `+TIME_UPDATED` URC is ESP32 AT 2.x behaviour. Make it configurable: personality `esp8266-1.7` vs `esp32-2.x`.

#### `AT+UART_CUR` / `AT+UART_DEF` / `AT+UART`

- **Never sent by code.**
- Documented as manual setup:
  - `;AT+UART_CUR=115200,8,1,0,3` (`MRF/drivers/uart-evo.asm:2`)
  - `;AT+UART_CUR=38400,8,1,0,0` (`MRZ/drivers/evo-uart.asm:3`)
  - `MRF/README.md:170-176`: EVO `AT+UART=115200,8,1,0,3`, ATM `AT+UART=38400,8,2,0,3`, ZX-WiFi 115200 8N1, AY 9600
  - ZiFi client, commented out (`ZSP/ZiFi/zifi.asm:4520-4523`)
- `espprob1.a80` sends `AT+UART_CUR=9600...` (`espprob1.a80:645-664`).
- **[inferred]** The emulator should accept it, answer OK, change the virtual baud after the OK, and honour the flow-control field (3 = RTS+CTS) for §2.

#### `AT+CIUPDATE`

- cuart ALT+U only (`KAP/cuart/main.c:717-719`). Answer `ERROR`, or a fake progress sequence. Low priority.

#### `AT+PING`

- espprob1 only; ATLOG shows `AT+PING="google.com"` → `+24`, `OK`.

#### `busy p...`

- Stock firmware prints it when a command arrives while the previous one is still processing (ATLOG, CIPSTART and CWLAP).
- **No ZX client handles it explicitly.** `checkOkErr`, the `strstr` loops and ring matches skip it. girc/calendar/zxart-radio may re-send CIPSTART on a non-CONNECT line (§CIPSTART).
- **[inferred] Recommendation:**
  - Do not emit `busy p...` unless a second command really arrives mid-operation. Then emit it and drop the command, as stock firmware does.
  - jnext instead defers such input (`JN/src/esp01/src/esp_at.cpp:159-170`).

### 1.3 Unsolicited messages: what ZX software relies on

| Message | Relied on by | Citation |
|---|---|---|
| `WIFI GOT IP` after `AT+RST` | All NedoOS C apps (espReBoot) | `KAP/common/esp-com.c:715-746` |
| `ready` after `AT+RST` | KNT error path | `KNT/alerts/http.asm:160-168` |
| `+IPD,<len>:` at any time after `CONNECT` | All; servers speak first in girc/svnesp | `KAP/girc/main.c:1065-1111` |
| `CLOSED` on peer close | gopher, zifi (C), MRZ/MRF, KNT, ZiFi client | above |
| `+TIME_UPDATED` (ESP32) | time2 with espType=32 (non-fatal if absent) | `KAP/time2/main.c:340-347` |

**Must not appear during a transfer** (MRF desync): `WIFI DISCONNECT`, `WIFI CONNECTED`, any `+...` other than `+IPD`. **[inferred]** Suppress Wi-Fi state URCs after boot unless a scenario test asks for them.

### 1.4 Per-program matrix

| Program | RST/GOT IP | ATE0 | CIPCLOSE init | CIPDINFO=0 | CIPMUX=0 | CIPSERVER=0 | CIPRECVMODE=0 | CWJAP | CIPSTART TCP | CIPSEND | waits SEND OK | relies on CLOSED | SNTP |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| getpic, calendar, updater, zxdb | yes | yes | yes | yes | yes | yes | yes | – | yes | yes | no | no (Content-Length) | – |
| zxart-radio | yes | yes | yes | yes | yes | yes | yes | – | yes | yes | **yes** | no | – |
| svnesp | yes | yes | yes | yes | yes | yes | yes | – | yes | yes | no | no | – |
| girc | yes | yes | yes | yes | yes | yes | yes | – | yes (6667) | many | no | aborts on it | – |
| gopher, zifi (C) | yes | yes | yes | yes | yes | yes | yes | – | yes | yes | no | **yes** | – |
| time2 | yes | yes | yes | yes | yes | yes | yes | – | – | – | – | – | **yes** |
| cuart | – (uart_init only, `KAP/cuart/main.c:871-877`) | – | – | – | – | – | – | user | user | user | – | – | Ctrl+T |
| MRZ | – | yes | yes | yes | yes | yes | – | – | yes | yes (5 digits) | via checkOkErr | **yes** | – |
| MRF | – | yes | yes | yes | yes | yes | – | auth.pwd | yes | yes (5 digits) | via checkOkErr | **yes** (loose) | – |
| KNT alerts/weather | only on error (`ready`) | yes | – | yes | yes | – | – | – | yes :80 | yes | via okErrCmd | **yes** | – |
| KNT wget | – (no init AT) | – | – | – | – | – | – | – | yes | yes | yes | yes | – |
| ZiFi client (TS) | – | yes | – | – | yes | – | – | `CWJAP_CUR` | yes :80 | yes | `OK` chars | **yes** | – |

Notes:
- ZiFi client also sends `CWMODE_DEF=1` and `CWAUTOCONN=0`.
- netprint's ESP code is inside `/* */` (`KAP/netprint/main.c:238-661`).
- enet uses ESPNET binary or kernel sockets only.

---

### 2. Timing and flow-control assumptions

### 2.1 16550 setup (ZX-Evo, #F8EF..#FFEF)

**Port map:** RBR/THR #F8EF, IER #F9EF, IIR/FCR #FAEF, LCR #FBEF, MCR #FCEF, LSR #FDEF, MSR #FEEF, SCR #FFEF (`KAP/calendar/main.c:53-63`; `MRF/drivers/uart-evo.asm:18-25`). Overridable via `ini/espcom.ini` (`KAP/common/esp-com.c:873-919`).

**C `uart_init`** (`KAP/common/esp-com.c:181-217`):
1. FCR=0x87
2. LCR=0x83
3. DLL=divisor (1 = 115200), DLM=0
4. LCR=0x03 (8N1)
5. IER=0
6. MCR=0x2F

`cuart` offers divisors 1..96 with baud = 115200/div (`KAP/cuart/main.c:494, 735-783`).

**MRF Evo init** (`MRF/drivers/uart-evo.asm:30-65`):
1. MCR=0x0D
2. FCR=0x87
3. LCR=0x83
4. DLL=1, DLM=0
5. LCR=3
6. IER=0
7. MCR=0x2F

Comments: "In Evo only bit #1 is used. For RTS control" (`:33`) and "Enable AFE. Not implemented in EVO" (`:61`).

**MRZ Evo init bug:** the `outp` macro emits `out (n),a` instead of `out (c),a`, so its init writes go to the wrong ports (`MRZ/drivers/evo-uart.asm:18-25`). **[inferred]** On real hardware MRZ relies on the AVR defaults: LSR 0x60, divisor 1 (`AVR/rs232.c:58-67`).

**AVR emulation of the 16550**, i.e. what real Evo hardware does (`AVR/rs232.c`):
- Defaults: DLL=1, IER=0, ISR=0x01, LCR=0, MCR=0, LSR=0x60, MSR=0xA0, SCR=0xFF (`:58-67`).
- Baud = 115200/divisor; divisor 0 = 230400; DLM bit 7 = raw UBRR (`:107-134`).
- FCR bit 1/bit 2 clear RX/TX; the trigger level is ignored (`:330-353`).
- IIR always reads 0x01 (`:580-582`). IER is stored, but no interrupts are generated (`:326`).
- MCR is stored &0x1F, **so AFE (bit 5) does not exist**. MCR bit 1 drives the RTS pin (`:360-373`).
- LSR: DR, OE, PE/FE, THRE, TEMT (`:674-688`; RX overflow sets OE, `AVR/rs232_asm.S:39-41`).
- MSR bit 4 is CTS from a pin, with delta-CTS in bit 0; the low nibble clears on read (`:693-710`).
- RX ring 512 (511 usable), TX ring 256 (255 usable) (`AVR/rs232.h:158-161`; `AVR/rs232.c:43-51, 402, 440`). **So the "16550" FIFO on Evo is about 511 bytes, not 16.**

### 2.2 RTS pulsing (comType 0, Evo without AFC): the critical behaviour

- **C read loop** (`KAP/common/esp-com.c:312-326`): while LSR.DR==0, with DI, write MCR=2 then MCR=0 (RTS asserted for a few µs), EI, decrement timeout. Once DR=1 it reads RBR without pulsing.
  - `getdataEsp(n)` does the same for each byte of a block, under DI for the whole block (`:391-500`).
  - `uartReadburst` pulses once per byte (`KAP/common/esp-com2.c:4-78`).
- **MRF read** (`MRF/drivers/uart-evo.asm:76-101`): if DR=0, DI, MCR=2, **spin until DR=1**, MCR=0, read. RTS is held until the first byte arrives. No timeout.
- **MRZ read** (`MRZ/drivers/evo-uart.asm:51-76`): MCR=2, then a fixed delay of about 6000 T (about 1.7 ms ≈ 20 bytes at 115200), MCR=0, recheck LSR.
- **uartFlush(ms)** holds RTS=1 continuously while discarding bytes for ms/20 ticks, then sets RTS=0 (`KAP/common/esp-com.c:265-303`).
- **espnet.asm** warns that RTS is a byte strobe on this path and holding RTS hangs (`NOS/_sdk/espnet.asm:1502-1504`). That refers to the ESPNET firmware, not AT.
- **[inferred] Bytes per pulse:**
  - The ESP8266 UART checks CTS (= Evo RTS) before starting each character.
  - A pulse of a few µs (C) lets out at most the 1 character whose start falls inside the pulse.
  - MRF releases until one byte lands, so 1-2 bytes, depending on whether a second character started before MCR=0.
  - MRZ's 1.7 ms window admits ~20 bytes.
  - **Model:** the ESP may start transmitting a character only while RTS is asserted. A started character completes, taking 10 bit times at the current baud, regardless of RTS. Deliver into the 511-byte RX ring and set OE on overflow.
  - With AFC clients (comType 2 or ZX-WiFi AFE), RTS stays asserted.
- **[inferred]** If the emulator ignores RTS and dumps bytes into a large FIFO, all known clients still work: they read as long as DR=1. **RTS gating only matters for fidelity, and for MRZ's 20-byte window if the FIFO were 16 bytes.** The cheapest correct model is the AVR one: a 511-byte RX ring with RTS gating at character starts.

### 2.3 Timeouts

- **C:** per-byte timeout counted in poll iterations, calibrated by `uartBench()` against 50 Hz ticks: `factor = (magic*cycles/takes)*espRetry*50/10` (`KAP/common/esp-com.c:629-702`). magic is 11/15/16 per app; espRetry is 5 in code and 40 in the shipped ini. **[inferred]** That is roughly 5-40 s per byte.
  - `espReBoot` gives 10 s for `WIFI GOT IP` (`:717`).
  - `uartFlush(200)` = 10 ticks.
  - `time2` delays 250/300/500 ms (`KAP/time2/main.c`).
  - Wall clock is `time()` at 50 Hz; `delay(ms)` divides by 20 (`KAP/common/network.c:132-146`).
- **MRZ/MRF:** no timeouts; `checkOkErr` and `read` block forever (`MRF/drivers/uart-evo.asm:76-101`). ZX-WiFi build: 50-frame (~1 s) read timeout that returns 0x00 with CF ignored by callers (`MRF/drivers/uart-zxwifi.asm:58-74`). **[inferred]** A >1 s gap inside `+IPD` data inserts zero bytes.
- **KNT:** re-polls after 7×256 halts (`KNT/alerts/main.asm:65-71`).
- **ZiFi client:** the input wait gives up after 256 frames, ~5 s (`ZSP/ZiFi/zifi.asm:898-912`).
- **Startup delays:** MRZ shows its logo for 150 halts before init (`MRZ/main.asm:58-60`); MRF waits 50 halts (`MRF/main-all.asm:63-65`). Neither waits for `ready`. **[inferred]** The emulated ESP must already be up (power-on banner done) within about 1 s of machine start, or buffer the banner so it is ignored by checkOkErr.

### 2.4 Ordering rules the emulator must guarantee

1. `CONNECT` before `OK` on CIPSTART.
2. `OK` before `> ` on CIPSEND (`> ` has no CRLF).
3. `SEND OK\r\n` before the first `+IPD` after a send (zxart-radio, `KAP/zxart-radio/main.c:1108-1122`). **[inferred]** Hold `+IPD` until `SEND OK` is out. jnext serialises URCs similarly (`JN/src/esp01/src/esp_at.cpp:1266-1281`).
4. `CLOSED` only after all `+IPD` data.
5. Exactly one response line per init command (C reads one `getAnswer3` per command, then `uartFlush(200)`; `KAP/common/esp-com.c:771-781`).
6. No URCs between CIPSEND and CLOSED other than `+IPD` (MRF).

---

### 3. ZiFi API (TS-Conf)

**Source:** [zx-evo-docs ZiFi/zifi.md](https://github.com/tslabs/zx-evo-docs/blob/main/ZiFi/zifi.md) (110 lines), plus the AVR implementation.

### 3.1 Registers

The port low byte is `#EF`; the high byte selects the register (`zifi.md:5-19`; `AVR/rs232.h:82-121`).

| Port | R/W | Name | Semantics |
|---|---|---|---|
| `#00EF..#BFEF` | RW | DR | Data. Target is ZiFi or RS-232, depending on which FIFO-status register was read last (`zifi.md:23-26`). |
| `#C0EF` | R | ZIFR | ZiFi input FIFO used. AVR returns `min(used, 0xBF)` and sets select_zf=1 (`AVR/rs232.c:469-475`). The doc says 255 = full (`zifi.md:8`); the 0xBF cap keeps `INIR` with `B=count` inside the DR range. |
| `#C1EF` | R | ZOFR | ZiFi output FIFO **free**, `min(free, 0xBF)`, sets select_zf=1 (`AVR/rs232.c:478-489`). |
| `#C2EF` / `#C3EF` | R | RIFR / ROFR | Same for the RS-232 (COM) UART; sets select_zf=0 (`AVR/rs232.c:513-533`). |
| `#C4EF` | W/R | IMR / ISR | Bit0 ZIBTR threshold, bit1 ZITOR timeout, bit2 RIBTR, bit3 RITOR. IMR writes OR in; each bit is one-shot (cleared when it fires). ISR clears on read (`zifi.md:56-79`; `AVR/rs232.c:268-270, 492-495`). |
| `#C5EF` | RW | ZIBTR | Input threshold, default 0x80 (`zifi.md:14`). |
| `#C6EF` | RW | ZITOR | Input timeout in ms, default 1 (`zifi.md:15`). |
| `#C7EF` | W/R | CR / ER | Command / error (`zifi.md:16-17`). |
| `#C8EF` / `#C9EF` | RW | RIBTR / RITOR | RS-232 threshold and timeout. |

### 3.2 Commands (CR) and errors

From `zifi.md:30-54` and `AVR/rs232.c:213-263`:

| Code | Name | Action |
|---|---|---|
| `000000oi` | ZFCLRFIFO | Clear ZiFi input (i) / output (o) FIFO. |
| `000001oi` | RSCLRFIFO | Same for RS-232. |
| `11110mmm` | SETAPI | Accepted in any mode. `zf_api = m`; any m > `ZF_VER` (1) becomes 0. ER = 0. |
| `0xFF` | GETVER | ER = highest API (1). |

- Commands other than SETAPI are processed only when the API is non-zero (`AVR/rs232.c:222`).
- ER codes: 0x00 OK, 0xFF REJ (`zifi.md:51-54`).
- API modes: 0 disabled, 1 transparent ("all data is sent/received to/from external UART directly"), 2-7 reserved (`zifi.md:41-45`).

### 3.3 Read and write semantics

- **Writes:**
  - A DR write with select_zf and api==1 goes into the 255-byte ZiFi TX ring; if the ring is full, the byte is **silently dropped** (`AVR/rs232.c:177-189`).
  - Otherwise, with api≠0, the write goes to the RS-232 TX ring (`:192-204`).
- **Reads:**
  - Any register ≤ #CF reads `0xFF` by default. **With the API disabled, every ZiFi register, ER included, reads 0xFF.** Software uses this to detect "no ZiFi" (`AVR/rs232.c:426, 465`).
  - An empty DR reads 0xFF (`:437-441`). The original client relies on that: it always `INIR`s 0xBF bytes and scans them (`ZSP/ZiFi/zifi.asm:817-825, 944-959`).
- **ESP-side UART:**
  - AVR UART0, fixed 115200 (UBRR=5 at F_CPU 11.0592 MHz), TX with 2 stop bits, no RTS/CTS (`AVR/rs232.c:99-103`; `AVR/default/Makefile:45`).
  - ZiFi RX ring 512 (511 usable); on overflow the byte is dropped silently with no flag (`AVR/rs232_asm.S:101-143`).
  - **[inferred]** Effective ESP→Z80 rate is ≤11.5 KB/s. A Z80 that does not drain in time loses data.
- **Interrupt:** when `zf_int_src` is non-zero, the AVR pulses `MODE_TS_WTP_INT` (`AVR/rs232.c:614-664`). That raises the TS-Conf WTP interrupt: vector `#F9`, INTMASK bit 3, priority 4 (`.../zx-evo/pentevo/fpga/current/z80/zint.v:74,98,130`).
- **Port decode:**
  - The FPGA maps high bytes `#00..#BF` → DR, `#C0..#CF` → regs, `#F8..#FF` → 16550, via a 5-bit code over SPI (`.../fpga/current/common/slavespi.v:21-25, 96`; `AVR/zx.c:963-969`).
  - **[inferred]** `#D0..#EF` alias onto `#C0..#CF`, and `#F0..#F7` alias onto DR.
  - Every #EF access inserts Z80 WAIT while the AVR answers (`.../fpga/current/z80/zports.v:735-763`).
- **Init sequence:** `OUT #C7EF,#F1`; `OUT #C7EF,#FF`; `IN #C7EF` → version, where 0xFF means none (`zifi.md:81-110`). The ZiFi client says "Please update TS Conf" on 0xFF (`ZSP/ZiFi/zifi.asm:4566-4571`).
- **Discrepancies between implementations:**

  | Behaviour | Karabas Pro `zifi.vhd` | pico-spec | zx-evo-unreal | AVR |
  |---|---|---|---|---|
  | FIFO count cap | 255 | 255 | 191 | 0xBF |
  | RX depth | ~4K | — | — | 511 |
  | Extra commands | 03 clear both, 04/05 UART select, F0 disable (`.../karabas-pro/firmware/src/fpga/profi/rtl/uart/zifi.vhd:272-303`) | — | — | — |
  | ZOFR | — | returns **fill**, not free (`PS/ZiFi.cpp:644`) | — | free |
  | ER | — | constant 0x10 (`PS/ZiFi.cpp:648`) | — | — |

  **Follow the AVR** for TS-Conf fidelity.

---

### 4. New ZiFi projects and existing emulations

| Project | What it is | UART / host | Speaks | Reuse for us |
|---|---|---|---|---|
| `ZSP/ZiFi` (HackerVBI/DDp; v0.733, last commit 790451a 2026-05-21) | Z80 HTTP client `zifi.spg` for TS-Conf | ZiFi API v1 (#BFEF/#C0EF/#C1EF/#C7EF, `zifi.asm:4480-4488`), or the 16550 with `cable_zifi=1` (`:47`) | **Stock AT**: ATE0, CWMODE_DEF=1, CWAUTOCONN=0, CIPMUX=0, CWJAP_CUR, CIPSTART TCP :80, CIPSEND, `+IPD,len:`, CLOSED (`zifi.asm:528-707, 3768-3806, 4507-4550`) | **Primary TS-Conf AT test client.** Note: `zifi_send_raw` writes without per-byte OFR checks (`:880-896`), so TX ring overflow is possible with long requests. |
| `ZSP/ZiFi-ESP-01S-Native-C-Project` (6 commits, 2026-08-01, `native-0.2.2`) | Native C++ firmware for ESP-01S (Arduino ESP8266 4.2.1) plus patched Z80 client | UART0 115200 8N1, no FC, 2 KB RX (`src/uart_transport.cpp:9-17`); ESP-01S 3.3 V, TX/RX crossed (`firmware/README_RU.md:9-14`) | **Own binary protocol, not AT**: `5A CMD LEN_L LEN_H DATA XOR`, max 1024, 500 ms inter-byte resync (`docs/PROTOCOL.md:5-12`; `src/protocol.cpp:26-66`). Cmds 00-0F system/Wi-Fi/FTP, 10-14 NET_OPEN/SEND/RECV/CLOSE/HTTP_GET, 20-22 ping/ipconfig/NTP, FE ACK, EE error (`include/zifi/protocol.hpp:12-68`; `src/main.cpp:662-725`) | Separate "ZiFi native" device personality. `ZiFi/ORIGINAL_AT_COMMANDS.md:16-51` documents a later AT client variant (CIPRECVMODE=1, CIPRECVDATA, GMR). |
| `ZSP/ZiFi-ESP32-S3-Zero` (most active; 0.6.94 on 2026-09-29) | ESP32-S3-Zero firmware + KiCad ESP-01-footprint adapter + Z80 SPG/WC plugins | 115200 8N1, RX=GPIO44 / TX=GPIO43, 2 KB RX, no FC (`include/zifi/uart_transport.hpp:13-15`; `src/uart_transport.cpp:14-22`). Adapter wires 4 nets (GND, 3V3, RX, TX), no level shifting, GPIO0/EN/RST NC (`Zifi ESP32 Zero Adapter/README.md:7-27`) | **Same binary protocol, extended**: SMB, online update, proxy status, weather, WCU_*, VFS 40-5E, events 60-68 (`docs/PROTOCOL.md:18-48, 99-118, 347-366`). Busy rule: commands are dropped while waiting for a Z80 VFS reply; retry on no ACK within 0.5 s (`:315-320`). Z80 transport `shared/z80/zifi_uart.asm:12-175` polls TX_FREE per byte and INIRs ≤ #BF. | Same as above. The Z80 side shows correct ZiFi driver practice (OFR polled per byte). The ESP→Z80 VFS direction means the emulated device would need host filesystem access. **[inferred]** That is out of scope for an AT emulator. |
| `ZEU/zifi32/` (`esp32_emul.cpp`, `zifi32.cpp`) | TS-Conf ESP32-over-SPI "VDAC3" personality on Z-Controller ports #57/#77 | SPI, not UART | Custom register protocol (WR_REGS/RD_REGS/WR_DATA/RD_DATA; cmds GET_INFO, WSCAN, AP_CONNECT, GET_IP...). Fake AP "Unreal Speccy WiFi", fixed IP, **no sockets** (`esp32_emul.h:27-89`; `esp32_emul.cpp:51-153`) | Not relevant to AT. |
| `ZEU/zf232.cpp` | ZiFi #00EF..#C7EF and 16550 #F8EF..#FFEF as Win32 COM-port bridges | Real COM port; ZiFi 115200 fixed (`zf232.cpp:235-251`); RX moves ≤512 B **once per frame** (`zf232.cpp:308-423`; `mainloop.cpp:36-37`) | Nothing; needs a real ESP | Register semantics: 191 clamp, DR switching on IFR/OFR reads, CR/ER, DLAB→baud, LSR/IIR logic (`zf232.cpp:427-783`). Do not copy the per-frame burst delivery. |
| `PS/ZiFi.cpp` | ZiFi + 16550 + ZX-Uno UART bridge to a real ESP-01S (RP2350) | 8 KB IRQ ring + 1 MB PSRAM spill, byte-granular delivery (`ZiFi.cpp:249-251, 286-300, 398-449`) | Nothing; needs a real ESP | Lesson: per-frame-only delivery broke MRF's AT handshake (`ZiFi.cpp:594-613`). Bugs: ZOFR = fill (`:644`), ER constant 0x10 (`:648`), LSR THRE always set (`:827-831`), MSR fixed 0x30, divisor ignored. |
| `PS/ZiFiAT.cpp`, `ZiFiSock.cpp` | Host-side AT clients (OSD Wi-Fi/SNTP/FTP) driving a real ESP | — | AT client | **Oracle of real firmware output**: `WIFI CONNECTED`/`WIFI GOT IP`/`OK`, `+CIFSR:STAIP,"..."`, `+CWJAP:` quoting varies by build, CWLAP first scan often empty, `+CIPSNTPTIME:` 1970 before sync, `ALREADY CONNECTED` on CIPSTART, `>` without CRLF, `SEND FAIL` = buffer full, `+CIPRECVDATA,<len>:` (NonOS 1.x), passive `+IPD,id,len`, data lost on CLOSED in active mode, +IPD continuing after CIPCLOSE (`ZiFiAT.cpp:113-482`; `ZiFiSock.cpp:54-301, 320-585`) |
| `JN/src/esp01/` (jnext; last commit 2026-08-13; **GPLv3** via repo LICENSE, no file headers) | Real in-process ESP-01 AT emulator with host sockets, for the Next UART #133B/#143B | Paced in emulated time: one byte per `ticks_per_byte` from the live prescaler, no banked credit (`esp_at.h:146-166`; `esp_at.cpp:1345-1392`) | AT subset: AT, ATE0/1, RST, GMR, CWJAP?, CIPSTA?, CIFSR, CIPDNS_CUR?, CIPSTART TCP/UDP, CIPSEND(EX), CIPCLOSE[=id], CIPMUX, CIPSERVER, CIPSTO, UART_* (`esp_at.cpp:119-144`). **Deliberate deviations:** echo default off, TCP CIPSTART answers bare OK (no CONNECT), no `ready`/`busy`/`ALREADY CONNECTED`/`SEND FAIL`/`DNS Fail`, one outbound link only, no CWMODE/CWLAP/CWQAP/CIPSTATUS/CIPMODE/CIPRECVMODE/CIPSNTP/CIPDINFO (`esp_at.h:107-111, 198-314`) | **Best architecture reference** (rewrite, do not copy, because of GPLv3): `EspDevice` seam (`esp_at.h:420-466`), passive core + transport interface (`esp_socket.h:258-383, 509-543`), threaded wrapper with try_lock tick (`esp_threaded.h:37-140`), async DNS, address policy denying loopback/link-local/metadata (`esp_address_policy.cpp`), injectable clock, exact-byte tests with a fake transport (`test/esp_at_test.cpp`), inert during replay/RZX (`emulator.cpp:7721-7731`). |
| `KZ/ZXMAK2.Hardware.Circuits/Network/Esp8266AtFirmware.cs`, `Esp8266AT.cs` | Toy AT emulator for the ZX-Uno UART (`KZ/ZXMAK2.Hardware/Uno/UnoUart.cs:11-17`) | Bytes handed over instantly | Every command answers `OK`. `RST`→`ready`, fake `CWLAP`, `CIPSTART` with blocking connect (`DNS Fail\r\n\r\nERROR`), `CIPSEND`→`OK\r\n>` (no space), `Recv n bytes\r\nSEND OK`, `+IPD,len:` chunks of 4096, `CLOSED` (`Esp8266AtFirmware.cs:49-158`) | Minimal proof that the MRZ dialect works with very little. Bugs: always UDP→`SocketType.Stream`; no ATE0 handling (strips `AT+` only); one socket. |

### Conceptual reuse plan **[inferred]**

1. **Device seam (from jnext):** `receive(byte)`, `poll()` on a worker or wall clock, `tick(elapsedT, tPerByte)` on the emulation thread, and `wantsTick()` as a hot-path gate.
   - For the 16550: tPerByte = divisor × 16 × frame bits, from DLL/DLM/LCR; add RTS gating.
   - For ZiFi: a fixed 115200 rate into the 511-byte ring.
2. **Front ends, kept separate from the AT core:**
   - `Uart16550Evo`: the AVR-faithful register model (§2.1).
   - `ZiFiApi`: the AVR-faithful register model (§3).
   - Both talk to the same `EspAtDevice`.
   - Later, a separate `ZiFiNativeDevice` for the binary protocol.
3. **AT core personality:**
   - Stock NonOS AT 1.x behaviour: echo on at boot, `ready` after RST/power-on, autoconnect URCs, `CONNECT`+`OK`, `> `, `Recv n bytes`/`SEND OK`, `+IPD` in both mux forms and the CIPDINFO form, `[id,]CLOSED`, `ALREADY CONNECTED`, `busy p...` only on true overlap, CIPSNTP*, CIPRECVMODE/CIPRECVDATA, CIPSTATUS.
   - Optional ESP32-AT-2.x flavour for `+TIME_UPDATED`.
4. **Sockets:** a non-blocking host transport with async DNS and an address policy (concept from jnext), kept out of the TTD sealed track.
   - Inbound network bytes are an outside input and must be recorded at the register or UART level for replay.
   - **[inferred]** Per the project's TTD sealed-replay principle, record ESP→UART bytes with emulated timestamps. The device is then inert during replay, like jnext's gate.

---

### 5. Open questions

1. **Firmware generation to emulate.**
   - ATLOG is AT 0.60 / SDK 1.5.2.
   - CIPRECVMODE and CIPSNTP* need AT ≥ 1.5/1.6 (NonOS); `+TIME_UPDATED` is ESP32 AT 2.x.
   - Proposal: default to "ESP8266 NonOS AT 1.7.x" and add a config switch for ESP32 AT 2.x. NedoOS's shipped `espType=32` hints that many users run ESP32 modules.
   - Needs confirmation of the exact `AT+CIPSNTPCFG` reply on 8266 AT 1.7 (only OK?).
2. **Boot banner bytes.** Should the 74880-baud ROM garbage be emitted? It is only harmless if clients scan for substrings. **[inferred]** Emitting a few non-ASCII bytes plus `\r\nready\r\n` is fine for every client seen. Power-on timing should be about 0.3-1 s.
3. **CIPCLOSE after a peer close:** stock replies `ERROR` or `CLOSED`+`OK`? It affects calendar/zxdb two-line readers. Needs a hardware log.
4. **`AT+CIPSERVER=0` with no server:** `OK` (ATDOC implies) or `ERROR` (jnext claims hardware)? Clients ignore it, so low impact.
5. **`+IPD` segmentation:** should one host recv map to one `+IPD` (≤1460)? The ZiFi and KNT clients need the HTTP header inside the first `+IPD` (`ZSP/ZiFi/zifi.asm:581-605`; `KNT/alerts/http.asm:105-143`). A small first segment would break them, as it would on real hardware.
6. **RTS fidelity vs simplicity:** does any client break if the emulator ignores RTS and uses the 511-byte AVR ring? No case was found. Decide whether to model OE/overrun at all.
7. **ZX-WiFi card (MRF TR-ZW, AFE on MCR bit 5):** is it a real 16550 with a 16-byte FIFO and auto-RTS, unlike the AVR? If so, it needs its own front-end variant. Not verified locally.
8. **ZiFi ER after SETAPI and GETVER (AVR 0x00/0x01) vs pico-spec 0x10:** confirm on hardware or against TS-Conf firmware release notes. The AVR source is authoritative here.
9. **Binary-protocol ZiFi firmwares (ESP-01S native, ESP32-S3-Zero):** emulate them later as a second personality? Their VFS-to-Z80 direction and FTP/SMB/WebDAV servers go far beyond AT. Is that scope wanted?
10. **ATM2 COM (#55FE protocol, comType 1) and ATM2IOESP (comType 3, #FB/#FA index/data):** in scope for the ATM710/ATM3 models? Clients exist (`MRF/drivers/uart-atm.asm`; `KAP/common/esp-com.c:46-62`).
11. **Licence:** jnext is GPLv3, so take concepts only. pico-spec and zx-evo-unreal licences were not checked.

---

## Part 3. How the model follows it

Code: `core/src/emulator/io/serial/esp/` - `EspStack` (socket slots on the
virtual network: TCP, UDP, servers with a client queue, DNS, ICMP echo, the
firmware's own UDP query), `EspModule` (UART side, 4 KB receive buffer,
turnaround, line format, flow control, Wi-Fi on the virtual access point
`UnrealNG`, exchange log), `EspnetModule`, `AtModule`.

| Topic | Model |
|---|---|
| ESPNET framing, commands, errno, payload sizes | Part 1 §1-§2 byte for byte (INFO 53, WIFI_STATUS 45, scan record 42, UART 8; CRC frames honored; bad CRC dropped) |
| One request at a time | CONNECT and DNSRESOLVE hold later frames in the receive buffer until their reply is out |
| Incoming TCP buffered until READ | `EspStack` keeps all of it (the 2 KB slot buffer plus the TCP window), journal references in TTD |
| AT ordering (Part 2 §2.4) | CONNECT before OK, OK before `> `, SEND OK before `+IPD`, CLOSED after the last data, nothing else between a command and its answer; `busy p...` for a line that arrives mid-operation |
| AT personality | ESP8266 = NonOS AT 1.7.4 strings; ESP32 = ESP32 AT 2.2 (`+TIME_UPDATED` after SNTP) |
| AT DNS, SNTP, PING | through the virtual network (Hosts=, then the host resolver; NTP over UDP 123; ICMP echo): every answer journaled |
| Wi-Fi | powers up joined to the virtual access point `UnrealNG` with a DHCP lease of the virtual network; joining another SSID never completes (ESPNET) / fails with `+CWJAP:3`, `FAIL` (AT); WIFI_DISC / CWQAP drop it |
| Line and flow control | the module runs 115200 8N1 until UART SET / UART_CUR; a ZX at another rate or format loses bytes both ways; AT flow field bit 1 off = the module ignores the ZX's RTS |
| Reply timing | 200 us turnaround; scan ~1.2-1.5 s; join 1.5 s; RST "ready" 300 ms after OK |
| Z80 reset | does not reach the module (separate chip) |

Not modeled: the ESP8266 ROM boot log at 74880 baud (garbage at 115200) after
power-up / RST; SSL links (`CIPSTART="SSL"` answers ERROR: the virtual network
has no TLS); OTA, the web UI and the USB-side AT console of ESPNET.

## Part 4. Open questions

1. **RX FIFO on the ZX-Evo**: the BaseConf AVR firmware has 16-byte FIFOs (the
   model, [reference-evo-com-port.md](reference-evo-com-port.md) §2); the TS-Conf
   AVR firmware (also boots BaseConf) has 511 / 255-byte rings. Moon Rabbit's
   ~1.7 ms RTS window lets ~20 bytes through, more than a 16-byte FIFO holds.
   Which AVR firmware do real users run with these clients?
2. **Overruns during an AT reboot**: NedoOS `espReBoot` showed 3 overruns in the
   live check (the data path itself had none) - `uartFlush` holding RTS while
   the boot lines arrive. Expected on hardware with a 16-byte FIFO, or a sign the
   AVR access time (52 T, estimated) is too long?
3. Part 1 §7 and Part 2 §5 list the protocol-level ambiguities (bytes per RTS
   pulse, the dead close-all branch in the kernel, CIPSERVER=0 and CIPCLOSE
   replies by firmware version, `+IPD` segmentation).
