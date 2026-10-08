# Status: TODO

LSY256 (Orel BK-08, 256K). Design only, written 2026-10-07 from a single source; nothing implemented. Plan: [design.md](design.md) section 10.

## Done
- [x] Reference search: eleven emulators, one has the model ([research-reference-consensus.md](research-reference-consensus.md))
- [x] ROM image identified, checksums, page roles ([roms.md](roms.md)); ROM bytes analysed ([research-rom-analysis.md](research-rom-analysis.md))
- [x] Requirements, design, test plan

## Phases
- [ ] 1: state, decoder, factory, config folder, screen case, ROM signatures; SYS ROM boots; recipe v1
- [ ] 2: window-0 modes, page bit, read-only RAM, NMI; paging state on all surfaces; recipe v2
- [ ] 3: BK-08 keyboard; recipe v3
- [ ] 4: TTD blob, `PagingFamily::Lsy256`, capture view; recipe v4
- [ ] 5: slots row, timing and sound answers, docs, TTD fixture

## Open questions

| # | Question | Recommendation |
|:--|:--|:--|
| Q1 | Window 3: Unreal gives page `16 + n` (`latch & 0x10`); the ROM's RAM test says bit 4 is page bit 3 | Use `8 + n`; pin it with the ROM-test boot case; it is a bug in Unreal's stub |
| Q2 | Who sets the EMUL bit (`#08`)? The ROM's immediate writes never do; who fills RAM 8..11? | Implement the table as Unreal has it; record the ROM boot under TTD in phase 1 and read the `#7B` values; if EMUL never appears, document that the 128 / 48 pages are reachable only through the far-call stub or a user program |
| Q3 | TR-DOS entry when `#0000` is RAM (Unreal arms none) | Arm the `#3Dxx` trap in mode (1,1) too only if the ROM boot shows TR-DOS being entered that way; otherwise follow Unreal |
| Q4 | BK-08 key positions and the ZX-name mapping for `type_input` | Take the row/bit table from Unreal and the symbols from the key tables in ROM page 1; ask for a keyboard photo or schematic |
| Q5 | Real frame length, INT position, contention | Pentagon-class 71680 T, as Unreal's INI; revisit when a schematic or a measurement appears; keep it a config value |
| Q6 | Sound hardware (AY, Covox, TurboSound) and joystick on the real board | Pentagon 128 set (one AY, Covox, Kempston) |
| Q7 | Does the board have a ZX bus / expansion slot? | Treat as the Pentagon row in slots until known |
| Q8 | Is `#7B` readable; what do bits 2 and 5-7 do? | No read, ignored; add a trace log line when a program writes them |
| Q9 | Other firmware images for this board; the fan site `orel.pp.ua` (unreachable on 2026-10-07) | Retry later; search the zx-pk.ru threads that link it |
| Q10 | Next free `PeripheralId` on the day of the work (62 now) | Re-read `ttdserializable.h` at phase 4 |
| Q11 | Xpeccy and ZXMAK2 have no model: is the Unreal "stub" a faithful board? | State it in the recipe and the model description ("Unreal-compatible; not checked against hardware") |
