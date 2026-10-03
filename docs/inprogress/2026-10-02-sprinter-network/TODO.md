# TODO - Sprinter network adapters (Ethernet, Wi-Fi, 3C509B, modem)

**Status:** research and design drafted 2026-10-02 (branch `sprinter-network-design`), waiting for owner review.
Nothing built. Part of the Sprinter program ([2026-09-28-sprinter](../2026-09-28-sprinter/TODO.md), PLAN row #59,
roadmap row **S6c**); builds on the ISA design ([2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/TODO.md)); its
shared pieces (`IIoBusDevice`, slot list in `DescribeNetwork()`, guest registry) are early parts of PLAN row #82.

Owner decisions so far (2026-10-02): NE2000-class Ethernet is built and comes first (Q1 = B: fitted by default in
slot 2); maximum reuse of the shared network stack, no Sprinter-only parallel paths; Q2 the host-LAN bridge wanted
(SN6 no longer optional); Q9 = A our own gateway, tested well.

Built so far (branch `sprinter-isa-network`, as-built notes in [tdd.md](tdd.md) §18): ISA I1 (the bus the cards sit
on), SN0, SN1, SN2 - the RTL8019AS kit runs end to end (`IFUP`, `PING`, `NSLOOKUP`, `WGET`); recipe
[.recipe/machines/sprinter-network.md](../../../.recipe/machines/sprinter-network.md). SN3 (branch `sprinter-esp-sn3`):
the SprinterESP Wi-Fi card - the ESP kit runs end to end (`NETUP`, `PING`, `WGET`, `UNETESP.DLL`).

## Documents

- [research.md](research.md): every network adapter for the Sprinter, software, activity, sources, glossary
- [tdd.md](tdd.md): scoring, reuse map, generic extensions, NE2000 + Ethernet gateway, SprinterESP, 3C509B, modem,
  config, TTD, automation, tests, phases SN0-SN6
- [open-questions.md](open-questions.md): Q1-Q11 with recommendations

## Remaining

- [ ] Owner review; Q9 (gateway implementation) before SN2, Q7 (order) and Q1 (default card) before SN1
- [x] SN0 (2026-10-03): kit releases as fixtures (`testdata/machines/sprinter/network/`: RTL8019AS kit 0.3.8, ESP kit
  0.2.1, byte for byte, `.gitattributes` `-text`), the scripted host server `core/tests/_helpers/scriptedhostnet.h`
  (HTTP, echo, Gopher, banner, UDP echo, NTP, DNS names, ping, host clients for forwards) with its own test
- [ ] MAME-fork reference captures (T-NET-16): not cheap (the fork's RTL8019AS / SprinterESP cards need a MAME build
  of `witchcraft2001/mame_sprinter`); deferred, the kit programs on the emulator are the acceptance instead
- [x] SN1 (2026-10-03): `IIoBusDevice` (ATM INTERNAL bus migrated), slots in `DescribeNetwork()`, `Dp8390` +
  `Ne2000Board` (RTL8019AS / UM9003 / NE1000), TTD blob **45** `EthernetNics` (39-44 were taken on master; 44 = Profi XT keyboard controller), slot
  report + ISA resources / conflicts / access journal on all five surfaces and in Qt. The guest registry is minimal
  (fixed guest 6 for the gateway); the key registry of Q10 is open
- [x] SN2 (2026-10-03): `EthernetGateway` (ARP / IPv4 / ICMP / UDP / TCP, DHCP + DNS + forwards shared), RTL kit end
  to end incl. a TTD replay without the host, frame capture (JSON / pcap) + injection on every surface, recipe
- [ ] SN2 follow-ups: host-side receive pause (now: reset above 256 KB queued), TCP zero-window probes
- [x] SN3 (2026-10-03, branch `sprinter-esp-sn3`, as built in [tdd.md](tdd.md) §18): `PcSerialCard` (preset
  `SPRINTERESP`, read from the rev 1.0.5 schematic: A13-A3 decode without AEN, INTR to IRQ3 ungated), OUT1 = ESP RST,
  OUT2 = GPIO0 (`Uart16550::onAuxLines`, `EspModule::SetResetPin` / `SetFlashPin`), ESP8266 ESP-AT 2.2.1 / 2.2.2
  presets (`EspChip=ESP8266-AT221|ESP8266-AT222`) with the commands the kit sends, `[ISA] SlotNPeer`, runtime
  `isa1_peer` / `isa2_peer`, guests 7 / 8, TTD blobs **46 / 47** (`SlotSerial1` / `2`; 40-43 had gone to other
  devices); the Sprinter ESP Network Kit 0.2.1 end to end (`NETUP` 2.2.2 and 2.2.1 profiles, DHCP, `PING` with DNS,
  `WGET` byte-exact, `UNETESP.DLL` through `UNETTEST`, TTD replay without the host); recipe
  [.recipe/machines/sprinter-network.md](../../../.recipe/machines/sprinter-network.md)
- [ ] SN3 follow-ups: run the kit's `FTP` (passive, two links), `NTP`, `TELNET`, `TFTP`, `WTERM` and the Gopher browser
  through `UNETESP.DLL`; ESPT / wterm from the MAME-pack disk; both `NET_ESP_FLOW` modes in a test (the kit picks 3 on
  the emulated ESP); the IRQ3 line to the PIO (I4); the ESP32 preset's `+PING` / `+CIPRECVDATA` forms are NonOS-style
  (ESP32 AT 2.x prints the 2.x forms); the ROM's 74 880-baud boot log and flashing in download mode are not modeled
- [ ] SN4 Hayes modem peer, SprinterSerial, BC-Term with interrupts (S-M) - after ISA I4
- [ ] SN5 3Com 3C509B (M)
- [ ] SN6 optional bridge to the host LAN (M), only on request (Q2)
- [ ] When the hardware facts are final: move them to `docs/hardware/` with the Sprinter S7 docs move
  (the "Sprinter RTL8019" line of the NedoOS network catalog is already corrected in this branch)
