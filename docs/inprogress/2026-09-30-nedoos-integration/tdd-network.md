# Network adapters: technical design (TDD)

**Status:** design 2026-09-30; steps N0, N1a and N1b implemented the same day
on branch `network-w5300` (see §15). Order of work agreed with the user: this
TDD, then the W5300 card (ZXNETUSB), then the ESP adapters, then the rest.
Interface and traffic debugging: [tdd-network-debugging.md](tdd-network-debugging.md), built later.

| Related | |
|---|---|
| Adapter catalog and priorities | [network-adapters-catalog.md](network-adapters-catalog.md) |
| W5300 behavior for the model | [reference-w5300-model.md](reference-w5300-model.md) |
| wizcfg.com, disassembled | [reference-wizcfg.md](reference-wizcfg.md) |
| NedoOS layer (socket view NK-14, busy report NK-21) | [requirements-nedoos-layer.md](requirements-nedoos-layer.md) |
| The zxdb hang this fixes | [nedoos-kernel-reference.md](nedoos-kernel-reference.md) §6 |
| TTD sealed replay | `docs/inprogress/2026-09-25-ttd-v2-migration/requirements.md` §2.5 |

---

## 0. In one page

A ZX program that wants the network talks to a network adapter through I/O
ports. We emulate the adapters the software actually uses and connect them to
the host computer's network through ordinary host sockets. No drivers, no
administrator rights, no real network card is touched.

```
 ZX program (NedoOS kernel, wizcfg, zxdb, TR-DOS tools)
      | IN / OUT
 Adapter model         ZXNETUSB card + W5300 chip   |  COM port (16550) + ESP module
      | socket operations (open, connect, send, receive, close)
 Virtual network       the machine's own small LAN: DHCP server, gateway, DNS
      | host socket operations, and events back
 Host bridge           one thread, non-blocking sockets (macOS, Linux, Windows)
      |
 Host network / Internet
```

Three ideas carry the design:

1. **Socket level, not packet level.** Both the W5300 and the ESP modules are
   socket chips: the ZX side says "connect to 1.2.3.4:80, send these bytes".
   We never build Ethernet frames. A small virtual network in the middle
   answers what a home router would (DHCP, gateway, DNS) and forwards the rest
   to host sockets. This is how QEMU's user networking works.
2. **Everything from the host is a recorded input.** Every event coming back
   from the host (connect succeeded, 512 bytes arrived, peer closed) enters the
   machine through the TTD input journal with its exact emulated time. A
   recording replays bit-exact with the network unplugged (sealed replay).
3. **No adapter looks like no adapter.** When nothing is configured, the ports
   read `#FF` exactly as today; the machine behaves like real hardware without
   the card (NedoOS then hangs in the driver, as it does on a real Evo).

**Worked example.** The user's NedoOS card with ZXNETUSB enabled:

1. At boot, `autoexec.bat` runs `wizcfg.com`. It finds the card (`#81AB`
   reads back `#0A`), pulses the W5300 reset, writes the MAC, sees that the
   chip stores `#AA` in the subnet register (a real chip does), and runs DHCP.
   The virtual network's DHCP server answers: IP `10.0.2.15`, gateway
   `10.0.2.2`, DNS `10.0.2.3`.
2. In zxdb the user searches for "dizzy". zxdb asks the kernel's DNS
   (`8.8.4.4`, hard-coded in the kernel) for `next.zxart.ee`; the virtual
   network answers the DNS query from the host resolver.
3. zxdb opens a TCP connection to the answer, port 80. The W5300 model shows
   `SYNSENT` while the host bridge connects, then `ESTABLISHED`. The HTTP
   request goes out, the reply comes back as W5300 receive packets, zxdb shows
   the list.
4. The session is recorded. Replayed later with Wi-Fi off, zxdb shows the same
   list at the same frame.

## 1. Terms

| Term | Meaning |
|---|---|
| **Adapter** | the device the ZX sees on its ports (ZXNETUSB card, COM port) |
| **W5300** | the WIZnet Ethernet chip on the ZXNETUSB card: 8 hardware sockets, TCP/UDP/raw IP in the chip |
| **Socket (guest)** | one of the chip's (or ESP's) connections, driven by the ZX program |
| **Host socket** | a socket of the host operating system, owned by the bridge |
| **Virtual network (VNet)** | the emulated LAN between adapter and host: DHCP, gateway, DNS, port forwarding |
| **Host bridge** | the thread that does all host socket work and reports events |
| **Net event** | anything the host reports to the machine: connect result, data, close, error, DNS answer |
| **Payload store** | where the TTD journal keeps the bytes of net events (journal events have a fixed size) |
| **ESP personality** | what a virtual ESP module speaks on the UART: stock AT commands or NedoOS ESPNET frames |
| **Sealed replay** | replaying a recording with no access to the outside world; only recorded inputs feed the machine |

## 2. Requirements

| # | Requirement |
|---|---|
| NET-1 | ZXNETUSB card on every machine with a ZX-Bus (ZX-Evo, ATM, Pentagon, Scorpion, Profi), ports decoded as the card's CPLD does |
| NET-2 | W5300 model faithful to the datasheet where software can see it: registers, reset, FIFO rules, packet headers, state machine; TCP client and server, UDP, raw IP for ping |
| NET-3 | The user's NedoOS card works unchanged: `wizcfg` (DHCP), zxdb, browser, telnet, ping, time2, zxart radio, 3ws/scrnet servers |
| NET-4 | A virtual network per emulator instance: DHCP, gateway, DNS answered from the host resolver, configurable port forwarding for guest servers |
| NET-5 | Host access through plain sockets on macOS, Linux, Windows; no privileges; one bridge thread; nothing blocks the emulation thread |
| NET-6 | Every host event is a TTD input with exact time and payload; replay is sealed; state blobs let TTD seek; returning to live after a seek closes guest connections deterministically |
| NET-7 | Nothing configured = today's behavior (ports `#FF`); zero cost on the port path and per frame when no adapter is fitted |
| NET-8 | Configuration per machine (INI) and at runtime (feature + automation), including the legacy `[MISC] Modem=` / `ZiFi=` keys |
| NET-9 | ZX-Evo COM port and ZX-WiFi (16550) with pluggable peers: ESPNET module, AT module, host serial port, TCP pass-through, loopback |
| NET-10 | ESP personalities on the same virtual network: ESPNET 1.27 (all commands NedoOS uses) and stock AT (the subset NedoOS, Moon Rabbit and zx-net-tools use) |
| NET-11 | ATM Turbo 2+ COM port with the same peers |
| NET-12 | State and control on CLI, WebAPI + OpenAPI, MCP, Lua, Python and Qt (automation parity); docs and a recipe per surface |
| NET-13 | Hooks for interface and traffic debugging at every boundary (§11), designed now, built later; nothing in the design blocks them |
| NET-14 | Hermetic tests: no Internet in core-tests; the virtual network can map names to local test servers |

