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
On the ZX-Evo (ATM3) and TS-Conf (`TSL`) it is the AVR's COM port, always
there; `ComPort=` says what is plugged into it. Other machines get one with a ZX-WiFi card
(`Card=ZXWIFI`, or `ZXNETUSB,ZXWIFI` for both cards); `ZxWifi=` (default `AT`)
takes the same values for the card's side:

| `ComPort=` / `ZxWifi=` | Peer |
|:--|:--|
| `NONE` | nothing on the line (the ZX-Evo's registers still answer) |
| `LOOPBACK` | every byte the ZX sends comes back |
| `TCP:<host>:<port>` | a host TCP endpoint (telnet BBS, a test harness); the host is an address or a name (resolved through the virtual network's DNS: `Hosts=`, then the host resolver); reconnects every ~5 s after a drop |
| `ESPNET[,<baud>]` | an emulated ESP module with NedoOS's ESPNET firmware 1.27 (binary sockets; NedoOS `sd_bootesp.$C` kernel and `currentNetwork=2` apps); `<baud>` = the rate its firmware was built for, by default the port's (ATM Turbo 2+ controller 38400, else 115200) |
| `AT[,<baud>]` | an emulated ESP module with Espressif's AT firmware (NedoOS `currentNetwork=1` apps, Moon Rabbit, Karabas net-tools); `<baud>` as for `ESPNET` |
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
`ZiFi=` says what is on that UART: `NONE` (default: no ZiFi board), `AT` (the
original board: an ESP-01 with Espressif's AT firmware, HackerVBI's
`zifi.spg`), or any `ComPort=` value (`LOOPBACK`, `TCP:...`, `SERIAL:...` for
a real ESP on USB). The Z80 sees the same `#xxEF` range as the COM port:

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
line's peer, `dropped` = bytes the full ring lost); at runtime
`network set zifi=at`. Details:
[2026-10-02-tsconf-zifi](../../docs/inprogress/2026-10-02-tsconf-zifi/README.md).

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
