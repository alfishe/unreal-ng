; AnalysisL3 (review): a key test. The loop reads the Q-T half-row (port $FBFE) and branches on bit 0 (Q); no key
; is pressed in the session, so `pressed` never runs. Holding Q in an experiment (or the coverage driver) takes it.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld a,$FB
        in a,($FE)
        bit 0,a
        jr z,pressed
        jr main
pressed:
        ld a,$55
        ld (hits),a
        jr main
hits:   db 0
