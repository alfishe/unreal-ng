; LoopTripAndStride / DelayLoops: a copy loop of 10 trips (HL +1 read, DE +2 write, B -1), a 6 trip loop, and a
; delay loop that only burns time (DJNZ to itself, no memory access), and a shift loop that is no delay.
        org $8000
start:
        ld sp,$9F00
        di
main:
        ld hl,$A000
        ld de,$B000
        ld b,10
copy:
        ld a,(hl)
        ld (de),a
        inc hl
        inc de
        inc de
        djnz copy
        ld b,6
six:
        ld (hl),b
        dec hl
        djnz six
        ld b,40
delay:
        djnz delay
        ld b,8
shift:  rrc e                ; no memory access either, but it computes: no delay loop
        djnz shift
        jr main
