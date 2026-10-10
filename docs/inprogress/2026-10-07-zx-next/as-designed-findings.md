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
