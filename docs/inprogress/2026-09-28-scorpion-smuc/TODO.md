# Scorpion SMUC integration - TODO

**Status:** analysis and plan written 2026-09-28; nothing implemented. Starts after branch
`ide-atapi` (IDE rollout 1) is on `master` - met (`f5fc5f05`). Verdict: moderate, the IDE core is done; the rest is
small bus fixes, a persistent / deterministic clock and NVRAM, TTD, and real-ProfROM acceptance.
Details: [README.md](README.md), [integration-plan.md](integration-plan.md).

Done:
- [x] Hardware consensus across UnrealSpeccy, Xpeccy, MAME, ZXMAK2, the ports guide and the ProfROM 4.01 firmware ([hardware-reference.md](hardware-reference.md))
- [x] IDE design Q3 (reset polarity) answered from the firmware: `#FFBA` D0 = 0 resets
- [x] Current state and gaps G1-G13 ([current-state-and-gaps.md](current-state-and-gaps.md))
- [x] Software, boot path, disk formats, test images ([software-and-boot.md](software-and-boot.md))

Remaining:
- [ ] S1 TR-DOS gate + decode mask `#B8E7` (S)
- [ ] S2 INTRQ on `#FFBA` D7, `#7FBA | #37`, drop the fake IDE registers (S); the reset polarity (D0 = 0) and the `#D8BE` latch rule are already fixed on master (`f5fc5f05`)
- [ ] S3 one presence rule, parse `[MISC] SMUC` (S)
- [ ] S4 persistent NVRAM / CMOS, emulated-time RTC, with PLAN #60(c) (M)
- [ ] S5 TTD blob `SmucBoard` (M)
- [ ] S6 real-ProfROM acceptance A1-A6, fixture T1 (L)
- [ ] S7 device report `smuc` section on every surface, Qt option, docs, recipe (M)
- [ ] S8 8259 + card interrupts (L, optional)
- [ ] Open: Q2 (`#FFBA` D7), Q3 (`SMUC=1` without IDE), Q4 (NVRAM file location), Q5 (virtual FDD trap), Q6 (LW ProfROM builds)
- [x] PLAN.md row: [#62](../PLAN.md)