Non-goals: Ethernet frames (MACRAW), PPPoE, the SL811 USB host (reported absent),
IPv6, TLS inside the emulator (ZX software does not do TLS), Spectranet and
ZX Next / ZX-Uno UARTs (other machines).

## 3. Architecture

### 3.1 Components and ownership

| Component | Where | Thread | Owns |
|---|---|---|---|
| `ZxNetUsb` (card) | `core/src/emulator/io/network/zxnetusb.{h,cpp}` | emulation | card ports `#83AB/#82AB/#81AB`, reset, mapping, the chip |
| `W5300` (chip) | `core/src/emulator/io/network/w5300.{h,cpp}` | emulation | registers, 8 sockets, TX/RX memory |
| `Uart16550` | `core/src/emulator/io/serial/uart16550.{h,cpp}` | emulation | 16550 registers, FIFOs, baud timing, modem lines |
| `ISerialPeer` + peers | `core/src/emulator/io/serial/` | emulation | what sits on the other end of a UART |
| `EspModule` (ESPNET / AT) | `core/src/emulator/io/network/esp/` | emulation | the virtual ESP: parser, its sockets (as VNet sockets) |
| `VirtualNetwork` | `core/src/emulator/io/network/vnet/` | emulation | guest-side socket table, DHCP/DNS/gateway, forwarding rules, journal submission |
| `HostNetBridge` | `core/src/common/network/` | its own thread | host sockets, resolver, poll loop |
| `NetSockets` (portable wrapper) | `core/src/common/network/netsockets.{h,cpp}` | any | BSD / Winsock differences, non-blocking helpers |

The existing CLI shim `core/automation/cli/include/platform-sockets.h`
becomes a user of `NetSockets` (one wrapper in the tree, not two). OS
differences stay inside `netsockets.cpp` with `#ifdef`, as the rest of
`core/src/common` does today.

### 3.2 Data flow

```
emulation thread                                     bridge thread
----------------                                     -------------
W5300 / EspModule
   | VNet::Connect(sock, ip, port) ------------------> command queue (SPSC)
   |                                                    connect() non-blocking
   |                                                    poll(): writable -> ok
   |                                   event queue <--- NetEvent{Connected, sock}
TimeTravelManager::SubmitLiveInput(NetEvent)  <-- drained at the instruction boundary
   | journaled with time + payload
VNet::Apply(NetEvent) -> W5300 socket: SSR = ESTABLISHED
```

- Commands (emulation -> bridge) are facts, not inputs: they are not needed
  for replay. They go into an SPSC ring (`AudioRingBuffer` pattern) and are
  dropped while the bridge is detached.
- Events (bridge -> emulation) are inputs. They enter only through
  `TimeTravelManager::SubmitLiveInput` (the one path that gives replay
  ownership, marshalling to the machine thread and an exact journal time, as
  the PS/2 keyboard does). Applying an event changes device state; the next
  `IN` the program does sees it.
- Data bytes travel in the payload store (§6.2), the event carries a reference.

### 3.3 Timing

- The emulation never waits for the host. A guest socket stays in its
  transient state (`SYNSENT`, "no data yet") until an event arrives.
- Events are drained and applied only at the frame boundary, after the TTD
  checkpoint and with the other host input (never while the program touches
  the adapter: an event applied inside an `IN` would be journaled at a time
  the replay cannot hit). Latency is at most one frame plus the host's own.
- Host-side timeouts replace chip timeouts: connect timeout (default 10 s,
  config), then the model fails the connect the way the chip does on a TCP
  timeout (`SSR = CLOSED`, `Sn_IR.TIMEOUT`). NedoOS busy-waits in the kernel
  during connect, so shorter than the chip's 31.8 s is kinder.
- Paused emulator: events queue in the ring (bounded; overflow closes the
  affected connection with a reset event, never blocks).
- Speed changes (turbo, max speed) need nothing special: time is emulated time.

### 3.4 Cost when off (NET-7)

- No adapter configured: no port observer is registered, no bridge thread is
  started, no per-frame hook runs. The port path is exactly today's.
- Adapter fitted: the card is a low-byte full-decode observer on `#AB`
  (§4.1): one table lookup per I/O, the same as MoonSound today. The
  per-frame drain is one atomic load. A/B benchmark per the performance
  guidelines before landing.

## 4. ZXNETUSB card and W5300 chip (step N1)

### 4.1 Port claim

The card claims every port with low byte `#AB` through
`RegisterFullDecodeLowBytePort(0xAB, this)`: the model-independent path used
by ZXM-MoonSound (`soundchip_moonsound.cpp:445`). It sees the raw 16-bit port
before the model decoder, on every machine, so the high byte (the W5300
register address) arrives intact. `OverrideDecodeForFullDecodeClaim` already
stands the model decode down for such claims. On ZX-Evo this replaces the
`ZxBus` arm's `#FF` for `#xxAB`; nothing else on the Evo mainboard decodes
`#AB`.

Decode, exactly as the CPLD (`zbus.v`, `wizmap.v`, `ports.v`):

