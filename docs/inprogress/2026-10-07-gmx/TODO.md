# Status: TODO

GMX (`MM_GMX`, "ZS Scorpion + GMX", 2048K): **design written 2026-10-07, revised 2026-10-30 with the original manual,
real software and the current firmware; nothing implemented.** The model is not creatable today. Plan:
[design.md](design.md) section 7; tests: [tdd-plan.md](tdd-plan.md).

## Phases

- [ ] Phase 1: creatable machine (factory, `GmxState`, register file, RAM and ROM mapping, config folder with `gmx644.rom`, roles, recipe sections 1-2, parity on all surfaces)
- [ ] Phase 2: TTD blob, snapshot capture policy, `MachineStateTransfer`, paging fields on all surfaces
- [ ] Phase 3: 640x200 extended video, scroller, Qt, screenshots; Edge Grinder boot test
- [ ] Phase 4: turbo, Magic latch and the `#7EFD` bit 2 gate, BLKEXT, `#00` RECONF, INT select (`#00` bit 7)
- [ ] Phase 5: slots and media (General Sound incompatibility, SMUC disk list, Z-Controller SD); OSZ boot test
- [ ] Phase 6: evidence-driven corrections

Done so far:
- [x] Source survey 2026-10-07: Unreal Speccy (zx-evo fork), MAME, pico-speccy, the 1999 and 2004 articles, the Black_Cat
  port table ([research-reference-consensus.md](research-reference-consensus.md))
- [x] Requirements, design, test plan, firmware survey ([roms.md](roms.md))
- [x] 2026-10-30: the original Scorpion manual "GMX ... Руководство" (22.11.1998) found and kept in
  [references/](references/); every register re-checked against it
  ([research-2026-10-30-emulators-and-manual.md](research-2026-10-30-emulators-and-manual.md)), with the rev.2022
  documentation (same programming section; adds the 14 MHz clock output and the host CLOCK / `#1FFD` latch
  modifications). ZEsarUX, Xpeccy,
  ZXMAK2, UnrealSpeccyP, Zero, ZX-M8XXX, zxsp, Spectral have no GMX (re-checked)
- [x] 2026-10-30: Edge Grinder and OSZ read for the hardware they use
  ([research-2026-10-30-software.md](research-2026-10-30-software.md)); fixtures in `testdata/machines/gmx/`
- [x] 2026-10-30: the web Unreal Speccy GMX model (zxby.org) examined
  ([research-2026-10-30-web-unreal.md](research-2026-10-30-web-unreal.md)); its firmware `gmx644.rom` (Loader V2.00 +
  monitor 6.44su, 2025) tracked in `data/rom/`

## Open questions

Each has a recommendation, chosen so that the phase can start without a reply. "Closed" rows name the evidence.

