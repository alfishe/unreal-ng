# TODO - Sprinter ISA slots, ZX-bus adapter, General Sound on the Sprinter

**Status:** design drafted 2026-10-02 (branch `sprinter-isa-design`), waiting for owner review. Nothing built.
Part of the Sprinter program ([2026-09-28-sprinter](../2026-09-28-sprinter/README.md), PLAN row #59, phase S6b).

## Documents

- [research.md](research.md): how the real Sprinter reaches its two ISA-8 slots, the ZX-bus adapter, MAME, the
  software that uses ISA cards, glossary
- [tdd.md](tdd.md): the design (bus, card interface, slot config, adapter, GS / NeoGS on it, TTD, automation,
  tests, phases I0-I8)
- [open-questions.md](open-questions.md): owner decisions Q1-Q13 with recommendations
- ProPlay disassembly: [docs/disasm/software/sprinter/proplay/](../../disasm/software/sprinter/proplay/README.md)

## Remaining

- [ ] Owner review; answers to Q1-Q4 before I1 / I2
- [ ] I0 references (MAME ProPlay + NeoGS capture, ISA I/O tap), MOD generator (S)
- [ ] I1 ISA bus core (M), after `sprinter-s6` is merged. `PortDecoder::ZxBusPresent()` is already on master
  (2026-10-03, the Sprinter answers `false`: no GS / NeoGS fitted, TTD port journals on); I1 / I2 make it follow
  the adapter in a slot ([tdd.md](tdd.md) §2 "As built ahead of I1")
- [ ] I2 ZX-bus adapter + GS / NeoGS, ProPlay end to end vs MAME (M)
- [ ] I3 ISA RAM (S), I4 PIO IRQ lines (S)
- [ ] Network cards (NE2000 Ethernet first, owner decision 2026-10-02; SprinterESP, 3C509B, modem, SprinterSerial):
  [2026-10-02-sprinter-network](../2026-10-02-sprinter-network/TODO.md), phases SN0-SN6; SN1 needs I1, SN4 needs I4
- [ ] Deferred: I5 ZX-bus seam + MoonSound, I6 ESS688 / SB Pro, I7 SprinterJoy, I8 Sprinter-FT
- [ ] When I1 lands: apply research §10 corrections to the Sprinter `hardware-reference.md` §11, link this folder
  from the Sprinter roadmap S6b row and `TODO.md`
