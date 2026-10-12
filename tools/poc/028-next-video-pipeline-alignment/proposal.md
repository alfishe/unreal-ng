# Proposal

Adopt into `ScreenNext::PictureLatencyOf` (done): per register the latency and the quantum. A copper MOVE of a cell-read register
(ULA scroll, tilemap scroll) now takes effect at the next 8-pixel cell; the pixel-group registers at the next 4-clock pixel.

Next, if a program needs it: model `spram_320_9` / `sdpbram_16k_8` / `sdpram_128_8` in `patch_rtl.py` and add sprite scenarios
(NR #19 clip, NR #15 bit 0, the sprite palette select); the LoRes scroll (NR #32 / #33) with the LoRes layer on.
