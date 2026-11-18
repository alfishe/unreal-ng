; InterruptJumpExit: an IM 2 handler that leaves by a patched JP to the interrupted address, not by RETI (it takes
; the return address off the stack); main never returns or HALTs, so only that exit ends the interrupt's time.
        org $8000
start:
        ld sp,$9F00
        di
        ld a,$90
        ld i,a
        im 2
        ei
main:
        inc de
        jr main
        org $9191
handler:
        pop hl
        ld (exit+1),hl
        ld a,(ticks)
        inc a
        ld (ticks),a
        ei
exit:
        jp $0000
ticks:  db 0
