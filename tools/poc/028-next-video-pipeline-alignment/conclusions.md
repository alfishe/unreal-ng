# Conclusions

- Go: for every fetch-stage register listed in the charter that a picture can show (Layer 2 scroll / clip / bank / enable, ULA scroll /
  clip / fine scroll, tilemap scroll / clip) the clocks from the WAIT and the group it is read for are measured, from the official RTL
  simulated whole. The counts reproduce the ones made earlier from the VHDL (22 for the chain, 20 for ULANext) to the clock; the layer
  order is 22, not the 24 counted by hand.
- The Layer 2 / ULA alignment is explained: they are aligned (the hand count was wrong).
- Second set (`run-diff.sh`, differential runs, the sprite RAMs modelled): the LoRes scroll 22 clocks per pixel, the sprite clip 22 per
  pixel, the palette value writes 16 (sprites) / 17 (tilemap) / 18 (ULA, Layer 2), the tilemap control bits 53 per cell, 80 columns 24,
  the tilemap transparent index 20, NR #6A 23, the Layer 2 resolution 27. The sprite fill (attribute / pattern / NR #4B writes) shows a line
  or two later: the rule and its numbers are in the third set of the measurements; modelled in the emulator since (ScreenNext sprite fill snapshots).
