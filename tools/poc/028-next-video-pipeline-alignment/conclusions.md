# Conclusions

- Go: for every fetch-stage register listed in the charter that a picture can show (Layer 2 scroll / clip / bank / enable, ULA scroll /
  clip / fine scroll, tilemap scroll / clip) the clocks from the WAIT and the group it is read for are measured, from the official RTL
  simulated whole. The counts reproduce the ones made earlier from the VHDL (22 for the chain, 20 for ULANext) to the clock; the layer
  order is 22, not the 24 counted by hand.
- The Layer 2 / ULA alignment is explained: they are aligned (the hand count was wrong).
- Not met: the sprite registers and the LoRes scroll (sprites need the coregen RAMs modelled; LoRes' effect was not exercised).
