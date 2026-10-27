# Status: TODO

Nemo's KAY (`MM_KAY`, Kay-1024) as a creatable machine. Research and design written 2026-10-07; nothing is
implemented. Plan: [design.md](design.md) section 8.

## Phases

- [ ] Phase 0: evidence (frame / INT counters, top RAM bits, Kempston wiring, ROM role boot experiment)
- [ ] Phase 1: `PortDecoderKay`, paging, ROM, config, `KayPaging` blob, automation parity, recipe `.recipe/machines/kay.md`, TTD recording
- [ ] Phase 2: ports and devices, parity, recipe, TTD
- [ ] Phase 3: video and timing, parity, recipe, TTD
- [ ] Phase 4: turbo, parity, recipe, TTD
- [ ] Phase 5: snapshots, `PagingFamily::Kay`, TTD fixture
- [ ] Phase 6: variants (Kay-256, NemoBus cards, 2006 NB, 2010 SL4), each its own design
- [ ] When phase 1 is accepted: add the folder to `docs/inprogress/PLAN.md` (not done here)

## Open questions

| ID | Question | Recommendation |
|:--|:--|:--|
| Q1 | Which image page holds TR-DOS and which the Kramis service for the role table? Unreal's formula and the schematic's physical A15 / A14 reading disagree | Start with Unreal's roles (the existing `rom.cpp` pointers); settle by the `TrDosBanner` boot test; the table is four entries, `[KAY] RomLayout=` for `kay1024b.rom` |
| Q2 | #7FFD decode: Unreal ignores A0 and A14 | Tight decode of the passport, schematic and BC table; note it in the recipe |
| Q3 | Order of the two top RAM bits, and do they also move #4000 / #8000 (global in DD9)? | Unreal's order, #C000 only; trace DD47 / DD9 in phase 0; only snapshots and rare software can tell |
| Q4 | Exact frame length, INT position (Unreal's dialog shows 69887 and paper 16132), INT length by M1 count (DD53) | 69888 / 224 / 32 T first; decode the counters from the redraw in phase 0; M1-counted INT later if a test program shows it |
| Q5 | Kempston bit order: the passport prints D0 = Left, D1 = Right | Standard Kempston until the wiring is traced |
| Q6 | #FE read D5 = 0 and D7 = BUSY (1 with no printer) | Follow the passport; revisit only if a real-software test breaks |
| Q7 | Default RAM: the row says 256, the shipped ROM and passport say 1024 | Default 1024, `KAY256` alias for 256 |
| Q8 | Kay-256 (v1.4): 48 KB ROM, turbo per machine cycle, WAITs at 3.5 MHz | Phase 6 variant with its three 16K ROMs (speccy4ever, not in the repository yet) |
| Q9 | Turbo waits (ROM 2.0, RAM 1.75 vs 1.9, stretched IORQ) | Flat 2x clock; measure before an overlay |
| Q10 | The BETA-TURBO, NemoIDE and XT keyboard are slot cards | Built-in Beta 128 and `IDE_NEMO` now; cards with the zx-bus-slots work |
| Q11 | Centronics through AY ports A / B | Stub (no printer); BUSY 1 |
| Q12 | AY chip: passport AY-3-8910, 2010 redraw YM2149F | Keep the config default |
| Q13 | Capture: no `.szx` machine id for Kay | `.sna` / `.z80` only for a plain 128K state; TTD carries the rest |
| Q14 | Floating bus | None found in any source; `IN` of an undecoded port is Kempston anyway (A0 = 1), even ports read #FF |
| Q15 | Kempston mouse variant for Kay (BC #4 lists model 7 in the standard group) | Standard decode |
| Q16 | `/IODOS` and IORQGE rules of the NemoBus (card wins) | With the slots work; none in phases 1-5 |
