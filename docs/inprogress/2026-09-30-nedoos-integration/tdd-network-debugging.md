# Network debugging: Wireshark, bus sniffing, monitoring and decoding (TDD)

**Status:** design, 2026-09-30. Built after the adapters themselves (network
TDD step D); this document fixes what the adapters must expose so it can be
built without reworking them. Folder index: [README.md](README.md).

| Related | |
|---|---|
| Network adapters TDD (boundaries, `NetTap` promise) | [tdd-network.md](tdd-network.md) §11 |
| Adapters and their buses | [network-adapters-catalog.md](network-adapters-catalog.md) |
| W5300 registers the bus decoder names | [reference-w5300-model.md](reference-w5300-model.md) |
| NedoOS layer (socket owner = task) | [requirements-nedoos-layer.md](requirements-nedoos-layer.md) NK-14 |
| A bug these tools would have shown at once | [nedoos-bugs.md](nedoos-bugs.md) B-1 |

---

## 0. In one page

When a ZX network program misbehaves, the question is always "at which
level": did the program drive the chip wrong, did the chip model answer
wrong, did the virtual network do something odd, or did the Internet say no.
We make every level visible, in the emulator and in Wireshark:

| Level | Example of what you see |
|---|---|
| **Bus** (Z80 <-> adapter) | `OUT #03AB,#20` decoded as `S0.CR <- SEND (31 bytes queued)`; 2000 identical `IN #09AB` collapsed into `S0.SSR polled x2000 = #FF` |
| **Line** (UART only) | `ZX -> ESP  AT+CIPSTART="TCP","next.zxart.ee",80\r\n`, RTS pulses, overruns |
| **Socket** (adapter <-> virtual network) | `task 6 zxdb, S1 UDP 10.0.2.15:49153 -> 8.8.4.4:53, 31 bytes, DNS A next.zxart.ee` |
| **Host** (virtual network <-> host) | `connect 94.130.x.x:80 ok in 43 ms`, `getaddrinfo next.zxart.ee -> 94.130.x.x` |

**Wireshark** shows all four at once, live or from a file: one capture
with several interfaces. The socket level becomes real Ethernet / IP / TCP /
UDP frames that Wireshark's own dissectors decode (HTTP, DNS, DHCP, NTP ...);
the bus and line levels use small Lua dissectors we ship. Time is the
emulated time, so a capture of a TTD replay is identical to the live one.

**Worked example (the zxdb hang).** Start a capture from Wireshark's
interface list ("unreal-ng: ZX-Evo #1, all adapters"), search in zxdb:
- the socket interface shows a DNS query frame from `10.0.2.15` to `8.8.4.4`
  and nothing after it;
- the bus interface shows `S1.TX_FIFO <- 31 bytes`, `S1.WRSR <- 31`,
  `S1.CR <- SEND`, then `S1.CR polled x180000 = #FF`: no card answers;
- the bus event carries the PC and the NedoOS task (`w53_cmd0`, task 6).

## 1. Terms

| Term | Meaning |
|---|---|
| **Bus event** | one I/O cycle the adapter saw: time, PC, port, direction, value, plus the decoded meaning |
| **Line event** | a byte or a modem-line change on a serial line, with direction |
| **Socket event** | open, connect, send, receive, close, error on a guest socket, with payload |
| **Host event** | what the host bridge did: resolve, connect, send, receive, close, error, timing |
| **Tap** | a subscriber to one level's events; zero cost when nobody subscribes |
| **pcapng** | Wireshark's file format: several interfaces with different link types in one file, per-packet comments |
| **extcap** | Wireshark's plugin interface for external capture sources: Wireshark runs a helper that lists interfaces and streams pcapng into a pipe |
| **Dissector** | Wireshark decoder for one protocol; ours are Lua scripts |

## 2. Requirements