| Port | Condition | Target |
|---|---|---|
| `#83AB` | A15 = 1, A9..A8 = 11 (A14..A10 ignored: mirrors) | control: bit4 W5300 /RESET (0 = held in reset), bit2 route W5300 INT, bit6 INT to Z80, bit0 (read) W5300 INT, bit7 (read) card INT; reset value 0 |
| `#82AB` | A15 = 1, A9..A8 = 10 | mode: bit4 W5300 in I/O space, bit3 invert chip A0, bit2 W5300 over a ROM window, bits1..0 which window, bit6 SL811 M/S; bits 2 and 4 together = both off; read: bit7 VBUS, bit6 M/S |
| `#81AB` | A15 = 1, A9..A8 = 01 | W5300 address bits 9..6 (readable: `wizcfg`'s presence test writes `#0A` and reads it back) |
| `#80AB` | A15 = 1, A9..A8 = 00 | SL811 address: accepted, no device |
| `#00AB..#7FAB` | A15 = 0 | `#82AB` bit4 = 1: W5300 at `{#81AB[3..0], A13..A9, A8 ^ invert}` (A14 ignored); bit4 = 0: SL811 data, reads `#FF` |

The chip is in reset (register writes ignored, reads `#FF`) until software
sets `#83AB` bit 4; the rising edge is a full chip reset. `wizcfg` does the
pulse; NedoOS then relies on it.

The memory-mapped mode (`#82AB` bit2: W5300 over a ROM window, `#2000/#3000`
FIFO windows for `LDIR`) is a host bus overlay installed only while the mode
is on (zero cost otherwise): writes into the window always reach the chip,
reads come from it while ROM is paged at `#0000` (window 0, the /CSROM
condition). The card's INT reaches the Z80 as a device INT line (§4.5 item 6).

### 4.2 Chip model

The model follows [reference-w5300-model.md](reference-w5300-model.md) §10.1,
not the Unreal_NS model (its deviations are listed there in §10.2). Key
points:

- **Common registers** are storage with datasheet reset values (MR `#3800`,
  IDR `#5300`, RTR `#07D0`, RCR 8, TMSR / RMSR 8 KB each); MR.RST self-clears
  with a full reset. SHAR, GAR, SUBR, SIPR are read/write: `wizcfg` writes
  them and checks `#AA` in SUBR0. The chip's own IP, mask and gateway are
  guest facts; the VNet uses the DHCP lease, not these registers, for routing
  (the W5300 ARP erratum makes guests keep SUBR at 0 at times).
- **Sn_CR always reads 0 after a command**, valid or not (datasheet p.70).
  The effect of a command is immediate on the model state; the host part
  follows as events. This alone removes the class of hangs where software polls
  `Sn_CR` forever.
- **Sn_SSR is an explicit state machine** driven by commands and net events,
  never computed on read. Reads have no side effects.
- **TX:** a byte queue of TMSR x 1 KB per socket. FIFO writes pair FIFOR0 then
  FIFOR1 (the word is stored on the FIFOR1 write; FSR drops per word written
  and recovers at SEND, which completes at once: FSR reads `#2000` again after
  every SEND, as the NedoOS close path needs). `SEND` takes `WRSR` bytes, sets `SENDOK` at once, and hands the
  bytes to the VNet. Back-to-back SENDs without clearing SENDOK work (NedoOS).
  FIFO writes are capped at the buffer size (NedoOS `write(0)` would push
  64 KB).
- **RX:** a byte FIFO holding, per packet, PACKET-INFO + data + one pad byte
  when the length is odd; `RSR` counts all queued bytes and drops by 2 per
  word read. PACKET-INFO: TCP size(2); UDP sender IP(4) + port(2) + size(2);
  IPRAW sender IP(4) + size(2); all MSB first. `RECV` gives window credit
  (TCP flow control towards the host bridge).
- **FIFO pairing survives other register accesses** between FIFOR0 and
  FIFOR1: NedoOS reads FIFOR0, returns to the program, and reads FIFOR1 on the
  next call after touching `Sn_MR` and the card ports.
- **Faithful, including the guest's bugs:** NedoOS's UDP read under-reads by
  one word when the buffer length is even and a larger datagram is odd
  (reference §6.1). A faithful FIFO reproduces what real hardware does; the
  model does not hide it. If it bites a real program, the debug surface
  (§11) will show exactly where.
- `Sn_IR` / `Sn_IMR` implemented (CON, DISCON, RECV, TIMEOUT, SENDOK), even
  though NedoOS does not use them: the WIZnet-style drivers of other software
  poll SENDOK.

### 4.3 Socket modes onto the virtual network

| W5300 mode / command | VNet action | Guest-visible states |
|---|---|---|
| TCP `OPEN` | allocate VNet socket | `INIT` (`#13`) |
| TCP `CONNECT` | destination in the virtual subnet: internal service (§5); else host connect | `SYNSENT` (`#15`) -> `ESTABLISHED` (`#17`) or `CLOSED` + TIMEOUT |
| TCP data in | net event with payload -> RX packet (one W5300 packet per event, split at the RX buffer size) | RSR > 0 |
| peer closes | net event | `CLOSE_WAIT` (`#1C`), remaining RX data still readable |
| TCP `DISCON` | host shutdown(write) | `FIN_WAIT` -> `CLOSED` on the host's close event |
| TCP `LISTEN` | forwarding rule for the guest port (§5.3) | `LISTEN` (`#14`) -> `ESTABLISHED` when a host client arrives |
| UDP `OPEN` | VNet UDP socket (host socket bound lazily on first send) | `UDP` (`#22`) |
| UDP `SEND` | to DHCP / DNS / gateway: internal; else host `sendto` | stays `UDP` |
| IPRAW + PROTOR 1 (`OS_NETSOCKET` ICMP) | ping: answered internally for VNet addresses; external via the host's unprivileged ICMP (macOS / Linux datagram ICMP, Windows `IcmpSendEcho`) where available, else no reply | `IPRAW` (`#32`) |
| MACRAW, PPPoE | not supported: `OPEN` leaves `CLOSED` | `CLOSED` |
| `CLOSE` | release VNet socket | `CLOSED` |

NedoOS's accept is special (reference §10.1): the listening W5300 socket
itself becomes `ESTABLISHED`, then NedoOS opens a fresh socket on the same
port and listens again. The VNet therefore keeps one host listening socket per
forwarded port with a queue of pending clients, and hands the next client to
whichever W5300 socket is in `LISTEN` on that port.

### 4.4 SL811 and the USB disk probe

The W5300 kernel also probes the SL811 USB host through `#82AB` / `#80AB` /
`#xxAB` for a USB disk. The card reports "no USB device": SL811 reads `#FF`
as today, which the kernel already handles (NedoOS boots today with all
`#xxAB` reading `#FF`). A USB mass-storage model is not planned here.

### 4.5 Open points to settle during N1 (with the default we take)

Settled in the implementation:

1. FIFOR1 moves the pointer (reference §1.2); NedoOS's split-call reads work
   (test `FifoPairSurvivesOtherRegisterAccessesInBetween`).
2. TX free space drops per word written and recovers at SEND (SEND completes
   at once).
3. Reads while the chip is in reset: `#FF`.
4. Refused connect: `CLOSED` without an IR bit; timeout / unreachable: `CLOSED` + TIMEOUT.
5. A TCP receive packet never spans two host chunks (each packet names one
   journal source); packets are at most the MSS and fit the free memory.
6. The card's /INT is a device INT line of the Z80
   (`Z80::SetDeviceIntLine`, `kDeviceIntZxNetUsb`): low while W5300 INTn
   AND `#83AB` b2 AND b6, re-driven after every access to the card, every
   network event, reset and state load; released when the card goes away.
   On the hardware the line is open drain, wired-OR with the machine's INT:
   card `zint_n` (CPLD pin 80) to ZX-Bus B13; ZX-Evo rev D `~INT` joins the
   Z80, the FPGA (pin 10, `zint.v` drives 0 / Z), both slots' B13 and a 680 Ω
   pull-up. No vector: the Z80 reads the bus, and the ZX-Evo FPGA drives #FF
   at every acknowledge (`zbus.v` `drive_ff`), so IM2 takes `I*256 + #FF`
   (programs keep a 256-byte table anyway: an original ULA machine reads
   whatever floats on the bus). A level: taken again after `EI` until the
   program clears Sn_IR. On ZX-Evo the acknowledge also ends the frame pulse
   (`zint.v`), whichever source was served. The CPU cost: a per-step work bit
   raised only while a line is low; a machine without a device, or with the
   line released, keeps the plain step. NedoOS itself polls and never enables
   the interrupt.

## 5. Virtual network (step N0)

### 5.1 Addresses

One virtual network per emulator instance, QEMU-style defaults (all
configurable):

| Address | Role |
|---|---|
| `10.0.2.0/24` | the LAN |
| `10.0.2.2` | gateway; answers ping |
| `10.0.2.3` | DNS server; answers ping |
| `10.0.2.15` | first DHCP lease (next: `.16` ...; one lease per MAC) |
| any other address | forwarded to the host network through host sockets |

The guest may use any addresses it likes (static `net.ini`): the VNet does
not route by the guest's own IP. It sees sockets, not packets, so a wrong
static mask or gateway does not break outgoing connections.

### 5.2 Internal services

- **DHCP server** on UDP 67 (broadcast `255.255.255.255:67` from port 68):
  OFFER and ACK with options 1 (mask), 3 (router), 6 (DNS), 51 (lease), 54
  (server id), ending with `#FF` and **without Pad options** (`wizcfg`'s option
  parser breaks on Pad; reference-wizcfg §3). `wizcfg` checks only xid and
  chaddr, so a standard reply works.
- **DNS**, `DnsMode`:
  - `HOST` (default): every UDP query to port 53 (any server address: the
    NedoOS kernel hard-codes `8.8.4.4`) is answered from the host resolver
    (`getaddrinfo` on the bridge thread). A records are synthesized; other
    types get NOTIMP or are passed to `PASS`.
  - `PASS`: queries go to the address the guest used, as plain UDP.
  - A per-instance **hosts table** overrides names (tests point
    `next.zxart.ee` at a local fake server; users can pin names).
- **Ping** of `10.0.2.2` / `10.0.2.3` is answered internally.

### 5.3 Guest servers (port forwarding)

A guest `LISTEN` on port P binds a host listener on `0.0.0.0` (every interface;
`RemoteAccess=off` = `127.0.0.1` only - owner decision 2026-10-04, PLAN #92 N0,
[tdd-smb-online-update.md](../2026-10-02-tsconf-zifi/tdd-smb-online-update.md) §6 item 1):
host port P when P >= 1024 and free, otherwise
per `Forward=` rules (`Forward=tcp:8080:80` = host 8080 to guest 80). The
actual host port is reported on every surface. Several emulator instances get
separate listeners; a clash is reported, not silently remapped.

### 5.4 Limits and safety

- Outgoing connections are allowed by default; an optional allow / deny list
  (host patterns, ports) for shared machines and CI.
- Per-instance caps: sockets (W5300 has 8, ESPNET 4 or 8), bytes buffered per
  socket (backpressure through `RECV` window credit), DNS queries in flight.
- Host listeners bind `0.0.0.0` by default; `RemoteAccess=off` keeps them on
  `127.0.0.1` (§5.3).

## 6. TTD and determinism (NET-6)

### 6.1 Input kinds

New `TTDInputKind` values, applied in `ApplyInputEvent` through a new
`TTDInputDevices` field (the network adapter set):

| Kind | Fields | Meaning |
|---|---|---|
| `NetEvent` (12) | network record: socket, event type, status, peer, payload | connect ok / failed, data in, peer closed, reset, accept (+ peer address), DNS answer, DHCP answer, ping reply |
| `NetLinkReset` (13) | adapter | all host connections of the adapter are gone (§6.4) |

`timetravelmanager.cpp:3212` (the bound check on the last kind) moves to the
new last kind; `ttd.ksy`, `ttdfileinfo.cpp` and the ttd-analyzer learn the
kinds, as for `PcKey`.

### 6.2 Payload store

`TTDInputEvent` keeps its size (32 bytes: the TTD bench gate caught a first
version that grew it to 48 for every session). A NetEvent carries a 1-based
`netIndex` into the journal's **network table** (`TTDNetInput`: socket,
event, status, peer, payload offset and length, journal index); the bytes go
into an append-only **payload store**. Both are truncated with the journal
(`DropAfter`). In the `.ttd` file they are one section after the port
journals (header bit 10), written only when the session has NetEvents: a
session without a network adapter is byte-identical to the older layout.
Typical cost: the bytes the guest actually received.

