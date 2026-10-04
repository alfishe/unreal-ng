# Network traffic for the debuggers

**Status:** task recorded 2026-10-04 (owner); design and build right after SN6
([sn6-bridge-design.md](../2026-10-02-sprinter-network/sn6-bridge-design.md)). PLAN #91.

## Goal

One view of everything a machine sends and receives on its network adapters, for people and for every automation
surface, built **once in the shared network core** and working the same for every machine and adapter and on every
host (macOS, Linux, Windows).

## What exists (2026-10-04)

- The Ethernet gateway keeps the last 256 frames of the frame-level cards (NE2000, 3C509B), both ways, as a report
  with one-line summaries or a pcap file: WebAPI `GET /network/frames` (`format=pcap`), CLI `network frames`, Lua /
  Python `network_frames`, Python `network_frames_pcap`, MCP through `invoke_api`; frame injection `POST /network/frame`
  (`EthernetAccess`, network tdd §14).
- The virtual network writes a text log of socket events (connected, reset, DHCP, ...), no packets.

## What is missing

| Gap | Wanted |
|---|---|
| Socket-level adapters (ESP-AT on SprinterESP / ATM2IOESP / ZiFi, ZXNETUSB / W5300, the Hayes modem) have no packet view | their traffic as packets in the same capture (synthesized frames for socket data, or pcapng with one interface per adapter) |
| No link to time | every packet stamped with machine time (frame + T-state) and the TTD position: select a packet, seek there |
| Depth and filters | a configurable capture size, filters by adapter / address / port / protocol, counters |
| Live | a live stream for Wireshark (pcapng over a pipe or a TCP port) and for automation (poll since an index) |
| UI | a Network traffic window in the Qt debugger (list, decode, hex, seek) |
| Bridge mode | the frames to and from the host LAN in the same capture (SN6 adds them, directions `lan_in` / `lan_out`) |

## Requirements

- One component (`NetworkTrafficTap` or similar) fed by the gateway, the virtual network and the serial peers;
  every machine's adapters reach it through the shared classes, no per-machine paths.
- All five automation surfaces (WebAPI + OpenAPI, MCP, CLI, Lua, Python) and Qt read the same reports; recipes in
  `.recipe/`.
- Costs nothing when nobody looks (no capture work on the hot path while the tap is off), measured.
- Works the same during a TTD replay (the replayed traffic is captured as it was live).

## Files

- [TODO.md](TODO.md)
