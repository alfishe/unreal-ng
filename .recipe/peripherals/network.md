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

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) only inside host-side pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

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

Guest servers (a NedoOS program in `LISTEN`) are reachable on `127.0.0.1`:
guest ports 1024 and up on the same host port, lower ones only through a
`Forward=` rule.

## COM port (16550 UART)

A 16550 on `#F8EF..#FFEF` (register = A10..A8) with a peer on its other end.
On the ZX-Evo (ATM3) it is the AVR's COM port, always there; `ComPort=` says
what is plugged into it. Other machines get one with a ZX-WiFi card
(`Card=ZXWIFI`, or `ZXNETUSB,ZXWIFI` for both cards); `ZxWifi=` (default `AT`)
takes the same values for the card's side:

| `ComPort=` / `ZxWifi=` | Peer |
|:--|:--|
| `NONE` | nothing on the line (the ZX-Evo's registers still answer) |
| `LOOPBACK` | every byte the ZX sends comes back |
| `TCP:<host>:<port>` | a host TCP endpoint (telnet BBS, a test harness); the host is an address or a name (resolved through the virtual network's DNS: `Hosts=`, then the host resolver); reconnects every ~5 s after a drop |
| `ESPNET` | an emulated ESP module with NedoOS's ESPNET firmware 1.27 (binary sockets; NedoOS `sd_bootesp.$C` kernel and `currentNetwork=2` apps) |
| `AT` | an emulated ESP module with Espressif's AT firmware (NedoOS `currentNetwork=1` apps, Moon Rabbit, Karabas net-tools) |
| `SERIAL:<device>[,<baud>]` | a host serial device (`/dev/tty.usbserial-0001`, `/dev/ttyUSB0`, `COM3`); it follows the rate and format the ZX programs (`<baud>` until then, 115200 by default); `ComModemLines=1` passes RTS / DTR and reports CTS / DSR / RI / DCD (off by default: USB ESP boards wire RTS / DTR to reset / boot) |

The ZX-Evo's UART behaves like the AVR firmware chosen by `[EVO] Avr=`
(default `BASECONF`, the newest NedoPC; every NedoPC and TS-Labs release
since 2010 is a preset: no interrupts, RTS by software, every access holds
the Z80 ~30-50 us). The ZX-WiFi card is a real 16550 (auto flow control with
MCR bit 5). A ZX-WiFi card on a ZX-Evo or TS-Conf is not fitted (#xxEF is
taken): `not_fitted` in `inspect_state network` says so. The
bytes that arrive are TTD input like the card's. Details:
[reference-evo-com-port.md](../../docs/inprogress/2026-09-30-nedoos-integration/reference-evo-com-port.md).

```json
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
  "body": {"com_port": "tcp:127.0.0.1:2323"}}}
```

CLI `network set com_port=loopback` (or `network set card=zxwifi zx_wifi=espnet` on a Pentagon), Lua `network_configure{com_port="serial:COM3"}`,
Python `emu.network_configure(com_port="tcp:127.0.0.1:2323")`. The state is
`com_port` in `inspect_state network` (registers, FIFO levels, peer, link
`phase` and `error`, bytes). A machine reset keeps the link.

NedoOS: `cuart` (`bin/cuart.com`) is a terminal for the port; `ini/espcom.ini`
`comType = 0` on the ZX-Evo (RTS pulses), `2` for a ZX-WiFi. The first key
after start closes its hello box; F1..F10 pick the divisor.

The ESP modules sit on the virtual network: joined to the access point
`UnrealNG` with a DHCP lease (10.0.2.15 first), DNS through the virtual
network, every answer journaled. `EspChip=ESP32` (8 ESPNET sockets, AT 2.x
answers) or `ESP8266` (4 sockets, NonOS AT 1.7). `com_port.recent_exchanges`
in the network state lists the last requests and replies (ESPNET frames by
name, AT lines as text) - the first place to look when a program and the
module disagree. Details: [reference-esp-modules.md](../../docs/inprogress/2026-09-30-nedoos-integration/reference-esp-modules.md).

NedoOS with an ESP module: boot `sd_bootesp.$C` for the kernel driver (ESPNET;
`ini/network.ini currentNetwork=0`), or set `currentNetwork=1` (AT) / `2`
(userland ESPNET) for the C apps (zxdb, gopher, girc, time2, ...).

A quick check without software: `com_port=loopback`, then from Z80 code
`LCR=3` (`#FBEF`), `MCR=2` (RTS, `#FCEF`), a byte to `#F8EF`, wait for LSR
bit 0 (`#FDEF`), read `#F8EF`: the same byte.

## NedoOS setup

| Kernel | Adapter | Notes |
|:--|:--|:--|
| `sd_boot.$C` (NedoOS release, the ZX-Evo W5300 kernel) | ZXNETUSB | `autoexec.bat` runs `wizcfg.com`: it finds the card, gets a DHCP lease, programs the chip |
| `sd_bootesp.$C` | ESP on the COM port | not emulated yet (network TDD step N3) |

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