| # | Requirement |
|---|---|
| DBG-1 | Four capture levels (bus, line, socket, host) per adapter, each with a tap; no subscriber = no cost on the port path and per frame |
| DBG-2 | Per-adapter bus decoders: W5300 / ZXNETUSB, 16550 (Evo AVR, ZX-WiFi), ATM2 COM; later ZiFi, AY-UART. Registers named, socket context tracked, FIFO transfers aggregated, poll loops collapsed |
| DBG-3 | Line protocol decoders: ESP AT (commands, responses, `+IPD` blocks) and ESPNET (frames, commands, status) |
| DBG-4 | Socket level as synthesized Ethernet / IPv4 / TCP / UDP / ICMP frames that Wireshark decodes natively: consistent MACs, addresses, ports, TCP sequence numbers, handshake and teardown, checksums, segmentation |
| DBG-5 | pcapng export: live capture ring to file; any TTD recording to file (replayed, sealed) |
| DBG-6 | Live Wireshark through extcap: interfaces per running emulator instance and adapter, capture filters, start / stop from Wireshark |
| DBG-7 | Lua dissectors shipped with the emulator for the bus and line link types; install instructions per OS |
| DBG-8 | Emulated time stamps by default (deterministic, same on replay), wall-clock optional; every frame carries frame number, T-state, PC, adapter, socket, and the NedoOS task when the NedoOS layer is active (pcapng comments + dissector fields) |
| DBG-9 | In-emulator monitoring: socket table, per-socket stream view (text / hex, both directions), counters and rates, bus log with filters, decoded AT / ESPNET dialog, HUD activity indicator; on all automation surfaces and a Qt panel |
| DBG-10 | Triggers and breakpoints: stop or mark on a bus condition (register / command / value), socket condition (connect to X, data containing Y, DNS name Z), host error |
| DBG-11 | Fault injection at the socket and host levels (delay, drop, refuse, reset, slow link, DNS failure, truncation); faults are journaled net events, so they replay |
| DBG-12 | Bounded memory: rings with caps, filters applied at the tap, payload by reference into the TTD payload store where possible |

## 3. Taps and events

### 3.1 Where the taps sit

```
 Z80 ---- bus tap ---- adapter (W5300 / UART) ---- line tap (UART) ---- ESP module
                          |                                               |
                          +------------- socket tap ----------------------+
                                            |
                                     virtual network
                                            |
                                        host tap
                                            |
                                       host bridge
```

- **Bus tap** is inside the adapter's port handler (it already has port,
  value, direction; PC and time come from the context). It is **not** the
  global port trace: the adapter adds its own decoded meaning and keeps state
  (current socket from `#81AB`, FIFO pairing, poll collapsing) that a generic
  port trace cannot know. The global port trace keeps working alongside.
- **Line tap** in `Uart16550` (both directions, modem lines, errors) and in
  the ATM2 COM port.
- **Socket tap** in `VirtualNetwork` (one place for every adapter: W5300 and
  ESP sockets are VNet sockets).
- **Host tap** in `HostNetBridge` (bridge thread; events cross to the reader
  through a lock-free ring, never blocking the bridge).

### 3.2 Event records

```cpp
struct NetDebugEventHeader
{
    uint64_t emuTime;        // T-states since power-on (frame * frameT + t)
    uint64_t hostTimeNs;     // wall clock, for host events and optional stamping
    uint32_t frame;
    uint16_t pc;             // bus / line events: PC of the IN / OUT
    uint8_t  level;          // Bus, Line, Socket, Host
    uint8_t  adapter;        // adapter instance id
    uint8_t  socket;         // guest socket, 0xFF = none
    uint8_t  task;           // NedoOS task id when known, 0 = unknown
    uint16_t kind;           // per-level event kind
};
// followed by a small fixed body per kind, and for data a payload reference
// (TTD payload store offset + length) or an inline copy for short data
```

Rings per level (`RingBuffer<T>` for the emulation-thread levels, an SPSC
ring for the host level), sized in the config; overflow drops the oldest and
counts it.

### 3.3 Zero cost when off

