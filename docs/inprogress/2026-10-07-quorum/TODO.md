# Status: TODO

Quorum (`MM_QUORUM`, 128K / 1024K): **design written 2026-10-07, nothing implemented.** The model slot, ROM path and ROM
page mapping already exist; the machine cannot be created. Plan: [design.md](design.md) section 5.

## Done
- [x] Reference research across UnrealSpeccy, ZXMAK2, kozynax, the Black_Cat port table and the factory ROM:
  [research-quorum-reference-consensus.md](research-quorum-reference-consensus.md), [research-quorum-keyboard.md](research-quorum-keyboard.md)
- [x] Firmware identified and checksummed (already in `data/rom/qu7v42.rom`): [roms.md](roms.md)
- [x] Requirements, design, test plan

## Phases (each ends with: zero warnings, `core-tests` green, gcc check, automation parity gate, recipe run live after a TTD recording is started)
- [ ] 1 `PortDecoder_Quorum` for 128K: `#00`, `#7FFD`, `#FE`, memory map, reset, NMI; creatable on every surface; config folder; boots to the ROM-MENU, 128 and 48 BASIC
- [ ] 2 FDC `#80..#85`, TR-DOS trap, TRD boot from the menu, drive B
- [ ] 3 Extra keyboard `#7E`, key names, F11 / F12, AY and joystick decode
- [ ] 4 Video mode and raster (69888 T per Z), no contention
- [ ] 5 TTD blob (`QuorumPaging`) and fixture, `MachineStateTransfer` family, snapshot capture, port map rows
- [ ] 6 1024K (and 256K if wanted), `BLK_128`, Pentagon timing bit - after Q2 / Q3
- [ ] 7 Permanent docs, `AGENTS.md` models list, `data/rom/README-ROMS.md` source line, slots `MachineDef`, DONE

## Open questions (each with a recommendation)

| # | Question | Sources | Recommendation |
|:--|:--|:--|:--|
| Q1 | What does the TR-DOS trap show at `#0000`? U: always the DOS ROM. Z: the DOS ROM if `#00` bit 7 = 1, RAM page 0 / 8 if bit 7 = 0 (comment: "shadow RAM on TR-DOS requests") | U, Z differ | Follow Z (bit 7 = 0 -> RAM) in phase 1 behind one function. The ROM never sets bit 7, and its SYS page copies code to RAM and patches `#0026` / `#0027` through `#00` = 1. **Close it** by disassembling the SYS page DOS entry: if TR-DOS runs from RAM, Z is right; if the RAM page is empty at the trap, U is |
| Q2 | 1024K page bits and the 48K lock. U: 7FFD bits 6, 7, 5 -> page bits 3, 4, 5, and bit 5 locks paging. K: bits 5, 6, 7 -> 3, 4, 5 (Pentagon-1024 order), no 7FFD lock, `#00` bit 4 = "128K block". Z (256K): only bit 6 -> bit 3, no lock | all three differ; U and Z agree on 256K | 128K and 256K first (U = Z). For 1024K take **no lock on 7FFD bit 5** (the ROM's own 48K lock is `#00` = `#60`) and keep the page-bit order open until a document or a ROM disassembly of the 128 page settles it; ship 1024K behind the Z/U-compatible low bits only |
| Q3 | Video timing: frame 69888 T / paper line 80 (+65 T) (Z, "proof???") vs the Pentagon preset in U's ini; the `#00` bit 2 Pentagon switch (K only) | Z, K; U gives nothing Quorum-specific | Z's values for phase 4; the bit-2 switch only in phase 6. Measure with a border-timing test program on the emulated board; no hardware figure exists locally |
| Q4 | What `#80FD` does (U latches it, BC calls it "CP/M paging", the ROM never writes it, Z lacks it) | U, BC | Latch it, record it in TTD, no effect |
| Q5 | FDC aliases: Z decodes `#80..#83` with A6 / A5 free (`0x9C`) and `#85` with `0x9F`; U exact; BC lists a `#1F/#FF`-style row for the Quorum that the ROM does not use | U, Z, BC differ | Exact `#80..#83`, `#85` (U + ROM). Add Z's aliases only if a program is found that needs them |
| Q6 | Should the FDC ports vanish when `#00` bit 7 = 1 (U's `CF_DOSPORTS`)? | U vs Z | Always on (Z); the ROM cannot tell |
| Q7 | Strictness of the AY (`0xE012` per BC) and joystick (`0x0099/0x0019` per BC) decodes | BC only | Standard ports first (they satisfy every reading); BC's masks as a phase 3 item with a test that the standard ports still work |
| Q8 | Overlapping decodes (e.g. `#7FFC` matches both `#7FFD` and `#FE`): fire all (Z) or first match (U) | Z, U | Fire all matching rules, in table order |
| Q9 | RAM sizes: add 256K (Z has it, K too; U and the model table only 128 / 1024) and ZXMAK2's 64K | Z, K | Keep 128 + 1024; add `RAM_256` only if the owner confirms a real 256K board - it costs nothing (a mask). 64K out |
| Q10 | Writes while a ROM is mapped: to the RAM beneath (Z) or dropped (U) | Z, U | Z (it gives `BLK_WR` a purpose); invisible to the shipped ROM |
| Q11 | Does the Quorum have an expansion connector (slots `BusDef`)? No source | - | None; revisit if a schematic turns up |
| Q12 | Do SZX / Z80 snapshots have a Quorum machine id? Not checked | - | Check the SZX spec when phase 5 starts; until then save as 128K |
| Q13 | `#7E` read: bits 6-7 value (Z: bus value, so 1) and whether the unused matrix positions (9 of 48) are keys (INV, `[<`) | Z, U layout comment | Bits 6-7 = 1; unused positions read 1 |
| Q14 | Floating bus on `#FF` / undecoded reads | Z's generic ULA reads the screen byte; no Quorum evidence | `#FF` |

Nothing in the repository was changed by this design; no ROMs or collection files were copied.
