# TODO - Sprinter ISA slots, ZX-bus adapter, General Sound on the Sprinter

**Status:** design drafted 2026-10-02 (branch `sprinter-isa-design`); owner decisions Q1-Q3 recorded. **I1 built**
2026-10-03 (branch `sprinter-isa-network`, as-built notes in [tdd.md](tdd.md) §14); the network cards come next
(owner order 2026-10-02: ISA I1 and network SN1-SN3 before the NeoGS adapter I2).
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
- [x] I1 ISA bus core (2026-10-03): `SprinterIsaBus` + `IIsaCard` (`core/src/emulator/io/sprinter/isa/`), window-3
  routing (read, write, opcode fetch, tool peek), the full `#9FBD` latch (A19-A14, AEN, RESET DRV edge to both slots;
  no reset input), `[ISA]` config + create options, TTD blob 33 + the session population guard, `state/isa` +
  `control/isa` on WebAPI / OpenAPI, MCP (`inspect_state` aspect `isa`, cycles through `invoke_api`), CLI `isa`, Lua /
  Python `isa_*`, the port trace codes `isa_io` / `isa_mem`, hardware-reference §11 corrected. `ZxBusPresent()` stays
  `false` until I2 fits the adapter ([tdd.md](tdd.md) §14)
- [ ] I2 ZX-bus adapter + GS / NeoGS, ProPlay end to end vs MAME (M)
- [ ] I3 ISA RAM (S), I4 PIO IRQ lines (S)
- [ ] Network cards (NE2000 Ethernet first, owner decision 2026-10-02; SprinterESP, 3C509B, modem, SprinterSerial):
  [2026-10-02-sprinter-network](../2026-10-02-sprinter-network/TODO.md), phases SN0-SN6; SN1 needs I1, SN4 needs I4
- [ ] Deferred: I5 ZX-bus seam + MoonSound, I6 ESS688 / SB Pro, I7 SprinterJoy, I8 Sprinter-FT
- [x] When I1 lands: research §10 corrections applied to the Sprinter `hardware-reference.md` §11; this folder linked
  from the Sprinter `TODO.md`
- [ ] T-ISA-15 A/B (`BM_HostFrame_*_Fast`): run with the network phase SN1 (the default NE2000 changes the Sprinter's
  frame cost); I1 itself touches no shared hot path (window 3's ISA branch sits behind the Sprinter's `_anyRedirect`)
