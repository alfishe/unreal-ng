# TODO - network traffic for the debuggers

- [x] Design ([design.md](design.md)): one tap in the virtual network (after the one-door refactor, master 81c12ef21);
  owner Q1 always ring + start / stop file, Q2 operations in our list + synthetic packets in pcapng, Q3 TCP port + extcap
- [x] T1 (2026-10-04): `NetworkTrafficTap` (owned by NetworkManager, recorded by the virtual network: frames through
  the gateway's capture point, socket operations in the socket API and `Deliver`), the 8 MiB ring, start / stop pcapng
  file, `TrafficAccess` on WebAPI + OpenAPI, CLI, Lua, Python, MCP text, the network report's `traffic` part; adapter
  names (`zxnetusb`, `com.esp`, `isa1.esp`, `isa1.modem`, `gateway-nat`, `lan`); verified live (recipe)
- [x] T2 (2026-10-04): `SocketPacketizer` - socket operations as synthetic TCP (handshake, chained segments of up
  to 1460 bytes, FIN, RST) / UDP / ICMP packets in the ring's pcapng and the file, one synthetic adapter address per
  adapter (10.0.2.15 and up), each packet's comment naming the real operation; summaries were already in T1
- [ ] T3 live stream (TCP port, extcap)
- [ ] T4 Qt window with Seek here