The virtual network's own answers (DHCP, hosts table, gateway ping, refused
connects) are journaled NetEvents too: every received byte then has a journal
source, which is what the checkpoint state below refers to.

This store is general: the ESP modules, a host serial port and later any
device with a byte stream from outside (MIDI in, a real COM port) use it.

### 6.3 State blobs

`PeripheralId::ZxNetUsb = 20` (next free after `EvoPs2 = 19`), and later
`EvoComPort`, `EspModule`, `Atm2ComPort`. The ZXNETUSB blob holds the card
ports, the W5300 registers, per-socket state, FIFO pointers and the VNet
guest-side table, a fixed size.

**FIFO contents: option A, decided by the user 2026-09-30 and implemented**
(`TTDZxNetUsb`, `netstate.h`): a fixed-size blob with the registers, socket
states, unsent TX bytes (up to 1536 per socket), per receive packet its
PACKET-INFO and a journal reference (source, offset, length; up to 64
packets per socket), per TCP backlog chunk a reference (up to 32), and the
virtual network's tables (sockets, guest servers, leases). The loader takes
the bytes from the journal; a state beyond the limits is saved as far as it
goes and marked incomplete (logged at restore). The options considered:

| Option | How | Cost |
|---|---|---|
| A (proposed) | RX contents are not stored: they are the unread tail of recorded `NetEvent` payloads; the blob stores, per socket, the payload-store position of the first unread byte and the count. TX contents (written by the Z80, not yet sent) are stored in the blob, bounded by what is pending (usually 0 at a frame boundary: the kernel writes and sends in one call) | small; needs a variable-size blob or a fixed TX cap |
| B | the occupied FIFO bytes in the blob, variable size | simple; up to 128 KB per checkpoint in the worst case |
| C | the whole 128 KB as a TTD v2 memory region | waits for TTD v2 |

### 6.4 Replay, seek, and going live again

- **Replay** (`ttdReplayActive` / `OwnsInput()`): the VNet detaches from the
  bridge: no host commands are issued, no live events accepted; `NetEvent`s
  come from the journal. The IN journal already returns the recorded port
  values; the device model still runs so its state stays right for later.
- **Seek then continue live** (resume from the past, or a what-if fork): the
  host connections that existed at that point are gone. The first live
  instruction applies `NetLinkReset`: every TCP socket goes to `CLOSED` with
  TIMEOUT, UDP sockets stay open. This is recorded like any input, so the new
  branch replays too. DHCP leases and forwarding rules survive (they are guest
  and config state).
- **Recording boundaries**: starting a recording does not touch the network;
  the first checkpoint's blob captures the socket states.

## 7. COM port and ESP modules (steps N2, N3)

### 7.1 UART model (N2)

`Uart16550` covers both flavors behind `#F8EF..#FFEF` (register = A10..A8):