Each tap is a pointer check on a subscriber list (the analyzer pattern). The
adapter's port handler pays one well-predicted branch per access when nobody
listens. The bus decoders run only for subscribers. A/B benchmark on the
port path before landing (performance guidelines).

## 4. Bus decoders (per adapter)

A decoder turns bus events into readable records and keeps just enough
state to do so. Interface:

```cpp
class IBusDecoder
{
public:
    virtual void OnBusEvent(const BusEvent& e, std::vector<BusRecord>& out) = 0;
    virtual void Flush(std::vector<BusRecord>& out) = 0;   // close open aggregations
    virtual ~IBusDecoder() = default;
};
```

### 4.1 ZXNETUSB / W5300

| Input | Record |
|---|---|
| `OUT #81AB,n` | `select S(n-8)` or `select common block n` (context for later accesses) |
| `OUT #82AB` / `#83AB` | `mode: W5300 in I/O space`, `W5300 reset asserted / released` |
| socket register write | `S1.MR <- UDP`, `S1.PORTR <- 49153`, `S1.DIPR <- 8.8.4.4` (multi-byte writes merged) |
| `S*.CR` write | command name + context: `S1.CR <- SEND (WRSR 31)` |
| TX FIFO words | aggregated: `S1.TX_FIFO <- 31 bytes` with the bytes as payload |
| RX FIFO words | aggregated per packet: `S1.RX packet UDP from 8.8.4.4:53, 45 bytes` (the decoder parses PACKET-INFO as the program reads it) |
| repeated identical reads | collapsed: `S1.SSR polled x2000 = #17`, with first / last time |
| accesses in reset, unknown registers, FIFO pairing breaks, lone FIFOR1 | flagged as warnings |

Checks the decoder can flag because it knows the chip: RX read past the
packet end, `RECV` before the packet was consumed, `RSR` not zero after the
last `RECV` (the NedoOS B-1 symptom), SEND with WRSR larger than written,
FIFO access while another register is selected mid-pair.

### 4.2 16550 (Evo AVR COM port, ZX-WiFi)

| Input | Record |
|---|---|
| register writes | `LCR <- 8N1 DLAB`, `DLL <- 1 (115200)`, `FCR <- FIFO on, clear, trigger 8`, `MCR <- RTS DTR AFE` |
| THR writes / RBR reads | aggregated into line runs: `ZX -> peer 23 bytes`, `peer -> ZX 7 bytes` |
| LSR polls | collapsed: `LSR polled x120 until DR` |
| RTS pulses (Evo software flow control) | `RTS pulse x31` with the bytes they let through |
| errors | overrun, framing (baud mismatch between ZX and peer), writes while THRE = 0 |

### 4.3 ATM Turbo 2+ COM port

The keyboard-controller command sequence (`IN #55FE` + command in the high
byte) decoded into `status`, `read byte`, `write byte`, `set DTR/RTS`,
`RX count`, `set baud`, with the same line aggregation as §4.2.

### 4.4 Later adapters

ZiFi (TS-Conf FIFOs and command register), AY-UART (bit-banged serial on AY
port A: the decoder reassembles bytes from register 14 writes and reports
bit timing errors).

## 5. Line protocol decoders

| Protocol | Records |
|---|---|
| **ESP AT** | one record per command line and per response line; `AT+CIPSEND=n` joined with the `>` prompt and the n data bytes; `+IPD,len:` blocks with data; `busy p...`; unanswered commands and timeouts flagged |
| **ESPNET** | one record per frame: SOF, command name, socket, seq, length, status name (errno), payload summary (`CONNECT 94.130.x.x:80`, `READ 512 -> 312 bytes`); desync (bytes before SOF) flagged |

Both run in the emulator (monitoring) and as Lua dissectors in Wireshark
(the line interface carries the raw bytes; the dissector does the same
decoding), sharing one test corpus.

## 6. Wireshark

### 6.1 Interfaces in one capture