| Q | Question | Status / recommendation |
|:--|:--|:--|
| Q1 | Which plane and page does the loader hand control to, what `RESET=` / role names does the config need? | **Closed (firmware).** At power-on the loader (`gmx644.rom` plane 0, unpacked to `#5C01`) checks the scheme and does `OUT (#00),#0A` (RECONF scheme 2, CPU reset); after the reset it reads the magic byte `#8A`, takes scheme 2 "Work Sch" (trailer D = E = `#FF`), writes `OUT (#7EFD),#40` and `OUT (#00),#02`, then `JP #0000`: plane 4 page 0, the Scorpion 128 menu, which turns turbo on (`#7EFD = #C0`). Every RESET goes through plane 0 again (RESET clears the plane bits unless FIXROM). Roles: plane 4 and 7 = 128 / 48 / SYS / DOS, plane 1 = Pentagon 128 / 48, plane 5 = SYS (shadow monitor), others `Specific`. `RESET=` is irrelevant: the hardware always starts the loader. This is also where OSZ's `#02` in `#00` comes from |
| Q2 | Does the `#0100-#010F` ProfROM plane strobe exist on the GMX? | **Off.** The manual's port list has no such strobe; pico-speccy's hardware trace shows it would break the monitor's self-checksum. (The web Unreal keeps it on the two low plane bits, inherited from its PROFSCORP base; MAME too) |
| Q3 | `#7EFD` bit 2: lifts the `#7FFD` lock (Unreal) or blocks Magic (MAME)? | **Closed: both.** Manual: D2 "MagEnable - 1 = the Magic button blocked", and section 3.1: the BLKPK1 lock "is switched off when D2 of port 7EFD (MGEN) is set" |
| Q4 | Scroller: line scroller (`offset / 80`) or byte offset? 8 bits or `& #F0` in `#7AFD`? | **Closed:** one write-only 14-bit start address in bytes, low four bits forced to 0 by hardware (manual: "80 = one graphic line, 640 = one text line", "the low four bits are reset in hardware"). Model: `start = ((#7CFD & #3F) << 8 \| (#7AFD & #F0))`, display address `(line * 80 + column + start) mod 16000`. OSZ uses it with multiples of 640 |
| Q5 | Do `IN` from the `#1FFD` / `#7FFD` families still toggle turbo on a GMX? | **Closed: no strobes.** The strobes are a feature of the host Turbo+ EPLD, which drives the CPU clock and WAIT; the GMX fitting cuts both (manual II.6 WAIT diode, rev.2022 II.7 the host CLOCK pin) and the card makes its own clock. The manual's only turbo controls are `#7EFD` bit 7 and the Normal/Turbo button. Unreal, pico-speccy and the web Unreal agree; MAME's GMX class inherits the strobes from its Turbo+ base (a deviation) |
| Q6 | What reads the magic shift register? | **Closed:** the loader (plane 0 page 0 at `#00F1`) reads it eight times after a reset; `#00` bits 0-2 are the FLEX8000 scheme number CONF0-2 and bit 3 RECONF reloads that scheme (manual). The value tells the loader which scheme was asked for |
| Q7 | The Magic latch of the read-backs | **Closed: real.** Manual section 2: the bits marked `*` of `#7AFD` / `#7EFD` are latched at Magic and released by a dummy read of `#FF`; MAME models exactly this. Phase 4 |
| Q8 | Pentagon, Composit and test schemes as models? | No (unchanged). The manual describes the Pentagon scheme (no GMX ports, `#7FFD` at A1 = A15 = 0, 320-line frame, INT at the start of frame sync); the existing Pentagon model covers it |
| Q9 | `#7EFD` bits 0-1 (flash EWR / Vpp) and the loader's flash update | Ignore; flash is read-only. The manual names the chip 28F400, the official site "28f040" (a 512K x 8 part fits the map); decide only if the update path must run |
| Q10 | RAM sizes 256K-2048K | Keep 2048. The manual adds the alias rule for two 256K SIMMs (SM0-1 ignored: banks `0-#F` and `#40-#4F`); smaller sizes need that rule, on request |
| Q11 | Buffer screen: `#3A / #7A` or `#3B / #7B`? | **Closed: `#3B / #7B`.** Both manuals (1998 and rev.2022) say `#3A / #7A`, but Edge Grinder flips `#7FFD` bit 3 every frame with screen 1 in `#3B / #7B` and ran at 25 fps on the real board (author's history, 2021-07-06, 2021-08-25): with `#3A` every other frame would be garbage. OSZ's driver hands out `#3B` ("экран 3b"); MAME, Unreal, pico-speccy and the web Unreal agree. `#39 -> #3B` is the bit-1 flip of ZX page 5 -> 7. The test cites the manual's text as a known documentation error |
| Q12 | Pixel timing and border width of the extended mode | Naive first: Scorpion line, 640 pixels in the 256-pixel time (two bytes per 2 T, Unreal), the raster `R_640_200` lines 76-275. Edge Grinder's loader screen and the author's screenshot (`testdata/machines/gmx/reference/`) are by-eye checks |
| Q13 | INT: Scorpion or Pentagon, the "132000 clocks in turbo" sentence | **Mechanism closed:** `#00` bit 7 ST/PENT: 0 = INT at the end of frame sync, 312 lines; 1 = INT at its start, 320 lines (manual); read back in `#7EFD` bit 4; `#00` survives RESET. Default Scorpion. The 132000 sentence stays ignored |
| Q14 | General Sound does not work with the card. Model it? | Mark `unrealistic` in the slot planner, allow with the override (unchanged; the official site later claimed a 20.04.99 firmware fix, conflicting) |
| Q15 | Port decode strictness | `#00`: full decode A0-A7 = 0, any high byte (manual: "полная дешифрация"). The `#xxFD` file: exact high byte (MAME, pico-speccy). It must be decoded before the Scorpion `#7FFD` arm (whose mask also matches all four `#7xFD` ports) and before the AY arm (whose `#FFFD` mask also matches `#DFFD`) |
| Q16 | Newer firmware | **Closed:** `data/rom/gmx644.rom` (Loader V2.00, monitor 6.44su, 21.08.2025), the image OSZ needs |
| Q17 | Does scheme 6 (board test) boot in our model? | Probably not (FPGA reload). Phase 3 video tests use a synthetic RAM pattern plus Edge Grinder |
| Q18 | RESET and `#00` | **New, closed by the manual:** RESET clears every GMX latch except `#00`; `#00` is cleared only by power-off or a RECONF. MAME and Unreal clear it on reset (deviation). Writes without bit 3 (OSZ writes `#02` around every monitor call, so does the firmware) only store the value: no reset |
| Q19 | Where does the NMI fetch when `#1FFD` bit 0 (RAM 0) is set? | **Closed (manual 3.2):** "if MAGIC is pressed while the ROM is blocked (BLK1), instructions and data are read from the ROM, write cycles go to the RAM": the fetch at `#0066` comes from the DOS page of the current plane, writes to `#0000-#3FFF` still land in RAM page 0, for as long as the DOS trigger the Magic set is on. MAME forces ROM while the NMI is pending (reads only). OSZ also keeps a copy of the firmware's NMI entry at `#0066` of page 0 (`os_main.asm`, "обработка NMI": save AF/R, `IN #7EFD`, `IN #7AFD`, `OUT #1FFD,#12`), so the monitor is entered from either source; the manual fixes the source as the ROM |
| Q20 | FAT16 or FAT32 for the SD card | **Closed: FAT32 only.** LW/PLM changelog: "поддержана только FAT32"; the 6.44 monitor knows FAT32 and MFS partitions only (strings in planes 5-6 of `gmx644.rom`). The host folder `osz/sdcard/` is presented as FAT32 (`fatsynthvolume` can) |
| Q22 | Attribute byte of the extended mode | **New, recommendation: standard ZX.** The manual: "the structure of the attribute byte is standard"; MAME and Unreal (zx-evo) decode ink 0-2, paper 3-5, bright 6, flash 7. The web Unreal decodes ink in bits 0-3 and paper in 4-7 (bit 3 / 7 = bright), no flash: a deviation with no second source. Edge Grinder's author screenshot (made on Unreal) is the by-eye check |
| Q21 | Turbo waits | **Mechanism known, constant to calibrate.** Facts: the host board's WAIT line is disconnected when the GMX is fitted (manual II.6: "remove the diode from the Altera to the CPU WAIT"), and the GMX makes its own CPU clock (rev.2022: the host CLOCK pin is cut); the Scorpion scheme has a WAIT, the Pentagon scheme has none (ZX-Format #07); turbo gives "1.8-1.9x running from RAM, exactly 2x from ROM" (ZX-Format #07). So the waits are on RAM accesses only, the same class as the Scorpion's Even M1 on opcode fetches from RAM. No source gives the phase rule and no emulator implements it. Implementation: an M1-from-RAM alignment wait in turbo (start with the Even M1 rule on the 7 MHz grid), tuned until a RAM-bound benchmark runs 1.8-1.9x and Edge Grinder's counter reads 25; ROM code exactly 2x |

## Pointers

[README.md](README.md) - [requirements.md](requirements.md) - [design.md](design.md) - [tdd-plan.md](tdd-plan.md) -
[roms.md](roms.md) - [research-reference-consensus.md](research-reference-consensus.md) -
[research-2026-10-30-emulators-and-manual.md](research-2026-10-30-emulators-and-manual.md) -
[research-2026-10-30-software.md](research-2026-10-30-software.md) -
[research-2026-10-30-web-unreal.md](research-2026-10-30-web-unreal.md) - [references/](references/)
