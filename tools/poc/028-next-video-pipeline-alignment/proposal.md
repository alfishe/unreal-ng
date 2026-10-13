# Proposal

Adopt into `ScreenNext::PictureLatencyOf` (done): per register the latency and the quantum. A copper MOVE of a cell-read register
(ULA scroll, tilemap scroll) now takes effect at the next 8-pixel cell; the pixel-group registers at the next 4-clock pixel.

Done (second set): the sprite RAMs are modelled, the sprite clip / enable, the LoRes scroll, the palette values and the tilemap control
bits are measured and in `ScreenNext::PictureLatencyOf`. Done after it: the emulator's sprite fill (snapshots at the fill end), the tilemap text mode (53). Left: a per-sprite fill time instead of one instant per row.