pcapng allows several interfaces with different link types in one file:

| Interface | Link type | Content | Decoded by |
|---|---|---|---|
| `net` (per adapter) | `LINKTYPE_ETHERNET` (1) | synthesized frames of the socket level | Wireshark itself (HTTP, DNS, DHCP, NTP, telnet ...) |
| `bus` (per adapter) | `LINKTYPE_USER0` (147) | bus records (raw access + decoded fields) | our Lua dissector `unrealng.bus` |
| `line` (per UART) | `LINKTYPE_USER1` (148) | line runs with direction and modem lines | `unrealng.line`, then `unrealng.at` / `unrealng.espnet` |
| `host` | `LINKTYPE_USER2` (149) | host bridge events | `unrealng.host` |

Every packet has a pcapng comment: `frame 12345 t=40211 pc=#5183 task 6 zxdb
S1`. The Lua dissectors expose the same as fields (`unrealng.pc`,
`unrealng.task`, `unrealng.socket`), so display filters like
`unrealng.task == 6` work on every interface.

### 6.2 Synthesized frames (socket level -> Ethernet / IP)

| Item | Rule |
|---|---|
| MAC addresses | guest: the W5300 `SHAR` (or the ESP's reported MAC); gateway: a fixed locally administered MAC; other hosts appear behind the gateway MAC |
| IP addresses | guest: the DHCP lease or the guest's `SIPR`; remote: the real destination |
| TCP | per connection: SYN / SYN-ACK / ACK at connect, data segments (split at 1460), ACKs, FIN / RST at close; sequence numbers start at a hash of the connection so repeated exports are identical; checksums computed |
| UDP, ICMP | one frame per datagram / echo |
| DHCP, DNS, gateway ping | the virtual network's answers are real frames too |
| Timing | frame time = emulated time of the socket event |

This is a faithful picture of what the guest sent and received, not of the
host's real packets (those belong to the host; `host` interface events say
what the host did).

### 6.3 Live capture: extcap

`tools/wireshark/extcap/unrealng-extcap.py` (Python 3, no dependencies), put
into Wireshark's extcap folder:

| Wireshark asks | The helper does |
|---|---|
| `--extcap-interfaces` | lists running emulators (WebAPI on the configured ports) and their adapters: `unreal-ng #1 ZX-Evo: ZXNETUSB`, `... COM port`, `... all` |
| `--extcap-dlts` | the link types of §6.1 |
| `--extcap-config` | options: levels to include, bus poll collapsing on / off, time base (emulated / wall), WebAPI address |
| `--capture --fifo <pipe>` | opens the WebAPI capture stream (§8) and copies pcapng blocks into the pipe until Wireshark stops it |

Works the same on macOS, Linux and Windows (Wireshark runs the helper; on
Windows through a `.bat` wrapper). No named pipes to set up by hand.

### 6.4 Files

- **Export the live ring**: `network capture save <file.pcapng>` on every
  surface.
- **From a TTD recording**: replay the recording (sealed, fast, no display)
  with the taps on and write pcapng; identical every time. The ttd-analyzer
  can later do the socket and host levels without the emulator (payloads and
  times are in the `.ttd`), the bus level needs the device replay.
- **Open in Wireshark** from the emulator: the Qt panel's "Open in Wireshark"
  saves and launches Wireshark if it is installed.

### 6.5 Dissector install

`tools/wireshark/dissectors/unrealng.lua` (one file: bus, line, AT, ESPNET,
host), copied to the Wireshark personal plugins folder; the extcap helper
prints the path when a capture starts and the dissector is missing. The
dissector reads a version byte in every record and refuses newer versions
with a clear message.

## 7. In-emulator monitoring

