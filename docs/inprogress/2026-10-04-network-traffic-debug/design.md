# Network traffic for the debuggers - design

**Status:** design settled 2026-10-04 (owner answers Q1-Q3 in section 8); build in phases T1-T4. Task: [README.md](README.md),
PLAN #91.

## 1. What a user gets

One list of everything a machine's network adapters sent and received, on every machine and host:

```text
#   time (machine)     adapter     dir   summary
12  f 4504  +20 ms     isa2.eth    out   ARP who-has 172.16.0.1 tell 172.16.15.142
13  f 4506  +2 ms      isa2.eth    in    ARP 172.16.0.1 is-at 74:AC:B9:1B:DE:A4
40  f 9120  +4 ms      zxnetusb    out   TCP 10.0.2.15:1025 > 93.184.216.34:80 connect
41  f 9122             zxnetusb    in    TCP 93.184.216.34:80 > 10.0.2.15:1025 connected
42  f 9123             zxnetusb    out   TCP 120 bytes: GET /index.html HTTP/1.0
```

Select a packet: its decode and hex. "Seek here": the time-travel debugger goes to the moment it passed. Save as
pcapng for Wireshark. The same list on every automation surface and in a Qt window.

## 2. One tap point: the virtual network

Since the refactor of 2026-10-04 ([sn6-bridge-design.md](../2026-10-02-sprinter-network/sn6-bridge-design.md) §12)
every adapter of every machine reaches the network only through `VirtualNetwork`:

| What crosses it | Where in `VirtualNetwork` |
|---|---|
| a frame card's frame (NE2000, 3C509B; NAT or bridge) | `Transmit` (out), the gateway's delivery to a card (in) |
| a socket adapter's operation (W5300, ESP-AT, ZiFi, the modem, the gateway's own NAT sockets) | `Connect`, `Send`, `SendTo`, `Listen`, `Close` (out), `Deliver` (in: the virtual network's own answers and the host's) |
| a frame from the host LAN (bridge) | `ApplyHostFrame` |

`NetworkTrafficTap` (`core/src/emulator/io/network/traffic/`) is a member of `VirtualNetwork`; those functions call it.
No adapter, gateway or machine code changes. Each record: the adapter (its link key, or the socket's owner name),
direction, kind (frame / socket operation), the bytes, machine frame + T-state, the TTD position.

## 3. Formats

- **Frames** are Ethernet frames already.
- **Socket operations** are written as packets Wireshark decodes: Ethernet + IPv4 + TCP / UDP between the adapter's
  address and the peer, sequence numbers kept per socket (connect = SYN / SYN-ACK / ACK, data = one segment, close =
  FIN, reset = RST), plus a pcapng comment with the real operation. The bytes are exact; the TCP framing is not what
  the host's stack did and the list marks these packets "socket".
- **pcapng**: one interface per adapter, timestamps in emulated time, a comment per packet with frame / T-state.

## 4. Time travel

The tap only records. A replay runs the same code, so the replayed traffic is captured again exactly; "Seek here"
uses the record's TTD position.

## 5. Interfaces

| Surface | Read | Control |
|---|---|---|
| WebAPI + OpenAPI | `GET .../network/traffic?since=&adapter=&proto=&last=` (JSON), `format=pcapng` | `POST .../network/traffic {action}` |
| CLI | `network traffic [filter] [file.pcapng]` | `network traffic clear` |
| Lua / Python | `network_traffic{...}`, `network_traffic_pcapng()` | `network_traffic_clear()` |
| MCP | `inspect_state` aspect `network` + `invoke_api` | |
| Qt | Tools > Network traffic (Ctrl+6): list, filter, decode, hex, Seek here, Save pcapng | Clear, Record to file, stream |
| Live | a pcapng stream for Wireshark (section 8 Q3) | |

`GET /network/frames` becomes a view of the tap (frame records only).

## 6. Cost

Work only when bytes move (no per-instruction or per-port cost); a copy into a bounded ring. Measured with a large
`WGET`, tap on and off, before landing.

## 7. Phases

| Phase | What | Size |
|---|---|---|
| T1 | the tap in `VirtualNetwork`, the always-on ring, start / stop recording into a pcapng file, JSON on every surface, pcapng export (frames + socket operations) | M |
| T2 | socket operations as decodable TCP / UDP packets; summaries (ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS, HTTP first line) | M |
| T3 | live stream: the pcapng TCP port, then the Wireshark extcap script | S-M |
| T4 | Qt window with Seek here | M |

## 8. Owner questions (one at a time)

- Q1. Always recording, or only after a "start"? **Owner, 2026-10-04: both** - a small ring always records (the
  last N packets are there when something went wrong); a "start" adds an unbounded recording into a pcapng file
  (written as packets come, until "stop"; the file path is the caller's). Default ring 8 MiB of packet bytes.
- Q2. Socket operations as synthetic TCP / UDP packets (Wireshark decodes them) or as plain records? **Owner,
  2026-10-04: both** - our list (JSON, CLI, Qt) shows the operation as it was (`zxnetusb socket 3 Send 120 bytes`
  + hex); the pcapng for Wireshark carries the synthetic TCP / UDP packets of section 3, each with the operation
  as its comment.
- Q3. Live stream: a TCP port Wireshark reads (`-i TCP@127.0.0.1:port`) or an extcap plugin? **Owner, 2026-10-04:
  both** - the TCP port with a pcapng stream is the base (nothing to install, every OS; one port per emulator); an
  extcap script (`tools/wireshark/unreal-ng-extcap.py`) on top lists the running emulators and their adapters in
  Wireshark's interface list and reads the same stream.
