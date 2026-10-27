# Status: TODO

ZXM-Phoenix (`PHOENIX`, 1024K / 2048K) as a creatable machine. **Design only; nothing implemented.** Plan:
[design.md](design.md) section 10. Overview: [README.md](README.md).

## Done
- [x] Reference research: Unreal Speccy (zx-evo line), Xpeccy, Xpeccy+ and the Black_Cat port table compared;
  UnrealSpeccyP, UnrealSpeccy 0.39 and ZXMAK2 checked and have no Phoenix: [research-reference-consensus.md](research-reference-consensus.md)
- [x] The firmware image read page by page, checksummed, compared with ROMs already in the repository: [roms.md](roms.md)
- [x] Requirements, design, test plan

## Phases
- [ ] 1. Creatable machine: factory, `PortDecoderPhoenix` (`#7FFD` / `#1FFD` / `#EFF7`), ROM table, `data/configs/phoenix/`, roles,
  timing defaults, screen mode; boots 128K menu, 48 BASIC, TR-DOS. Parity: CLI, WebAPI + OpenAPI, MCP, Lua, Python, Qt;
  recipe `.recipe/machines/phoenix.md` verified, TTD recording before every run
- [ ] 2. TTD blob `PhoenixPaging` (id 62), `PagingFamily::Phoenix`, `WindowMapCapture`, `state/paging` fields on every surface
- [ ] 3. Devices: `#EFF7` bit 7, `#00F7`, optional RTC, mouse / joystick, TR-DOS flow, IDE sweep
- [ ] 4. Slots: NemoBus v1.1m refdata row and planner test
- [ ] 5. Evidence-driven corrections (Q1-Q4, Q8) when a schematic, manual or board dump turns up

Phases 1-3 can start now.

## Open questions (each with a recommendation)

| ID | Question | Recommendation |
|:--|:--|:--|
| Q1 | The only image has an **erased SYS page** (ROM 0 = 16K of `#FF`). Does a fuller Phoenix BIOS image exist (a service / setup ROM, a "Phoenix BIOS")? | Ship the image as it is; reproduce `#FF`. Ask the zx-pk.ru Phoenix subforum for a dump (plan phase 5). No emulation of a SYS ROM until one exists |
| Q2 | Which `#7FFD` / `#1FFD` bits are RAM page bits 3 and 4? Xpeccy, Xpeccy+ and Xpeccy's "real" comment: bit 3 = `#1FFD` bit 4, bit 4 = `#7FFD` bit 7. Unreal: swapped | Take the Xpeccy order (3 of 4 statements); one function, one test per bit, so a schematic can flip it in one line |
| Q3 | Does the board decode A2 for `#7FFD` / `#1FFD`? Unreal and the KAY-1024SL column of Black_Cat's table: no. Xpeccy, Xpeccy+: yes | Do not decode A2 (the family's own table plus Unreal); `OUT (C),r` with the canonical ports works either way; revisit with a schematic |
| Q4 | What do the other `#EFF7` bits do on a Phoenix? Xpeccy / Xpeccy+: nothing but bit 7. Unreal runs Pentagon-1024 code that probably does not apply | Store the byte, bit 7 forces the DOS ports, every other bit inert (tests pin it; `EFF7OtherBitsChangeNothing`). Do not enable the Pentagon video modes |
| Q5 | Is there a Gluk RTC on `#DFF7` / `#BFF7`? Only Unreal (when `CMOS=` is set) enables it; a forum signature lists an "#EFF7 Gluk RTC" next to a Pentagon-1024, not the Phoenix | Off by default; available through the existing `CMOS=` option in phase 3, documented as "from Unreal only" |
| Q6 | What is `IN #00F7` (returns 0 in Xpeccy / Xpeccy+, named "Version")? The port table lists `#00F7` "Version" for the ZX Multi Card-2 only | Return 0, with a test named after the question so it is easy to drop; revisit with a manual |
| Q7 | Frame timing: Xpeccy+ lists the Pentagon geometry (71680 T, INT 36 T), no other source gives a number | Pentagon timing (frame 71680, INT as unreal-ng's Pentagon); the NemoBus clones are Pentagon-like in all three emulators |
| Q8 | Does the board have a 7 MHz switch or turbo port? Xpeccy+ offers x2 by an emulator key and notes `#EFF7` is "decoded for nothing" | Do not build a turbo; the front-panel-switch machinery of Profi/Scorpion can host one later if a source appears |
| Q9 | TR-DOS ROM rule when a session is active and `#7FFD` bit 4 = 0: Xpeccy / Xpeccy+ keep TR-DOS (ROM 1); Unreal and unreal-ng's Pentagon select the SYS page (ROM 0). Also the DOS-session exit flag (`CF_LEAVEDOSRAM` from Unreal vs the Pentagon's `CF_LEAVEDOSADR`) | Xpeccy rule (the shipped SYS page is erased), `CF_LEAVEDOSRAM`; both are one table row / one flag, each with its own named test |
| Q10 | A TR-DOS 6.11Q build "for ZXM-Phoenix" exists (named in the Karabas firmware tree); it is not in the local collection | Fetch it from the Karabas-Pro repository when needed; not required for phase 1-3; add to `data/rom/` only with provenance |
| Q11 | A Phoenix in extended paging has no `.sna` / `.z80` / `.szx` representation (no emulator defines a Phoenix machine id) | `capture_unsupported` with a clear reason; TTD dump is the way to keep such a state. No new snapshot format |
| Q12 | `#1FFD` bits 2 and 5 (and bit 3's second use): any source meaning? None of the three emulators uses bits 2 and 5 | Store, ignore; log once at debug level when set |
| Q13 | NemoBus v1.1m: which signals are missing on the "m" board (the bus guide only says "m = signals missing")? Which cards fit? | Planner row lists the bus type only; no card is offered until the guide's per-board table is read (phase 4) |

## Not planned

- A Phoenix BIOS emulation beyond the image; the 7 MHz mode; another Nemo-family machine (Kay, Quorum) - they would reuse
  `PortDecoderPhoenix`'s pager with a different bit table and their own folder.