| View | Content |
|---|---|
| Adapters | fitted adapters, link, lease, counters, rates, taps active |
| Sockets | the socket table (mode, state, addresses, bytes, RX queued, owner task) with history of closed sockets |
| Stream | one socket's bytes, both directions, text / hex, with times; follow mode |
| Bus log | decoded bus records with filters (adapter, socket, register, command, warnings only) |
| Line / dialog | the AT or ESPNET dialog of a UART |
| HUD | per adapter: link, TX / RX activity (the FDD / audio indicator pattern) |

All on CLI, WebAPI + OpenAPI, MCP, Lua, Python; the Qt "Network" panel shows
them together, with "Open in Wireshark".

## 8. Automation API (outline)

| Operation | |
|---|---|
| `network capture start` | levels, adapters, filters, ring sizes, time base |
| `network capture stop` / `status` | counts, drops, memory |
| `network capture save <file>` | pcapng |
| `network capture stream` | WebAPI: chunked HTTP (or WebSocket) stream of pcapng blocks, used by the extcap helper |
| `network bus [--adapter --socket --since]` | decoded bus records |
| `network stream <socket>` | the byte stream of one socket |
| `network dialog <uart>` | AT / ESPNET records |
| `network trigger add` | DBG-10 conditions: stop, mark, or start / stop the capture |
| `network fault add` | DBG-11 faults |

## 9. Triggers, breakpoints, faults

- **Triggers** evaluate on decoded records, not on raw ports: "`S*.CR <- CONNECT`
  to 94.130.0.0/16", "RX data contains `HTTP/1.1 404`", "DNS name
  `*.zxart.ee`", "bus warning of any kind", "host connect error". Actions: pause
  the machine (a breakpoint, visible in the debugger with the record), put a
  TTD marker, start or stop the capture.
- **Faults** are rules on the virtual network and host bridge: delay
  (fixed / jitter), drop (UDP), refuse / reset (TCP), truncate, throttle
  (bytes per second), DNS failure or override. A fault produces ordinary net
  events, journaled, so a failing run replays exactly.

## 10. TTD

- Capture is not an input: turning taps on or off changes nothing in the
  machine and nothing in the recording.
- Replay produces the same records and the same pcapng (emulated time
  stamps, deterministic synthesis).
- Every TTD marker placed by a trigger is a normal marker (search, timeline).

## 11. Tests

| Test | How |
|---|---|
| Bus decoder W5300 | recorded bus sequences from the NedoOS driver (socket, connect, send, recv, close, the zxdb hang) -> expected records; poll collapsing; B-1 symptom flagged |
| Bus decoder 16550 | register sequences from NedoOS `esp-com.c`, `_sdk/espnet.asm`, Moon Rabbit drivers |
| AT / ESPNET decoders | byte corpora (both directions) -> expected records; the same corpus checks the Lua dissectors (run with `tshark -X lua_script:...` in a tools test, skipped when tshark is absent) |
| Frame synthesis | TCP connection lifecycle -> frames that `tshark` parses without warnings (checksums, sequence numbers, handshake) |
| pcapng writer | round trip through `tshark -r` / a small pcapng reader; several interfaces, comments |
| Determinism | capture of a TTD replay == capture of the live run |
| Cost | port path A/B with no subscriber |

## 12. Steps

| Step | Content |
|---|---|
| D1 | taps and event records in the adapters (with the adapters, N1a / N2: only the hook points, no subscribers) |
| D2 | socket table and stream view on all surfaces (the most useful view, no Wireshark needed) |
| D3 | pcapng writer, frame synthesis, file export (live ring and TTD) |
| D4 | W5300 bus decoder, 16550 bus decoder, AT / ESPNET line decoders (emulator side) |
| D5 | Lua dissectors, extcap helper, capture stream endpoint |
| D6 | triggers and breakpoints, fault injection, HUD, Qt panel |

## 13. Decisions needed

1. Stream transport for extcap: chunked HTTP from the existing WebAPI
   (Drogon) or WebSocket. Proposed: chunked HTTP (simplest client).
2. Where the Wireshark files live: `tools/wireshark/` (proposed) or inside the
   app bundle, installed by the Qt app on request.
