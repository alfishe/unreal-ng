# Looks wrong, is right: findings checked against the author's own reference

A catalogue of pictures and numbers that look like emulator bugs and are not, each with the authoritative reference. Check here before
"fixing" one. A finding is listed only when there is a primary source (the author's screenshot or source, the RTL, a board photograph).

## Mapscroll 3 (demos/tech-demo, seedy1812): garbage on the ULA screen with a clean tilemap window on top

| | |
|:--|:--|
| What it looks like | A clean scrolling tile picture in a window; right of it and below it a field of coloured noise; the window starts left of the noise |
| Why it is right | The demo leaves the ULA on (its `nextreg $68,%10000000 ;ula disable` is commented out) and clips the tilemap to a window with NR #1B. The ULA shows bank 5, which holds the tile graphics (the tile definitions are at bank 5 offset 0) |
| Author's screenshot | [README image](https://github.com/user-attachments/assets/4a4db62c-146f-48e6-9f14-f0bdd9a892b0), a copy: [mapscroll3-author-readme.png](references/mapscroll3-author-readme.png) |
| Source | [repository](https://github.com/seedy1812/mapscroll3) at commit `f47c70a598a1e45caaedd74de92294d19e8778a1`: [mapscroll3.s#L102](https://github.com/seedy1812/mapscroll3/blob/f47c70a598a1e45caaedd74de92294d19e8778a1/mapscroll3.s#L102) (`video_setup`, the commented-out ULA disable), [backdrop.s#L45](https://github.com/seedy1812/mapscroll3/blob/f47c70a598a1e45caaedd74de92294d19e8778a1/backdrop.s#L45) (`backdrop_start`: clip window, `LAYER_3_CTRL` = %10000011, map base NR #6E = #A0, tiles NR #6F) |
| Ours vs the author's | [mapscroll3-ours-vs-author.png](references/mapscroll3-ours-vs-author.png): the ULA fields and the border are identical pixel for pixel (0 of 90 412 differ); one of 4070 captured frames equals the author's picture completely (0 of 131 970) |
| The real bug that hid behind it | the video side of bank 7 is an 8K BRAM (RTL `zxnext.vhd`, "ULA BANK 7 (8k only due to limited bram resources)"); the demo writes the map through MMU page 14 at offset 0 ([backdrop.s#L158](https://github.com/seedy1812/mapscroll3/blob/f47c70a598a1e45caaedd74de92294d19e8778a1/backdrop.s#L158)) and points NR #6E at bank 7 offset #2000. Fixed with `NextTilemap_Test.Bank7MapOffsetWrapsInTheEightKilobyteBram` |

## Changing8kBank: MAME's two photographs are identical

MAME has no contention (`specnext.cpp`: "TODO: contention", `SPECTRUM_ULA_UNCONTENDED`), so the board test's ON and OFF photographs
(`Tests/Timing/Changing8kBank*/MAME0.282.jpg`) look the same. They are not a reference for the contention-ON length. See
[audit-dma-timing-2026-10-09.md](audit-dma-timing-2026-10-09.md).

## NextZXOS menu says 3.5MHz while the status bar says 28 MHz

The menu's "3.5MHz >" is the speed NextZXOS gives user programs; the OS itself runs at 28 MHz ("the operating system runs at the maximum speed
when possible", the NextZXOS guide). See [.recipe/machines/next.md](../../../.recipe/machines/next.md).

## The DMA sample demo is quiet

Its DAC output is a small swing (samples 108..149 of 255) on a large DC level, so it is soft by design; it plays (14 300 `OUT #FFDF` in
1.76 s). Owner-confirmed audible 2026-10-09.

## Next Kong Rainbow: "flat" screen that changes colour (jonas-winkler)

| | |
|:--|:--|
| What it looks like | The whole Layer 2 320x256 screen is one colour (at most two with a slanted seam where the beam crossed the redraw); a minute later another colour |
| Why it is right | The program is a Layer 2 layout test, "Experience all the colors!": it fills the whole 80K screen with one colour index, then the next, 256 times, then `jr $`. At 28 MHz one fill is ~75 ms, so the 256 fills take ~19 s |
| Source | [rainbow.asm#L42-L69](https://github.com/jonas-winkler/next-kong/blob/801665988b161ccfe5b1e6c05d5da188b53a9869/rainbow.asm#L42-L69) (`FillColors`, `FillScreen`, `FillBank`); the 320x256 setup `nextreg $70,$10` at the top |
| Ours | frames 100 and 300 of a run show different colours; the seam is slanted because Layer 2 320x256 is stored column by column |

## Layer2 Fade Out: black half of the time (ped7g)

| | |
|:--|:--|
| What it looks like | A picture that fades to black in 7..124 frames, stays black for 0.5 s, is restored for 0.5 s and fades again; a screenshot after N seconds may catch the black phase |
| Source | [fadeout.asm](https://github.com/ped7g/ZXSpectrumNextMisc/blob/ca6fffdc1125412ec6a63a6e0e17e8eff363bea7/Layer2FadeOut/fadeout.asm#L109-L144) (`FadeOutLoop`, `.blackWait`, `.originalWait`, the jump back to `reinitFadeOutLoop`), [README](https://github.com/ped7g/ZXSpectrumNextMisc/blob/ca6fffdc1125412ec6a63a6e0e17e8eff363bea7/Layer2FadeOut/README.md) ("After the interpolation there is 0.5s pause keeping the screen black, then it restores original colours") |
| Ours | sampled every 10 frames: lit pixels 309 392 (picture) -> 272 940 -> 235 800 -> 0 (black, ~30 frames) -> picture again, a ~100-frame cycle, both through the Browser (NEXLOAD) and through the direct loader |

## Tile Screen, Tilemap Screen 320x256 / 640x256: "back in NextZXOS" from the Browser (ped7g, NEX V1.3)

| | |
|:--|:--|
| What it looks like | The Browser starts the file and NextZXOS prints "This is a V1.3 file. Please update to the latest .nexload version." and returns to the prompt |
| Why it is right | These are the NEXLOAD2 test files (header `NextV1.3`). The distribution's NEXLOAD knows up to V1.2: `LoaderVersion db $12` and `cp b : jp c,loaderUpdate` (its banner text claims V1.3, the constant does not). NEXLOAD2 ([nexload2](https://github.com/ped7g/ZXSpectrumNextMisc/tree/ca6fffdc1125412ec6a63a6e0e17e8eff363bea7/nexload2)) is the loader for them |
| Source | tbblue firmware `asm/nexload/nexload.asm` lines 290-291 (version check) and 749 (`LoaderVersion`); the V1.3 layout: [nexload2.asm#L120-L135](https://github.com/ped7g/ZXSpectrumNextMisc/blob/ca6fffdc1125412ec6a63a6e0e17e8eff363bea7/nexload2/nexload2.asm#L120-L135) |
| The real bug | the direct NEX loader (File -> Open, `load_software`) ignored the V1.3 fields: tilemap NR #6B/#6C/#6E/#6F, the copper block and `BANKSOFFSET`, and read the banks 2048 bytes early. Fixed with `LoaderNex_Test.V13*` |

## All Joystick Tester: the Browser only loads it ("back in NextZXOS")

| | |
|:--|:--|
| What it looks like | After ENTER in the Browser the NextBASIC editor shows the program listing (lines 10-80) |
| Why it is right | The main file is a PLUS3DOS BASIC program with no autostart (header bytes 18-19 = `#806C`, bit 15 set: no auto-run), so the Browser loads it into the editor and does not run it; typing `RUN` starts it, and it then waits at `INPUT "Option: "` (line 140 / 590) for a key |
| Source | the file itself (`ALL Joystick Tester4.bas`, `ALL Joystick Tester4.txt` next to it); [itch.io page](https://chucknz.itch.io/all-joystick-tester-spectrum-next) |

## Zeus NEX File Example: needs the open NEX file handle

The demo reads its private data (the poem) from its own NEX file through the handle NEXLOAD pokes into `Main.FileHandle` (`F_READ(TextBuffer.Length)`, [nex_example.asm#L44](https://github.com/Threetwosevensixseven/ZeusNexFileExample/blob/9b331c12c7100ee27beccc00bc38465ade4d8a3d/nex_example.asm#L44)). Started through the Browser (NEXLOAD) it prints the poem exactly like the author's [nexdemo.png](https://github.com/Threetwosevensixseven/ZeusNexFileExample/blob/9b331c12c7100ee27beccc00bc38465ade4d8a3d/nexdemo.png). The direct loader opens no esxDOS file, so there the screen stays grey: a limit of that loader, not of the machine.

## Board versus the official zxnext.vhd (CORE 3.02.03), checked 2026-10-24

The official core's CPU is T80 v0247 and its authors did **not** fix the undocumented flags or the other quirks of that T80 (the MiSTer port
repaired them by hand in its T80 v350; its behaviour is not the board's). The same holds for the board logic below: where `zxnext.vhd` differs
from what the wiki or a classic Spectrum does, the VHDL wins.

| Item | Official behaviour | State |
|:--|:--|:--|
| N1 MMU page >= #E0 | slots 0-1: ROM (config bank in config mode); slots 2-7: no SRAM access at all (`sram_pre_active = 0`), the write is lost | done (reads of an unmapped slot return the trash page, not the held SRAM output) |
| N2 floating bus | `#xxFF` only under the 48K / 128K timing; the +3 port (`#0FFD` type) only under the +3 timing with NR #82 bit 4; every other undecoded port reads #FF; bit 0 is forced only on the video byte, not on the last contended byte | done (`PortDecoder_Next::FloatingBusDrives`) |
| N3 NR #03 | timing only with bit 7, an open lock and bit 3 = 0 (000 -> 001, 101-111 -> 011); bit 3 toggles the lock; type 1-4 only, config mode only; 111 enters config mode; reads `sub_idx & timing & lock & type` | done |
| N4 60 Hz | NR #05 bit 2 latched at the frame sync, 264 lines, paper from line 40, INT on line 0; Pentagon forced to 50 Hz | done (48K 59136 T, 128K / +3 60192 T) |
| N5 NR #07 / #80 | NR #80 bit 7 (expansion bus on) forces 3.5 MHz; NR #07 reads `00 & speed in force & 00 & programmed` | done (the switch is immediate, the VHDL's within the instruction) |
| N6 NR #8E | a bank change writes #DFFD bits 2:0 = `00 & d7` and clears bit 3 (the Profi mapping is hard-wired off); #1FFD bit 1 = d0 | done |
| N7 NR #04 / #0A | NR #04 keeps 8 bits on the Issue 5 board and reads 0 (no read case); NR #0A reads `{mf type, sd swap, automap, reverse, 0, dpi}` (mask #FB; the report's #DB is wrong) | done |
| N8 contention | `port_contend = not A0 or port_7ffd_active or port_bf3b or port_ff3b`; `mem_contend` per 8K page; the +3 has no I/O contention | done; NR #08 bit 6 and speed latches (`hc(8)`) remain immediate |

Not modelled: the held SRAM output a read of an unmapped slot returns on the board (N1), the within-instruction speed latch (N5).
