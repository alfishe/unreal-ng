; A long LDIR an interrupt breaks into: the IM 2 handler makes a copy of its own in the middle of it. The long copy
; (6000 bytes, ~126000 T-states: two interrupts or more) is one run all the same.
        org $8000
start:
        ld sp,$9F00
        di
        ld hl,$9000
        ld (hl),$91
        ld de,$9001
        ld bc,256
        ldir
        ld a,$90
        ld i,a
        im 2
        ei
main:
        ld hl,$0000
        ld de,$A000
        ld bc,6000
        ldir
        jr main
        org $9191
handler:
        push af
        push bc
        push de
        push hl
        ld hl,$0100
        ld de,$9800
        ld bc,16
        ldir
        pop hl
        pop de
        pop bc
        pop af
        ei
        reti
