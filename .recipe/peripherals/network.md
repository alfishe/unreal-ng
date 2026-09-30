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
