# Scorpion SMUC integration - TODO

**Status (readiness check 2026-10-04, against the code on master + working tree):** the card
boots the real ProfROM 4.01 and passes IDENTIFY through the shared disk core; the clock is the
shared deterministic `Ds12887` chip and is in TTD; **everything else below is still open**. The
card stays off in the shipped configs (`Scheme=NONE`, `SMUC=0`), as in MAME and UnrealSpeccy.
Verdict unchanged: moderate; the missing pieces are small bus fixes, persistence, the rest of
TTD, and real-ProfROM acceptance. Details: [README.md](README.md),
[integration-plan.md](integration-plan.md), [current-state-and-gaps.md](current-state-and-gaps.md).
PLAN row: [#62](../PLAN.md).

## Readiness by component (2026-10-04)

| Component | State | Evidence / what is missing |
|---|---|---|
| IDE window (`#F8BE-#FFBE`, `#D8BE`) | **ready** | `ScorpionSMUC_Test.ProfRomIdentifiesTheDiskThroughTheDiskCore`; reset polarity `IdeResetIsBitZeroLow`; shared `IdeAdapter`, slots, image formats, media manager |
| Serial EEPROM 24C16 (`#FFBA` bits 4/5/6) | **ready for the protocol** | `SerialEEPROMWriteAndReadback`, ProfROM first-boot format path; no file, no TTD (below) |
| Clock `#DFBA` + `#FFBA` bit 7 | **ready** | shared `Ds12887` (256 cells, DS1685 registers, emulated-time base set in the decoder constructor, TTD id 18 captured even with the card absent, report on every surface, recipe `.recipe/peripherals/cmos-rtc.md`; PLAN #60(c), `04383910`, `5c09bb19c`). Open: periodic / alarm interrupt reaches nothing (S8) |
| Version `#5FBA` / revision `#5FBE` | ready | `#3F` / `#57`, as the consensus |
| TR-DOS gate, decode mask | **missing** | the card answers in any ROM; the mask is not `#B8E7` (G1, G2) |
| `#FFBA` D7 INTRQ, `#7FBA` read | **missing** | D7 reads constant 1; `#7FBA` reads `latch \| #3F`, hardware `\| #37` (G4, G5) |
| Presence rule | **missing** | two flags (`_smucEnabled`, `Scheme=SMUC`); `[MISC] SMUC` is not parsed; the fake `_smucIdeRegs` stub is still in the decoder (G6) |
| NVRAM / CMOS persistence | **missing** | `Ds12887::LoadNvram` / `SaveNvram` exist but the Scorpion decoder never calls them, `SMUCNvram` EEPROM has no file; the ProfROM reformats its settings (~4 s) on every cold boot (G7) |
| TTD of the card | **partly** | clock yes (id 18). Missing: EEPROM contents + protocol state, `pFFBA`, `p7FBA`, serial-link lines (G9); PLAN #40 Phase 1 lists the EEPROM as a memory region |
| Virtual floppy `#7FBA` | latch only | enough for the ProfROM's software redirection; hardware trap unknown (Q5) |
| 8259 + card interrupts | absent (consensus) | `#7EBE/#7FBE` read `#57`; optional (G12) |
| End-to-end ProfROM acceptance | **missing** | only IDENTIFY -> READ SECTORS is tested; no partition, no TR-DOS image from the disk, no program run (G10) |
| Card-specific report, Qt option, docs | **missing** | `state ide` and `rtc` report parts of it; nothing shows `#FFBA` / `#7FBA` / EEPROM; no Qt switch; no `.recipe` for SMUC itself (G11) |

Done:
- [x] Hardware consensus across UnrealSpeccy, Xpeccy, MAME, ZXMAK2, the ports guide and the ProfROM 4.01 firmware ([hardware-reference.md](hardware-reference.md))
- [x] IDE design Q3 (reset polarity) answered from the firmware: `#FFBA` D0 = 0 resets; fixed on master (`f5fc5f05`), with the `#D8BE` latch rule
- [x] Current state and gaps G1-G13 ([current-state-and-gaps.md](current-state-and-gaps.md))
- [x] Software, boot path, disk formats, test images ([software-and-boot.md](software-and-boot.md))
- [x] S4 first half: emulated-time DS1685 clock on the shared `Ds12887` (G8) - PLAN #60(c), on master
- [x] S5 first half: clock in TTD (`PeripheralId::Ds12887` = 18)
- [x] S7 first half: clock report and cell read / write on CLI, WebAPI, MCP, Lua, Python (`5c09bb19c`)
- [x] PLAN.md row: [#62](../PLAN.md)

Remaining (each phase: automation on all five surfaces + recipe, per the project rules):
- [ ] S1 TR-DOS gate + decode mask `#B8E7` (S) - G1, G2
- [ ] S2 INTRQ on `#FFBA` D7, `#7FBA | #37`, drop the fake IDE registers (S) - G4, G5
- [ ] S3 one presence rule, parse `[MISC] SMUC` (S); ship a variant config / Qt switch that fits the card - G6
- [ ] S4b persistent files: EEPROM image + CMOS cells through `Ds12887::LoadNvram` / `SaveNvram`, config key and location (Q4), start-up and shutdown / change flush (M) - G7
- [ ] S5b TTD blob `SmucBoard`: EEPROM (as a TTD v2 memory region, PLAN #40 Phase 1), serial-link state, `pFFBA`, `p7FBA`; rebuild the TTD fixture corpus (M) - G9
- [ ] S6 real-ProfROM acceptance A1-A6, fixture T1: create a partition, mount a TR-DOS image from the disk on drive A, run a program from it (L) - G10
- [ ] S7b `smuc` device-report section (`#FFBA` bits, `#7FBA`, EEPROM, virtual drives, gate state) on every surface, Qt option, docs, `.recipe/machines/scorpion-smuc.md` (M) - G11
- [ ] S8 8259 + card interrupts (RTC -> 8259 -> IM 2) (L, optional) - G12
- [ ] Open: Q2 (`#FFBA` D7), Q3 (`SMUC=1` without IDE), Q4 (NVRAM file location), Q5 (virtual FDD trap), Q6 (LW ProfROM builds)

Order for ProfROM readiness: S3 -> S4b (removes the 4 s first-boot cost, the reason the card is off by
default) -> S1/S2 (bus correctness) -> S5b -> S6 (the acceptance run) -> S7b. S8 only on demand.
