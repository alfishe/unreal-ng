; InterruptProfile: an IM 2 handler reads port $FE and counts; main HALTs.
        org $8000
start:
        ld sp,$9F00
        di
        ld a,$90
        ld i,a
        im 2
        ei
main:
        halt
        jr main
        org $9191
handler:
        push af
        in a,($FE)
        ld a,(ticks)
        inc a
        ld (ticks),a
        pop af
        ei
        reti
ticks:  db 0
