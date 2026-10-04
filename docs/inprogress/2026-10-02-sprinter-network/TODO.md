# TODO - Sprinter network adapters (Ethernet, Wi-Fi, 3C509B, modem)

**Status (2026-10-04):** SN0-SN5 built and on master (NE2000 + the Ethernet gateway, SprinterESP, the Hayes modem
and SprinterSerial, 3Com 3C509B; each card runs its Sprinter kit end to end with a TTD replay without the host).
Open: SN6 (the host-LAN bridge, wanted by the owner - Q2) and the follow-ups below. Design drafted 2026-10-02. Part of the Sprinter program ([2026-09-28-sprinter](../2026-09-28-sprinter/TODO.md), PLAN row #59,
roadmap row **S6c**); builds on the ISA design ([2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/TODO.md)); its
shared pieces (`IIoBusDevice`, slot list in `DescribeNetwork()`, guest registry) are early parts of PLAN row #82.

Owner decisions so far (2026-10-02): NE2000-class Ethernet is built and comes first (Q1 = B: fitted by default in
slot 2); maximum reuse of the shared network stack, no Sprinter-only parallel paths; Q2 the host-LAN bridge wanted
(SN6 no longer optional); Q9 = A our own gateway, tested well.

Built so far (branch `sprinter-isa-network`, as-built notes in [tdd.md](tdd.md) §18): ISA I1 (the bus the cards sit
on), SN0, SN1, SN2 - the RTL8019AS kit runs end to end (`IFUP`, `PING`, `NSLOOKUP`, `WGET`); recipe
[.recipe/machines/sprinter-network.md](../../../.recipe/machines/sprinter-network.md). SN3 (branch `sprinter-esp-sn3`):
the SprinterESP Wi-Fi card - the ESP kit runs end to end (`NETUP`, `PING`, `WGET`, `UNETESP.DLL`). SN4 (branch
`sprinter-sn4-modem`): the Hayes modem peer, the ISA modem card and SprinterSerial - BC-Term dials and talks over
interrupts. SN5 (branch
`sprinter-sn5-3c509b`): the 3Com 3C509B - the 3C509B kit runs end to end (`IFUP`, `PING`, `NSLOOKUP`, `WGET`).

## Documents

- [research.md](research.md): every network adapter for the Sprinter, software, activity, sources, glossary
- [tdd.md](tdd.md): scoring, reuse map, generic extensions, NE2000 + Ethernet gateway, SprinterESP, 3C509B, modem,
  config, TTD, automation, tests, phases SN0-SN6
- [open-questions.md](open-questions.md): Q1-Q13 with recommendations (Q12, Q13 from SN4: SprinterSerial)

## Remaining

- [x] Owner review: Q1 = B (NE2000 fitted by default), Q2 = the host-LAN bridge wanted, Q7 settled by the owner's order
  (the network before ISA RAM), Q9 = A (our own gateway), Q12 / Q13 decided 2026-10-04 ([open-questions.md](open-questions.md))
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
  the emulated ESP); ~~the IRQ3 line to the PIO (I4)~~ done with ISA I4 2026-10-03 (BC-Term receives the ESP through IRQ3 -> PB0); the ESP32 preset's `+PING` / `+CIPRECVDATA` forms are NonOS-style
  (ESP32 AT 2.x prints the 2.x forms); the ROM's 74 880-baud boot log and flashing in download mode are not modeled
- [x] SN4 (2026-10-03, branch `sprinter-sn4-modem`, as built in [tdd.md](tdd.md) §18): `HayesModemPeer` (ComPortSpec
  `MODEM[,<guest port>]`, shared by every machine's serial port; `[NETWORK] ModemPhonebook`, runtime
  `modem_phonebook`; the call is a `StreamPeer` Dialer; inbound RING / RI / ATA), presets `MODEM` (ISA modem: 16550A
  at a COM base, OUT2-gated IRQ) and `DUAL16552` (SprinterSerial rev 1.1.1 from its netlist: PC16552D, A8 = CHSEL, D3 /
  J1-J2 decode, AFR, J5 / J6), TTD blobs **48 / 49** for the second UART, guests 9 / 10; BC-Term 1.11 dials a scripted
  BBS through the phone book and talks over the ISA interrupt, TTD replay without the host; reports on all surfaces +
  Qt; recipe [.recipe/machines/sprinter-network.md](../../../.recipe/machines/sprinter-network.md#isa-hayes-modem-and-sprinterserial)
- [ ] SN4 follow-ups: ~~Q12 (SprinterSerial COM1's floating modem inputs)~~ decided 2026-10-04 (inactive as the hardware,
  plus the `PLUG` loopback test plug); ~~Q13 (both IRQ jumpers fitted)~~ decided 2026-10-04 (high wins + a warning); BC-Term's file transfers (X / Y / Zmodem) over the modem not run yet; a modem on the ZX-Evo COM port is
  unit-tested (the peer), not run with a ZX program
- [x] SN5 (2026-10-03, branch `sprinter-sn5-3c509b`, as built in [tdd.md](tdd.md) §18): `EtherLink3` (ID port
  isolation, EEPROM from the real boards, windows 0-6, FIFOs, status / IRQ, 10BASE-T link test, loopback, statistics,
  power) as an `IEthernetCard`, `[ISA] SlotN=EL3C509B` (`SlotNChip=TPO | TP`, base in steps of `#10`), blob 45 v2;
  the Sprinter 3C509B kit 0.1.2 end to end (`EL3INFO`, `NETCFG`, `IFUP` DHCP, `PING`, `NSLOOKUP`, `WGET` byte-exact,
  TTD replay without the host); recipe [.recipe/machines/sprinter-network.md](../../../.recipe/machines/sprinter-network.md)
- [ ] SN5 follow-ups: `UNET509B.DLL` through `UNETTEST` (assemble from the kit's tag with sjasmplus); run `FTP`, `NTP`,
  `TFTP`, `TELNET`; ISA Plug and Play isolation (not used by the kit; the boards ship "contention only")
- [ ] SN6 bridge to the host LAN (M): **wanted** (owner, Q2, 2026-10-02: "the bridge right away", NAT stays the
  no-admin default and the bridge is the second host path for the same card); priority P2 (owner, 2026-10-04: after the
  ATAPI CD)
- [ ] When the hardware facts are final: move them to `docs/hardware/` with the Sprinter S7 docs move
  (the "Sprinter RTL8019" line of the NedoOS network catalog is already corrected in this branch)
