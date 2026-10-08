# Status: TODO

GMX (`MM_GMX`, "ZS Scorpion + GMX", 2048K): **design written 2026-10-07, nothing implemented.** The model is not
creatable today. Plan: [design.md](design.md) section 7; tests: [tdd-plan.md](tdd-plan.md).

## Phases

- [ ] Phase 1: creatable machine (factory, `GmxState`, register file, RAM and ROM mapping, config folder, roles, recipe sections 1-2, parity on all surfaces)
- [ ] Phase 2: TTD blob, snapshot capture policy, `MachineStateTransfer`, paging fields on all surfaces
- [ ] Phase 3: 640x200 extended video, Qt, screenshots
- [ ] Phase 4: turbo strobes, Magic freeze, BLKEXT details, INT select
- [ ] Phase 5: slots and media (General Sound incompatibility, SMUC disk list)
- [ ] Phase 6: evidence-driven corrections

Done so far:
- [x] Source survey: Unreal Speccy (zx-evo fork), MAME, pico-speccy, the 1999 and 2004 articles, the Black_Cat port table
  ([research-reference-consensus.md](research-reference-consensus.md)); ZXMAK2, Xpeccy, UnrealSpeccyP and others have no GMX
- [x] Requirements, design, test plan, firmware survey ([roms.md](roms.md))

## Open questions

Each has a recommendation, chosen so that the phase can start without a reply.

| Q | Question | Recommendation |
|:--|:--|:--|
| Q1 | Which plane and page does the loader hand control to for the Scorpion scheme, and what `RESET=` / role names does the config need? | Boot `gmx.rom` in phase 1 and trace the `#7EFD` writes (TTD makes this cheap); name roles from that. Until then plane 0 = `Specific`, planes 1-4 as Section 2.4 of [roms.md](roms.md) |
| Q2 | Does the `#0100-#010F` ProfROM plane strobe exist on the GMX scheme? MAME inherits it, pico-speccy disabled it after a real-firmware crash | Off (pico-speccy's trace). Keep the code path behind a config key `[GMX] ProfRomStrobe=0` only if a firmware turns up that needs it |
| Q3 | `#7EFD` bit 2: "lifts the `#7FFD` lock" (Unreal) or "ignores Magic / NMI" (MAME)? | MAME's meaning; no lock lifting. Unreal's reading has no second source, MAME's names the Magic gate and is used by `do_nmi` |
| Q4 | Scroller semantics: line scroller (`offset / 80`, MAME, pico-speccy, article "line scroller") or byte offset (Unreal)? Does `#7AFD` keep 8 bits (Unreal, article) or `& #F0` (MAME, pico-speccy)? | Line scroller `offset / 80 mod 200`, store 8 bits, use all 14 bits in the division. Revisit with a scroll demo or the board test (scheme 6) |
| Q5 | Do `IN` from the `#1FFD` / `#7FFD` families still toggle turbo on a GMX? | Yes, as the Scorpion Turbo+ host board (MAME inherits it); the GMX firmware writes `#7EFD`, so a test with both writers is cheap. Config key `[GMX] TurboStrobe=1` |
| Q6 | What reads the Magic shift register, and in what order? | Implement as MAME; no extra behavior |
| Q7 | The Magic lock of `#7AFD` / `#7EFD` read-backs (MAME only) | Phase 4, implement as MAME, flagged "M" in the port map |
| Q8 | Pentagon, Composit and test schemes as models? | No. Pentagon exists; the others have no software worth the cost. The test scheme can be reached through the loader with `gmx.rom` anyway, but it needs the FPGA reload, which we do not model, so `[GMX]` stays Scorpion-scheme only. Ask the owner if a Pentagon-scheme mode is wanted |
| Q9 | `#7EFD` bits 0-1 (flash EWR / Vpp) and the loader's flash update | Ignore; flash is read-only. Revisit only if the loader's "P - program from disk" path must run |
| Q10 | RAM sizes: the article lists 256, 512, 1024, 1280 and 2048 KB SIMM sets; the model table has 2048 only | Keep 2048. Smaller sizes need the same wrap rule MAME and pico-speccy use (`% pages`) and an `AvailRAMs` change; add on request |
| Q11 | Buffer screen pages: `#3A / #7A` (article) or `#3B / #7B` (three emulators)? | `#3B / #7B` (code agrees thrice; `#7FFD` bit 3 selects, the Scorpion habit of 5 / 7). The article may count pages from `#39` as "first / second" |
| Q12 | Exact pixel timing and border width of the extended mode | Naive first: Scorpion line, 640 pixels at twice the dot clock, border as MAME (24 pixels sides). Measure against the board test screen photo if one turns up |
| Q13 | INT: Scorpion or Pentagon (shadow monitor option); the "132000 clocks in turbo" sentence | Scorpion default; a `[GMX] Int=Scorpion\|Pentagon` key later; ignore the sentence (not a frame length at 7 MHz) |
| Q14 | General Sound does not work with the card (article). Model it? | Mark `unrealistic` in the slot planner, allow with the override, as the ZX-MultiSound on `SCORPION` |
| Q15 | Port decode strictness: Black_Cat's patterns and Unreal's mask are loose, MAME and pico-speccy use exact 16-bit addresses; BC's `#7FFD` has A14 free | Exact high byte, `A5 = 1, A1 = 0, A0 = 1` for the low part, as design 2.2; a sweep test pins it |
| Q16 | Newer firmware (ProfROM + GMX 5.xx / 6.xx, loader V2.00) is not in the local collection | Look in the KLUG BBS archive and the zxart / zx-pk file sections; phase 1 does not need it |
| Q17 | Does scheme 6 (board test) boot in our model? It needs a scheme reload we do not do | Probably not; the phase 3 video test then uses a synthetic RAM pattern |

## Pointers

[README.md](README.md) - [requirements.md](requirements.md) - [design.md](design.md) - [tdd-plan.md](tdd-plan.md) -
[roms.md](roms.md) - [research-reference-consensus.md](research-reference-consensus.md)