| Flavor | Behavior |
|---|---|
| **Evo AVR** (ZX-Evo COM port, on the mainboard: always there) | the AVR firmware chosen by `[EVO] Avr=` (default: the newest NedoPC, every release since 2010 available, [reference-evo-com-port.md](reference-evo-com-port.md) §9). Newest NedoPC: 16-byte FIFOs, no interrupts (IIR always #01), MCR masked to `& #1F` (no auto flow control, no loopback), RTS driven by software, TX ignores CTS, OE sticky until an FCR RX reset, LSR bit 7 = RX half full, RBR of an empty FIFO = #00, 8N2 until the first LCR write, divisor 0 = 345600 baud; each access holds the Z80 on /WAIT for the AVR's interrupt, main-loop phase and service (27-31 us minimum, 45-50 us when polled, §3 there); a Z80 reset leaves it alone |
| **ZX-WiFi** (a ZX-Bus card: a real 16550 + an ESP; `Card=ZXWIFI`, its ESP by `ZxWifi=`; not on a ZX-Evo or TS-Conf, where #xxEF is taken) | 16-byte FIFOs, auto RTS/CTS when MCR bit5 is set, 1.8432 MHz clock (divider 1 = 115200) |

Common: DLL/DLM, LCR, FCR, LSR (DR, THRE, TEMT, OE), MSR (CTS, DSR, DCD),
scratch register. Bytes move at the programmed baud rate in emulated time
(one byte per 10 or 11 bit times), so software that paces the ESP with RTS
pulses sees real timing. Overrun sets LSR.OE when the peer sends while the
FIFO is full and RTS allowed it, as on hardware.

On the ZX-Evo the FPGA decodes only the low byte (#EF) and A10..A8 (the
register), so #00EF..#FFEF all alias the eight registers. The UART claims
low byte #EF as a full-decode observer (like the W5300's #AB), over the
ATM3 decoder's `ComPort` arm, which reads #FF without it. On TS-Conf the same
ports belong to ZiFi (N5): no COM port is fitted there.

Timing: a byte takes one character time (start + data + parity + stop bits
at the programmed baud rate) in base-clock T-states (`t_states` + the Z80's
`t` scaled back by the turbo multiplier, so turbo does not speed up the
line). The peer starts a byte only while RTS is asserted and the byte then
arrives a character later whatever RTS does meanwhile: the NedoOS type 0 RTS
pulse (`MCR 2`, `MCR 0`) gets one byte per pulse, as with a flow-controlled
ESP. The ZX-WiFi's auto-RTS (MCR AFE) holds the peer at the FIFO trigger
level, auto-CTS holds the transmitter.

### 7.2 Serial peers

`ISerialPeer` (`core/src/emulator/io/serial/serialpeer.h`): the UART owns
the timing and asks the peer only at character times - `Transmit(byte)` when
the ZX's character left the line, `HasByte` / `TakeByte` when the receiver is
free and RTS allows (`HonorsRts` false: regardless of RTS), `Cts` / `Dsr` /
`Dcd` / `Ri` for MSR, `OnModemLines` / `OnLineSettings` for what the ZX
programmed, `onReceive` when bytes arrive from outside (the UART moves its
clock there), `SetClock` for peers with their own timing (the ESP modules'
turnaround and timeouts).

| Peer | Use |
|---|---|
| `EspnetModule` | NedoOS ESPNET firmware 1.27, byte for byte ([reference-esp-modules.md](reference-esp-modules.md) Part 1): frames `#A5` + header (CRC frames too), SOCKET, SHUTDOWN, CONNECT, ACCEPT, BIND, LISTEN, READ, WRITE, GETDNS, DNSRESOLVE, INFO, WIFI_SCAN / CONNECT / DISC / STATUS (the virtual access point `UnrealNG`), UART (baud change), ECHO; 8 sockets on ESP32, 4 on ESP8266 (`EspChip=`); sockets are VNet sockets |
| `AtModule` | Espressif AT ([reference-esp-modules.md](reference-esp-modules.md) Part 2): ESP8266 as NonOS AT 1.7.4, ESP32 as AT 2.2; echo, RST / RESTORE, GMR, CWMODE / CWJAP / CWQAP / CWLAP / CWAUTOCONN, CIFSR / CIPSTA, CIPMUX 0 / 1, CIPDINFO, CIPSTART TCP / UDP (DNS on the module), CIPSEND (leading zeros, `> `), CIPCLOSE, CIPSTATUS, CIPSERVER, CIPRECVMODE / CIPRECVDATA / CIPRECVLEN, CIPMODE=1 + `+++`, CIPDOMAIN, PING, CIPSNTPCFG / CIPSNTPTIME (real NTP over the virtual network), UART_CUR / DEF (rate and flow control); `_CUR` / `_DEF` and plain forms; the order clients rely on |
| `HostSerialPeer` | a real host serial port (a real ESP on USB, a modem) |
| `TcpPeer` | bytes to / from a host TCP endpoint (telnet BBS, a test harness) |
| `LoopbackPeer` | tests |

Every peer honors RTS (the UART asks it only while RTS allows).

A TCP peer's host is an address or a name; a name is resolved at each
connect through the virtual network's DNS (the `Hosts=` table first, then
the host resolver), so the answer is journaled and a replay needs no host.
A host serial device follows the line format the ZX programs (divisor, data
bits, parity, stop bits; rates outside the OS constants through IOSSIOSPEED
on macOS, termios2 on Linux, the DCB on Windows). With `ComModemLines=1` the
ZX's RTS / DTR drive the device and its CTS / DSR / RI / DCD come back as
journaled `ModemLines` events; off by default, because USB ESP boards often
wire RTS / DTR to the module's reset and boot pins and the NedoOS RTS pulses
would reset it. A machine reset (ZX-Bus /RESET) resets the card and its
sockets, not the COM port's link. Bytes from peers that come from outside (`HostSerialPeer`,
`TcpPeer`) are journaled through the payload store; the ESP modules journal at
their VNet boundary like the W5300 (their UART side is deterministic).

Which peer is plugged is configuration (§8) and switchable at runtime.

### 7.3 ATM Turbo 2+ COM port (N4)

The port is the keyboard controller's own UART (an i8031 / AT89S52 behind
`IN #FE`; commands after the escape `#55`: `#02` read, `#03` + data write,
`#42` status, `#82` modem lines, `#43` DTR / RTS, `#C2` RX count, `#C3` baud
divisor). The firmware sources and images were found (2.2 .. 4.1), so the
emulator runs the real firmware on an MCS-51 core, in front of the same
`ISerialPeer`s: [2026-10-01-atm2-keyboard-controller](../2026-10-01-atm2-keyboard-controller/README.md).

## 8. Configuration (NET-8)

Machine INI:

