# Proposal

Adopt into `ScreenNext::PictureLatencyOf` (done): per register the latency and the quantum. A copper MOVE of a cell-read register
(ULA scroll, tilemap scroll) now takes effect at the next 8-pixel cell; the pixel-group registers at the next 4-clock pixel.

Done (second set): the sprite RAMs are modelled, the sprite clip / enable, the LoRes scroll, the palette values and the tilemap control
bits are measured and in `ScreenNext::PictureLatencyOf`. Left: sprite attribute / pattern writes at the beam, NR #4B, NR #6A, the Layer 2
resolution.
