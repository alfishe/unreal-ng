# DONE - network traffic for the debuggers (PLAN #91 retired)

- [x] Design ([design.md](design.md)): one tap in the virtual network (after the one-door refactor, master 81c12ef21);
  owner Q1 always ring + start / stop file, Q2 operations in our list + synthetic packets in pcapng, Q3 TCP port + extcap
- [x] T1 (2026-10-04): `NetworkTrafficTap` (owned by NetworkManager, recorded by the virtual network: frames through
  the gateway's capture point, socket operations in the socket API and `Deliver`), the 8 MiB ring, start / stop pcapng
  file, `TrafficAccess` on WebAPI + OpenAPI, CLI, Lua, Python, MCP text, the network report's `traffic` part; adapter
  names (`zxnetusb`, `com.esp`, `isa1.esp`, `isa1.modem`, `gateway-nat`, `lan`); verified live (recipe)
- [x] T2 (2026-10-04): `SocketPacketizer` - socket operations as synthetic TCP (handshake, chained segments of up
  to 1460 bytes, FIN, RST) / UDP / ICMP packets in the ring's pcapng and the file, one synthetic adapter address per
  adapter (10.0.2.15 and up), each packet's comment naming the real operation; summaries were already in T1
- [x] T3 (2026-10-04): `TrafficStream` - the tap's live readers served as a pcapng stream on a TCP port (netsock:
  macOS, Linux, Windows; the ring first, then each packet; a reader 16 MiB behind is dropped), `[NETWORK]
  TrafficStream=off|auto|<port>`, actions `stream` / `stream-stop` on every surface, the Wireshark extcap script
  `tools/wireshark/unreal-ng-extcap.py` (+ `.bat` for Windows) with its README
- [x] T4 (2026-10-05): the Qt window Tools > Network traffic (Ctrl+6, dockable): the newest 20000 records polled
  through `TrafficAccess` (frame, time since the row above, adapter, direction, length, summary), word filter + kind,
  decode tree (Ethernet / ARP / IPv4 / ICMP / UDP / TCP / DHCP / DNS, socket operation fields), hex dump; Seek here
  (pauses, ends a running TTD recording - a seek is refused while recording - and seeks to `frame` / `t_in_frame`;
  off with the reason outside a recording), Clear, Save pcapng, Record to file / stop, stream start / stop. Qt-free
  model `unreal-qt/src/network/core/trafficpanelmodel` (hud-core-tests), the window on a real machine in
  unreal-qt-tests (`trafficwindow_test.cpp`). Fixed with it: the stream server handled a reader accepted in the
  same round as the polled ones (a read past its poll items, which could close the new reader; 50 of 300 runs of
  `TrafficStream_Test` failed under load, 0 of 600 after)