```ini
[NETWORK]
Card=NONE                 ; ZX-Bus cards, a list: NONE | ZXNETUSB | ZXWIFI | ZXNETUSB,ZXWIFI
HostAccess=1              ; 1 = reach the host network | 0 = internal services only
ComPort=NONE              ; the machine's own serial port: NONE | LOOPBACK | TCP:<host>:<port> | SERIAL:<device>[,<baud>] | ESPNET | AT
ZxWifi=AT                 ; the ZX-WiFi card's 16550: AT | ESPNET (its ESP's firmware) or any ComPort= value
ComModemLines=0           ; 1: a SERIAL: device gets RTS / DTR and reports CTS / DSR / RI / DCD
EspChip=ESP32             ; ESP32 (8 sockets) | ESP8266 (4)
Subnet=10.0.2.0/24
DnsMode=HOST              ; HOST | PASS
Hosts=                    ; name=ip,name=ip
Forward=                  ; tcp:<hostport>:<guestport>,...
RemoteAccess=on           ; host listeners on 0.0.0.0 | off = 127.0.0.1 only
ConnectTimeoutMs=10000
Allow=                    ; optional allow list
```

- Settings are per slot, not per machine. The machine declares what it offers
  (`PortDecoder::DescribeNetwork`: the ZX-Bus, its own serial port - ZX-Evo:
  the AVR's, TS-Conf: ZiFi, others: none); `NetworkManager` fits from that and
  never names a model. A device that clashes with the machine is not fitted and
  `GET /state/network` lists it in `not_fitted` with the reason (a ZX-WiFi card
  on a ZX-Evo: #xxEF is the AVR's). Machine-specific hardware lives in the
  machine's own section: `[EVO] Avr=` (the AVR firmware, also `avr_firmware` at
  runtime). The Qt window Tools > Network (`unreal-qt/src/network/`) edits all of
  it through the same `ParseChange` keys, offers only what the machine takes
  (with the reason for the rest) and shows the `GET /state/network` tree live. The step after this:
  machine -> buses / extension slots -> devices, unlimited until ports clash.
- The legacy keys already shipped in `data/configs/*/unreal.ini`
  (`[MISC] Modem=NONE`, `ZiFi=NONE`) are read too: `Modem=COMn` means
  `ComPort=SERIAL:COMn`. Nothing parses them today.
- Feature `network` (runtime, `FeatureManager`), on by default: the INI says
  what is fitted, the feature can unplug it, as `kempstonmouse` does with
  `[INPUT] Mouse=`. Lists use `,` (`;` starts an INI comment).
- Automation can fit / unplug the card and switch the COM peer at runtime
  (a device refit, like `Core::RefitIde`).
- NedoOS setup is documented in the recipe: which kernel goes with which
  adapter (`sd_boot.$C` = ZXNETUSB, `sd_bootesp.$C` = ESPNET on the COM port),
  `network.ini currentNetwork`, `espcom.ini comType`.

## 9. Automation (NET-12)

Status (read-only) first, on every surface in step N1; control in N1b:

| Operation | Content |
|---|---|
| `network status` | adapters fitted, link, DHCP leases, forwarded ports (actual host ports), counters |
| `network sockets` | per adapter socket: mode, state, local / remote address and port, bytes in / out, RX queued, owner hint (NedoOS task via the NedoOS layer when active) |
| `network config` | read / change §8 settings at runtime |
| `network disconnect <socket>` | reset a connection (debug aid) |
| `network hosts` / `forward` | edit the hosts table and forwarding rules |

Template: the CMOS RTC access layer (commit `5c09bb19`) for the surface
wiring; Kempston Mouse (commit `a8767455`) for feature + TTD input + config.
MCP: an `inspect_state` aspect `network` plus control through `emulator_manage`
or a dedicated tool (decided with the other surfaces in N1b).

## 10. Trace attribution

- `PortDeviceId::Network` (next id after `Ide`) and `PortTag::NetworkEth` /
  `Serial` (already reserved) for port trace and the port-decode reports.
- The card's full-decode claim shows in the port trace as
  `FullDecodeClaim` today; the device id makes it `Network`.

## 11. Interface and traffic debugging (designed now, built later)

Four boundaries, each with one tap point. Built later; the design only
promises that every event already exists as data at these points.

| Boundary | Tap | What it will show |
|---|---|---|
| Z80 <-> adapter ports | existing port trace + a decoded register log (`Sn_CR <- SEND` on socket 1, not `OUT #03AB,#20`) | how a driver drives the chip; wrong register sequences |
| Adapter <-> virtual network | `NetTap` events: socket open / connect / send / receive / close with payload | what the program asked for and got, per socket |
| Virtual network <-> host | bridge events: host connect, errors, timings | why a connection failed on the host side |
| UART line | byte stream per direction with modem lines, AT / ESPNET decoding | ESP dialogs, framing errors, flow control |

Planned uses, in order of value:

1. **Socket view** on all surfaces and in the NedoOS layer (NK-14): who owns
   which socket, what it is connected to, what went through it.
2. **Stream view**: the bytes of one connection as text / hex, both directions,
   with emulated time; for ESP, the decoded AT or ESPNET dialog.
3. **pcap export**: synthesized Ethernet / IP / TCP / UDP frames for Wireshark
   (addresses from the VNet, sequence numbers made up consistently).
4. **Offline analysis from TTD**: every received byte is already in the
   payload store, every sent byte in the OUT journal; the ttd-analyzer can
   rebuild the traffic of a recording without the emulator.
5. **Fault injection** for testing ZX network code: delay, drop, refuse,
   reset, slow link, DNS failure. Injected faults are ordinary net events, so
   they are journaled and replay.
6. **Breakpoints on network events**: stop when socket N connects, when data
   containing X arrives, when a DNS name is asked.
7. **HUD**: link and activity indicator per adapter (the notification
   pattern of the FDD / audio indicators).

Design rules that keep this possible: `NetTap` is a subscriber list on the
VNet and the UART (zero cost with no subscriber); events carry emulated time
and a stable socket identity; payload references point into the payload
store, so taps copy nothing.

## 12. Tests (NET-14)

| Level | What | How |
|---|---|---|
| Unit: card | port decode with mirrors, reset gating, `#81AB` read-back, bits 2/4 exclusion, A0 inversion | `ZxNetUsbCUT`, bare decoder fixture (`portdecoder_atm3_test.cpp` pattern) |
| Unit: chip | reset values; CR always clears; SSR transitions per command; FIFO pairing incl. split across other accesses; PACKET-INFO per mode; odd lengths and padding; RSR arithmetic; TX cap; NedoOS UDP under-read reproduced | fake VNet (records commands, injects events) |
| Unit: VNet | DHCP OFFER/ACK bytes (no Pad, ends `#FF`); DNS answers from a fake resolver; hosts table; forwarding; allow list | fake host bridge |
| Unit: bridge | TCP connect / data / close / refused / timeout, UDP, listen / accept against **local** servers on 127.0.0.1 | real sockets, no Internet |
| Unit: UART | registers, FIFO, baud timing, RTS pacing, overrun, both flavors | loopback peer |
| Unit: ESP | ESPNET frames for every command; AT dialogs from NedoOS `esp-com.c` and Moon Rabbit | fake VNet |
| TTD | record a session with net events, replay with the bridge absent: identical port reads and screen; seek + continue live gives `NetLinkReset`; blob round trip | TTD test helpers |
| Machine | NedoOS full card, ZX-Evo: `wizcfg` gets a lease; `ping 10.0.2.2`; zxdb search against a local fake zxart server (hosts table) shows results | `zxevo_ers_test.cpp` pattern (`NedoOsShellRunsATypedCommand`), turbo mode, boot-bound (justified) |
| Machine, ESP | `sd_bootesp.$C` with the ESPNET module: same zxdb test | as above |
| Performance | port path with the card fitted vs not; per-frame cost | A/B benchmark (performance guidelines) |

Test data: the full card stays uncommitted (user decision); the machine tests
use the committed minimal card plus the few network programs they need
(`wizcfg.com`, `net.ini`, `zxdb`, `ping`), added to
`testdata/machines/zxevo/nedoos/` with their NedoOS revision noted.

## 13. Steps and acceptance

| Step | Content | Done when |
|---|---|---|
| **N0** | `NetSockets`, `HostNetBridge`, `VirtualNetwork` (DHCP, DNS, gateway, forwarding), `NetEvent` / `NetLinkReset` + payload store in TTD, config, feature, "no adapter" unchanged | unit tests with fake and loopback backends; zero cost when off |
| **N1a** | ZXNETUSB card + W5300 (TCP, UDP), status on every surface, PortDeviceId | NedoOS full card in the GUI: `wizcfg` lease, zxdb search, browser page, telnet; machine test green; replay sealed |
| **N1b** | LISTEN / forwarding, IPRAW ping, memory-mapped mode, INT, blob (after the §6.3 decision), control on every surface | 3ws / scrnet reachable from the host browser; `ping`; TTD seek round trip |
| **N2** | `Uart16550` (Evo, ZX-WiFi), `HostSerialPeer`, `TcpPeer`, `LoopbackPeer` | `cuart` talks to a TCP echo; a real ESP on USB works through `SERIAL:`. **Done** (§15): NedoOS `cuart` holds an AT dialog with a pretend ESP over TCP live; the SERIAL: path is checked on a pseudo-terminal; the real-ESP test is postponed until the hardware is at hand |
| **N3** | `EspnetModule`, then `AtModule` | `sd_bootesp.$C` + zxdb works; Moon Rabbit `mrfue.com` and an ESPCOM app work. **Done** (§15): zxdb over ESPNET (kernel driver) and over AT (`currentNetwork=1`) live; Moon Rabbit not run yet |
| **N4** | ATM Turbo 2+ COM port | NedoOS ATM ESP kernel on ATM710 reaches the network |
| **N5, N6** | ZiFi (with TS-Conf), AY-UART, ATM2IOESP | per demand |
| **D** | debugging (§11) in the order listed | per item |

## 14. Decisions needed

Asked one at a time when the step needs them:

1. FIFO contents in the TTD blob (§6.3): option A, B or C. Needed for N1b.
2. Default subnet: QEMU's `10.0.2.0/24`, or `192.168.1.0/24` to match the
   shipped `net.ini` static fallback. Only cosmetic for software that uses
   DHCP; proposed: `10.0.2.0/24`.
3. Guest servers on privileged ports (below 1024): host port = guest port +
   10000 by default, or only explicit `Forward=` rules. Proposed: explicit
   rules plus the automatic mapping for ports >= 1024.

## 15. Implementation status (2026-09-30, branch `network-w5300`)

| Part | Where | Tests |
|---|---|---|
| Portable sockets, host bridge (socket thread, DNS thread, ping thread) | `core/src/common/network/` | `hostnetbridge_test.cpp` (loopback TCP, UDP, listen, DNS, ping), `dnsmessage_test.cpp` |
| Virtual network (DHCP, DNS, gateway, forwarding, activity, counters, TTD tables) | `core/src/emulator/io/network/virtualnetwork.*`, `vnet/dhcpserver.*` | `dhcpserver_test.cpp`, through `w5300_test.cpp` |
| W5300 chip model | `w5300.*` | `w5300_test.cpp` (26 cases incl. the NedoOS UDP under-read, DHCP like wizcfg, ping) |
| ZXNETUSB card (ports, reset, memory-mapped window) | `zxnetusb.*` | `zxnetusb_test.cpp`, `networkmanager_test.cpp` |
| Fitting, config `[NETWORK]`, feature `network`, runtime change | `networkmanager.*`, `config.cpp`, `featuremanager.cpp`, shipped INIs | `networkmanager_test.cpp` |
| TTD: NetEvent / NetLinkReset, network table, payload store, file section, checkpoint state (option A) | `debugger/ttd/*`, `network/ttdzxnetusb.*` | `ttdinputjournal_test.cpp`, `ttdzxnetusb_test.cpp` (record, seek, replay sealed, file round trip) |
| Status and settings on every surface | WebAPI `GET /state/network`, `POST /network/config` (+ OpenAPI), MCP aspect `network`, CLI `network` / `network set`, Lua / Python `network_state()` / `network_configure()` | automation test filters |
| Machine test: NedoOS W5300 kernel gets a DHCP lease, pings the gateway | `testdata/machines/zxevo/nedoos/sdcard-net/` | `NetworkManager_Test.NedoOsGetsALeaseAndPingsTheGateway` |

Checked live in the GUI (NedoOS full card, zxdb): wizcfg lease, DNS through
the host resolver, TCP through the virtual network to a local server
(`Hosts=` pointed the name at `127.0.0.1`), the HTTP request arrived and the
reply reached zxdb. On the development Mac, outgoing connections of a new
emulator build are held by the host firewall (Little Snitch) until allowed.

A/B benchmark (base 77f627bd vs the branch, no card fitted, 6 interleaved
runs x 5 repetitions, load average falling to 5): frame without TTD 2221 vs
2211 us, frame recording TTD 2801 vs 2780 us, TTD hooks only 2301 vs 2285 us,
`OnFrameBoundary` 0.99-1.01 at 0 / 4 / 16 / 64 dirty pages: no measurable
change. Checked live with the user: zxdb on the full NedoOS card talks to the
real next.zxart.ee through the virtual network.

Card INT to the Z80 wired (§4.5 item 6). A/B of the device INT line (A = master
`2e106688`, B = the change; two runs of 10 interleaved rounds, the second in
reverse order, load 4-9): the plain step's machine code (`StepInstruction`,
`ProcessInterruptsImpl<false, false>`) is identical instruction for instruction;
B/A of the minimum CPU time per frame: 48K fast 1.010 / 0.996, Scorpion fast
1.006 / 0.999, TSConf (machine INT source) 1.018 / 1.004, Pentagon fast 1.013 /
1.016. The Pentagon shift repeats with unchanged code and no card fitted: code
layout, noted for the next A/B on this path. Open: the debugging views
([tdd-network-debugging.md](tdd-network-debugging.md)).


