# TODO - network traffic for the debuggers

- [x] Design ([design.md](design.md)): one tap in the virtual network (after the one-door refactor, master 81c12ef21);
  owner Q1 always ring + start / stop file, Q2 operations in our list + synthetic packets in pcapng, Q3 TCP port + extcap
- [x] T1 (2026-10-04): `NetworkTrafficTap` (owned by NetworkManager, recorded by the virtual network: frames through
  the gateway's capture point, socket operations in the socket API and `Deliver`), the 8 MiB ring, start / stop pcapng
  file, `TrafficAccess` on WebAPI + OpenAPI, CLI, Lua, Python, MCP text, the network report's `traffic` part; adapter
  names (`zxnetusb`, `com.esp`, `isa1.esp`, `isa1.modem`, `gateway-nat`, `lan`); verified live (recipe)
- [ ] T2 synthetic TCP / UDP for socket operations, summaries
- [ ] T3 live stream (TCP port, extcap)
- [ ] T4 Qt window with Seek here
