# TODO - Sprinter network adapters (Ethernet, Wi-Fi, 3C509B, modem)

**Status:** research and design drafted 2026-10-02 (branch `sprinter-network-design`), waiting for owner review.
Nothing built. Part of the Sprinter program ([2026-09-28-sprinter](../2026-09-28-sprinter/TODO.md), PLAN row #59,
roadmap row **S6c**); builds on the ISA design ([2026-10-02-sprinter-isa](../2026-10-02-sprinter-isa/TODO.md)); its
shared pieces (`IIoBusDevice`, slot list in `DescribeNetwork()`, guest registry) are early parts of PLAN row #82.

Owner decisions so far (2026-10-02): NE2000-class Ethernet is built and comes first; maximum reuse of the shared
network stack, no Sprinter-only parallel paths.

## Documents

- [research.md](research.md): every network adapter for the Sprinter, software, activity, sources, glossary
- [tdd.md](tdd.md): scoring, reuse map, generic extensions, NE2000 + Ethernet gateway, SprinterESP, 3C509B, modem,
  config, TTD, automation, tests, phases SN0-SN6
- [open-questions.md](open-questions.md): Q1-Q11 with recommendations

## Remaining

- [ ] Owner review; Q9 (gateway implementation) before SN2, Q7 (order) and Q1 (default card) before SN1
- [ ] SN0 fixtures (kit releases), MAME-fork reference captures, scripted host test server (S)
- [ ] SN1 `IIoBusDevice`, slots in `DescribeNetwork()`, guest registry, `Dp8390` + `Ne2000Board`, TTD blob 38 (M) -
  after ISA I1
- [ ] SN2 Ethernet gateway, RTL kit end to end, frame capture, recipe (M-L)
- [ ] SN3 `PcSerialCard` + SprinterESP, ESP reset pins, ESP8266 AT 2.2.1 / 2.2.2 presets, TTD ids 39-42; the Sprinter
  ESP Network Kit ([sprinter_wifi](https://github.com/witchcraft2001/sprinter_wifi), `UNETESP.DLL`) end to end (M)
- [ ] SN4 Hayes modem peer, SprinterSerial, BC-Term with interrupts (S-M) - after ISA I4
- [ ] SN5 3Com 3C509B (M)
- [ ] SN6 optional bridge to the host LAN (M), only on request (Q2)
- [ ] When the hardware facts are final: move them to `docs/hardware/` with the Sprinter S7 docs move
  (the "Sprinter RTL8019" line of the NedoOS network catalog is already corrected in this branch)