### N2: COM port (2026-09-30, branch `com-port`)

| Part | Where | Tests |
|---|---|---|
| 16550 model, both flavors (ZX-Evo AVR, ZX-WiFi 16C550 with AFE) | `core/src/emulator/io/serial/uart16550.*` | `uart16550_test.cpp` (reset values, baud the AVR's way, character timing, RTS pulses, overrun rules, auto-RTS / auto-CTS, loopback, state round trip) |
| Peers: loopback, TCP (address or name, resolved through the virtual network's DNS) and host serial device as virtual-network streams (bytes, DNS answers and modem lines journaled as NetEvents) | `serialpeer.*`; `VirtualNetwork::ConnectSerial` / `ConfigureSerial` / `SerialModemLines`, `NetProto::Serial`, `NetEventType::ModemLines`; `dns::BuildQuery` / `ParseAnswer` | `serialpeer_test.cpp` (hosts table, host resolver, unknown name, DNS timeout, retry, line format, modem lines both ways) |
| Host serial device (termios / Win32 COMM) on its own bridge thread: line format incl. custom rates, RTS / DTR, CTS / DSR / RI / DCD polling | `common/serial/hostserialport.h`, `platform/posix/`, `platform/macos/` + `platform/linux/` (custom baud), `platform/windows/`; `HostNetBridge::SerialOpen` / `SerialConfigure` / `SerialModemLines` | `hostnetbridge_test.cpp` (missing device; pseudo-terminal exchange and line format on POSIX) |
| The port on the bus (#xxEF, A10..A8), AVR wait (ISR + loop phase + service), reset rule; fitting from the machine's capabilities (`PortDecoder::DescribeNetwork`), `[NETWORK] ComPort=` / `Card=ZXWIFI` / `ZxWifi=`; AVR firmware presets `[EVO] Avr=` | `comport.*`, `comportspec.*`, `networkspec.*`, `networkmanager.*`, `uart16550.*`, `portdecoder_atm3.cpp`, `config.cpp` | `comport_test.cpp` (always-there Evo UART, decode, wait, refit keeps registers, ZX-WiFi card and its clash, TS firmware on BaseConf, status, TTD), `uart16550_test.cpp` (every firmware preset, wait model), `comportspec_test.cpp` |
| TTD: the serial port in its own blob (`PeripheralId::SerialPort` 24, a short UART-only blob without a peer): UART registers, echo queue by value, peer bytes by journal reference; the network adapters blob (version 3) keeps the card and the virtual network, saved without a card too | `netstate.h`, `network/ttdserialport.*`, `network/ttdzxnetusb.*` | `ttdzxnetusb_test.cpp` (`TTDComPort_Test`: replay without the host, seek restores the peer's bytes) |
| Status and settings on every surface | `com_port` in `GET /state/network` and the MCP / CLI / Lua / Python views; keys `card`, `com_port`, `zx_wifi` in `POST /network/config`, `network set`, `network_configure()` | through the tests above |

Checked live on the ZX-Evo:
- `com_port=tcp:127.0.0.1:2323`, a Python echo server: Z80 code sent `HELLO`
  through the Evo UART, the answer `hello` landed in RAM.
- NedoOS (full card) with `com_port=tcp:localhost:2323` (the name resolved
  through the host resolver) and a Python pretend ESP: `cuart`
  (`src/kapps/cuart`, comType 0: RTS pulses) sent `AT` and `AT+GMR` and showed
  `OK` and the three version lines (103 bytes, no overrun); F3 switched the
  UART to divisor 3, 38400. Found on the way and fixed: a machine reset closed
  the COM port's TCP link with the card's sockets, and the UART's clock did
  not follow the machine counter restarting at a reset.

Postponed: testing with a real ESP on USB through `SERIAL:` (the path is built
and checked on a pseudo-terminal), with the other real-device bridges
(Greaseweazle / KryoFlux, PLAN #12) when the hardware is on the desk. Boards,
wiring, auto-reset circuits, firmware and other emulators' ESP code:
[reference-evo-com-port.md](reference-evo-com-port.md) §8. The port path of a
machine without a COM port is unchanged (the low-byte observer table stays
empty): no A/B needed for it.

### N3: ESP modules (2026-10-01, branch `esp-modules`)

| Part | Where | Tests |
|---|---|---|
| Socket stack of the module on the virtual network: TCP / UDP slots, servers with a client queue, DNS, ICMP echo, the firmware's own UDP query (SNTP); received bytes by journal reference | `core/src/emulator/io/serial/esp/espstack.*` | through the module tests |
| Module base: UART side (4 KB receive buffer, turnaround, line format, flow control), Wi-Fi on the virtual access point with a DHCP lease, exchange log, TTD state | `espmodule.*`, `netstate::EspModuleState` | |
| ESPNET firmware 1.27 | `espnetmodule.*` | `espnetmodule_test.cpp` (INFO, TCP client like the kernel, failed connect, busy ordering, UDP DNS like the kernel, DNSRESOLVE / GETDNS, server + queued clients like 3ws, errno cases, CRC, Wi-Fi, UART SET, state) |
| AT firmware (NonOS 1.7 / ESP32 2.x personality) | `atmodule.*` | `atmodule_test.cpp` (banner, echo, NedoOS espReBoot, GMR, TCP session order, segments, failures + busy, CIPMUX + server, passive mode, CIPDINFO, transparent + `+++`, SNTP, UART_CUR, CWJAP, CIFSR / CIPSTATUS, PING, state) |
| Fitting `ComPort=ESPNET / AT`, `EspChip=`, status (`com_port.recent_exchanges`, `requests`), TTD in the network adapters blob | `networkmanager.*`, `comport.*`, `devicestate.cpp`, `ttdzxnetusb.*` | `ttdzxnetusb_test.cpp` (`TTDEspModule_Test`: replay without the host), `comportspec_test.cpp` |

Checked live on the ZX-Evo with the full NedoOS card (a copy with
`sd_bootesp.$C` as the boot kernel):
- ESPNET through the kernel driver: espcfg (INFO), zxdb search - DNS over UDP,
  SOCKET, CONNECT, WRITE 113, four READs of 192, SHUTDOWN; results shown.
- AT with `ini/network.ini currentNetwork=1`: zxdb's espReBoot (RST, ready,
  WIFI GOT IP, ATE0, CIPCLOSE, CIPDINFO=0, CIPMUX=0, CIPSERVER=0,
  CIPRECVMODE=0), CIPSTART to the name (DNS on the module), CIPSEND=115,
  `+IPD,611`, CIPCLOSE; results shown.
- next.zxart.ee did not answer HTTP that day and the host firewall held the
  emulator's own connections, so the name was pinned (`Hosts=`) to a local
  stand-in serving zxdb's record format.

Open: Moon Rabbit / Karabas net-tools not run yet; the questions in
[reference-esp-modules.md](reference-esp-modules.md) Part 4 (16-byte vs
TS-Conf 511-byte AVR FIFO, overruns during an AT reboot).

